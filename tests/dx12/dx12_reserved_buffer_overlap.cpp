#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstring>
#include <functional>
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

constexpr UINT kTileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT kValue = 0x12345678;

template <typename T> struct Owned {
  T *ptr = nullptr;

  ~Owned() {
    if (ptr)
      ptr->Release();
  }

  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;

  T *operator->() const { return ptr; }
};

void Check(const char *operation, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << operation << ": 0x" << std::hex
              << static_cast<unsigned long>(hr) << std::dec << "\n";
    throw std::runtime_error(operation);
  }
}

D3D12_HEAP_PROPERTIES Properties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = type;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC BufferDescription(UINT64 bytes,
                                      D3D12_RESOURCE_FLAGS flags) {
  D3D12_RESOURCE_DESC description = {};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Width = bytes;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  description.Flags = flags;
  return description;
}

void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  list->ResourceBarrier(1, &barrier);
}

void Wait(ID3D12Fence *fence, UINT64 value) {
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    throw std::runtime_error("CreateEvent");
  Check("SetEventOnCompletion", fence->SetEventOnCompletion(value, event));
  const DWORD status = WaitForSingleObject(event, 30000);
  CloseHandle(event);
  if (status != WAIT_OBJECT_0)
    throw std::runtime_error("GPU completion timeout");
}

void CreateBuffer(ID3D12Device *device, Owned<ID3D12Resource> &resource,
                  UINT64 bytes, D3D12_HEAP_TYPE type,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
  const auto properties = Properties(type);
  const auto description = BufferDescription(bytes, flags);
  Check("CreateBuffer", device->CreateCommittedResource(
                            &properties, D3D12_HEAP_FLAG_NONE, &description,
                            state, nullptr, IID_PPV_ARGS(&resource.ptr)));
}

int Run(bool bad_oracle, bool cross_resource, bool bad_cross, bool cross_queue,
        bool reverse_queue, bool early_work) {
  Owned<ID3D12Device> device;
  Check("device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&device.ptr)));
  Owned<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC q = {};
  q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Check("queue", device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue.ptr)));
  Owned<ID3D12CommandQueue> consumer;
  Owned<ID3D12Fence> handoff;
  if (cross_queue) {
    Check("consumer queue",
          device->CreateCommandQueue(&q, IID_PPV_ARGS(&consumer.ptr)));
    Check("handoff fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                               IID_PPV_ARGS(&handoff.ptr)));
  }
  ID3D12CommandQueue *active_queue = queue.ptr;
  std::function<void()> after_submission;
  Owned<ID3D12Fence> fence;
  Check("fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&fence.ptr)));
  const auto d = BufferDescription(3ull * kTileBytes, D3D12_RESOURCE_FLAG_NONE);
  Owned<ID3D12Resource> texture;
  Check("reserved buffer",
        device->CreateReservedResource(&d, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                       IID_PPV_ARGS(&texture.ptr)));
  if (!texture->GetGPUVirtualAddress()) {
    std::cout << "SKIP: no shader-visible sparse buffer backing\n";
    return 77;
  }
  UINT total = 0;
  device->GetResourceTiling(texture.ptr, &total, nullptr, nullptr, nullptr, 0,
                            nullptr);
  if (total != 3)
    throw std::runtime_error("unexpected buffer tile count");
  D3D12_HEAP_DESC hd = {};
  hd.SizeInBytes = 3ull * kTileBytes;
  hd.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  Owned<ID3D12Heap> heap;
  Check("heap", device->CreateHeap(&hd, IID_PPV_ARGS(&heap.ptr)));
  Owned<ID3D12Resource> upload, readback;
  CreateBuffer(device.ptr, upload, 3ull * kTileBytes, D3D12_HEAP_TYPE_UPLOAD,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
  CreateBuffer(device.ptr, readback, 3ull * kTileBytes,
               D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
               D3D12_RESOURCE_STATE_COPY_DEST);
  UINT *data = nullptr;
  Check("upload map",
        upload->Map(0, nullptr, reinterpret_cast<void **>(&data)));
  const std::array<UINT, 3> values{0x11112222, 0x33334444, 0x55556666};
  for (UINT i = 0; i < 3; i++)
    for (UINT j = 0; j < kTileBytes / 4; j++)
      data[i * (kTileBytes / 4) + j] = values[i];
  upload->Unmap(0, nullptr);
  D3D12_TILED_RESOURCE_COORDINATE zero = {};
  D3D12_TILE_REGION_SIZE all = {};
  all.NumTiles = 3;
  UINT offset = 0;
  auto map_all = [&] {
    queue->UpdateTileMappings(texture.ptr, 1, &zero, &all, heap.ptr, 1, nullptr,
                              &offset, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
  };
  UINT64 serial = 0;
  auto execute = [&](bool write) {
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Check("allocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&allocator.ptr)));
    Check("list", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            allocator.ptr, nullptr,
                                            IID_PPV_ARGS(&list.ptr)));
    D3D12_RESOURCE_BARRIER alias = {};
    alias.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    alias.Aliasing = {nullptr, nullptr};
    list->ResourceBarrier(1, &alias);
    Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON,
               write ? D3D12_RESOURCE_STATE_COPY_DEST
                     : D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyTiles(
        texture.ptr, &zero, &all, write ? upload.ptr : readback.ptr, 0,
        write ? D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE
              : D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
    Transition(list.ptr, texture.ptr,
               write ? D3D12_RESOURCE_STATE_COPY_DEST
                     : D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_COMMON);
    Check("close", list->Close());
    ID3D12CommandList *lists[] = {list.ptr};
    active_queue->ExecuteCommandLists(1, lists);
    if (after_submission) {
      auto action = std::move(after_submission);
      after_submission = {};
      action();
    }
    Check("signal", active_queue->Signal(fence.ptr, ++serial));
    Wait(fence.ptr, serial);
    Check("device status", device->GetDeviceRemovedReason());
  };
  auto verify = [&](const char *name, std::array<UINT, 3> expected) {
    execute(false);
    Check("readback map",
          readback->Map(0, nullptr, reinterpret_cast<void **>(&data)));
    bool ok = true;
    for (UINT i = 0; i < 3; i++) {
      UINT actual = data[i * (kTileBytes / 4)];
      std::cout << name << " tile " << i << " actual=0x" << std::hex << actual
                << " expected=0x" << expected[i] << std::dec << "\n";
      ok &= actual == expected[i];
    }
    readback->Unmap(0, nullptr);
    if (!ok)
      throw std::runtime_error(name);
  };
  map_all();
  execute(true);
  D3D12_TILED_RESOURCE_COORDINATE src = {}, dst = {};
  D3D12_TILE_REGION_SIZE pair = {};
  pair.NumTiles = 2;
  dst.X = 1;
  queue->CopyTileMappings(texture.ptr, &dst, texture.ptr, &src, &pair,
                          D3D12_TILE_MAPPING_FLAG_NONE);
  verify("forward", {values[0], bad_oracle ? values[1] : values[0], values[1]});
  map_all();
  src.X = 1;
  dst.X = 0;
  queue->CopyTileMappings(texture.ptr, &dst, texture.ptr, &src, &pair,
                          D3D12_TILE_MAPPING_FLAG_NONE);
  verify("reverse", {values[1], values[2], values[2]});
  map_all();
  D3D12_TILED_RESOURCE_COORDINATE one = {};
  one.X = 1;
  D3D12_TILE_REGION_SIZE single = {};
  single.NumTiles = 1;
  D3D12_TILE_RANGE_FLAGS null_flag = D3D12_TILE_RANGE_FLAG_NULL;
  queue->UpdateTileMappings(texture.ptr, 1, &one, &single, nullptr, 1,
                            &null_flag, &offset, nullptr,
                            D3D12_TILE_MAPPING_FLAG_NONE);
  src.X = 0;
  dst.X = 1;
  queue->CopyTileMappings(texture.ptr, &dst, texture.ptr, &src, &pair,
                          D3D12_TILE_MAPPING_FLAG_NONE);
  verify("null", {values[0], values[0], 0});
  // Writing an unbacked sparse tile must be discarded, not affect the heap.
  {
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Check("null write allocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&allocator.ptr)));
    Check("null write list",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    allocator.ptr, nullptr,
                                    IID_PPV_ARGS(&list.ptr)));
    D3D12_TILED_RESOURCE_COORDINATE null_tile = {};
    null_tile.X = 2;
    Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyTiles(
        texture.ptr, &null_tile, &single, upload.ptr, 0,
        D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
    Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_COMMON);
    Check("null write close", list->Close());
    ID3D12CommandList *lists[] = {list.ptr};
    queue->ExecuteCommandLists(1, lists);
    Check("null write signal", queue->Signal(fence.ptr, ++serial));
    Wait(fence.ptr, serial);
  }
  map_all();
  verify("null write discarded", values);
  if (cross_resource) {
    map_all();
    Owned<ID3D12Resource> destination;
    Check("cross-resource destination",
          device->CreateReservedResource(&d, D3D12_RESOURCE_STATE_COMMON,
                                         nullptr,
                                         IID_PPV_ARGS(&destination.ptr)));
    if (!destination->GetGPUVirtualAddress())
      throw std::runtime_error("destination has no sparse buffer backing");
    if (reverse_queue || early_work)
      Check("early consumer wait", consumer->Wait(handoff.ptr, 1));
    if (!early_work)
      queue->CopyTileMappings(destination.ptr, &zero, texture.ptr, &zero, &all,
                              D3D12_TILE_MAPPING_FLAG_NONE);
    UINT64 handoff_value = 0;
    const auto synchronize = [&] {
      if (!cross_queue)
        return;
      ++handoff_value;
      Check("producer handoff signal",
            queue->Signal(handoff.ptr, handoff_value));
      if (!((reverse_queue || early_work) && handoff_value == 1))
        Check("consumer handoff wait",
              consumer->Wait(handoff.ptr, handoff_value));
    };
    if (!early_work)
      synchronize();
    if (cross_queue)
      active_queue = consumer.ptr;
    std::swap(texture.ptr, destination.ptr);
    if (early_work) {
      after_submission = [&] {
        queue->CopyTileMappings(texture.ptr, &zero, destination.ptr, &zero,
                                &all, D3D12_TILE_MAPPING_FLAG_NONE);
        synchronize();
      };
    }
    verify("cross copied",
           {values[0], bad_cross ? values[0] : values[1], values[2]});
    queue->UpdateTileMappings(destination.ptr, 1, &one, &single, nullptr, 1,
                              &null_flag, &offset, nullptr,
                              D3D12_TILE_MAPPING_FLAG_NONE);
    synchronize();
    verify("cross independent", {values[0], values[1], values[2]});
    queue->CopyTileMappings(texture.ptr, &one, destination.ptr, &one, &single,
                            D3D12_TILE_MAPPING_FLAG_NONE);
    synchronize();
    verify("cross null", {values[0], 0, values[2]});
    std::cout << "cross-resource buffer mapping readback passed\n";
  }
  std::cout << "buffer overlap mapping readback passed\n";
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  bool bad = argc == 2 && std::strcmp(argv[1], "--bad-oracle") == 0;
  bool cross = argc == 2 && std::strcmp(argv[1], "--cross-resource") == 0;
  bool bad_cross = argc == 2 && std::strcmp(argv[1], "--bad-cross-oracle") == 0;
  bool cross_queue =
      argc == 2 && (std::strcmp(argv[1], "--cross-queue") == 0 ||
                    std::strcmp(argv[1], "--reverse-queue") == 0 ||
                    std::strcmp(argv[1], "--early-work") == 0 ||
                    std::strcmp(argv[1], "--bad-early-work") == 0 ||
                    std::strcmp(argv[1], "--bad-queue-oracle") == 0);
  bool reverse_queue =
      argc == 2 && std::strcmp(argv[1], "--reverse-queue") == 0;
  bool bad_queue =
      argc == 2 && (std::strcmp(argv[1], "--bad-queue-oracle") == 0 ||
                    std::strcmp(argv[1], "--bad-early-work") == 0);
  bool early_work =
      argc == 2 && (std::strcmp(argv[1], "--early-work") == 0 ||
                    std::strcmp(argv[1], "--bad-early-work") == 0);
  if (argc > 2 || (argc == 2 && !bad && !cross && !bad_cross && !cross_queue))
    return 2;
  try {
    return Run(bad, cross || bad_cross || cross_queue, bad_cross || bad_queue,
               cross_queue, reverse_queue, early_work);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}

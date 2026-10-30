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

UINT TileWord(UINT value, UINT word) { return value ^ (word * 0x9e3779b9u); }

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
        bool reverse_queue, bool early_work, DXGI_FORMAT format, bool bad_edge,
        bool bad_position) {
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
  D3D12_RESOURCE_DESC d = {};
  d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  const UINT bytes_per_texel = format == DXGI_FORMAT_R8_UINT             ? 1
                               : format == DXGI_FORMAT_R16_UINT          ? 2
                               : format == DXGI_FORMAT_R32G32_UINT       ? 8
                               : format == DXGI_FORMAT_R32G32B32A32_UINT ? 16
                                                                         : 4;
  const UINT tile_width = bytes_per_texel <= 2    ? 256
                          : bytes_per_texel == 16 ? 64
                                                  : 128;
  const UINT tile_height = bytes_per_texel == 1 ? 256
                           : bytes_per_texel == 8 || bytes_per_texel == 16
                               ? 64
                               : 128;
  // The last tile is half-width, so readback padding is observable.
  d.Width = tile_width * 5 / 2;
  d.Height = tile_height;
  d.DepthOrArraySize = 1;
  d.MipLevels = 1;
  d.Format = format;
  std::cout << "texture format=" << static_cast<unsigned>(format) << "\n";
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  Owned<ID3D12Resource> texture;
  Check("reserved texture",
        device->CreateReservedResource(&d, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                       IID_PPV_ARGS(&texture.ptr)));
  UINT total = 0, count = 1;
  D3D12_TILE_SHAPE shape = {};
  D3D12_SUBRESOURCE_TILING tiling = {};
  device->GetResourceTiling(texture.ptr, &total, nullptr, &shape, &count, 0,
                            &tiling);
  if (total != 3 || count != 1 || shape.WidthInTexels != tile_width ||
      shape.HeightInTexels != tile_height || tiling.WidthInTiles != 3 ||
      tiling.HeightInTiles != 1)
    throw std::runtime_error("unexpected texture tile shape");
  D3D12_HEAP_DESC hd = {};
  hd.SizeInBytes = 3ull * kTileBytes;
  hd.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
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
      data[i * (kTileBytes / 4) + j] = TileWord(values[i], j);
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
    if (!write)
      list->CopyBufferRegion(readback.ptr, 0, upload.ptr, 0, 3ull * kTileBytes);
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
    const UINT row_words = tile_width * bytes_per_texel / sizeof(UINT);
    for (UINT y = 0; y < tile_height; ++y) {
      for (UINT x = 0; x < row_words; ++x) {
        UINT word = y * row_words + x;
        if (bad_position && std::strcmp(name, "forward") == 0 &&
            y == tile_height - 1 && x == row_words / 2 - 1)
          word -= row_words;
        UINT wanted =
            x < row_words / 2 && expected[2] ? TileWord(expected[2], word) : 0;
        if (bad_edge && std::strcmp(name, "forward") == 0 &&
            y == tile_height - 1 && x == row_words / 2 - 1)
          wanted ^= 1u;
        const UINT actual = data[2 * (kTileBytes / 4) + y * row_words + x];
        if (actual != wanted) {
          std::cerr << name << " edge mismatch row=" << y << " word=" << x
                    << " actual=0x" << std::hex << actual << " expected=0x"
                    << wanted << std::dec << "\n";
          ok = false;
          break;
        }
      }
      if (!ok)
        break;
    }
    if (ok)
      std::cout << name << " full edge and padding passed\n";
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
    std::cout << "cross-resource texture mapping readback passed\n";
  }
  std::cout << "texture overlap mapping readback passed\n";
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 2)
    return 2;
  const char *arg = argc == 2 ? argv[1] : "";
  const auto is = [&](const char *name) { return std::strcmp(arg, name) == 0; };
  const bool rgba8 = is("--rgba8") || is("--bad-rgba8");
  const bool rg16 = is("--rg16");
  const bool rgb10a2 = is("--rgb10a2");
  const bool bgrx = is("--bgrx");
  const bool bad_edge = is("--bad-edge-r8");
  const bool bad_position = is("--bad-position-r8");
  const bool r8 = is("--r8") || is("--bad-r8") || bad_edge || bad_position;
  const bool r16 = is("--r16");
  const bool rg32 = is("--rg32");
  const bool rgba32 = is("--rgba32");
  const bool format_case =
      rgba8 || rg16 || rgb10a2 || bgrx || r8 || r16 || rg32 || rgba32;
  const bool bad = is("--bad-oracle");
  const bool cross = is("--cross-resource");
  const bool bad_cross = is("--bad-cross-oracle");
  const bool reverse_queue = is("--reverse-queue");
  const bool early_work =
      format_case || is("--early-work") || is("--bad-early-work");
  const bool cross_queue = early_work || reverse_queue || is("--cross-queue") ||
                           is("--bad-queue-oracle");
  const bool bad_queue = is("--bad-rgba8") || is("--bad-r8") ||
                         is("--bad-queue-oracle") || is("--bad-early-work");
  if (argc == 2 && !bad && !cross && !bad_cross && !cross_queue)
    return 2;
  const DXGI_FORMAT format = rgba8     ? DXGI_FORMAT_R8G8B8A8_UNORM
                             : rg16    ? DXGI_FORMAT_R16G16_UNORM
                             : rgb10a2 ? DXGI_FORMAT_R10G10B10A2_UNORM
                             : bgrx    ? DXGI_FORMAT_B8G8R8X8_UNORM
                             : r8      ? DXGI_FORMAT_R8_UINT
                             : r16     ? DXGI_FORMAT_R16_UINT
                             : rg32    ? DXGI_FORMAT_R32G32_UINT
                             : rgba32  ? DXGI_FORMAT_R32G32B32A32_UINT
                                       : DXGI_FORMAT_R32_UINT;
  try {
    return Run(bad, cross || bad_cross || cross_queue, bad_cross || bad_queue,
               cross_queue, reverse_queue, early_work, format, bad_edge,
               bad_position);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}

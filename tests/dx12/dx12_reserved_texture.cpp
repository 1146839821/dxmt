#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr UINT TileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT A = 0x12345678;
constexpr UINT B = 0x76543210;
constexpr UINT Written = 0xabcdef01;
constexpr UINT WriteStatus = 0xfeedface;

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
    std::cerr << operation << ": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
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

D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = bytes;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
  return desc;
}

D3D12_RESOURCE_DESC TextureDesc() {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 256;
  desc.Height = 128;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  return desc;
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

std::vector<char> ReadShader(const char *path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() <= 0)
    throw std::runtime_error("cannot open shader");
  const auto size = file.tellg();
  std::vector<char> data(static_cast<size_t>(size));
  file.seekg(0);
  file.read(data.data(), data.size());
  if (!file)
    throw std::runtime_error("cannot read shader");
  return data;
}

void Wait(ID3D12Fence *fence, UINT64 value) {
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    throw std::runtime_error("CreateEvent");
  const HRESULT hr = fence->SetEventOnCompletion(value, event);
  const DWORD status = SUCCEEDED(hr) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event);
  if (status != WAIT_OBJECT_0) {
    std::cerr << "GPU completion failed or timed out at " << value << "\n";
    ExitProcess(1);
  }
}

void CreateBuffer(ID3D12Device *device, Owned<ID3D12Resource> &resource, UINT64 bytes, D3D12_HEAP_TYPE type,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
  auto properties = Properties(type);
  auto desc = BufferDesc(bytes, flags);
  Check("committed buffer", device->CreateCommittedResource(
                                &properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                IID_PPV_ARGS(&resource.ptr)));
}

void CreateTextureUpload(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Resource *texture,
                         ID3D12Resource *upload, UINT tile, UINT upload_offset) {
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check("texture upload allocator", device->CreateCommandAllocator(
                                      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr)));
  Check("texture upload list", device->CreateCommandList(
                                  0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, nullptr,
                                  IID_PPV_ARGS(&list.ptr)));
  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  coordinate.X = tile;
  D3D12_TILE_REGION_SIZE region = {};
  region.NumTiles = 1;
  Transition(list.ptr, texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
  list->CopyTiles(texture, &coordinate, &region, upload, upload_offset,
                  D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  Transition(list.ptr, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
  Check("texture upload close", list->Close());
  ID3D12CommandList *lists[] = {list.ptr};
  queue->ExecuteCommandLists(1, lists);
}

int Run(const char *read_path, const char *write_path) {
  const auto read_shader = ReadShader(read_path);
  const auto write_shader = ReadShader(write_path);

  Owned<ID3D12Device> device;
  Check("device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)));

  Owned<ID3D12CommandQueue> direct;
  Owned<ID3D12CommandQueue> compute;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Check("direct queue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&direct.ptr)));
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  Check("compute queue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&compute.ptr)));

  Owned<ID3D12Fence> mapping_done;
  Owned<ID3D12Fence> compute_done;
  Owned<ID3D12Fence> gate;
  Check("mapping fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mapping_done.ptr)));
  Check("compute fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&compute_done.ptr)));
  Check("gate fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate.ptr)));

  Owned<ID3D12Heap> heap_a;
  Owned<ID3D12Heap> heap_b;
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = 2ull * TileBytes;
  heap_desc.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  Check("tile heap A", device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap_a.ptr)));
  heap_desc.SizeInBytes = TileBytes;
  Check("tile heap B", device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap_b.ptr)));

  Owned<ID3D12Resource> texture;
  const auto texture_desc = TextureDesc();
  Check("reserved texture", device->CreateReservedResource(
                                 &texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                 IID_PPV_ARGS(&texture.ptr)));

  Owned<ID3D12Resource> upload;
  Owned<ID3D12Resource> output;
  Owned<ID3D12Resource> output_readback;
  Owned<ID3D12Resource> tile_readback;
  CreateBuffer(device.ptr, upload, 2ull * TileBytes, D3D12_HEAP_TYPE_UPLOAD,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
  CreateBuffer(device.ptr, output, 2ull * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
  CreateBuffer(device.ptr, output_readback, 2ull * sizeof(UINT), D3D12_HEAP_TYPE_READBACK,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  CreateBuffer(device.ptr, tile_readback, 2ull * TileBytes, D3D12_HEAP_TYPE_READBACK,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

  UINT *upload_data = nullptr;
  Check("upload map", upload->Map(0, nullptr, reinterpret_cast<void **>(&upload_data)));
  for (UINT i = 0; i < TileBytes / sizeof(UINT); i++) {
    upload_data[i] = A;
    upload_data[TileBytes / sizeof(UINT) + i] = B;
  }
  upload->Unmap(0, nullptr);

  // The descriptor table is created before any mapping operation. Remapping
  // the native sparse texture must therefore not require descriptor rebuilds.
  Owned<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc = {};
  descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_desc.NumDescriptors = 3;
  descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check("descriptor heap", device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&descriptors.ptr)));
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
  const UINT stride = device->GetDescriptorHandleIncrementSize(descriptor_desc.Type);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(texture.ptr, &srv, cpu);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 2;
  uav.Buffer.StructureByteStride = sizeof(UINT);
  cpu.ptr += stride;
  device->CreateUnorderedAccessView(output.ptr, nullptr, &uav, cpu);

  uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  uav.Texture2D.MipSlice = 0;
  cpu.ptr += stride;
  device->CreateUnorderedAccessView(texture.ptr, nullptr, &uav, cpu);

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 1};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  Owned<ID3DBlob> root_blob;
  Owned<ID3DBlob> root_errors;
  Check("serialize root signature", D3D12SerializeRootSignature(
                                         &root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &root_blob.ptr, &root_errors.ptr));
  Owned<ID3D12RootSignature> root;
  Check("root signature", device->CreateRootSignature(
                              0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                              IID_PPV_ARGS(&root.ptr)));

  Owned<ID3D12PipelineState> read_pso;
  Owned<ID3D12PipelineState> write_pso;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.ptr;
  pipeline_desc.CS = {read_shader.data(), read_shader.size()};
  Check("read PSO", device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&read_pso.ptr)));
  pipeline_desc.CS = {write_shader.data(), write_shader.size()};
  Check("write PSO", device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&write_pso.ptr)));

  // Map the two standard tiles to distinct heap tiles and initialize them by
  // CopyTiles. Tile 0 contains A; tile 1 contains B.
  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  D3D12_TILE_REGION_SIZE one_tile = {};
  one_tile.NumTiles = 1;
  UINT heap_tile = 0;
  direct->UpdateTileMappings(texture.ptr, 1, &coordinate, &one_tile, heap_a.ptr, 1,
                              nullptr, &heap_tile, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
  coordinate.X = 1;
  heap_tile = 1;
  direct->UpdateTileMappings(texture.ptr, 1, &coordinate, &one_tile, heap_a.ptr, 1,
                              nullptr, &heap_tile, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);

  Owned<ID3D12CommandAllocator> init_allocator;
  Owned<ID3D12GraphicsCommandList> init_list;
  Check("init allocator", device->CreateCommandAllocator(
                                  D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&init_allocator.ptr)));
  Check("init list", device->CreateCommandList(
                            0, D3D12_COMMAND_LIST_TYPE_DIRECT, init_allocator.ptr, nullptr,
                            IID_PPV_ARGS(&init_list.ptr)));
  coordinate.X = 0;
  D3D12_TILE_REGION_SIZE two_tiles = {};
  two_tiles.NumTiles = 2;
  Transition(init_list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
  init_list->CopyTiles(texture.ptr, &coordinate, &two_tiles, upload.ptr, 0,
                       D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  Transition(init_list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
  Check("init close", init_list->Close());
  ID3D12CommandList *init_lists[] = {init_list.ptr};
  direct->ExecuteCommandLists(1, init_lists);
  Check("init signal", direct->Signal(mapping_done.ptr, 1));

  UINT64 serial = 1;
  auto phase = [&](const char *name, ID3D12Heap *target_heap, UINT target_heap_tile,
                   bool null_mapping, bool upload_b, bool write, bool copy_tiles, bool delayed,
                   UINT expected0, UINT expected1) {
    if (serial > 1)
      Check("direct waits previous compute", direct->Wait(compute_done.ptr, serial - 1));
    if (delayed)
      Check("hold mapping queue", direct->Wait(gate.ptr, 1));

    coordinate.X = 0;
    heap_tile = target_heap_tile;
    D3D12_TILE_RANGE_FLAGS range_flags = null_mapping ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE;
    direct->UpdateTileMappings(texture.ptr, 1, &coordinate, &one_tile,
                               null_mapping ? nullptr : target_heap, 1, &range_flags,
                               &heap_tile, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
    if (upload_b)
      CreateTextureUpload(device.ptr, direct.ptr, texture.ptr, upload.ptr, 0, TileBytes);
    Check("mapping signal", direct->Signal(mapping_done.ptr, serial + 1));
    Check("compute waits mapping", compute->Wait(mapping_done.ptr, serial + 1));

    // A new allocator/list per phase avoids racing DXMT's asynchronous Metal
    // command-buffer retirement when the test remaps repeatedly.
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Check("phase allocator", device->CreateCommandAllocator(
                                   D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator.ptr)));
    Check("phase list", device->CreateCommandList(
                             0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                             allocator.ptr, write ? write_pso.ptr : read_pso.ptr,
                             IID_PPV_ARGS(&list.ptr)));
    list->SetComputeRootSignature(root.ptr);
    ID3D12DescriptorHeap *heaps[] = {descriptors.ptr};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());

    const auto texture_access = write ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                                      : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON, texture_access);
    Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->Dispatch(1, 1, 1);
    Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(output_readback.ptr, 0, output.ptr, 0, 2 * sizeof(UINT));
    Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    if (copy_tiles) {
      Transition(list.ptr, texture.ptr, texture_access, D3D12_RESOURCE_STATE_COPY_SOURCE);
      coordinate.X = 0;
      list->CopyTiles(texture.ptr, &coordinate, &two_tiles, tile_readback.ptr, 0,
                      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    } else {
      Transition(list.ptr, texture.ptr, texture_access, D3D12_RESOURCE_STATE_COMMON);
    }
    Check("phase close", list->Close());
    ID3D12CommandList *lists[] = {list.ptr};
    compute->ExecuteCommandLists(1, lists);
    Check("compute signal", compute->Signal(compute_done.ptr, serial));
    if (delayed)
      Check("release mapping queue", gate->Signal(1));
    Wait(compute_done.ptr, serial++);

    UINT *result = nullptr;
    Check("output readback map", output_readback->Map(0, nullptr, reinterpret_cast<void **>(&result)));
    const UINT actual0 = result[0];
    const UINT actual1 = result[1];
    output_readback->Unmap(0, nullptr);
    if (actual0 != expected0 || actual1 != expected1) {
      std::cerr << name << " expected output 0x" << std::hex << expected0 << "/0x" << expected1
                << ", got 0x" << actual0 << "/0x" << actual1 << std::dec << "\n";
      throw std::runtime_error("texture shader result mismatch");
    }

    if (copy_tiles) {
      Check("tile readback map", tile_readback->Map(0, nullptr, reinterpret_cast<void **>(&result)));
      const UINT tile0_first = result[0];
      const UINT tile0_last = result[TileBytes / sizeof(UINT) - 1];
      const UINT tile1_first = result[TileBytes / sizeof(UINT)];
      tile_readback->Unmap(0, nullptr);
      if (tile0_first != Written || tile0_last != A || tile1_first != B) {
        std::cerr << name << " CopyTiles expected first/last 0x" << std::hex << Written << "/" << A
                  << " and tile1 0x" << B << ", got 0x" << tile0_first << "/" << tile0_last
                  << " and 0x" << tile1_first << std::dec << "\n";
        throw std::runtime_error("texture CopyTiles result mismatch");
      }
    }
    std::cout << name << " passed\n";
  };

  // The first phase repeats the initial mapping and proves a descriptor made
  // before mapping sees the initialized nonzero tiles.
  phase("nonzero backing A/B / descriptor before map", heap_a.ptr, 0, false, false, false, false, false, A, B);
  // Map tile 0 to a different heap, initialize that backing with B, and hold
  // the direct queue until after compute submission to prove ordering.
  phase("remap tile 0 to backing B / cross-queue wait", heap_b.ptr, 0, false, true, false, false, true, B, B);
  phase("NULL sample returns zero", nullptr, 0, true, false, false, false, false, 0, B);
  phase("remap back to backing A preserves contents", heap_a.ptr, 0, false, false, false, false, false, A, B);
  phase("NULL UAV write is harmless", nullptr, 0, true, false, true, false, false, WriteStatus, 0);
  phase("mapped UAV write and CopyTiles readback", heap_a.ptr, 0, false, false, true, true, false, WriteStatus, 0);
  phase("SRV sees mapped UAV write", heap_a.ptr, 0, false, false, false, false, false, Written, B);

  std::cout << "Reserved texture shader, remap, NULL and cross-queue tests passed\n";
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: dx12_reserved_texture <read.cso> <write.cso>\n";
    return 2;
  }
  try {
    return Run(argv[1], argv[2]);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}

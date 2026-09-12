#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

constexpr UINT TileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT PackedTileCount = 1;
constexpr UINT ValueA = 0x11112222;
constexpr UINT ValueB = 0x33334444;

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

D3D12_RESOURCE_DESC PackedTextureDesc() {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  // Keep the whole resource in the packed tail. This makes mip 1 a direct
  // packed-mip shader access while avoiding an assumed D3D/Metal tile index.
  desc.Width = 64;
  desc.Height = 64;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 4;
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
  Check("SetEventOnCompletion", fence->SetEventOnCompletion(value, event));
  const DWORD status = WaitForSingleObject(event, 30000);
  CloseHandle(event);
  if (status != WAIT_OBJECT_0)
    throw std::runtime_error("GPU completion timeout");
}

void CreateBuffer(ID3D12Device *device, Owned<ID3D12Resource> &resource, UINT64 bytes, D3D12_HEAP_TYPE type,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
  const auto properties = Properties(type);
  const auto desc = BufferDesc(bytes, flags);
  Check("CreateBuffer", device->CreateCommittedResource(
                            &properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource.ptr)));
}

int Run(const char *read_path, const char *write_a_path, const char *write_b_path) {
  const auto read_shader = ReadShader(read_path);
  const auto write_a_shader = ReadShader(write_a_path);
  const auto write_b_shader = ReadShader(write_b_path);

  Owned<ID3D12Device> device;
  Check("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)));

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Owned<ID3D12CommandQueue> queue;
  Check("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue.ptr)));

  Owned<ID3D12Fence> fence;
  Check("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.ptr)));

  Owned<ID3D12Resource> texture;
  const auto texture_desc = PackedTextureDesc();
  const HRESULT reserved_hr = device->CreateReservedResource(
      &texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture.ptr));
  if (FAILED(reserved_hr) &&
      (reserved_hr == E_NOTIMPL || reserved_hr == E_NOINTERFACE || reserved_hr == DXGI_ERROR_UNSUPPORTED)) {
    std::cout << "SKIP: packed reserved texture is unavailable\n";
    return 77;
  }
  Check("CreateReservedResource", reserved_hr);

  UINT total_tiles = 0;
  D3D12_PACKED_MIP_INFO packed_info = {};
  D3D12_TILE_SHAPE tile_shape = {};
  UINT subresource_count = texture_desc.MipLevels;
  std::vector<D3D12_SUBRESOURCE_TILING> tilings(subresource_count);
  device->GetResourceTiling(texture.ptr, &total_tiles, &packed_info, &tile_shape, &subresource_count, 0,
                            tilings.data());
  if (total_tiles != 1 || packed_info.NumStandardMips != 0 || packed_info.NumPackedMips != 4 ||
      packed_info.NumTilesForPackedMips != PackedTileCount || packed_info.StartTileIndexInOverallResource != 0 ||
      tile_shape.WidthInTexels || tile_shape.HeightInTexels || tile_shape.DepthInTexels ||
      subresource_count != texture_desc.MipLevels) {
    throw std::runtime_error("packed texture tiling matrix mismatch");
  }
  for (UINT mip = 0; mip < texture_desc.MipLevels; mip++) {
    if (tilings[mip].WidthInTiles || tilings[mip].HeightInTiles || tilings[mip].DepthInTiles ||
        tilings[mip].StartTileIndexInOverallResource != ~static_cast<UINT>(0))
      throw std::runtime_error("packed texture subresource tiling mismatch");
  }

  Owned<ID3D12Resource> output;
  Owned<ID3D12Resource> readback;
  CreateBuffer(device.ptr, output, 2 * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
  CreateBuffer(device.ptr, readback, 2 * sizeof(UINT), D3D12_HEAP_TYPE_READBACK,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

  Owned<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc = {};
  descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_desc.NumDescriptors = 3;
  descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check("CreateDescriptorHeap", device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&descriptors.ptr)));
  const UINT descriptor_stride = device->GetDescriptorHandleIncrementSize(descriptor_desc.Type);
  auto descriptor_cpu = descriptors->GetCPUDescriptorHandleForHeapStart();

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = texture_desc.MipLevels;
  device->CreateShaderResourceView(texture.ptr, &srv, descriptor_cpu);

  D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
  output_uav.Format = DXGI_FORMAT_UNKNOWN;
  output_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  output_uav.Buffer.NumElements = 2;
  output_uav.Buffer.StructureByteStride = sizeof(UINT);
  descriptor_cpu.ptr += descriptor_stride;
  device->CreateUnorderedAccessView(output.ptr, nullptr, &output_uav, descriptor_cpu);

  D3D12_UNORDERED_ACCESS_VIEW_DESC tail_uav = {};
  tail_uav.Format = DXGI_FORMAT_R32_UINT;
  tail_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  tail_uav.Texture2D.MipSlice = 1;
  descriptor_cpu.ptr += descriptor_stride;
  device->CreateUnorderedAccessView(texture.ptr, nullptr, &tail_uav, descriptor_cpu);

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 1};
  D3D12_ROOT_PARAMETER root_parameter = {};
  root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameter.DescriptorTable.NumDescriptorRanges = 2;
  root_parameter.DescriptorTable.pDescriptorRanges = ranges;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &root_parameter;
  Owned<ID3DBlob> root_blob;
  Owned<ID3DBlob> root_errors;
  Check("D3D12SerializeRootSignature", D3D12SerializeRootSignature(
                                             &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob.ptr,
                                             &root_errors.ptr));
  Owned<ID3D12RootSignature> root;
  Check("CreateRootSignature", device->CreateRootSignature(
                                    0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                    IID_PPV_ARGS(&root.ptr)));

  Owned<ID3D12PipelineState> read_pso;
  Owned<ID3D12PipelineState> write_a_pso;
  Owned<ID3D12PipelineState> write_b_pso;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.ptr;
  pipeline_desc.CS = {read_shader.data(), read_shader.size()};
  Check("CreateReadPSO", device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&read_pso.ptr)));
  pipeline_desc.CS = {write_a_shader.data(), write_a_shader.size()};
  Check("CreateWriteAPSO", device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&write_a_pso.ptr)));
  pipeline_desc.CS = {write_b_shader.data(), write_b_shader.size()};
  Check("CreateWriteBPSO", device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&write_b_pso.ptr)));

  Owned<ID3D12Heap> heap_a;
  Owned<ID3D12Heap> heap_b;
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = UINT64(PackedTileCount) * TileBytes;
  heap_desc.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  Check("CreatePackedHeapA", device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap_a.ptr)));
  Check("CreatePackedHeapB", device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap_b.ptr)));
  const ULONG heap_a_public_ref_after_create = heap_a->AddRef();
  heap_a->Release();

  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  coordinate.Subresource = 0;
  D3D12_TILE_REGION_SIZE region = {};
  region.NumTiles = PackedTileCount;
  UINT heap_tile = 0;

  auto map_tail = [&](ID3D12Heap *heap) {
    queue->UpdateTileMappings(texture.ptr, 1, &coordinate, &region, heap, 1, nullptr, &heap_tile, nullptr,
                              D3D12_TILE_MAPPING_FLAG_NONE);
  };

  // Create both views before the first mapping. The physical mapping must not
  // alter the descriptor's virtual resource identity.
  map_tail(heap_a.ptr);
  const ULONG heap_a_public_ref_after_mapping = heap_a->AddRef();
  heap_a->Release();
  if (heap_a_public_ref_after_mapping != heap_a_public_ref_after_create) {
    std::cerr << "packed texture mapping changed heap A public ref count from "
              << heap_a_public_ref_after_create - 1 << " to " << heap_a_public_ref_after_mapping - 1 << "\n";
    throw std::runtime_error("packed texture mapping retained a public heap reference");
  }
  std::cout << "packed texture mapping kept heap A public ref count at "
            << heap_a_public_ref_after_mapping - 1 << "\n";

  auto submit = [&](const char *name, ID3D12PipelineState *pso, bool read, UINT expected) {
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Check("CreateCommandAllocator", device->CreateCommandAllocator(
                                        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr)));
    Check("CreateCommandList", device->CreateCommandList(
                                  0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, pso,
                                  IID_PPV_ARGS(&list.ptr)));
    list->SetComputeRootSignature(root.ptr);
    ID3D12DescriptorHeap *bound_heaps[] = {descriptors.ptr};
    list->SetDescriptorHeaps(1, bound_heaps);
    list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
    if (read) {
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON,
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->Dispatch(1, 1, 1);
      Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, 2 * sizeof(UINT));
      Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATE_COMMON);
    } else {
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->Dispatch(1, 1, 1);
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    }
    Check("Close", list->Close());
    ID3D12CommandList *lists[] = {list.ptr};
    queue->ExecuteCommandLists(1, lists);
    static UINT64 fence_value = 0;
    ++fence_value;
    Check("Signal", queue->Signal(fence.ptr, fence_value));
    Wait(fence.ptr, fence_value);

    if (read) {
      UINT *mapped = nullptr;
      Check("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
      const UINT first = mapped[0];
      const UINT last = mapped[1];
      readback->Unmap(0, nullptr);
      if (first != expected || last != expected) {
        std::cerr << name << " expected 0x" << std::hex << expected << "/" << expected
                  << ", got 0x" << first << "/" << last << std::dec << "\n";
        throw std::runtime_error("packed tail shader result mismatch");
      }
    }
    if (read)
      std::cout << name << " passed\n";
  };

  submit("packed tail write on heap A", write_a_pso.ptr, false, ValueA);
  submit("packed tail read on heap A", read_pso.ptr, true, ValueA);

  // A reserved resource does not retain one application reference for each
  // mapped heap. Keep heap B alive until the remap and all GPU work that uses
  // it have completed; releasing the final heap reference earlier is invalid
  // D3D12 usage and can remove the device on native hardware.
  map_tail(heap_b.ptr);
  submit("packed tail write on heap B", write_b_pso.ptr, false, ValueB);
  submit("packed tail read on heap B", read_pso.ptr, true, ValueB);
  heap_b.ptr->Release();
  heap_b.ptr = nullptr;

  map_tail(heap_a.ptr);
  submit("packed tail remap to heap A preserves data", read_pso.ptr, true, ValueA);
  std::cout << "Packed mip sparse-tail mapping, remap, and lifetime tests passed\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: dx12_reserved_texture_packed <read.cso> <write_a.cso> <write_b.cso>\n";
    return 2;
  }
  try {
    return Run(argv[1], argv[2], argv[3]);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}

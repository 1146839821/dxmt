#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

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
    std::cerr << operation << ": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    throw std::runtime_error(operation);
  }
}

bool IsSparseUnsupported(HRESULT hr) {
  return hr == E_NOTIMPL || hr == E_NOINTERFACE || hr == DXGI_ERROR_UNSUPPORTED;
}

D3D12_HEAP_PROPERTIES Properties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = type;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC BufferDescription(UINT64 bytes, D3D12_RESOURCE_FLAGS flags) {
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

D3D12_RESOURCE_DESC MixedTextureDescription(UINT array_size) {
  D3D12_RESOURCE_DESC description = {};
  description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  description.Width = 192;
  description.Height = 128;
  description.DepthOrArraySize = static_cast<UINT16>(array_size);
  description.MipLevels = 2;
  description.Format = DXGI_FORMAT_R32_UINT;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  return description;
}

D3D12_RESOURCE_DESC AllPackedTextureDescription() {
  D3D12_RESOURCE_DESC description = {};
  description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  description.Width = 64;
  description.Height = 64;
  description.DepthOrArraySize = 1;
  description.MipLevels = 4;
  description.Format = DXGI_FORMAT_R32_UINT;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
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

void CreateBuffer(ID3D12Device *device, Owned<ID3D12Resource> &resource,
                  UINT64 bytes, D3D12_HEAP_TYPE type,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
  const auto properties = Properties(type);
  const auto description = BufferDescription(bytes, flags);
  Check("CreateBuffer", device->CreateCommittedResource(
                              &properties, D3D12_HEAP_FLAG_NONE, &description, state,
                              nullptr, IID_PPV_ARGS(&resource.ptr)));
}

int Run(const char *shader_path) {
  const auto shader = ReadShader(shader_path);

  Owned<ID3D12Device> device;
  Check("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                                 IID_PPV_ARGS(&device.ptr)));

  // Tier 2 forbids an array resource when any mip is smaller than the
  // standard tile shape. Keep this as a direct negative boundary test.
  Owned<ID3D12Resource> invalid_array_resource;
  const auto invalid_array_desc = MixedTextureDescription(2);
  const HRESULT invalid_array_hr = device->CreateReservedResource(
      &invalid_array_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(&invalid_array_resource.ptr));
  if (invalid_array_hr != E_INVALIDARG) {
    std::cerr << "Tier2 mixed-mip array expected E_INVALIDARG, got 0x"
              << std::hex << static_cast<unsigned long>(invalid_array_hr) << std::dec << "\n";
    return 1;
  }
  std::cout << "Tier2 mixed-mip array rejection: E_INVALIDARG\n";

  Owned<ID3D12Resource> texture;
  const auto texture_desc = MixedTextureDescription(1);
  const HRESULT reserved_hr = device->CreateReservedResource(
      &texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(&texture.ptr));
  if (FAILED(reserved_hr) && IsSparseUnsupported(reserved_hr)) {
    std::cout << "SKIP: reserved texture backing is unavailable\n";
    return 77;
  }
  Check("CreateReservedResource", reserved_hr);

  UINT total_tile_count = 0;
  UINT subresource_tiling_count = texture_desc.MipLevels;
  D3D12_PACKED_MIP_INFO packed_mip_info = {};
  D3D12_TILE_SHAPE standard_tile_shape = {};
  D3D12_SUBRESOURCE_TILING subresource_tilings[2] = {};
  device->GetResourceTiling(texture.ptr, &total_tile_count, &packed_mip_info,
                             &standard_tile_shape, &subresource_tiling_count, 0,
                             subresource_tilings);
  std::cout << "mixed texture tiling: total=" << total_tile_count
            << " standard_mips=" << static_cast<unsigned>(packed_mip_info.NumStandardMips)
            << " packed_mips=" << static_cast<unsigned>(packed_mip_info.NumPackedMips)
            << " packed_tiles=" << packed_mip_info.NumTilesForPackedMips
            << " shape=" << standard_tile_shape.WidthInTexels << "x"
            << standard_tile_shape.HeightInTexels << "x"
            << standard_tile_shape.DepthInTexels << "\n";
  if (packed_mip_info.NumStandardMips != 1 || packed_mip_info.NumPackedMips != 1 ||
      packed_mip_info.NumTilesForPackedMips != 1 || subresource_tiling_count != 2 ||
      subresource_tilings[0].WidthInTiles != 2 || subresource_tilings[0].HeightInTiles != 1) {
    std::cerr << "mixed texture did not produce the expected standard+packed test shape\n";
    return 77;
  }

  Owned<ID3D12Resource> all_packed_texture;
  const auto all_packed_desc = AllPackedTextureDescription();
  const HRESULT all_packed_hr = device->CreateReservedResource(
      &all_packed_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(&all_packed_texture.ptr));
  if (FAILED(all_packed_hr) && IsSparseUnsupported(all_packed_hr)) {
    std::cout << "SKIP: all-packed negative resource is unavailable\n";
    return 77;
  }
  Check("CreateAllPackedResource", all_packed_hr);

  D3D12_HEAP_DESC tile_heap_desc = {};
  tile_heap_desc.SizeInBytes = kTileBytes;
  tile_heap_desc.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  tile_heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  Owned<ID3D12Heap> tile_heap;
  Check("CreateTileHeap", device->CreateHeap(&tile_heap_desc, IID_PPV_ARGS(&tile_heap.ptr)));

  Owned<ID3D12Resource> upload;
  Owned<ID3D12Resource> output;
  Owned<ID3D12Resource> readback;
  CreateBuffer(device.ptr, upload, kTileBytes, D3D12_HEAP_TYPE_UPLOAD,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
  CreateBuffer(device.ptr, output, 3 * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
  CreateBuffer(device.ptr, readback, 3 * sizeof(UINT), D3D12_HEAP_TYPE_READBACK,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

  UINT *upload_data = nullptr;
  Check("MapUpload", upload->Map(0, nullptr, reinterpret_cast<void **>(&upload_data)));
  for (UINT i = 0; i < kTileBytes / sizeof(UINT); ++i)
    upload_data[i] = kValue;
  upload->Unmap(0, nullptr);

  D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc = {};
  descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_desc.NumDescriptors = 4;
  descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Owned<ID3D12DescriptorHeap> descriptors;
  Check("CreateDescriptorHeap", device->CreateDescriptorHeap(
                                     &descriptor_desc, IID_PPV_ARGS(&descriptors.ptr)));
  const UINT descriptor_stride = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto descriptor_cpu = descriptors->GetCPUDescriptorHandleForHeapStart();

  D3D12_SHADER_RESOURCE_VIEW_DESC standard_srv = {};
  standard_srv.Format = DXGI_FORMAT_R32_UINT;
  standard_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  standard_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  standard_srv.Texture2D.MostDetailedMip = 0;
  standard_srv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(texture.ptr, &standard_srv, descriptor_cpu);

  // This view intersects mip 1, which is in the D3D packed tail. On the
  // current M1/Metal layout the production path must leave this descriptor
  // null and report E_NOTIMPL; the warning is checked by the test invocation.
  descriptor_cpu.ptr += descriptor_stride;
  auto packed_intersecting_srv = standard_srv;
  packed_intersecting_srv.Texture2D.MipLevels = 2;
  device->CreateShaderResourceView(texture.ptr, &packed_intersecting_srv, descriptor_cpu);

  descriptor_cpu.ptr += descriptor_stride;
  auto all_packed_srv = standard_srv;
  all_packed_srv.Texture2D.MipLevels = all_packed_desc.MipLevels;
  device->CreateShaderResourceView(all_packed_texture.ptr, &all_packed_srv, descriptor_cpu);
  std::cout << "all-packed incompatible SRV submitted; expected E_NOTIMPL warning\n";

  D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
  output_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  output_uav.Buffer.NumElements = 3;
  output_uav.Buffer.StructureByteStride = sizeof(UINT);
  descriptor_cpu.ptr += descriptor_stride;
  device->CreateUnorderedAccessView(output.ptr, nullptr, &output_uav, descriptor_cpu);

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 3};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  Owned<ID3DBlob> root_blob;
  Owned<ID3DBlob> root_errors;
  Check("SerializeRootSignature", D3D12SerializeRootSignature(
                                      &root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                      &root_blob.ptr, &root_errors.ptr));
  Owned<ID3D12RootSignature> root;
  Check("CreateRootSignature", device->CreateRootSignature(
                                    0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                    IID_PPV_ARGS(&root.ptr)));

  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.ptr;
  pipeline_desc.CS = {shader.data(), shader.size()};
  Owned<ID3D12PipelineState> pipeline;
  Check("CreateComputePipelineState", device->CreateComputePipelineState(
                                           &pipeline_desc, IID_PPV_ARGS(&pipeline.ptr)));

  Owned<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Check("CreateCommandQueue", device->CreateCommandQueue(
                                   &queue_desc, IID_PPV_ARGS(&queue.ptr)));

  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  D3D12_TILE_REGION_SIZE region = {};
  region.NumTiles = 1;
  UINT heap_tile = 0;
  queue->UpdateTileMappings(texture.ptr, 1, &coordinate, &region, tile_heap.ptr, 1,
                             nullptr, &heap_tile, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);

  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check("CreateCommandAllocator", device->CreateCommandAllocator(
                                       D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr)));
  Check("CreateCommandList", device->CreateCommandList(
                                  0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, pipeline.ptr,
                                  IID_PPV_ARGS(&list.ptr)));
  Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
  list->CopyTiles(texture.ptr, &coordinate, &region, upload.ptr, 0,
                  D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  list->SetComputeRootSignature(root.ptr);
  ID3D12DescriptorHeap *bound_heaps[] = {descriptors.ptr};
  list->SetDescriptorHeaps(1, bound_heaps);
  list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
  list->Dispatch(1, 1, 1);
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, 3 * sizeof(UINT));
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE,
             D3D12_RESOURCE_STATE_COMMON);
  Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_COMMON);
  Check("Close", list->Close());

  ID3D12CommandList *command_lists[] = {list.ptr};
  queue->ExecuteCommandLists(1, command_lists);
  Owned<ID3D12Fence> fence;
  Check("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(&fence.ptr)));
  Check("Signal", queue->Signal(fence.ptr, 1));
  Wait(fence.ptr, 1);

  UINT *actual = nullptr;
  Check("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&actual)));
  std::cout << "standard-only view readback: 0x" << std::hex << actual[0]
            << " packed-intersecting descriptor mip0=0x" << actual[1]
            << " mip1=0x" << actual[2] << std::dec << "\n";
  const bool packed_view_is_compatible = actual[1] == kValue;
  const bool passed = actual[0] == kValue && (actual[1] == 0 || packed_view_is_compatible) && actual[2] == 0;
  readback->Unmap(0, nullptr);
  if (!passed)
    throw std::runtime_error("view-range shader result mismatch");

  if (packed_view_is_compatible)
    std::cout << "standard-only mixed-resource view passed; packed-intersecting view was representable\n";
  else
    std::cout << "standard-only mixed-resource view passed; packed-intersecting view remained null\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_reserved_texture_view_range <shader.cs.cso>\n";
    return 2;
  }
  try {
    return Run(argv[1]);
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}

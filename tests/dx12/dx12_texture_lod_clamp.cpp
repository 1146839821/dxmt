#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

constexpr UINT TileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT OutputCount = 5;

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

D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags) {
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

D3D12_RESOURCE_DESC TextureDesc(UINT width, UINT height, UINT mip_levels, D3D12_TEXTURE_LAYOUT layout) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = static_cast<UINT16>(mip_levels);
  desc.Format = DXGI_FORMAT_R32_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Layout = layout;
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

void Wait(ID3D12Fence *fence, UINT64 value) {
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    throw std::runtime_error("CreateEvent");
  Check("SetEventOnCompletion", fence->SetEventOnCompletion(value, event));
  if (WaitForSingleObject(event, 30000) != WAIT_OBJECT_0) {
    CloseHandle(event);
    throw std::runtime_error("GPU completion timeout");
  }
  CloseHandle(event);
}

std::vector<uint8_t> ReadFile(const char *path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() <= 0)
    throw std::runtime_error("cannot open shader");
  const auto size = file.tellg();
  file.seekg(0);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  file.read(reinterpret_cast<char *>(data.data()), data.size());
  if (!file)
    throw std::runtime_error("cannot read shader");
  return data;
}

void CreateBuffer(ID3D12Device *device, Owned<ID3D12Resource> &resource, UINT64 bytes,
                  D3D12_HEAP_TYPE type, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
  const auto properties = Properties(type);
  const auto desc = BufferDesc(bytes, flags);
  Check("CreateBuffer", device->CreateCommittedResource(
                            &properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource.ptr)));
}

void FillTile(uint32_t *destination, float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  for (UINT i = 0; i < TileBytes / sizeof(uint32_t); i++)
    destination[i] = bits;
}

void InitializeOrdinaryTexture(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Resource *texture,
                               const D3D12_RESOURCE_DESC &desc, const float *values, ID3D12Fence *fence) {
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(desc.MipLevels);
  std::vector<UINT> row_counts(desc.MipLevels);
  std::vector<UINT64> row_sizes(desc.MipLevels);
  UINT64 total_size = 0;
  device->GetCopyableFootprints(&desc, 0, desc.MipLevels, 0, footprints.data(), row_counts.data(), row_sizes.data(),
                                &total_size);

  Owned<ID3D12Resource> upload;
  CreateBuffer(device, upload, total_size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
               D3D12_RESOURCE_STATE_GENERIC_READ);
  uint8_t *mapped = nullptr;
  Check("MapTextureUpload", upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
  for (UINT mip = 0; mip < desc.MipLevels; mip++) {
    auto *row = mapped + footprints[mip].Offset;
    for (UINT y = 0; y < row_counts[mip]; y++) {
      auto *pixels = reinterpret_cast<float *>(row + y * footprints[mip].Footprint.RowPitch);
      for (UINT x = 0; x < row_sizes[mip] / sizeof(float); x++)
        pixels[x] = values[mip];
    }
  }
  upload->Unmap(0, nullptr);

  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check("CreateTextureUploadAllocator", device->CreateCommandAllocator(
                                            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr)));
  Check("CreateTextureUploadList", device->CreateCommandList(
                                        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, nullptr,
                                        IID_PPV_ARGS(&list.ptr)));
  for (UINT mip = 0; mip < desc.MipLevels; mip++) {
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = texture;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = mip;
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = upload.ptr;
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprints[mip];
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  }
  Transition(list.ptr, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
  Check("CloseTextureUploadList", list->Close());
  ID3D12CommandList *lists[] = {list.ptr};
  queue->ExecuteCommandLists(1, lists);
  Check("SignalTextureUpload", queue->Signal(fence, 1));
  Wait(fence, 1);
}

bool InitializeReservedTexture(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Resource *texture,
                               const D3D12_RESOURCE_DESC &desc, const float *values, ID3D12Heap *heap,
                               ID3D12Fence *fence) {
  UINT total_tiles = 0;
  D3D12_PACKED_MIP_INFO packed = {};
  D3D12_TILE_SHAPE shape = {};
  UINT tiling_count = desc.MipLevels;
  std::vector<D3D12_SUBRESOURCE_TILING> tilings(tiling_count);
  device->GetResourceTiling(texture, &total_tiles, &packed, &shape, &tiling_count, 0, tilings.data());
  if (packed.NumStandardMips != desc.MipLevels || packed.NumPackedMips || !total_tiles || tiling_count != desc.MipLevels)
    throw std::runtime_error("reserved LOD texture is not standard-mip-only");

  Owned<ID3D12Resource> upload;
  CreateBuffer(device, upload, UINT64(total_tiles) * TileBytes, D3D12_HEAP_TYPE_UPLOAD,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
  uint32_t *mapped = nullptr;
  Check("MapReservedTextureUpload", upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
  for (UINT mip = 0; mip < desc.MipLevels; mip++) {
    const auto &tiling = tilings[mip];
    const UINT tile_count = tiling.WidthInTiles * tiling.HeightInTiles * tiling.DepthInTiles;
    for (UINT tile = 0; tile < tile_count; tile++)
      FillTile(mapped + (tiling.StartTileIndexInOverallResource + tile) * (TileBytes / sizeof(uint32_t)), values[mip]);
  }
  upload->Unmap(0, nullptr);

  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  D3D12_TILE_REGION_SIZE region = {};
  region.NumTiles = total_tiles;
  UINT heap_tile = 0;
  queue->UpdateTileMappings(texture, 1, &coordinate, &region, heap, 1, nullptr, &heap_tile, nullptr,
                             D3D12_TILE_MAPPING_FLAG_NONE);

  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check("CreateReservedTextureUploadAllocator", device->CreateCommandAllocator(
                                                    D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr)));
  Check("CreateReservedTextureUploadList", device->CreateCommandList(
                                                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, nullptr,
                                                IID_PPV_ARGS(&list.ptr)));
  Transition(list.ptr, texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
  list->CopyTiles(texture, &coordinate, &region, upload.ptr, 0,
                  D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  Transition(list.ptr, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
  Check("CloseReservedTextureUploadList", list->Close());
  ID3D12CommandList *lists[] = {list.ptr};
  queue->ExecuteCommandLists(1, lists);
  Check("SignalReservedTextureUpload", queue->Signal(fence, 1));
  Wait(fence, 1);
  return true;
}

void CreatePipeline(ID3D12Device *device, const std::vector<uint8_t> &shader, Owned<ID3D12RootSignature> &root,
                    Owned<ID3D12PipelineState> &pipeline) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 5};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;

  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxAnisotropy = 1;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  sampler.MinLOD = 0;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderRegister = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  root_desc.NumStaticSamplers = 1;
  root_desc.pStaticSamplers = &sampler;
  Owned<ID3DBlob> root_blob;
  Owned<ID3DBlob> root_errors;
  Check("SerializeLODRootSignature", D3D12SerializeRootSignature(
                                            &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob.ptr,
                                            &root_errors.ptr));
  Check("CreateLODRootSignature", device->CreateRootSignature(
                                       0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                       IID_PPV_ARGS(&root.ptr)));

  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.ptr;
  pipeline_desc.CS.pShaderBytecode = shader.data();
  pipeline_desc.CS.BytecodeLength = shader.size();
  Check("CreateLODPipeline", device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline.ptr)));
}

std::vector<uint32_t> RunCase(ID3D12Device *device, ID3D12CommandQueue *queue,
                              const std::vector<uint8_t> &shader, bool reserved) {
  constexpr UINT mip_levels = 3;
  const auto desc = reserved ? TextureDesc(256, 256, 2, D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE)
                             : TextureDesc(512, 512, mip_levels, D3D12_TEXTURE_LAYOUT_UNKNOWN);
  const UINT case_mips = desc.MipLevels;
  const float values[] = {1.0f, 3.0f, 5.0f};
  Owned<ID3D12Fence> fence;
  Check("CreateLODFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.ptr)));

  Owned<ID3D12Resource> texture;
  if (reserved) {
    const HRESULT hr = device->CreateReservedResource(
        &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture.ptr));
    if (FAILED(hr) && IsSparseUnsupported(hr)) {
      std::cout << "SKIP: reserved standard-mip LOD backing is unavailable\n";
      return {};
    }
    Check("CreateReservedLODTexture", hr);
  } else {
    const auto properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
    Check("CreateOrdinaryLODTexture", device->CreateCommittedResource(
                                          &properties, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          IID_PPV_ARGS(&texture.ptr)));
  }

  Owned<ID3D12Heap> tile_heap;
  if (reserved) {
    UINT total_tiles = 0;
    D3D12_PACKED_MIP_INFO packed = {};
    D3D12_TILE_SHAPE shape = {};
    UINT tiling_count = case_mips;
    std::vector<D3D12_SUBRESOURCE_TILING> tilings(case_mips);
    device->GetResourceTiling(texture.ptr, &total_tiles, &packed, &shape, &tiling_count, 0, tilings.data());
    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = UINT64(total_tiles) * TileBytes;
    heap_desc.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    Check("CreateReservedLODHeap", device->CreateHeap(&heap_desc, IID_PPV_ARGS(&tile_heap.ptr)));
    InitializeReservedTexture(device, queue, texture.ptr, desc, values, tile_heap.ptr, fence.ptr);
  } else {
    InitializeOrdinaryTexture(device, queue, texture.ptr, desc, values, fence.ptr);
  }

  Owned<ID3D12Resource> output;
  Owned<ID3D12Resource> readback;
  CreateBuffer(device, output, OutputCount * sizeof(uint32_t), D3D12_HEAP_TYPE_DEFAULT,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
  CreateBuffer(device, readback, OutputCount * sizeof(uint32_t), D3D12_HEAP_TYPE_READBACK,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

  Owned<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc = {};
  descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_desc.NumDescriptors = 6;
  descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check("CreateLODDescriptorHeap", device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&descriptors.ptr)));
  const UINT stride = device->GetDescriptorHandleIncrementSize(descriptor_desc.Type);
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();

  const float clamps[] = {0.0f, 0.5f, 1.0f, 1.5f};
  for (UINT index = 0; index < 5; index++) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MostDetailedMip = index == 4 ? 1 : 0;
    srv.Texture2D.MipLevels = index == 4 ? case_mips - 1 : case_mips;
    srv.Texture2D.ResourceMinLODClamp = index == 4 ? 0.0f : clamps[index];
    device->CreateShaderResourceView(texture.ptr, &srv, cpu);
    cpu.ptr += stride;
  }

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = OutputCount;
  uav.Buffer.StructureByteStride = sizeof(uint32_t);
  device->CreateUnorderedAccessView(output.ptr, nullptr, &uav, cpu);

  Owned<ID3D12RootSignature> root;
  Owned<ID3D12PipelineState> pipeline;
  CreatePipeline(device, shader, root, pipeline);
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check("CreateLODCommandAllocator", device->CreateCommandAllocator(
                                           D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr)));
  Check("CreateLODCommandList", device->CreateCommandList(
                                     0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, pipeline.ptr,
                                     IID_PPV_ARGS(&list.ptr)));
  Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  list->SetComputeRootSignature(root.ptr);
  ID3D12DescriptorHeap *heap_list[] = {descriptors.ptr};
  list->SetDescriptorHeaps(1, heap_list);
  list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
  list->Dispatch(1, 1, 1);
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, OutputCount * sizeof(uint32_t));
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
  Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
  Check("CloseLODCommandList", list->Close());
  ID3D12CommandList *lists[] = {list.ptr};
  queue->ExecuteCommandLists(1, lists);
  Check("SignalLOD", queue->Signal(fence.ptr, 2));
  Wait(fence.ptr, 2);

  uint32_t *mapped = nullptr;
  Check("MapLODReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
  std::vector<uint32_t> result(mapped, mapped + OutputCount);
  readback->Unmap(0, nullptr);
  return result;
}

bool CheckCase(const char *name, const std::vector<uint32_t> &actual, const std::vector<uint32_t> &expected) {
  if (actual.empty())
    return true;
  bool pass = actual.size() == expected.size();
  for (size_t i = 0; i < actual.size() && i < expected.size(); i++) {
    if (actual[i] != expected[i])
      pass = false;
  }
  std::cout << name << " outputs:";
  for (uint32_t value : actual)
    std::cout << " 0x" << std::hex << value;
  std::cout << std::dec << "\n";
  if (!pass) {
    std::cerr << name << " expected:";
    for (uint32_t value : expected)
      std::cerr << " 0x" << std::hex << value;
    std::cerr << std::dec << "\n";
  }
  return pass;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_texture_lod_clamp <shader.cs.cso>\n";
    return 2;
  }

  try {
    const auto shader = ReadFile(argv[1]);
    Owned<ID3D12Device> device;
    Check("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)));
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Owned<ID3D12CommandQueue> queue;
    Check("CreateLODQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue.ptr)));
    const std::vector<uint32_t> ordinary_expected = {0x3f800000, 0x40000000, 0x40400000, 0x40800000, 0x40400000};
    const std::vector<uint32_t> reserved_expected = {0x3f800000, 0x40000000, 0x40400000, 0x40400000, 0x40400000};
    const auto ordinary = RunCase(device.ptr, queue.ptr, shader, false);
    const bool ordinary_pass = CheckCase("ordinary ResourceMinLODClamp", ordinary, ordinary_expected);
    const auto reserved = RunCase(device.ptr, queue.ptr, shader, true);
    if (reserved.empty())
      return 77;
    const bool reserved_pass = CheckCase("reserved standard-mip ResourceMinLODClamp", reserved, reserved_expected);
    if (!ordinary_pass || !reserved_pass)
      return 1;
    std::cout << "Fractional ResourceMinLODClamp ordinary/reserved matrix passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}

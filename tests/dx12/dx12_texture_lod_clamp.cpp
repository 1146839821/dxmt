#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr UINT TileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT OutputCount = 5;

class HeapLifetimeMarker : public IUnknown {
  std::atomic<ULONG> refs_{1};
  std::atomic<bool> &destroyed_;

public:
  explicit HeapLifetimeMarker(std::atomic<bool> &destroyed) : destroyed_(destroyed) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown)
      return E_NOINTERFACE;
    *out = static_cast<IUnknown *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto count = --refs_;
    if (!count) {
      destroyed_ = true;
      delete this;
    }
    return count;
  }
};
const GUID heap_marker_guid = {
    0xf7a42dc1, 0x57d1, 0x46a8, {0xb1, 0x1e, 0x54, 0x8d, 0x3a, 0x5d, 0x17, 0x82}
};

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
                    Owned<ID3D12PipelineState> &pipeline, bool limit_sampler_max, bool dynamic_sampler = false,
                    bool multi_sampler = false, bool expect_patch_rejection = false,
                    const std::vector<uint8_t> *vertex_shader = nullptr, bool no_vertex_input = false) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 5};
  D3D12_DESCRIPTOR_RANGE sampler_range = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0};
  D3D12_DESCRIPTOR_RANGE second_sampler_range = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 1, 0, 0};
  D3D12_ROOT_PARAMETER parameters[3] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 2;
  parameters[0].DescriptorTable.pDescriptorRanges = ranges;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &sampler_range;
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[2].DescriptorTable.NumDescriptorRanges = 1;
  parameters[2].DescriptorTable.pDescriptorRanges = &second_sampler_range;

  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxAnisotropy = 1;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  sampler.MinLOD = 0;
  sampler.MaxLOD = limit_sampler_max ? 0.25f : D3D12_FLOAT32_MAX;
  sampler.ShaderRegister = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = multi_sampler ? 3 : dynamic_sampler ? 2 : 1;
  root_desc.pParameters = parameters;
  root_desc.NumStaticSamplers = dynamic_sampler ? 0 : 1;
  root_desc.pStaticSamplers = dynamic_sampler ? nullptr : &sampler;
  if (vertex_shader)
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
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
  HRESULT hr;
  if (vertex_shader) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics = {};
    graphics.pRootSignature = root.ptr;
    graphics.VS = {vertex_shader->data(), vertex_shader->size()};
    graphics.PS = {shader.data(), shader.size()};
    D3D12_INPUT_ELEMENT_DESC input_element = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                                               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
    if (!no_vertex_input)
      graphics.InputLayout = {&input_element, 1};
    graphics.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    graphics.SampleMask = UINT_MAX;
    graphics.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    graphics.RasterizerState.DepthClipEnable = TRUE;
    graphics.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    graphics.SampleDesc.Count = 1;
    hr = device->CreateGraphicsPipelineState(&graphics, IID_PPV_ARGS(&pipeline.ptr));
    char enabled[2] = {};
    if (SUCCEEDED(hr) && GetEnvironmentVariableA("DXMT_TEST_MSC_LOD_PATCH", enabled, sizeof(enabled)) == 1 &&
        enabled[0] == '1') {
      Owned<ID3DBlob> cache_blob;
      if (pipeline->GetCachedBlob(&cache_blob.ptr) != E_NOTIMPL || cache_blob.ptr)
        throw std::runtime_error("patched graphics pipeline exposed cached blob");
      Owned<ID3D12Device1> device1;
      Check("QueryGraphicsDevice1", device->QueryInterface(IID_PPV_ARGS(&device1.ptr)));
      Owned<ID3D12PipelineLibrary> library;
      Check("CreateGraphicsPipelineLibrary", device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library.ptr)));
      if (library->StorePipeline(L"patched", pipeline.ptr) != E_NOTIMPL)
        throw std::runtime_error("pipeline library accepted patched graphics pipeline");
      Owned<ID3D12PipelineState> loaded;
      if (library->LoadGraphicsPipeline(L"missing", &graphics, IID_PPV_ARGS(&loaded.ptr)) != E_NOTIMPL || loaded.ptr)
        throw std::runtime_error("experimental graphics mode allowed library loading");
    }
  } else {
    hr = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline.ptr));
  }
  if (expect_patch_rejection) {
    if (hr != E_NOTIMPL || pipeline.ptr)
      throw std::runtime_error("unsupported shader was not rejected by experimental patch");
    return;
  }
  Check("CreateLODPipeline", hr);
}

struct RunOptions {
  bool reserved = false, expect_unsupported = false, late_load = false, empty_sample = false;
  bool limit_sampler_max = false, mismatch_root = false, same_root = false, switch_pipeline = false;
  bool dynamic_sampler = false, skip_sampler_update = false, multi_sampler = false, swap_sampler_tables = false;
  bool heap_rebind = false, same_heap_range = false, resubmit = false, resubmit_control = false;
  bool cache_guard = false, fail_late_sampler = false, recover_late_sampler = false, sampler_lifetime = false;
  bool expect_patch_rejection = false, multi_queue = false;
};

std::vector<uint32_t> RunCase(ID3D12Device *device, ID3D12CommandQueue *queue,
                              const std::vector<uint8_t> &shader, const RunOptions &options,
                              const std::vector<uint8_t> *vertex_shader = nullptr, bool no_vertex_input = false) {
  const auto &[reserved, expect_unsupported, late_load, empty_sample, limit_sampler_max, mismatch_root, same_root,
               switch_pipeline, dynamic_sampler, skip_sampler_update, multi_sampler, swap_sampler_tables, heap_rebind,
               same_heap_range, resubmit, resubmit_control, cache_guard, fail_late_sampler, recover_late_sampler,
               sampler_lifetime, expect_patch_rejection, multi_queue] = options;
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

  if (multi_queue) {
    Owned<ID3D12CommandAllocator> setup_allocator;
    Owned<ID3D12GraphicsCommandList> setup_list;
    Check("CreateQueueSetupAllocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                       IID_PPV_ARGS(&setup_allocator.ptr)));
    Check("CreateQueueSetupList", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                           setup_allocator.ptr, nullptr, IID_PPV_ARGS(&setup_list.ptr)));
    Transition(setup_list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Check("CloseQueueSetupList", setup_list->Close());
    ID3D12CommandList *setup_lists[] = {setup_list.ptr};
    queue->ExecuteCommandLists(1, setup_lists);
    Check("SignalQueueSetup", queue->Signal(fence.ptr, 2));
    Wait(fence.ptr, 2);
  }

  Owned<ID3D12Resource> output;
  Owned<ID3D12Resource> readback;
  CreateBuffer(device, output, OutputCount * sizeof(uint32_t), D3D12_HEAP_TYPE_DEFAULT,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
  CreateBuffer(device, readback, OutputCount * sizeof(uint32_t) * (heap_rebind ? 2 : 1), D3D12_HEAP_TYPE_READBACK,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

  Owned<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc = {};
  descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_desc.NumDescriptors = 6;
  descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check("CreateLODDescriptorHeap", device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&descriptors.ptr)));
  Owned<ID3D12DescriptorHeap> sampler_heap;
  Owned<ID3D12DescriptorHeap> rebound_sampler_heap;
  static std::atomic<bool> heap_marker_destroyed{false};
  heap_marker_destroyed = false;
  Owned<ID3D12Fence> lifetime_gate;
  Owned<ID3D12CommandAllocator> lifetime_allocator;
  struct GateGuard {
    ID3D12Fence *fence = nullptr;
    ~GateGuard() {
      if (fence)
        fence->Signal(1);
    }
  } gate_guard;
  D3D12_SAMPLER_DESC dynamic_desc = {};
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu = {};
  D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu = {};
  D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_second = {};
  if (dynamic_sampler) {
    D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
    sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_heap_desc.NumDescriptors = multi_sampler ? 4 : 2;
    sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check("CreateLODSamplerHeap", device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&sampler_heap.ptr)));
    if (sampler_lifetime || multi_queue) {
      if (sampler_lifetime) {
        auto *marker = new HeapLifetimeMarker(heap_marker_destroyed);
        const auto marker_hr = sampler_heap->SetPrivateDataInterface(heap_marker_guid, marker);
        marker->Release();
        Check("SetHeapMarker", marker_hr);
      }
      Check("CreateLifetimeGate", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&lifetime_gate.ptr)));
      gate_guard.fence = lifetime_gate.ptr;
      Check("CreateLifetimeAllocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                        IID_PPV_ARGS(&lifetime_allocator.ptr)));
    }
    const auto sampler_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    sampler_cpu = sampler_heap->GetCPUDescriptorHandleForHeapStart();
    sampler_cpu.ptr += sampler_stride;
    sampler_gpu = sampler_heap->GetGPUDescriptorHandleForHeapStart();
    sampler_gpu.ptr += sampler_stride;
    dynamic_desc.Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    dynamic_desc.AddressU = dynamic_desc.AddressV = dynamic_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    dynamic_desc.MaxAnisotropy = 1;
    dynamic_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    dynamic_desc.MinLOD = (multi_sampler || heap_rebind) ? 0.0f : 1.0f;
    dynamic_desc.MaxLOD = multi_sampler ? 0.0f : D3D12_FLOAT32_MAX;
    device->CreateSampler(&dynamic_desc, sampler_cpu);
    if (heap_rebind) {
      Check("CreateReboundSamplerHeap", device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&rebound_sampler_heap.ptr)));
      auto rebound_cpu = rebound_sampler_heap->GetCPUDescriptorHandleForHeapStart();
      rebound_cpu.ptr += sampler_stride;
      dynamic_desc.MinLOD = same_heap_range ? 0.0f : 1.0f;
      device->CreateSampler(&dynamic_desc, rebound_cpu);
    }
    if (multi_sampler) {
      auto second_cpu = sampler_cpu;
      second_cpu.ptr += 2 * sampler_stride;
      sampler_gpu_second = sampler_gpu;
      sampler_gpu_second.ptr += 2 * sampler_stride;
      dynamic_desc.MinLOD = 1.5f;
      dynamic_desc.MaxLOD = D3D12_FLOAT32_MAX;
      device->CreateSampler(&dynamic_desc, second_cpu);
    }
  }
  const UINT stride = device->GetDescriptorHandleIncrementSize(descriptor_desc.Type);
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();

  const float clamps[] = {expect_unsupported ? 1.0f : 0.0f, late_load ? 0.0f : 0.5f,
                          late_load ? 0.0f : 1.0f,
                          late_load ? 0.0f : empty_sample ? float(case_mips) - 0.5f : 1.5f};
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
  CreatePipeline(device, shader, root, pipeline, limit_sampler_max, dynamic_sampler, multi_sampler,
                 expect_patch_rejection, vertex_shader, no_vertex_input);
  if (expect_patch_rejection)
    return {};
  if (cache_guard) {
    Owned<ID3DBlob> blob;
    if (pipeline->GetCachedBlob(&blob.ptr) != E_NOTIMPL || blob.ptr)
      throw std::runtime_error("patched pipeline exposed cached blob");
    Owned<ID3D12Device1> device1;
    Check("QueryDevice1", device->QueryInterface(IID_PPV_ARGS(&device1.ptr)));
    Owned<ID3D12PipelineLibrary> library;
    Check("CreatePipelineLibrary", device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library.ptr)));
    if (library->StorePipeline(L"patched", pipeline.ptr) != E_NOTIMPL)
      throw std::runtime_error("pipeline library accepted patched pipeline");
    SetEnvironmentVariableA("DXMT_TEST_MSC_LOD_PATCH", nullptr);
    Owned<ID3D12RootSignature> normal_root;
    Owned<ID3D12PipelineState> normal_pipeline;
    CreatePipeline(device, shader, normal_root, normal_pipeline, limit_sampler_max, dynamic_sampler, multi_sampler);
    Check("StoreNormalPipeline", library->StorePipeline(L"normal", normal_pipeline.ptr));
    SetEnvironmentVariableA("DXMT_TEST_MSC_LOD_PATCH", "1");
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = normal_root.ptr;
    desc.CS = {shader.data(), shader.size()};
    Owned<ID3D12PipelineState> loaded;
    if (library->LoadComputePipeline(L"normal", &desc, IID_PPV_ARGS(&loaded.ptr)) != E_NOTIMPL || loaded.ptr)
      throw std::runtime_error("experimental mode reused a library pipeline");
    return {};
  }
  Owned<ID3D12RootSignature> alternate_root;
  Owned<ID3D12PipelineState> alternate_pipeline;
  if (mismatch_root || same_root || switch_pipeline) {
    if (vertex_shader && mismatch_root) {
      D3D12_DESCRIPTOR_RANGE ranges[3] = {
          {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, 0},
          {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 5},
          {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0}};
      D3D12_ROOT_PARAMETER params[2] = {};
      params[0].ParameterType = params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      params[0].DescriptorTable = {2, ranges};
      params[1].DescriptorTable = {1, ranges + 2};
      D3D12_ROOT_SIGNATURE_DESC empty = {};
      empty.NumParameters = 2;
      empty.pParameters = params;
      empty.Flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                                            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS);
      Owned<ID3DBlob> blob, errors;
      Check("SerializeMismatchRoot", D3D12SerializeRootSignature(&empty, D3D_ROOT_SIGNATURE_VERSION_1,
                                                                 &blob.ptr, &errors.ptr));
      Check("CreateMismatchRoot", device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                               IID_PPV_ARGS(&alternate_root.ptr)));
    } else {
      if (switch_pipeline)
        SetEnvironmentVariableA("DXMT_TEST_MSC_LOD_PATCH", nullptr);
      CreatePipeline(device, shader, alternate_root, alternate_pipeline, mismatch_root ? !limit_sampler_max : limit_sampler_max,
                     vertex_shader ? dynamic_sampler : false, false, false, vertex_shader);
      if (switch_pipeline)
        SetEnvironmentVariableA("DXMT_TEST_MSC_LOD_PATCH", "1");
    }
  }
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Owned<ID3D12Resource> render_target, vertex_buffer;
  Owned<ID3D12DescriptorHeap> rtv_heap;
  if (vertex_shader && !no_vertex_input) {
    CreateBuffer(device, vertex_buffer, 6 * sizeof(float), D3D12_HEAP_TYPE_UPLOAD,
                 D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    float *vertices = nullptr;
    Check("MapLODVertices", vertex_buffer->Map(0, nullptr, reinterpret_cast<void **>(&vertices)));
    const float positions[] = {-1, -1, -1, 3, 3, -1};
    memcpy(vertices, positions, sizeof(positions));
    vertex_buffer->Unmap(0, nullptr);
  }
  if (vertex_shader) {
    auto render_desc = TextureDesc(1, 1, 1, D3D12_TEXTURE_LAYOUT_UNKNOWN);
    render_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    render_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    auto properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
    Check("CreateLODRenderTarget", device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &render_desc,
                         D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&render_target.ptr)));
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = 1;
    Check("CreateLODRTVHeap", device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap.ptr)));
    device->CreateRenderTargetView(render_target.ptr, nullptr, rtv_heap->GetCPUDescriptorHandleForHeapStart());
  }
  Check("CreateLODCommandAllocator", device->CreateCommandAllocator(
                                           D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr)));
  Check("CreateLODCommandList", device->CreateCommandList(
                                     0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, pipeline.ptr,
                                     IID_PPV_ARGS(&list.ptr)));
  if (!multi_queue)
    Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON,
               vertex_shader ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (vertex_shader) {
    list->SetGraphicsRootSignature(mismatch_root ? alternate_root.ptr : root.ptr);
    auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport = {0, 0, 1, 1, 0, 1};
    D3D12_RECT scissor = {0, 0, 1, 1};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (!no_vertex_input) {
      D3D12_VERTEX_BUFFER_VIEW vb = {vertex_buffer->GetGPUVirtualAddress(), 6 * sizeof(float), 2 * sizeof(float)};
      list->IASetVertexBuffers(0, 1, &vb);
    }
  } else {
    list->SetComputeRootSignature((mismatch_root || same_root || switch_pipeline) ? alternate_root.ptr : root.ptr);
  }
  ID3D12DescriptorHeap *heap_list[] = {descriptors.ptr, sampler_heap.ptr};
  list->SetDescriptorHeaps(dynamic_sampler ? 2 : 1, heap_list);
  if (vertex_shader) {
    list->SetGraphicsRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
    if (dynamic_sampler)
      list->SetGraphicsRootDescriptorTable(1, sampler_gpu);
  } else {
    list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
    if (dynamic_sampler)
      list->SetComputeRootDescriptorTable(1, swap_sampler_tables ? sampler_gpu_second : sampler_gpu);
  }
  if (multi_sampler)
    list->SetComputeRootDescriptorTable(2, swap_sampler_tables ? sampler_gpu : sampler_gpu_second);
  if (switch_pipeline) {
    list->SetPipelineState(alternate_pipeline.ptr);
    if (vertex_shader)
      list->DrawInstanced(3, 1, 0, 0);
    else
      list->Dispatch(1, 1, 1);
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = output.ptr;
    list->ResourceBarrier(1, &barrier);
    list->SetPipelineState(pipeline.ptr);
  }
  if (heap_rebind) {
    list->Dispatch(1, 1, 1);
    Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(readback.ptr, OutputCount * sizeof(uint32_t), output.ptr, 0, OutputCount * sizeof(uint32_t));
    Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    heap_list[1] = rebound_sampler_heap.ptr;
    list->SetDescriptorHeaps(2, heap_list);
    list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
    auto rebound_gpu = rebound_sampler_heap->GetGPUDescriptorHandleForHeapStart();
    rebound_gpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    list->SetComputeRootDescriptorTable(1, rebound_gpu);
  }
  if (vertex_shader)
    list->DrawInstanced(3, 1, 0, 0);
  else
    list->Dispatch(1, 1, 1);
  if (mismatch_root) {
    if (list->Close() != E_INVALIDARG)
      throw std::runtime_error("patched pipeline root signature mismatch was not rejected");
    return {};
  }
  if (expect_unsupported) {
    const HRESULT close_hr = list->Close();
    if (close_hr != E_FAIL) {
      std::cerr << "Texture.Load with non-zero ResourceMinLODClamp was not rejected: 0x" << std::hex
                << static_cast<unsigned long>(close_hr) << std::dec << "\n";
      throw std::runtime_error("Texture.Load min LOD rejection");
    }
    return {};
  }
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, OutputCount * sizeof(uint32_t));
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
  if (!multi_queue)
    Transition(list.ptr, texture.ptr,
               vertex_shader ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_COMMON);
  Check("CloseLODCommandList", list->Close());
  if (dynamic_sampler && !skip_sampler_update && !multi_sampler && !heap_rebind) {
    // Root signature 1.0 permits descriptor replacement before submission.
    if (fail_late_sampler || recover_late_sampler)
      SetEnvironmentVariableA("DXMT_TEST_FAIL_SAMPLER_LOD", "1");
    dynamic_desc.MinLOD = 0.0f;
    device->CreateSampler(&dynamic_desc, sampler_cpu);
    if (fail_late_sampler || recover_late_sampler)
      SetEnvironmentVariableA("DXMT_TEST_FAIL_SAMPLER_LOD", nullptr);
    if (recover_late_sampler)
      device->CreateSampler(&dynamic_desc, sampler_cpu);
  }
  if (late_load) {
    // Root signature 1.0 descriptors can be changed after recording, before submission.
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = case_mips;
    srv.Texture2D.ResourceMinLODClamp = 1.0f;
    device->CreateShaderResourceView(texture.ptr, &srv, descriptors->GetCPUDescriptorHandleForHeapStart());
  }
  if (multi_queue) {
    Owned<ID3D12CommandQueue> second_queue;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check("CreateSecondLODQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&second_queue.ptr)));
    Owned<ID3D12Fence> second_fence;
    Check("CreateSecondLODFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&second_fence.ptr)));
    Owned<ID3D12Resource> second_output, second_readback;
    CreateBuffer(device, second_output, OutputCount * sizeof(uint32_t), D3D12_HEAP_TYPE_DEFAULT,
                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    CreateBuffer(device, second_readback, OutputCount * sizeof(uint32_t), D3D12_HEAP_TYPE_READBACK,
                 D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    Owned<ID3D12DescriptorHeap> second_descriptors;
    Check("CreateSecondLODHeap", device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&second_descriptors.ptr)));
    auto second_cpu = second_descriptors->GetCPUDescriptorHandleForHeapStart();
    device->CopyDescriptorsSimple(5, second_cpu, descriptors->GetCPUDescriptorHandleForHeapStart(), descriptor_desc.Type);
    second_cpu.ptr += 5 * stride;
    device->CreateUnorderedAccessView(second_output.ptr, nullptr, &uav, second_cpu);
    Owned<ID3D12CommandAllocator> second_allocator, second_reset_allocator;
    Owned<ID3D12GraphicsCommandList> second_list;
    Check("CreateSecondLODAllocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                      IID_PPV_ARGS(&second_allocator.ptr)));
    Check("CreateSecondResetAllocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                        IID_PPV_ARGS(&second_reset_allocator.ptr)));
    Check("CreateSecondLODList", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, second_allocator.ptr,
                                                            pipeline.ptr, IID_PPV_ARGS(&second_list.ptr)));
    Transition(second_list.ptr, second_output.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    second_list->SetComputeRootSignature(root.ptr);
    ID3D12DescriptorHeap *second_heaps[] = {second_descriptors.ptr, sampler_heap.ptr};
    second_list->SetDescriptorHeaps(2, second_heaps);
    second_list->SetComputeRootDescriptorTable(0, second_descriptors->GetGPUDescriptorHandleForHeapStart());
    second_list->SetComputeRootDescriptorTable(1, sampler_gpu);
    second_list->Dispatch(1, 1, 1);
    Transition(second_list.ptr, second_output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    second_list->CopyBufferRegion(second_readback.ptr, 0, second_output.ptr, 0, OutputCount * sizeof(uint32_t));
    Check("CloseSecondLODList", second_list->Close());
    Check("WaitSecondQueueGate", second_queue->Wait(lifetime_gate.ptr, 1));
    ID3D12CommandList *second_lists[] = {second_list.ptr};
    second_queue->ExecuteCommandLists(1, second_lists);
    ID3D12CommandList *first_lists[] = {list.ptr};
    queue->ExecuteCommandLists(1, first_lists);
    Check("ResetFirstQueuedList", list->Reset(lifetime_allocator.ptr, nullptr));
    Check("ResetSecondQueuedList", second_list->Reset(second_reset_allocator.ptr, nullptr));
    Check("SignalFirstQueue", queue->Signal(fence.ptr, 3));
    Wait(fence.ptr, 3);
    Check("SignalSecondQueue", second_queue->Signal(second_fence.ptr, 1));
    Check("ReleaseSecondQueueGate", lifetime_gate->Signal(1));
    Wait(second_fence.ptr, 1);
    std::vector<uint32_t> result;
    for (ID3D12Resource *buffer : {readback.ptr, second_readback.ptr}) {
      uint32_t *mapped = nullptr;
      Check("MapMultiQueueReadback", buffer->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
      result.insert(result.end(), mapped, mapped + OutputCount);
      buffer->Unmap(0, nullptr);
    }
    return result;
  }
  ID3D12CommandList *lists[] = {list.ptr};
  if (sampler_lifetime)
    Check("WaitLifetimeGate", queue->Wait(lifetime_gate.ptr, 1));
  queue->ExecuteCommandLists(1, lists);
  if (fail_late_sampler) {
    if (device->GetDeviceRemovedReason() != E_FAIL)
      throw std::runtime_error("invalid sampler sidecar was not rejected at submission");
    return {};
  }
  if (sampler_lifetime) {
    Check("ResetLODList", list->Reset(lifetime_allocator.ptr, nullptr));
    list.ptr->Release();
    list.ptr = nullptr;
    sampler_heap.ptr->Release();
    sampler_heap.ptr = nullptr;
    if (heap_marker_destroyed.load())
      throw std::runtime_error("sampler heap released before GPU completion");
    Check("SignalLifetimeGate", lifetime_gate->Signal(1));
  }
  Check("SignalLOD", queue->Signal(fence.ptr, 2));
  Wait(fence.ptr, 2);
  if (sampler_lifetime && !heap_marker_destroyed.load())
    throw std::runtime_error("sampler heap retained after GPU completion");

  uint32_t *mapped = nullptr;
  Check("MapLODReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
  std::vector<uint32_t> result(mapped, mapped + OutputCount * (heap_rebind ? 2 : 1));
  readback->Unmap(0, nullptr);
  if (resubmit) {
    if (!resubmit_control) {
      dynamic_desc.MinLOD = 1.0f;
      device->CreateSampler(&dynamic_desc, sampler_cpu);
    }
    queue->ExecuteCommandLists(1, lists);
    Check("SignalLODResubmit", queue->Signal(fence.ptr, 3));
    Wait(fence.ptr, 3);
    Check("MapLODResubmit", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
    result.insert(result.begin(), mapped, mapped + OutputCount);
    readback->Unmap(0, nullptr);
  }
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
  char identity_requested[2] = {};
  if (GetEnvironmentVariableA("DXMT_TEST_PRINT_LOADED_CODE", identity_requested, sizeof(identity_requested)) == 1 &&
      identity_requested[0] == '1') {
    for (const auto &[module_name, symbol] : {std::pair{"d3d12.dll", "D3D12CreateDevice"},
                                              std::pair{"winemetal.dll", "DXMTMSCPatchSamplerLOD"}}) {
      auto module = GetModuleHandleA(module_name);
      auto address = module ? reinterpret_cast<const unsigned char *>(GetProcAddress(module, symbol)) : nullptr;
      if (!address) {
        std::cerr << "loaded code identity unavailable for " << symbol << "\n";
        return 1;
      }
      static const char hex[] = "0123456789abcdef";
      std::cout << "LOADED_CODE " << module_name << " " << symbol << " ";
      for (unsigned i = 0; i < 16; ++i)
        std::cout << hex[address[i] >> 4] << hex[address[i] & 15];
      std::cout << "\n";
    }
  }
  const bool vertexid_unpatched = argc == 4 && strcmp(argv[2], "--vertexid-unpatched") == 0;
  const bool fragment_control = vertexid_unpatched || (argc == 4 && strcmp(argv[2], "--fragment-control") == 0);
  const bool fragment_mismatch = argc == 4 && strcmp(argv[2], "--fragment-mismatch") == 0;
  const bool fragment_switch = argc == 4 && strcmp(argv[2], "--fragment-switch") == 0;
  const bool fragment_vertexid = vertexid_unpatched || (argc == 4 && strcmp(argv[2], "--fragment-vertexid") == 0);
  const bool fragment_case = fragment_control || fragment_mismatch || fragment_switch || fragment_vertexid || (argc == 4 && strcmp(argv[2], "--fragment-lod") == 0);
  const bool unsupported_read = argc == 3 && strcmp(argv[2], "--load-unsupported") == 0;
  const bool late_load = argc == 3 && strcmp(argv[2], "--load-late") == 0;
  const bool empty_sample = argc == 3 && strcmp(argv[2], "--sample-empty") == 0;
  const bool limit_sampler_max = argc == 3 && strcmp(argv[2], "--sampler-max") == 0;
  const bool mismatch_root = argc == 3 && strcmp(argv[2], "--mismatch-root") == 0;
  const bool same_root = argc == 3 && strcmp(argv[2], "--same-root") == 0;
  const bool switch_pipeline = argc == 3 && strcmp(argv[2], "--pso-switch") == 0;
  const bool skip_sampler_update = argc == 3 && strcmp(argv[2], "--dynamic-stale") == 0;
  const bool swap_sampler_tables = argc == 3 && strcmp(argv[2], "--multi-swapped") == 0;
  const bool multi_sampler = swap_sampler_tables || (argc == 3 && strcmp(argv[2], "--multi-sampler") == 0);
  const bool same_heap_range = argc == 3 && strcmp(argv[2], "--heap-rebind-control") == 0;
  const bool heap_rebind = same_heap_range || (argc == 3 && strcmp(argv[2], "--heap-rebind") == 0);
  const bool resubmit_control = argc == 3 && strcmp(argv[2], "--resubmit-control") == 0;
  const bool cache_guard = argc == 3 && strcmp(argv[2], "--cache-guard") == 0;
  const bool fail_late_sampler = argc == 3 && strcmp(argv[2], "--dynamic-fail-late") == 0;
  const bool recover_late_sampler = argc == 3 && strcmp(argv[2], "--dynamic-recover") == 0;
  const bool sampler_lifetime = argc == 3 && strcmp(argv[2], "--dynamic-lifetime") == 0;
  const bool multi_queue_control = argc == 3 && strcmp(argv[2], "--multi-queue-control") == 0;
  const bool multi_queue = multi_queue_control || (argc == 3 && strcmp(argv[2], "--multi-queue") == 0);
  const bool patch_rejection = argc == 3 && strcmp(argv[2], "--patch-unsupported") == 0;
  const bool resubmit = resubmit_control || (argc == 3 && strcmp(argv[2], "--resubmit") == 0);
  const bool dynamic_sampler = multi_queue || sampler_lifetime || recover_late_sampler || fail_late_sampler || resubmit || heap_rebind || multi_sampler || skip_sampler_update || (argc == 3 && strcmp(argv[2], "--dynamic-late") == 0);
  if (argc != 2 && !fragment_case && !unsupported_read && !late_load && !empty_sample && !limit_sampler_max && !mismatch_root &&
      !same_root && !switch_pipeline && !dynamic_sampler && !cache_guard && !patch_rejection) {
    std::cerr << "usage: dx12_texture_lod_clamp <shader.cs.cso> [mode]\n"
                 "modes: --load-unsupported --load-late --sample-empty --sampler-max --mismatch-root\n"
                 "       --same-root --pso-switch --dynamic-late --dynamic-stale --dynamic-fail-late\n"
                 "       --dynamic-recover --dynamic-lifetime --multi-sampler --multi-swapped\n"
                 "       --heap-rebind --heap-rebind-control --resubmit --resubmit-control --cache-guard\n"
                 "       --patch-unsupported --multi-queue --multi-queue-control\n";
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
    RunOptions options;
    if (fragment_case) {
      auto vertex_shader = ReadFile(argv[3]);
      if (fragment_control)
        SetEnvironmentVariableA("DXMT_TEST_MSC_LOD_PATCH", nullptr);
      options.dynamic_sampler = true;
      options.switch_pipeline = fragment_switch;
      if (fragment_mismatch) {
        options.mismatch_root = true;
        RunCase(device.ptr, queue.ptr, shader, options, &vertex_shader);
        std::cout << "fragment root mismatch rejected\n";
        return 0;
      }
      const auto ordinary = RunCase(device.ptr, queue.ptr, shader, options, &vertex_shader, fragment_vertexid);
      const bool ordinary_pass = CheckCase("fragment ordinary", ordinary,
          {0x3f800000, 0x40000000, 0x40400000, 0x40800000, 0x40400000});
      options.reserved = true;
      const auto reserved = RunCase(device.ptr, queue.ptr, shader, options, &vertex_shader, fragment_vertexid);
      if (reserved.empty())
        return 77;
      const bool reserved_pass = CheckCase("fragment reserved", reserved,
          {0x3f800000, 0x40000000, 0x40400000, 0u, 0x40400000});
      if (!ordinary_pass || !reserved_pass)
        return 1;
      std::cout << "fragment LOD matrix passed\n";
      return 0;
    }
    if (patch_rejection) {
      options.expect_patch_rejection = true;
      RunCase(device.ptr, queue.ptr, shader, options);
      std::cout << "unsupported shader patch rejected\n";
      return 0;
    }
    if (fail_late_sampler) {
      options.dynamic_sampler = true;
      options.fail_late_sampler = true;
      RunCase(device.ptr, queue.ptr, shader, options);
      std::cout << "late sampler sidecar failure rejected\n";
      return 0;
    }
    if (cache_guard) {
      options.cache_guard = true;
      RunCase(device.ptr, queue.ptr, shader, options);
      std::cout << "experimental pipeline cache guards passed\n";
      return 0;
    }
    if (mismatch_root) {
      options.mismatch_root = true;
      RunCase(device.ptr, queue.ptr, shader, options);
      std::cout << "patched pipeline root mismatch rejected\n";
      return 0;
    }
    if (unsupported_read) {
      options.expect_unsupported = true;
      RunCase(device.ptr, queue.ptr, shader, options);
      std::cout << "Texture.Load with non-zero ResourceMinLODClamp rejected\n";
      return 0;
    }
    if (late_load) {
      options.late_load = true;
      const auto result = RunCase(device.ptr, queue.ptr, shader, options);
      if (result.empty() || result[0] != 0) {
        std::cerr << "late ResourceMinLODClamp Texture.Load mip 0 expected 0, got 0x" << std::hex
                  << (result.empty() ? 0xffffffffu : result[0]) << std::dec << "\n";
        return 1;
      }
      std::cout << "late ResourceMinLODClamp Texture.Load passed\n";
      return 0;
    }
    std::vector<uint32_t> ordinary_expected = (heap_rebind || resubmit)
        ? std::vector<uint32_t>{0x40400000, 0x40400000, 0x40400000, 0x40800000, 0x40a00000,
                                0x3f800000, 0x40000000, 0x40400000, 0x40800000, 0x40400000}
        : multi_sampler
        ? std::vector<uint32_t>{0x3f800000, 0x40800000, 0x40400000, 0x40800000, 0x40400000}
        : std::vector<uint32_t>{0x3f800000, 0x40000000, 0x40400000,
                                empty_sample ? 0u : 0x40800000, 0x40400000};
    // This two-mip view ends at LOD 1; a resource clamp of 1.5 is
    // beyond its final mip and therefore produces the out-of-bounds value.
    std::vector<uint32_t> reserved_expected = (heap_rebind || resubmit)
        ? std::vector<uint32_t>{0x40400000, 0x40400000, 0x40400000, 0u, 0x40400000,
                                0x3f800000, 0x40000000, 0x40400000, 0u, 0x40400000}
        : std::vector<uint32_t>{0x3f800000, multi_sampler ? 0x40400000u : 0x40000000u,
                                0x40400000, 0u, 0x40400000};
    if (multi_queue) {
      const auto ordinary_copy = ordinary_expected;
      const auto reserved_copy = reserved_expected;
      ordinary_expected.insert(ordinary_expected.end(), ordinary_copy.begin(), ordinary_copy.end());
      reserved_expected.insert(reserved_expected.end(), reserved_copy.begin(), reserved_copy.end());
      if (multi_queue_control) {
        ordinary_expected[OutputCount] = 0;
        reserved_expected[OutputCount] = 0;
      }
    }
    options.empty_sample = empty_sample;
    options.limit_sampler_max = limit_sampler_max;
    options.same_root = same_root;
    options.switch_pipeline = switch_pipeline;
    options.dynamic_sampler = dynamic_sampler;
    options.skip_sampler_update = skip_sampler_update;
    options.multi_sampler = multi_sampler;
    options.swap_sampler_tables = swap_sampler_tables;
    options.heap_rebind = heap_rebind;
    options.same_heap_range = same_heap_range;
    options.resubmit = resubmit;
    options.resubmit_control = resubmit_control;
    options.recover_late_sampler = recover_late_sampler;
    options.sampler_lifetime = sampler_lifetime;
    options.multi_queue = multi_queue;
    const auto ordinary = RunCase(device.ptr, queue.ptr, shader, options);
    const bool ordinary_pass = CheckCase("ordinary ResourceMinLODClamp", ordinary, ordinary_expected);
    options.reserved = true;
    const auto reserved = RunCase(device.ptr, queue.ptr, shader, options);
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

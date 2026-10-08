#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

constexpr UINT kTileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT kOutputCount = 18;
constexpr UINT kOneBits = 0x3f800000;
constexpr UINT kThreeBits = 0x40400000;

template <typename T> struct Owned {
  T *ptr = nullptr;

  ~Owned() {
    if (ptr)
      ptr->Release();
  }

  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
};

void Check(const char *operation, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << operation << ": 0x" << std::hex
              << static_cast<unsigned long>(hr) << std::dec << "\n";
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

D3D12_RESOURCE_DESC TextureDescription() {
  D3D12_RESOURCE_DESC description = {};
  description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  description.Width = 256;
  description.Height = 128;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.Format = DXGI_FORMAT_R32_FLOAT;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  return description;
}

D3D12_RESOURCE_DESC LodTextureDescription() {
  auto description = TextureDescription();
  description.Width = 256;
  description.Height = 256;
  description.MipLevels = 2;
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
  if (WaitForSingleObject(event, 30000) != WAIT_OBJECT_0) {
    CloseHandle(event);
    throw std::runtime_error("GPU completion timeout");
  }
  CloseHandle(event);
}

bool CompileShader(pD3DCompile compile_shader, std::vector<uint8_t> &bytecode) {
  static constexpr char source[] = R"(
Texture2D<float> point_texture : register(t0);
Texture2D<float> linear_mapped_texture : register(t1);
Texture2D<float> linear_null_texture : register(t2);
Texture2D<float> linear_mixed_texture : register(t3);
Texture2D<float> lod_texture : register(t4);
SamplerState point_sampler : register(s0);
SamplerState linear_sampler : register(s1);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint load_status_mapped = 0;
  uint load_status_null = 0;
  float load_mapped = point_texture.Load(int3(0, 0, 0), int2(0, 0), load_status_mapped);
  float load_null = point_texture.Load(int3(128, 0, 0), int2(0, 0), load_status_null);

  uint sample_status_mapped = 0;
  uint sample_status_null = 0;
  float sample_mapped = point_texture.SampleLevel(point_sampler, float2(0.25, 0.25), 0.0, int2(0, 0), sample_status_mapped);
  float sample_null = point_texture.SampleLevel(point_sampler, float2(0.75, 0.25), 0.0, int2(0, 0), sample_status_null);

  uint linear_status_mapped = 0;
  uint linear_status_null = 0;
  uint linear_status_mixed = 0;
  float linear_mapped = linear_mapped_texture.SampleLevel(linear_sampler, float2(0.5, 0.5), 0.0, int2(0, 0), linear_status_mapped);
  float linear_null = linear_null_texture.SampleLevel(linear_sampler, float2(0.5, 0.5), 0.0, int2(0, 0), linear_status_null);
  float linear_mixed = linear_mixed_texture.SampleLevel(linear_sampler, float2(0.5, 0.5), 0.0, int2(0, 0), linear_status_mixed);

  uint lod_status_unclamped = 0;
  uint lod_status_clamped = 0;
  float lod_unclamped = lod_texture.SampleGrad(
      point_sampler, float2(0.5, 0.5), float2(1.0 / 256.0, 0.0),
      float2(0.0, 1.0 / 256.0), int2(0, 0), 0.0, lod_status_unclamped);
  float lod_clamped = lod_texture.SampleGrad(
      point_sampler, float2(0.5, 0.5), float2(1.0 / 256.0, 0.0),
      float2(0.0, 1.0 / 256.0), int2(0, 0), 1.0, lod_status_clamped);

  output[0] = asuint(load_mapped);
  output[1] = CheckAccessFullyMapped(load_status_mapped) ? 0xffffffffu : 0u;
  output[2] = asuint(sample_mapped);
  output[3] = CheckAccessFullyMapped(sample_status_mapped) ? 0xffffffffu : 0u;
  output[4] = asuint(load_null);
  output[5] = CheckAccessFullyMapped(load_status_null) ? 0xffffffffu : 0u;
  output[6] = asuint(sample_null);
  output[7] = CheckAccessFullyMapped(sample_status_null) ? 0xffffffffu : 0u;
  output[8] = asuint(linear_mapped);
  output[9] = CheckAccessFullyMapped(linear_status_mapped) ? 0xffffffffu : 0u;
  output[10] = asuint(linear_null);
  output[11] = CheckAccessFullyMapped(linear_status_null) ? 0xffffffffu : 0u;
  output[12] = asuint(linear_mixed);
  output[13] = CheckAccessFullyMapped(linear_status_mixed) ? 0xffffffffu : 0u;
  output[14] = asuint(lod_unclamped);
  output[15] = CheckAccessFullyMapped(lod_status_unclamped) ? 0xffffffffu : 0u;
  output[16] = asuint(lod_clamped);
  output[17] = CheckAccessFullyMapped(lod_status_clamped) ? 0xffffffffu : 0u;
}
)";

  ID3DBlob *shader = nullptr;
  ID3DBlob *errors = nullptr;
  const HRESULT hr = compile_shader(
      source, std::strlen(source), "dx12_tiled_status_sm5.hlsl", nullptr,
      nullptr, "main", "cs_5_0",
      D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &shader,
      &errors);
  if (FAILED(hr)) {
    if (errors)
      std::cerr << static_cast<const char *>(errors->GetBufferPointer())
                << "\n";
    if (errors)
      errors->Release();
    if (shader)
      shader->Release();
    return false;
  }

  const auto *data = static_cast<const uint8_t *>(shader->GetBufferPointer());
  bytecode.assign(data, data + shader->GetBufferSize());
  if (errors)
    errors->Release();
  shader->Release();
  return !bytecode.empty();
}

int Run(pD3DCompile compile_shader) {
  std::vector<uint8_t> shader;
  if (!CompileShader(compile_shader, shader))
    throw std::runtime_error("D3DCompile feedback shader");

  Owned<ID3D12Device> device;
  Check("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                               IID_PPV_ARGS(&device.ptr)));

  D3D12_COMMAND_QUEUE_DESC queue_description = {};
  queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Owned<ID3D12CommandQueue> queue;
  Check("CreateCommandQueue",
        device.ptr->CreateCommandQueue(&queue_description,
                                       IID_PPV_ARGS(&queue.ptr)));

  Owned<ID3D12CommandAllocator> allocator;
  Check("CreateCommandAllocator",
        device.ptr->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&allocator.ptr)));

  D3D12_HEAP_DESC tile_heap_description = {};
  tile_heap_description.SizeInBytes = kTileBytes;
  tile_heap_description.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  tile_heap_description.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  Owned<ID3D12Heap> tile_heap_a;
  Owned<ID3D12Heap> tile_heap_b;
  Check("CreateTileHeapA", device.ptr->CreateHeap(&tile_heap_description,
                                                   IID_PPV_ARGS(&tile_heap_a.ptr)));
  Check("CreateTileHeapB", device.ptr->CreateHeap(&tile_heap_description,
                                                   IID_PPV_ARGS(&tile_heap_b.ptr)));

  Owned<ID3D12Resource> textures[4];
  const auto texture_description = TextureDescription();
  for (auto &texture : textures) {
    const HRESULT reserved_hr = device.ptr->CreateReservedResource(
        &texture_description, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&texture.ptr));
    if (FAILED(reserved_hr) && IsSparseUnsupported(reserved_hr)) {
      std::cout << "SKIP: reserved texture backing is unavailable\n";
      return 77;
    }
    Check("CreateReservedResource", reserved_hr);
  }

  Owned<ID3D12Resource> lod_texture;
  const auto lod_texture_description = LodTextureDescription();
  const HRESULT lod_reserved_hr = device.ptr->CreateReservedResource(
      &lod_texture_description, D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(&lod_texture.ptr));
  if (FAILED(lod_reserved_hr) && IsSparseUnsupported(lod_reserved_hr)) {
    std::cout << "SKIP: reserved per-sample LOD backing is unavailable\n";
    return 77;
  }
  Check("CreateReservedLodResource", lod_reserved_hr);

  UINT lod_total_tile_count = 0;
  D3D12_PACKED_MIP_INFO lod_packed_mip_info = {};
  D3D12_TILE_SHAPE lod_tile_shape = {};
  UINT lod_subresource_count = lod_texture_description.MipLevels;
  D3D12_SUBRESOURCE_TILING lod_subresource_tilings[2] = {};
  device.ptr->GetResourceTiling(lod_texture.ptr, &lod_total_tile_count, &lod_packed_mip_info,
                                &lod_tile_shape, &lod_subresource_count, 0,
                                lod_subresource_tilings);
  if (lod_packed_mip_info.NumPackedMips || !lod_total_tile_count ||
      !lod_tile_shape.WidthInTexels || !lod_tile_shape.HeightInTexels ||
      lod_subresource_count != lod_texture_description.MipLevels) {
    std::cerr << "per-sample LOD resource is not the expected two-level standard-mip chain\n";
    return 77;
  }

  D3D12_HEAP_DESC lod_tile_heap_description = {};
  lod_tile_heap_description.SizeInBytes = UINT64(lod_total_tile_count) * kTileBytes;
  lod_tile_heap_description.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  lod_tile_heap_description.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  Owned<ID3D12Heap> lod_tile_heap;
  Check("CreateLodTileHeap", device.ptr->CreateHeap(&lod_tile_heap_description,
                                                     IID_PPV_ARGS(&lod_tile_heap.ptr)));

  Owned<ID3D12Resource> upload;
  const auto upload_description =
      BufferDescription(kTileBytes * 2, D3D12_RESOURCE_FLAG_NONE);
  const auto upload_properties = Properties(D3D12_HEAP_TYPE_UPLOAD);
  Check("CreateUpload",
        device.ptr->CreateCommittedResource(
            &upload_properties, D3D12_HEAP_FLAG_NONE, &upload_description,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload.ptr)));
  UINT *upload_data = nullptr;
  Check("MapUpload",
        upload.ptr->Map(0, nullptr, reinterpret_cast<void **>(&upload_data)));
  for (UINT tile = 0; tile < 2; ++tile)
    for (UINT i = 0; i < kTileBytes / sizeof(UINT); ++i)
      upload_data[tile * kTileBytes / sizeof(UINT) + i] = tile ? kThreeBits : kOneBits;
  upload.ptr->Unmap(0, nullptr);

  Owned<ID3D12Resource> lod_upload;
  const auto lod_upload_description = BufferDescription(
      UINT64(lod_total_tile_count) * kTileBytes, D3D12_RESOURCE_FLAG_NONE);
  Check("CreateLodUpload", device.ptr->CreateCommittedResource(
                                  &upload_properties, D3D12_HEAP_FLAG_NONE,
                                  &lod_upload_description, D3D12_RESOURCE_STATE_GENERIC_READ,
                                  nullptr, IID_PPV_ARGS(&lod_upload.ptr)));
  UINT *lod_upload_data = nullptr;
  Check("MapLodUpload", lod_upload.ptr->Map(0, nullptr,
                                             reinterpret_cast<void **>(&lod_upload_data)));
  for (UINT tile = 0; tile < lod_total_tile_count; ++tile)
    for (UINT i = 0; i < kTileBytes / sizeof(UINT); ++i)
      lod_upload_data[tile * kTileBytes / sizeof(UINT) + i] = kThreeBits;
  const auto &lod0 = lod_subresource_tilings[0];
  const UINT lod0_tiles = lod0.WidthInTiles * lod0.HeightInTiles * lod0.DepthInTiles;
  for (UINT tile = 0; tile < lod0_tiles; ++tile)
    for (UINT i = 0; i < kTileBytes / sizeof(UINT); ++i)
      lod_upload_data[(lod0.StartTileIndexInOverallResource + tile) * (kTileBytes / sizeof(UINT)) + i] = kOneBits;
  lod_upload.ptr->Unmap(0, nullptr);

  Owned<ID3D12Resource> output;
  const auto output_description = BufferDescription(
      kOutputCount * sizeof(UINT), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  const auto default_properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  Check("CreateOutput",
        device.ptr->CreateCommittedResource(
            &default_properties, D3D12_HEAP_FLAG_NONE, &output_description,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&output.ptr)));

  Owned<ID3D12Resource> readback;
  const auto readback_description =
      BufferDescription(kOutputCount * sizeof(UINT), D3D12_RESOURCE_FLAG_NONE);
  const auto readback_properties = Properties(D3D12_HEAP_TYPE_READBACK);
  Check("CreateReadback",
        device.ptr->CreateCommittedResource(
            &readback_properties, D3D12_HEAP_FLAG_NONE, &readback_description,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&readback.ptr)));

  Owned<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_description = {};
  descriptor_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_description.NumDescriptors = 6;
  descriptor_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check("CreateDescriptorHeap",
        device.ptr->CreateDescriptorHeap(&descriptor_description,
                                         IID_PPV_ARGS(&descriptors.ptr)));
  const UINT descriptor_stride = device.ptr->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto cpu = descriptors.ptr->GetCPUDescriptorHandleForHeapStart();

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  for (auto &texture : textures) {
    device.ptr->CreateShaderResourceView(texture.ptr, &srv, cpu);
    cpu.ptr += descriptor_stride;
  }

  srv.Texture2D.MipLevels = lod_texture_description.MipLevels;
  device.ptr->CreateShaderResourceView(lod_texture.ptr, &srv, cpu);
  cpu.ptr += descriptor_stride;

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kOutputCount;
  uav.Buffer.StructureByteStride = sizeof(UINT);
  device.ptr->CreateUnorderedAccessView(output.ptr, nullptr, &uav, cpu);

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 5};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;

  D3D12_STATIC_SAMPLER_DESC static_samplers[2] = {};
  auto &point_sampler = static_samplers[0];
  point_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  point_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  point_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  point_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  point_sampler.MipLODBias = 0.0f;
  point_sampler.MaxAnisotropy = 1;
  point_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  point_sampler.MinLOD = 0.0f;
  point_sampler.MaxLOD = D3D12_FLOAT32_MAX;
  point_sampler.ShaderRegister = 0;
  point_sampler.RegisterSpace = 0;
  point_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  auto &linear_sampler = static_samplers[1];
  linear_sampler = point_sampler;
  linear_sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  linear_sampler.ShaderRegister = 1;

  D3D12_ROOT_SIGNATURE_DESC root_description = {};
  root_description.NumParameters = 1;
  root_description.pParameters = &parameter;
  root_description.NumStaticSamplers = 2;
  root_description.pStaticSamplers = static_samplers;
  Owned<ID3DBlob> root_blob;
  Owned<ID3DBlob> root_errors;
  Check("SerializeRootSignature",
        D3D12SerializeRootSignature(&root_description,
                                    D3D_ROOT_SIGNATURE_VERSION_1,
                                    &root_blob.ptr, &root_errors.ptr));

  Owned<ID3D12RootSignature> root_signature;
  Check("CreateRootSignature",
        device.ptr->CreateRootSignature(0, root_blob.ptr->GetBufferPointer(),
                                        root_blob.ptr->GetBufferSize(),
                                        IID_PPV_ARGS(&root_signature.ptr)));

  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_description = {};
  pipeline_description.pRootSignature = root_signature.ptr;
  pipeline_description.CS.pShaderBytecode = shader.data();
  pipeline_description.CS.BytecodeLength = shader.size();
  Owned<ID3D12PipelineState> pipeline;
  Check("CreateComputePipelineState",
        device.ptr->CreateComputePipelineState(&pipeline_description,
                                               IID_PPV_ARGS(&pipeline.ptr)));

  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  D3D12_TILE_REGION_SIZE region = {};
  region.NumTiles = 1;
  UINT total_tile_count = 0;
  UINT subresource_tiling_count = 1;
  D3D12_PACKED_MIP_INFO packed_mip_info = {};
  D3D12_TILE_SHAPE tile_shape = {};
  D3D12_SUBRESOURCE_TILING subresource_tiling = {};
  device.ptr->GetResourceTiling(textures[1].ptr, &total_tile_count, &packed_mip_info, &tile_shape,
                                &subresource_tiling_count, 0, &subresource_tiling);
  std::cout << "status texture tiling: total=" << total_tile_count
            << " standard=" << tile_shape.WidthInTexels << "x"
            << tile_shape.HeightInTexels << "x" << tile_shape.DepthInTexels
            << " subresource=" << subresource_tiling.WidthInTiles << "x"
            << subresource_tiling.HeightInTiles << " start="
            << subresource_tiling.StartTileIndexInOverallResource << "\n";
  auto map_tile = [&](UINT texture_index, UINT resource_tile, ID3D12Heap *heap) {
    coordinate.X = resource_tile;
    UINT heap_tile = 0;
    queue.ptr->UpdateTileMappings(textures[texture_index].ptr, 1, &coordinate, &region,
                                  heap, 1, nullptr, &heap_tile, nullptr,
                                  D3D12_TILE_MAPPING_FLAG_NONE);
  };
  map_tile(0, 0, tile_heap_a.ptr);
  map_tile(1, 0, tile_heap_a.ptr);
  map_tile(1, 1, tile_heap_b.ptr);
  map_tile(3, 0, tile_heap_a.ptr);

  D3D12_TILED_RESOURCE_COORDINATE lod_coordinate = {};
  D3D12_TILE_REGION_SIZE lod_region = {};
  lod_region.NumTiles = lod_total_tile_count;
  UINT lod_heap_tile = 0;
  queue.ptr->UpdateTileMappings(lod_texture.ptr, 1, &lod_coordinate, &lod_region,
                                lod_tile_heap.ptr, 1, nullptr, &lod_heap_tile,
                                nullptr, D3D12_TILE_MAPPING_FLAG_NONE);

  Owned<ID3D12GraphicsCommandList> list;
  Check("CreateCommandList",
        device.ptr->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      allocator.ptr, pipeline.ptr,
                                      IID_PPV_ARGS(&list.ptr)));
  for (auto &texture : textures)
    Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_DEST);
  Transition(list.ptr, lod_texture.ptr, D3D12_RESOURCE_STATE_COMMON,
             D3D12_RESOURCE_STATE_COPY_DEST);
  auto copy_tile = [&](UINT texture_index, UINT resource_tile, UINT64 upload_offset) {
    coordinate.X = resource_tile;
    list.ptr->CopyTiles(
        textures[texture_index].ptr, &coordinate, &region, upload.ptr, upload_offset,
        D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  };
  copy_tile(0, 0, 0);
  coordinate.X = 0;
  region.UseBox = TRUE;
  region.Width = 2;
  region.Height = 1;
  region.Depth = 1;
  region.NumTiles = 2;
  list.ptr->CopyTiles(
      textures[1].ptr, &coordinate, &region, upload.ptr, 0,
      D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  region = {};
  region.NumTiles = 1;
  copy_tile(3, 0, 0);
  list.ptr->CopyTiles(lod_texture.ptr, &lod_coordinate, &lod_region, lod_upload.ptr,
                      0, D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  for (auto &texture : textures)
    Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(list.ptr, lod_texture.ptr, D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  list.ptr->SetComputeRootSignature(root_signature.ptr);
  ID3D12DescriptorHeap *heap_list[] = {descriptors.ptr};
  list.ptr->SetDescriptorHeaps(1, heap_list);
  list.ptr->SetComputeRootDescriptorTable(
      0, descriptors.ptr->GetGPUDescriptorHandleForHeapStart());
  list.ptr->Dispatch(1, 1, 1);
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  list.ptr->CopyBufferRegion(readback.ptr, 0, output.ptr, 0,
                             kOutputCount * sizeof(UINT));
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE,
             D3D12_RESOURCE_STATE_COMMON);
  for (auto &texture : textures)
    Transition(list.ptr, texture.ptr,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_COMMON);
  Transition(list.ptr, lod_texture.ptr,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_COMMON);
  Check("Close", list.ptr->Close());

  ID3D12CommandList *command_lists[] = {list.ptr};
  queue.ptr->ExecuteCommandLists(1, command_lists);
  Owned<ID3D12Fence> fence;
  Check("CreateFence", device.ptr->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                               IID_PPV_ARGS(&fence.ptr)));
  Check("Signal", queue.ptr->Signal(fence.ptr, 1));
  Wait(fence.ptr, 1);

  UINT *actual = nullptr;
  Check("MapReadback",
        readback.ptr->Map(0, nullptr, reinterpret_cast<void **>(&actual)));
  const float expected_values[kOutputCount] = {
      1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
      2.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f,
      3.0f, 0.0f,
  };
  const UINT expected_status[kOutputCount] = {
      0, 0xffffffffu, 0, 0xffffffffu, 0, 0, 0, 0,
      0, 0xffffffffu, 0, 0, 0, 0, 0, 0xffffffffu,
      0, 0xffffffffu,
  };
  std::cout << "feedback outputs:";
  for (UINT i = 0; i < kOutputCount; ++i)
    std::cout << " 0x" << std::hex << actual[i];
  std::cout << std::dec << "\n";
  for (UINT i = 0; i < kOutputCount; ++i) {
    bool mismatch = false;
    if (i & 1) {
      mismatch = actual[i] != expected_status[i];
    } else {
      float actual_value = 0.0f;
      std::memcpy(&actual_value, &actual[i], sizeof(actual_value));
      mismatch = std::fabs(actual_value - expected_values[i]) > 1.0e-4f;
    }
    if (mismatch) {
      std::cerr << "feedback output[" << i << "] expected value "
                << expected_values[i] << " status 0x" << std::hex
                << expected_status[i] << ", got 0x" << actual[i] << std::dec << "\n";
      std::cerr << "actual:";
      for (UINT j = 0; j < kOutputCount; ++j)
        std::cerr << " 0x" << std::hex << actual[j];
      std::cerr << std::dec << "\n";
      readback.ptr->Unmap(0, nullptr);
      return 1;
    }
  }
  readback.ptr->Unmap(0, nullptr);
  std::cout << "DXBC cs_5_0 tiled Load/Sample/linear-footprint/per-sample-LOD "
               "feedback and CheckAccessFullyMapped passed\n";
  return 0;
}

} // namespace

int main() {
  HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
  if (!compiler) {
    std::cout << "SKIP: d3dcompiler_47.dll is unavailable\n";
    return 77;
  }

  auto compile_shader =
      reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile_shader) {
    FreeLibrary(compiler);
    std::cout << "SKIP: D3DCompile is unavailable\n";
    return 77;
  }

  int result = 1;
  try {
    result = Run(compile_shader);
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
  }
  FreeLibrary(compiler);
  return result;
}

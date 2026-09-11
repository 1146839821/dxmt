#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

constexpr UINT kTileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT kOutputCount = 8;
constexpr UINT kOneBits = 0x3f800000;

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
Texture2D<float> tiled : register(t0);
SamplerState point_sampler : register(s0);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint load_status_mapped = 0;
  uint load_status_null = 0;
  float load_mapped = tiled.Load(int3(0, 0, 0), int2(0, 0), load_status_mapped);
  float load_null = tiled.Load(int3(128, 0, 0), int2(0, 0), load_status_null);

  uint sample_status_mapped = 0;
  uint sample_status_null = 0;
  float sample_mapped = tiled.SampleLevel(point_sampler, float2(0.25, 0.25), 0.0, int2(0, 0), sample_status_mapped);
  float sample_null = tiled.SampleLevel(point_sampler, float2(0.75, 0.25), 0.0, int2(0, 0), sample_status_null);

  output[0] = asuint(load_mapped);
  output[1] = CheckAccessFullyMapped(load_status_mapped) ? 0xffffffffu : 0u;
  output[2] = asuint(sample_mapped);
  output[3] = CheckAccessFullyMapped(sample_status_mapped) ? 0xffffffffu : 0u;
  output[4] = asuint(load_null);
  output[5] = CheckAccessFullyMapped(load_status_null) ? 0xffffffffu : 0u;
  output[6] = asuint(sample_null);
  output[7] = CheckAccessFullyMapped(sample_status_null) ? 0xffffffffu : 0u;
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
  Owned<ID3D12Heap> tile_heap;
  Check("CreateTileHeap", device.ptr->CreateHeap(&tile_heap_description,
                                                 IID_PPV_ARGS(&tile_heap.ptr)));

  Owned<ID3D12Resource> texture;
  const auto texture_description = TextureDescription();
  const HRESULT reserved_hr = device.ptr->CreateReservedResource(
      &texture_description, D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(&texture.ptr));
  if (FAILED(reserved_hr) && IsSparseUnsupported(reserved_hr)) {
    std::cout << "SKIP: reserved texture backing is unavailable\n";
    return 77;
  }
  Check("CreateReservedResource", reserved_hr);

  Owned<ID3D12Resource> upload;
  const auto upload_description =
      BufferDescription(kTileBytes, D3D12_RESOURCE_FLAG_NONE);
  const auto upload_properties = Properties(D3D12_HEAP_TYPE_UPLOAD);
  Check("CreateUpload",
        device.ptr->CreateCommittedResource(
            &upload_properties, D3D12_HEAP_FLAG_NONE, &upload_description,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload.ptr)));
  UINT *upload_data = nullptr;
  Check("MapUpload",
        upload.ptr->Map(0, nullptr, reinterpret_cast<void **>(&upload_data)));
  for (UINT i = 0; i < kTileBytes / sizeof(UINT); ++i)
    upload_data[i] = kOneBits;
  upload.ptr->Unmap(0, nullptr);

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
  descriptor_description.NumDescriptors = 2;
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
  device.ptr->CreateShaderResourceView(texture.ptr, &srv, cpu);

  cpu.ptr += descriptor_stride;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kOutputCount;
  uav.Buffer.StructureByteStride = sizeof(UINT);
  device.ptr->CreateUnorderedAccessView(output.ptr, nullptr, &uav, cpu);

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;

  D3D12_STATIC_SAMPLER_DESC static_sampler = {};
  static_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  static_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  static_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  static_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  static_sampler.MipLODBias = 0.0f;
  static_sampler.MaxAnisotropy = 1;
  static_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  static_sampler.MinLOD = 0.0f;
  static_sampler.MaxLOD = D3D12_FLOAT32_MAX;
  static_sampler.ShaderRegister = 0;
  static_sampler.RegisterSpace = 0;
  static_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC root_description = {};
  root_description.NumParameters = 1;
  root_description.pParameters = &parameter;
  root_description.NumStaticSamplers = 1;
  root_description.pStaticSamplers = &static_sampler;
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
  UINT heap_tile = 0;
  queue.ptr->UpdateTileMappings(texture.ptr, 1, &coordinate, &region,
                                tile_heap.ptr, 1, nullptr, &heap_tile, nullptr,
                                D3D12_TILE_MAPPING_FLAG_NONE);

  Owned<ID3D12GraphicsCommandList> list;
  Check("CreateCommandList",
        device.ptr->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      allocator.ptr, pipeline.ptr,
                                      IID_PPV_ARGS(&list.ptr)));
  Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON,
             D3D12_RESOURCE_STATE_COPY_DEST);
  list.ptr->CopyTiles(
      texture.ptr, &coordinate, &region, upload.ptr, 0,
      D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COPY_DEST,
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
  Transition(list.ptr, texture.ptr,
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
  const UINT expected[kOutputCount] = {
      kOneBits, 0xffffffffu, kOneBits, 0xffffffffu, 0, 0, 0, 0,
  };
  for (UINT i = 0; i < kOutputCount; ++i) {
    if (actual[i] != expected[i]) {
      std::cerr << "feedback output[" << i << "] expected 0x" << std::hex
                << expected[i] << ", got 0x" << actual[i] << std::dec << "\n";
      std::cerr << "actual:";
      for (UINT j = 0; j < kOutputCount; ++j)
        std::cerr << " 0x" << std::hex << actual[j];
      std::cerr << std::dec << "\n";
      readback.ptr->Unmap(0, nullptr);
      return 1;
    }
  }
  readback.ptr->Unmap(0, nullptr);
  std::cout << "DXBC cs_5_0 tiled Load/Sample feedback and "
               "CheckAccessFullyMapped passed\n";
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

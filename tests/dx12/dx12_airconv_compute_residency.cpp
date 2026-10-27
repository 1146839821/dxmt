#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include "d3d12_device.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

template <typename T> class ComPtr {
public:
  ComPtr() = default;
  ComPtr(const ComPtr &) = delete;
  ComPtr &operator=(const ComPtr &) = delete;
  ~ComPtr() { reset(); }

  T *get() const { return value_; }
  T **put() {
    reset();
    return &value_;
  }
  void reset() {
    if (value_)
      value_->Release();
    value_ = nullptr;
  }

private:
  T *value_ = nullptr;
};

bool CheckHR(const char *operation, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << operation << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = type;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC BufferDescription(UINT64 size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC description = {};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Width = size;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  description.Flags = flags;
  return description;
}

bool CompileComputeShader(std::vector<uint8_t> &bytecode, std::vector<uint8_t> &array_bytecode) {
  static constexpr char source[] = R"HLSL(
StructuredBuffer<uint> input_data : register(t0);
RWStructuredBuffer<uint> output_data : register(u0);
cbuffer Config : register(b0) { uint base_index; uint addend; };

[numthreads(1, 1, 1)]
void cs_main(uint3 dispatch_id : SV_DispatchThreadID) {
  output_data[base_index + dispatch_id.x] = input_data[dispatch_id.x] + addend;
}
)HLSL";

  HMODULE compiler = LoadLibraryA(D3DCOMPILER_DLL_A);
  if (!compiler) {
    std::cerr << "failed to load d3dcompiler_47.dll\n";
    return false;
  }
  auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile) {
    FreeLibrary(compiler);
    std::cerr << "failed to resolve D3DCompile\n";
    return false;
  }

  ID3DBlob *shader = nullptr;
  ID3DBlob *errors = nullptr;
  const HRESULT hr = compile(
      source, sizeof(source) - 1, "airconv_compute_residency.hlsl", nullptr, nullptr, "cs_main", "cs_5_0",
      D3DCOMPILE_ENABLE_STRICTNESS, 0, &shader, &errors
  );
  if (FAILED(hr)) {
    if (errors)
      std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
    if (errors)
      errors->Release();
    if (shader)
      shader->Release();
    FreeLibrary(compiler);
    return false;
  }

  static_assert(sizeof(MTL_SM50_SHADER_ARGUMENT) == 16);
  sm50_shader_t parsed = {};
  sm50_error_t parse_error = {};
  MTL_SHADER_REFLECTION reflection = {};
  bool ranges_valid = !SM50Initialize(shader->GetBufferPointer(), shader->GetBufferSize(), &parsed,
                                     &reflection, &parse_error);
  if (ranges_valid) {
    std::array<SM50_RESOURCE_RANGE, 2> bounded{};
    bounded[1].lower_bound = 0xabcdef;
    ranges_valid = SM50GetResourceRanges(parsed, nullptr, 0) == 3 &&
                   SM50GetResourceRanges(parsed, bounded.data(), 1) == 3 && bounded[1].lower_bound == 0xabcdef;
    std::array<SM50_RESOURCE_RANGE, 3> ranges{};
    ranges_valid = ranges_valid && SM50GetResourceRanges(parsed, ranges.data(), 3) == 3;
    for (const auto &range : ranges)
      ranges_valid = ranges_valid && range.lower_bound == 0 && range.count == 1 && range.space == 0;
    std::array<SM50_RESOURCE_RANGE, 5> oversized{};
    oversized[3].lower_bound = 0x123456;
    oversized[4].lower_bound = 0x654321;
    bounded[0].lower_bound = 0xfedcba;
    ranges_valid = ranges_valid && SM50GetResourceRanges(parsed, bounded.data(), 0) == 3 &&
                   bounded[0].lower_bound == 0xfedcba &&
                   SM50GetResourceRanges(parsed, oversized.data(), oversized.size()) == 3 &&
                   oversized[3].lower_bound == 0x123456 && oversized[4].lower_bound == 0x654321 &&
                   SM50GetResourceRanges({}, nullptr, 0) == UINT32_MAX;
    SM50Destroy(parsed);
  }
  if (parse_error)
    SM50FreeError(parse_error);
  if (!ranges_valid) {
    std::cerr << "AIRCONV declared resource range introspection mismatch\n";
    if (errors)
      errors->Release();
    shader->Release();
    FreeLibrary(compiler);
    return false;
  }

  static constexpr char array_source[] = R"HLSL(
StructuredBuffer<uint> first : register(t0, space0);
StructuredBuffer<uint> seventh : register(t7);
StructuredBuffer<uint> sixty_third : register(t63);
StructuredBuffer<uint> second : register(t0, space1);
StructuredBuffer<uint> textures[16] : register(t8, space1);
RWStructuredBuffer<uint> output_data : register(u2);
cbuffer Params : register(b1) { uint index; };
[numthreads(1, 1, 1)]
void cs_main(uint3 id : SV_DispatchThreadID) {
  output_data[id.x] = first[id.x] + seventh[id.x] + sixty_third[id.x] + second[id.x] + textures[(index + id.x) & 15][id.x] + 5;
}
)HLSL";
  ID3DBlob *array_shader = nullptr;
  ID3DBlob *array_errors = nullptr;
  bool array_valid = SUCCEEDED(compile(array_source, sizeof(array_source)-1, "resource_ranges.hlsl", nullptr,
      nullptr, "cs_main", "cs_5_1", D3DCOMPILE_ENABLE_STRICTNESS, 0, &array_shader, &array_errors));
  const bool array_compiler_unavailable = !array_valid && array_errors &&
      std::strstr(static_cast<const char *>(array_errors->GetBufferPointer()), "unrecognized compiler target") != nullptr;
  parsed = {};
  parse_error = {};
  if (array_valid)
    array_valid = !SM50Initialize(array_shader->GetBufferPointer(), array_shader->GetBufferSize(), &parsed,
                                 &reflection, &parse_error);
  if (array_valid) {
    std::array<SM50_RESOURCE_RANGE, 7> ranges{};
    array_valid = SM50GetResourceRanges(parsed, ranges.data(), ranges.size()) == ranges.size();
    bool found_array = false, found_space0 = false, found_space1 = false;
    for (const auto &range : ranges) {
      if (range.type != static_cast<uint32_t>(SM50BindingType::SRV))
        continue;
      found_array |= range.lower_bound == 8 && range.count == 16 && range.space == 1;
      found_space0 |= range.lower_bound == 0 && range.count == 1 && range.space == 0;
      found_space1 |= range.lower_bound == 0 && range.count == 1 && range.space == 1;
    }
    array_valid = array_valid && found_array && found_space0 && found_space1;
  }
  if (parsed)
    SM50Destroy(parsed);
  if (!array_valid && !array_compiler_unavailable && array_errors)
    std::cerr << static_cast<const char *>(array_errors->GetBufferPointer()) << "\n";
  if (!array_valid && parse_error)
    std::cerr << SM50GetErrorMessageString(parse_error) << "\n";
  if (array_errors)
    array_errors->Release();
  if (parse_error)
    SM50FreeError(parse_error);
  if (array_valid) {
    const auto *data = static_cast<const uint8_t *>(array_shader->GetBufferPointer());
    array_bytecode.assign(data, data + array_shader->GetBufferSize());
  }
  if (array_shader)
    array_shader->Release();
  if (array_compiler_unavailable)
    std::cout << "SKIP SM5.1 array/space introspection: compiler target unavailable\n";
  if (!array_valid && !array_compiler_unavailable) {
    std::cerr << "AIRCONV dynamic array and register space declarations mismatch\n";
    if (errors)
      errors->Release();
    shader->Release();
    FreeLibrary(compiler);
    return false;
  }

  const auto *data = static_cast<const uint8_t *>(shader->GetBufferPointer());
  bytecode.assign(data, data + shader->GetBufferSize());
  if (errors)
    errors->Release();
  shader->Release();
  FreeLibrary(compiler);
  return !bytecode.empty();
}

bool CreateRootSignature(ID3D12Device *device,
                         ID3D12RootSignature **root_signature, bool extended,
                         bool directly_indexed = false, bool ambiguous = false,
                         UINT uav_count = 1) {
  D3D12_DESCRIPTOR_RANGE ranges[3] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 1;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = uav_count;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].OffsetInDescriptorsFromTableStart = 0;

  if (extended) {
    ranges[0].NumDescriptors = 128;
    ranges[1].BaseShaderRegister = 2;
    ranges[2] = ranges[0];
    ranges[2].RegisterSpace = 1;
    ranges[2].OffsetInDescriptorsFromTableStart = 128;
  }
  const D3D12_DESCRIPTOR_RANGE srv_ranges[] = {ranges[0], ambiguous ? ranges[0] : ranges[2]};
  D3D12_ROOT_PARAMETER parameters[3] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = (extended || ambiguous) ? 2 : 1;
  parameters[0].DescriptorTable.pDescriptorRanges = srv_ranges;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &ranges[1];
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[2].Descriptor.ShaderRegister = extended ? 1 : 0;
  parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC description = {};
  description.NumParameters = 3;
  if (directly_indexed)
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
  description.pParameters = parameters;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *errors = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(
      &description, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &errors
  );
  if (FAILED(hr)) {
    if (errors)
      std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
    if (errors)
      errors->Release();
    if (root_blob)
      root_blob->Release();
    return false;
  }
  hr = device->CreateRootSignature(
      0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(), IID_PPV_ARGS(root_signature)
  );
  if (errors)
    errors->Release();
  root_blob->Release();
  return CheckHR("CreateRootSignature", hr);
}

bool CreateBuffer(
    ID3D12Device *device, D3D12_HEAP_TYPE heap_type, UINT64 size, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initial_state, ID3D12Resource **resource
) {
  const auto heap = HeapProperties(heap_type);
  const auto description = BufferDescription(size, flags);
  return CheckHR(
      "CreateCommittedResource",
      device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &description, initial_state, nullptr, IID_PPV_ARGS(resource)
      )
  );
}

void SetRootBindings(
    ID3D12GraphicsCommandList *list, ID3D12PipelineState *pipeline, ID3D12RootSignature *root_signature,
    ID3D12DescriptorHeap *descriptor_heap, UINT descriptor_stride, ID3D12Resource *config, bool extended
) {
  ID3D12DescriptorHeap *heaps[] = {descriptor_heap};
  list->SetDescriptorHeaps(1, heaps);
  list->SetPipelineState(pipeline);
  list->SetComputeRootSignature(root_signature);
  auto srv_table = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
  auto uav_table = srv_table;
  uav_table.ptr += uint64_t(descriptor_stride) * (extended ? 256 : 1);
  list->SetComputeRootDescriptorTable(0, srv_table);
  list->SetComputeRootDescriptorTable(1, uav_table);
  list->SetComputeRootConstantBufferView(2, config->GetGPUVirtualAddress());
}

void Transition(
    ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after
) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  list->ResourceBarrier(1, &barrier);
}

bool WaitForQueue(ID3D12Device *device, ID3D12CommandQueue *queue) {
  ComPtr<ID3D12Fence> fence;
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()))) ||
      !CheckHR("Signal", queue->Signal(fence.get(), 1)))
    return false;
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event) {
    std::cerr << "CreateEvent failed\n";
    return false;
  }
  const HRESULT event_hr = fence.get()->SetEventOnCompletion(1, event);
  const DWORD wait_result = event_hr == S_OK ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event);
  if (!CheckHR("SetEventOnCompletion", event_hr) || wait_result != WAIT_OBJECT_0) {
    std::cerr << "queue wait failed: " << wait_result << "\n";
    return false;
  }
  return CheckHR("GetDeviceRemovedReason", device->GetDeviceRemovedReason());
}

bool TestAirconvErrorOwnership(const std::vector<uint8_t> &shader_bytecode) {
  using InitializeProc = int (*)(const void *, size_t, void **, void *, void **);
  using DestroyProc = void (*)(void *);
  using CompileProc = int (*)(void *, void *, const char *, void **, void **);
  using DestroyBitcodeProc = void (*)(void *);
  using FreeErrorProc = void (*)(void *);
  using PipelineCompileProc = int (*)(void *, void *, void *, const char *, void **, void **);

  HMODULE winemetal = LoadLibraryA("winemetal.dll");
  if (!winemetal) {
    std::cerr << "failed to load winemetal.dll for AIRCONV error ownership checks\n";
    return false;
  }
  auto initialize = reinterpret_cast<InitializeProc>(GetProcAddress(winemetal, "SM50Initialize"));
  auto destroy = reinterpret_cast<DestroyProc>(GetProcAddress(winemetal, "SM50Destroy"));
  auto compile = reinterpret_cast<CompileProc>(GetProcAddress(winemetal, "SM50Compile"));
  auto destroy_bitcode = reinterpret_cast<DestroyBitcodeProc>(GetProcAddress(winemetal, "SM50DestroyBitcode"));
  auto free_error = reinterpret_cast<FreeErrorProc>(GetProcAddress(winemetal, "SM50FreeError"));
  auto compile_geometry_vertex = reinterpret_cast<PipelineCompileProc>(
      GetProcAddress(winemetal, "SM50CompileGeometryPipelineVertex")
  );
  auto compile_geometry = reinterpret_cast<PipelineCompileProc>(
      GetProcAddress(winemetal, "SM50CompileGeometryPipelineGeometry")
  );
  auto compile_tessellation_hull = reinterpret_cast<PipelineCompileProc>(
      GetProcAddress(winemetal, "SM50CompileTessellationPipelineHull")
  );
  auto compile_tessellation_domain = reinterpret_cast<PipelineCompileProc>(
      GetProcAddress(winemetal, "SM50CompileTessellationPipelineDomain")
  );
  if (!initialize || !destroy || !compile || !destroy_bitcode || !free_error || !compile_geometry_vertex ||
      !compile_geometry || !compile_tessellation_hull || !compile_tessellation_domain) {
    std::cerr << "one or more AIRCONV error API exports are missing\n";
    FreeLibrary(winemetal);
    return false;
  }

  const std::array<uint8_t, 4> invalid_bytecode = {'D', 'X', 'B', 'C'};
  for (unsigned i = 0; i < 16; i++) {
    void *shader = nullptr;
    void *error = nullptr;
    if (initialize(shader_bytecode.data(), shader_bytecode.size(), &shader, nullptr, &error) != 0 || !shader || error) {
      std::cerr << "SM50Initialize repeated success failed at iteration " << i << "\n";
      if (error)
        free_error(error);
      if (shader)
        destroy(shader);
      FreeLibrary(winemetal);
      return false;
    }

    void *bitcode = nullptr;
    error = nullptr;
    if (compile(shader, nullptr, "cs_main", &bitcode, &error) != 0 || !bitcode || error) {
      std::cerr << "SM50Compile repeated success failed at iteration " << i << "\n";
      if (error)
        free_error(error);
      if (bitcode)
        destroy_bitcode(bitcode);
      destroy(shader);
      FreeLibrary(winemetal);
      return false;
    }
    destroy_bitcode(bitcode);
    destroy(shader);
    shader = nullptr;

    if (initialize(invalid_bytecode.data(), invalid_bytecode.size(), nullptr, nullptr, nullptr) == 0 ||
        initialize(invalid_bytecode.data(), invalid_bytecode.size(), &shader, nullptr, nullptr) == 0) {
      std::cerr << "SM50Initialize failure with null ppError was not rejected\n";
      FreeLibrary(winemetal);
      return false;
    }
    error = nullptr;
    if (initialize(invalid_bytecode.data(), invalid_bytecode.size(), &shader, nullptr, &error) == 0 || !error) {
      std::cerr << "SM50Initialize failure did not return an error object\n";
      if (error)
        free_error(error);
      FreeLibrary(winemetal);
      return false;
    }
    free_error(error);

    if (compile(shader, nullptr, "cs_main", nullptr, nullptr) == 0) {
      std::cerr << "SM50Compile failure with null ppError was not rejected\n";
      FreeLibrary(winemetal);
      return false;
    }
    destroy(shader);

    if (compile_geometry_vertex(nullptr, nullptr, nullptr, "unused", nullptr, nullptr) == 0 ||
        compile_geometry(nullptr, nullptr, nullptr, "unused", nullptr, nullptr) == 0 ||
        compile_tessellation_hull(nullptr, nullptr, nullptr, "unused", nullptr, nullptr) == 0 ||
        compile_tessellation_domain(nullptr, nullptr, nullptr, "unused", nullptr, nullptr) == 0) {
      std::cerr << "AIRCONV geometry/tessellation null ppError failure was not rejected\n";
      FreeLibrary(winemetal);
      return false;
    }
  }

  FreeLibrary(winemetal);
  std::cout << "AIRCONV repeated success/failure error ownership checks passed\n";
  return true;
}

bool RunTest(ID3D12Device *device, const D3D12_SHADER_BYTECODE &shader, bool extended = false, bool force_fallback = false) {
  constexpr UINT kElementCount = 4;
  constexpr UINT64 kBufferSize = kElementCount * sizeof(uint32_t);
  static constexpr std::array<uint32_t, kElementCount> input_values = {11, 22, 33, 44};
  static constexpr std::array<uint32_t, kElementCount> replacement_input_values = {101, 202, 303, 404};
  const std::array<uint32_t, kElementCount> expected_values = extended
      ? std::array<uint32_t, kElementCount>{510, 1015, 1520, 2025}
      : std::array<uint32_t, kElementCount>{106, 207, 308, 409};
  struct Config {
    uint32_t base_index;
    uint32_t addend;
  };

  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_description = {};
  queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_description, IID_PPV_ARGS(queue.put()))))
    return false;

  ComPtr<ID3D12RootSignature> root_signature;
  if (!CreateRootSignature(device, root_signature.put(), extended))
    return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_description = {};
  pipeline_description.pRootSignature = root_signature.get();
  pipeline_description.CS = shader;
  ComPtr<ID3D12PipelineState> pipeline;
  if (!CheckHR(
          "CreateComputePipelineState",
          device->CreateComputePipelineState(&pipeline_description, IID_PPV_ARGS(pipeline.put()))
      ))
    return false;

  auto *internal_pipeline = static_cast<dxmt::MTLD3D12PipelineState *>(pipeline.get());
  const auto &footprint = internal_pipeline->descriptor_footprint;
  bool footprint_valid = internal_pipeline->descriptor_footprint_exact && footprint.size() == (extended ? 6 : 2);
  if (footprint_valid && extended)
    footprint_valid = footprint[0].parameter_index == 0 && footprint[0].offset == 0 && footprint[0].count == 1 &&
        footprint[1].parameter_index == 0 && footprint[1].offset == 7 && footprint[1].count == 1 &&
        footprint[2].parameter_index == 0 && footprint[2].offset == 63 && footprint[2].count == 1 &&
        footprint[3].parameter_index == 0 && footprint[3].offset == 128 && footprint[3].count == 1 &&
        footprint[4].parameter_index == 0 && footprint[4].offset == 136 && footprint[4].count == 16 &&
        footprint[5].parameter_index == 1 && footprint[5].offset == 0 && footprint[5].count == 1;
  if (footprint_valid && !extended)
    footprint_valid = footprint[0].count == 1 && footprint[1].count == 1;
  if (!footprint_valid) {
    std::cerr << "AIRCONV compute static descriptor footprint mismatch\n";
    return false;
  }

  if (force_fallback)
    internal_pipeline->descriptor_footprint_exact = false;

  ComPtr<ID3D12Resource> input;
  ComPtr<ID3D12Resource> replacement_input;
  ComPtr<ID3D12Resource> output;
  ComPtr<ID3D12Resource> config;
  ComPtr<ID3D12Resource> scratch;
  ComPtr<ID3D12Resource> readback;
  if (!CreateBuffer(
          device, D3D12_HEAP_TYPE_UPLOAD, kBufferSize, D3D12_RESOURCE_FLAG_NONE,
          D3D12_RESOURCE_STATE_GENERIC_READ, input.put()
      ) ||
      !CreateBuffer(
          device, D3D12_HEAP_TYPE_UPLOAD, kBufferSize, D3D12_RESOURCE_FLAG_NONE,
          D3D12_RESOURCE_STATE_GENERIC_READ, replacement_input.put()
      ) ||
      !CreateBuffer(
          device, D3D12_HEAP_TYPE_DEFAULT, kBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS, output.put()
      ) ||
      !CreateBuffer(
          device, D3D12_HEAP_TYPE_UPLOAD, 256, D3D12_RESOURCE_FLAG_NONE,
          D3D12_RESOURCE_STATE_GENERIC_READ, config.put()
      ) ||
      !CreateBuffer(
          device, D3D12_HEAP_TYPE_DEFAULT, kBufferSize, D3D12_RESOURCE_FLAG_NONE,
          D3D12_RESOURCE_STATE_COPY_DEST, scratch.put()
      ) ||
      !CreateBuffer(
          device, D3D12_HEAP_TYPE_READBACK, kBufferSize, D3D12_RESOURCE_FLAG_NONE,
          D3D12_RESOURCE_STATE_COPY_DEST, readback.put()
      ))
    return false;

  void *mapped = nullptr;
  if (!CheckHR("MapInput", input.get()->Map(0, nullptr, &mapped)))
    return false;
  std::memcpy(mapped, input_values.data(), kBufferSize);
  input.get()->Unmap(0, nullptr);
  if (!CheckHR("MapReplacementInput", replacement_input.get()->Map(0, nullptr, &mapped)))
    return false;
  std::memcpy(mapped, replacement_input_values.data(), kBufferSize);
  replacement_input.get()->Unmap(0, nullptr);
  mapped = nullptr;
  if (!CheckHR("MapConfig", config.get()->Map(0, nullptr, &mapped)))
    return false;
  const Config config_value = {0, 5};
  std::memcpy(mapped, &config_value, sizeof(config_value));
  config.get()->Unmap(0, nullptr);

  D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_description = {};
  descriptor_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_heap_description.NumDescriptors = extended ? 258 : 130;
  descriptor_heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> descriptor_heap;
  if (!CheckHR(
          "CreateDescriptorHeap",
          device->CreateDescriptorHeap(&descriptor_heap_description, IID_PPV_ARGS(descriptor_heap.put()))
      ))
    return false;
  const UINT descriptor_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if (!descriptor_stride)
    return false;

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Buffer.NumElements = kElementCount;
  srv.Buffer.StructureByteStride = sizeof(uint32_t);
  auto write_inputs = [&](ID3D12Resource *resource) {
    auto handle = descriptor_heap.get()->GetCPUDescriptorHandleForHeapStart();
    device->CreateShaderResourceView(resource, &srv, handle);
    if (extended) {
      for (UINT index : {7u, 63u}) {
        auto disjoint = handle;
        disjoint.ptr += uint64_t(descriptor_stride) * index;
        device->CreateShaderResourceView(resource, &srv, disjoint);
      }
      handle.ptr += uint64_t(descriptor_stride) * 128;
      device->CreateShaderResourceView(resource, &srv, handle);
      handle.ptr += uint64_t(descriptor_stride) * 8;
      for (UINT i = 0; i < 16; i++, handle.ptr += descriptor_stride)
        device->CreateShaderResourceView(resource, &srv, handle);
    }
  };
  write_inputs(input.get());

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kElementCount;
  uav.Buffer.StructureByteStride = sizeof(uint32_t);
  auto uav_handle = descriptor_heap.get()->GetCPUDescriptorHandleForHeapStart();
  uav_handle.ptr += uint64_t(descriptor_stride) * (extended ? 256 : 1);
  device->CreateUnorderedAccessView(output.get(), nullptr, &uav, uav_handle);

  ComPtr<ID3D12CommandAllocator> allocator_a;
  ComPtr<ID3D12CommandAllocator> allocator_b;
  if (!CheckHR(
          "CreateCommandAllocator A",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator_a.put()))
      ) ||
      !CheckHR(
          "CreateCommandAllocator B",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator_b.put()))
      ))
    return false;

  ComPtr<ID3D12GraphicsCommandList> list_a;
  ComPtr<ID3D12GraphicsCommandList> list_b;
  if (!CheckHR(
          "CreateCommandList A",
          device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_a.get(), pipeline.get(), IID_PPV_ARGS(list_a.put())
          )
      ) ||
      !CheckHR(
          "CreateCommandList B",
          device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_b.get(), pipeline.get(), IID_PPV_ARGS(list_b.put())
          )
      ))
    return false;

  SetRootBindings(list_a.get(), pipeline.get(), root_signature.get(), descriptor_heap.get(), descriptor_stride, config.get(), extended);
  list_a.get()->Dispatch(kElementCount, 1, 1);
  auto unrelated_descriptor = descriptor_heap.get()->GetCPUDescriptorHandleForHeapStart();
  unrelated_descriptor.ptr += uint64_t(descriptor_stride) * 64;
  device->CreateShaderResourceView(input.get(), &srv, unrelated_descriptor);
  list_a.get()->Dispatch(kElementCount, 1, 1);
  write_inputs(replacement_input.get());
  list_a.get()->Dispatch(kElementCount, 1, 1);
  Transition(list_a.get(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list_a.get()->CopyBufferRegion(scratch.get(), 0, output.get(), 0, kBufferSize);
  Transition(list_a.get(), output.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  list_a.get()->Dispatch(kElementCount, 1, 1);
  if (!CheckHR("Close command list A", list_a.get()->Close()))
    return false;

  SetRootBindings(list_b.get(), pipeline.get(), root_signature.get(), descriptor_heap.get(), descriptor_stride, config.get(), extended);
  D3D12_RESOURCE_BARRIER uav_barrier = {};
  uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uav_barrier.UAV.pResource = output.get();
  list_b.get()->ResourceBarrier(1, &uav_barrier);
  list_b.get()->Dispatch(kElementCount, 1, 1);
  Transition(list_b.get(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list_b.get()->CopyBufferRegion(readback.get(), 0, output.get(), 0, kBufferSize);
  if (!CheckHR("Close command list B", list_b.get()->Close()))
    return false;

  auto *internal_list_a = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list_a.get());
  const auto recording_counters = internal_list_a->GetAirconvResidencyCounters();
  if (recording_counters.descriptor_requests != 4 || recording_counters.descriptor_scans_executed != 4 ||
      recording_counters.descriptor_scan_skips != 0 || recording_counters.heap_global_generation_changes_seen != 2 ||
      recording_counters.heap_generation_invalidations != 2 ||
      recording_counters.conservative_descriptor_slots != (extended ? 1028 : 8) ||
      recording_counters.shader_reachable_descriptor_slots != (extended ? (force_fallback ? 1028 : 84) : 8) ||
      recording_counters.descriptor_slots_visited != (extended ? (force_fallback ? 1028 : 84) : 8)) {
    std::cerr << "AIRCONV compute descriptor-generation recording coverage mismatch: requests="
              << recording_counters.descriptor_requests << ", scans=" << recording_counters.descriptor_scans_executed
              << ", skips=" << recording_counters.descriptor_scan_skips
              << ", global=" << recording_counters.heap_global_generation_changes_seen
              << ", invalidations=" << recording_counters.heap_generation_invalidations << "\n";
    return false;
  }

  // Root Signature 1.0 makes descriptor-table entries volatile. Rewriting the
  // SRV after both lists close but before ExecuteCommandLists changes what they use.
  write_inputs(replacement_input.get());

  // The closed command lists still refer to the descriptor heap, whose current
  // contents are resolved when the lists reach the queue. Drop the application's
  // heap reference to exercise the command list's unique-heap retention.
  descriptor_heap.reset();

  // Drop application references to the descriptor resources before submission.
  input.reset();
  replacement_input.reset();
  output.reset();
  config.reset();

  ID3D12CommandList *lists[] = {list_a.get(), list_b.get()};
  queue.get()->ExecuteCommandLists(2, lists);
  if (!WaitForQueue(device, queue.get()))
    return false;

  const auto submission_counters = internal_list_a->GetAirconvResidencyCounters();
  if (submission_counters.submission_pending_uses != (extended ? (force_fallback ? 514 : 42) : 4) || submission_counters.submission_unique_heaps != 1 ||
      submission_counters.submission_unique_slots != (extended ? (force_fallback ? 257 : 21) : 2) || submission_counters.submission_batch_locks != 1 ||
      submission_counters.submission_live_slot_resolutions != (extended ? (force_fallback ? 257 : 21) : 2) ||
      submission_counters.submission_slot_reuse_hits != (extended ? (force_fallback ? 257 : 21) : 2) || submission_counters.submission_fanout_uses != (extended ? (force_fallback ? 514 : 42) : 4)) {
    std::cerr << "AIRCONV compute submission batching/fan-out mismatch: pending="
              << submission_counters.submission_pending_uses << ", unique=" << submission_counters.submission_unique_slots
              << ", live=" << submission_counters.submission_live_slot_resolutions
              << ", reuse=" << submission_counters.submission_slot_reuse_hits
              << ", fanout=" << submission_counters.submission_fanout_uses << "\n";
    return false;
  }

  void *readback_mapped = nullptr;
  D3D12_RANGE read_range = {0, static_cast<SIZE_T>(kBufferSize)};
  if (!CheckHR("MapReadback", readback.get()->Map(0, &read_range, &readback_mapped)))
    return false;
  const auto *readback_values = static_cast<const uint32_t *>(readback_mapped);
  const bool passed = std::equal(expected_values.begin(), expected_values.end(), readback_values);
  if (!passed) {
    std::cerr << "AIRCONV compute readback mismatch: got ";
    for (UINT i = 0; i < kElementCount; i++)
      std::cerr << readback_values[i] << (i + 1 == kElementCount ? '\n' : ',');
  }
  D3D12_RANGE written_range = {0, 0};
  readback.get()->Unmap(0, &written_range);
  if (!passed)
    return false;
  std::cout << (extended ? (force_fallback ? "AIRCONV array/space conservative fallback passed\n"
                                         : "AIRCONV array/space filtered dispatch/readback passed\n")
                         : "AIRCONV compute residency dispatch/readback passed\n");
  return true;
}

bool TestCounterResidency(ID3D12Device *device) {
  static constexpr char source[] = R"HLSL(
RWStructuredBuffer<uint> output_data : register(u0);
[numthreads(1, 1, 1)]
void cs_main(uint3 id : SV_DispatchThreadID) {
  uint index = output_data.IncrementCounter();
  output_data[index] = 100 + id.x;
}
)HLSL";
  HMODULE compiler = LoadLibraryA(D3DCOMPILER_DLL_A);
  auto compile = compiler ? reinterpret_cast<pD3DCompile>(
                                GetProcAddress(compiler, "D3DCompile"))
                          : nullptr;
  if (!compile) {
    if (compiler)
      FreeLibrary(compiler);
    return false;
  }
  ComPtr<ID3DBlob> shader, errors;
  const auto compile_hr = compile(
      source, sizeof(source) - 1, "counter.hlsl", nullptr, nullptr, "cs_main",
      "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, shader.put(), errors.put());
  if (!CheckHR("Compile UAV counter shader", compile_hr)) {
    if (errors.get())
      std::cerr << static_cast<const char *>(errors.get()->GetBufferPointer())
                << '\n';
    errors.reset();
    shader.reset();
    FreeLibrary(compiler);
    return false;
  }
  const auto *shader_data =
      static_cast<const uint8_t *>(shader.get()->GetBufferPointer());
  const std::vector<uint8_t> bytecode(
      shader_data, shader_data + shader.get()->GetBufferSize());
  errors.reset();
  shader.reset();
  FreeLibrary(compiler);
  ComPtr<ID3D12RootSignature> root;
  if (!CreateRootSignature(device, root.put(), false))
    return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root.get();
  pso_desc.CS = {bytecode.data(), bytecode.size()};
  ComPtr<ID3D12PipelineState> pso;
  if (!CheckHR("Create counter PSO", device->CreateComputePipelineState(
                                         &pso_desc, IID_PPV_ARGS(pso.put()))))
    return false;

  constexpr UINT counter_offset = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
  ComPtr<ID3D12Resource> output, counters[2], zeros, readback;
  if (!CreateBuffer(device, D3D12_HEAP_TYPE_DEFAULT, 16,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, output.put()) ||
      !CreateBuffer(device, D3D12_HEAP_TYPE_UPLOAD, 4, D3D12_RESOURCE_FLAG_NONE,
                    D3D12_RESOURCE_STATE_GENERIC_READ, zeros.put()) ||
      !CreateBuffer(device, D3D12_HEAP_TYPE_READBACK, 24,
                    D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST,
                    readback.put()))
    return false;
  for (auto &counter : counters)
    if (!CreateBuffer(device, D3D12_HEAP_TYPE_DEFAULT, counter_offset + 4,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_COPY_DEST, counter.put()))
      return false;
  void *mapped = nullptr;
  if (!CheckHR("Map counter zero", zeros.get()->Map(0, nullptr, &mapped)))
    return false;
  *static_cast<uint32_t *>(mapped) = 0;
  zeros.get()->Unmap(0, nullptr);

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (!CheckHR(
          "Create counter queue",
          device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue.put()))) ||
      !CheckHR("Create counter allocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(allocator.put()))) ||
      !CheckHR("Create counter list",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator.get(), nullptr,
                                         IID_PPV_ARGS(list.put()))))
    return false;
  ID3D12CommandList *lists[] = {list.get()};
  auto execute = [&] {
    if (!CheckHR("Close counter list", list.get()->Close()))
      return false;
    queue.get()->ExecuteCommandLists(1, lists);
    return WaitForQueue(device, queue.get());
  };
  auto reset = [&] {
    return CheckHR("Reset counter allocator", allocator.get()->Reset()) &&
           CheckHR("Reset counter list",
                   list.get()->Reset(allocator.get(), pso.get()));
  };
  for (auto &counter : counters) {
    list.get()->CopyBufferRegion(counter.get(), counter_offset, zeros.get(), 0,
                                 4);
    Transition(list.get(), counter.get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  if (!execute())
    return false;

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 1;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> heap, source_heap;
  if (!CheckHR(
          "Create counter heap",
          device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(heap.put()))))
    return false;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (!CheckHR("Create counter source heap",
               device->CreateDescriptorHeap(&heap_desc,
                                            IID_PPV_ARGS(source_heap.put()))))
    return false;
  const auto cpu = heap.get()->GetCPUDescriptorHandleForHeapStart();
  const auto source_cpu =
      source_heap.get()->GetCPUDescriptorHandleForHeapStart();
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 4;
  uav.Buffer.StructureByteStride = 4;
  uav.Buffer.CounterOffsetInBytes = counter_offset;
  auto write = [&](UINT counter, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    device->CreateUnorderedAccessView(output.get(), counters[counter].get(),
                                      &uav, handle);
  };
  auto native_counter = [&](UINT index) {
    return static_cast<dxmt::MTLD3D12Resource *>(counters[index].get())
        ->buffer->current()
        ->buffer()
        .handle;
  };

  // Each mutation is before ExecuteCommandLists, and slot reuse is after a
  // fence. The second pass also replaces a counter-bearing descriptor with no
  // counter.
  for (UINT pass = 0; pass < 2; pass++) {
    if (!reset())
      return false;
    write(pass, cpu);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    list.get()->SetDescriptorHeaps(1, heaps);
    list.get()->SetComputeRootSignature(root.get());
    list.get()->SetComputeRootDescriptorTable(
        1, heap.get()->GetGPUDescriptorHandleForHeapStart());
    list.get()->Dispatch(4, 1, 1);
    if (!CheckHR("Close counter dispatch", list.get()->Close()))
      return false;
    auto *internal =
        static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list.get());
    bool recorded_counter = false;
    for (auto *encoder = internal->entry; encoder; encoder = encoder->next)
      for (const auto &resource : encoder->resource_refs)
        recorded_counter |= resource.handle == native_counter(pass);

    if (pass == 0) {
      write(1, source_cpu);
      device->CopyDescriptorsSimple(1, cpu, source_cpu,
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      source_heap.reset();
    } else {
      auto no_counter = uav;
      no_counter.Buffer.CounterOffsetInBytes = 0;
      device->CreateUnorderedAccessView(output.get(), nullptr, &no_counter,
                                        cpu);
      write(0, cpu);
    }
    std::vector<dxmt::SubmissionResourceUse> uses;
    std::vector<WMT::Reference<WMT::Resource>> resources;
    if (!CheckHR("Collect counter submission",
                 internal->CollectResourceUsesForSubmission(uses, resources)))
      return false;
    const auto read_write = WMTResourceUsageRead | WMTResourceUsageWrite;
    const bool submitted_counter =
        std::any_of(uses.begin(), uses.end(), [&](const auto &use) {
          return use.resource.handle == native_counter(1 - pass) &&
                 (use.usage & read_write) == read_write;
        });
    if (!recorded_counter || !submitted_counter) {
      std::cerr << "FAIL: UAV counter residency: recorded=" << recorded_counter
                << " submit-latest=" << submitted_counter << " pass=" << pass
                << '\n';
      return false; // Do not submit a known missing indirect resource to the
                    // GPU.
    }
    resources.clear();
    uses.clear();
    queue.get()->ExecuteCommandLists(1, lists);
    if (!WaitForQueue(device, queue.get()) || !reset())
      return false;
    Transition(list.get(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    list.get()->CopyBufferRegion(readback.get(), 0, output.get(), 0, 16);
    Transition(list.get(), output.get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (UINT i = 0; i < 2; i++) {
      Transition(list.get(), counters[i].get(),
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                 D3D12_RESOURCE_STATE_COPY_SOURCE);
      list.get()->CopyBufferRegion(readback.get(), 16 + i * 4,
                                   counters[i].get(), counter_offset, 4);
      Transition(list.get(), counters[i].get(),
                 D3D12_RESOURCE_STATE_COPY_SOURCE,
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (!execute() || !CheckHR("Map counter readback",
                               readback.get()->Map(0, nullptr, &mapped)))
      return false;
    std::array<uint32_t, 6> values;
    std::memcpy(values.data(), mapped, sizeof(values));
    readback.get()->Unmap(0, nullptr);
    std::sort(values.begin(), values.begin() + 4);
    if (values !=
        std::array<uint32_t, 6>{100, 101, 102, 103, pass ? 4u : 0u, 4}) {
      std::cerr << "FAIL: counter GPU readback pass=" << pass << " values=";
      for (auto value : values)
        std::cerr << value << ',';
      std::cerr << '\n';
      return false;
    }
  }
  // Replacing a counter UAV with either SRV or counterless UAV must clear its
  // CPU identity as well as the GPU counter pointer.
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 4;
  srv.Buffer.StructureByteStride = 4;
  device->CreateShaderResourceView(output.get(), &srv, cpu);
  auto *internal_heap = static_cast<dxmt::MTLD3D12DescriptorHeap *>(heap.get());
  {
    auto read = internal_heap->ReadDescriptor(0);
    if (read.get().type != dxmt::ShaderVisibleDescriptorType::SRVBuffer ||
        read.get().SRVBuffer.counter) {
      std::cerr << "FAIL: SRV retained a stale UAV counter\n";
      return false;
    }
  }
  write(0, cpu);
  uav.Buffer.CounterOffsetInBytes = 0;
  device->CreateUnorderedAccessView(output.get(), nullptr, &uav, cpu);
  {
    auto read = internal_heap->ReadDescriptor(0);
    if (read.get().type != dxmt::ShaderVisibleDescriptorType::UAVBuffer ||
        read.get().UAVBuffer.counter) {
      std::cerr << "FAIL: counterless UAV retained a stale counter\n";
      return false;
    }
  }
  std::cout << "AIRCONV UAV counter copy/mutation/fenced reuse residency and "
               "GPU readback passed\n";
  return true;
}

// The same oracle runs DXBC/Airconv and DXIL/MSC. Missing MSC counter
// texture handles fail before any append/consume dispatch reaches the GPU.
bool TestAppendConsume(ID3D12Device *device, const char *append_path,
                       const char *consume_path, UINT counter_offset,
                       UINT first_element, bool wrong_expected) {
  std::vector<uint8_t> bytecode[2];
  ComPtr<ID3D12RootSignature> root;
  if (!CreateRootSignature(device, root.put(), false, false, false, 2))
    return false;
  ComPtr<ID3D12PipelineState> psos[2];
  const char *paths[] = {append_path, consume_path};
  for (UINT i = 0; i < 2; ++i) {
    std::ifstream file(paths[i], std::ios::binary);
    bytecode[i].assign(std::istreambuf_iterator<char>(file), {});
    if (bytecode[i].empty())
      return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root.get();
    desc.CS = {bytecode[i].data(), bytecode[i].size()};
    if (!CheckHR("Create append/consume PSO",
                 device->CreateComputePipelineState(
                     &desc, IID_PPV_ARGS(psos[i].put()))))
      return false;
  }
  const auto backend =
      static_cast<dxmt::MTLD3D12PipelineState *>(psos[0].get())->shader_backend;
  if (static_cast<dxmt::MTLD3D12PipelineState *>(psos[1].get())
          ->shader_backend != backend)
    return false;
  const bool msc = backend == dxmt::D3D12ShaderBackend::MetalShaderConverter;
  constexpr uint32_t guard = 0x13579bdf;
  const UINT payload_size = (first_element + 5) * 4;
  constexpr UINT counter_size = 8192 + 8;
  const UINT readback_size = payload_size + 16 + counter_size * 3;
  ComPtr<ID3D12Resource> payload, consumed, counters[2], upload, readback;
  if (!CreateBuffer(device, D3D12_HEAP_TYPE_DEFAULT, payload_size,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COPY_DEST, payload.put()) ||
      !CreateBuffer(device, D3D12_HEAP_TYPE_DEFAULT, 16,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, consumed.put()) ||
      !CreateBuffer(device, D3D12_HEAP_TYPE_UPLOAD,
                    std::max(payload_size, counter_size),
                    D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ,
                    upload.put()) ||
      !CreateBuffer(device, D3D12_HEAP_TYPE_READBACK, readback_size,
                    D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST,
                    readback.put()))
    return false;
  for (auto &counter : counters)
    if (!CreateBuffer(device, D3D12_HEAP_TYPE_DEFAULT, counter_size,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_COPY_DEST, counter.put()))
      return false;
  void *mapped = nullptr;
  if (!CheckHR("Map guarded upload", upload.get()->Map(0, nullptr, &mapped)))
    return false;
  std::fill_n(static_cast<uint32_t *>(mapped),
              std::max(payload_size, counter_size) / 4, guard);
  upload.get()->Unmap(0, nullptr);
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (!CheckHR(
          "Create append queue",
          device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue.put()))) ||
      !CheckHR("Create append allocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(allocator.put()))) ||
      !CheckHR("Create append list",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator.get(), nullptr,
                                         IID_PPV_ARGS(list.put()))))
    return false;
  auto execute = [&] {
    if (!CheckHR("Close initialization", list.get()->Close()))
      return false;
    ID3D12CommandList *lists[] = {list.get()};
    queue.get()->ExecuteCommandLists(1, lists);
    return WaitForQueue(device, queue.get());
  };
  list.get()->CopyBufferRegion(payload.get(), 0, upload.get(), 0, payload_size);
  for (auto &counter : counters)
    list.get()->CopyBufferRegion(counter.get(), 0, upload.get(), 0,
                                 counter_size);
  if (!execute() ||
      !CheckHR("Map counter zeros", upload.get()->Map(0, nullptr, &mapped)))
    return false;
  *static_cast<uint32_t *>(mapped) = 0;
  upload.get()->Unmap(0, nullptr);
  if (!CheckHR("Reset initialization allocator", allocator.get()->Reset()) ||
      !CheckHR("Reset initialization list",
               list.get()->Reset(allocator.get(), nullptr)))
    return false;
  for (auto &counter : counters) {
    list.get()->CopyBufferRegion(counter.get(), counter_offset, upload.get(), 0,
                                 4);
    Transition(list.get(), counter.get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  Transition(list.get(), payload.get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!execute())
    return false;
  upload.reset();
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 2;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> heap, source_heap;
  if (!CheckHR("Create append heap", device->CreateDescriptorHeap(
                                         &heap_desc, IID_PPV_ARGS(heap.put()))))
    return false;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (!CheckHR("Create source heap",
               device->CreateDescriptorHeap(&heap_desc,
                                            IID_PPV_ARGS(source_heap.put()))))
    return false;
  const auto cpu = heap.get()->GetCPUDescriptorHandleForHeapStart();
  const auto source_cpu =
      source_heap.get()->GetCPUDescriptorHandleForHeapStart();
  auto output_cpu = cpu;
  output_cpu.ptr += device->GetDescriptorHandleIncrementSize(heap_desc.Type);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 4;
  uav.Buffer.StructureByteStride = 4;
  device->CreateUnorderedAccessView(consumed.get(), nullptr, &uav, output_cpu);
  uav.Buffer.FirstElement = first_element;
  uav.Buffer.CounterOffsetInBytes = counter_offset;
  auto write = [&](UINT i, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    device->CreateUnorderedAccessView(payload.get(), counters[i].get(), &uav,
                                      handle);
  };
  auto entry = [&] {
    auto *internal_heap =
        static_cast<dxmt::MTLD3D12DescriptorHeap *>(heap.get());
    const auto address = internal_heap->GetMSCDescriptorTableAddress(
        heap.get()->GetGPUDescriptorHandleForHeapStart());
    uint64_t offset = 0;
    auto *allocation =
        static_cast<dxmt::MTLD3D12Device *>(device)->LookupBufferByVA(address,
                                                                      &offset);
    dxmt_msc_descriptor_entry descriptor{};
    if (allocation)
      std::memcpy(&descriptor,
                  static_cast<const uint8_t *>(allocation->mappedMemory(0)) +
                      offset,
                  sizeof(descriptor));
    return descriptor;
  };
  write(0, cpu);
  const auto original_entry = entry();
  if (msc && (!original_entry.texture_view_id ||
              original_entry.gpu_va !=
                  payload.get()->GetGPUVirtualAddress() + first_element * 4 ||
              original_entry.metadata != 16)) {
    std::cerr << "FAIL MSC counter ABI preflight: texture="
              << original_entry.texture_view_id
              << " metadata=" << original_entry.metadata
              << " offset=" << counter_offset << '\n';
    return false;
  }
  // Different offsets in one allocation must not alias one cached texture.
  auto alternate = uav;
  alternate.Buffer.CounterOffsetInBytes = counter_offset ? 0 : 4096;
  device->CreateUnorderedAccessView(payload.get(), counters[0].get(),
                                    &alternate, cpu);
  if (msc && (!entry().texture_view_id ||
              entry().texture_view_id == original_entry.texture_view_id)) {
    std::cerr << "FAIL distinct counter offsets share a texture view\n";
    return false;
  }
  write(0, cpu);
  if (entry().texture_view_id != original_entry.texture_view_id)
    return false;
  auto copy_readback = [&](ID3D12Resource *source, UINT offset, UINT length) {
    Transition(list.get(), source, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    list.get()->CopyBufferRegion(readback.get(), offset, source, 0, length);
    Transition(list.get(), source, D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  };
  // Slot reuse follows a fence. Final application object release happens while
  // a queue wait deliberately keeps the submission in flight.
  for (UINT pass = 0; pass < 2; ++pass) {
    if (!CheckHR("Reset append allocator", allocator.get()->Reset()) ||
        !CheckHR("Reset append list",
                 list.get()->Reset(allocator.get(), psos[0].get())))
      return false;
    write(pass, cpu);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    list.get()->SetDescriptorHeaps(1, heaps);
    list.get()->SetComputeRootSignature(root.get());
    list.get()->SetComputeRootDescriptorTable(
        0, heap.get()->GetGPUDescriptorHandleForHeapStart());
    list.get()->SetComputeRootDescriptorTable(
        1, heap.get()->GetGPUDescriptorHandleForHeapStart());
    list.get()->Dispatch(4, 1, 1);
    copy_readback(payload.get(), 0, payload_size);
    for (UINT i = 0; i < 2; ++i) {
      Transition(list.get(), counters[i].get(),
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                 D3D12_RESOURCE_STATE_COPY_SOURCE);
      if (i == 1 - pass)
        list.get()->CopyBufferRegion(readback.get(), payload_size + 16,
                                     counters[i].get(), 0, counter_size);
      Transition(list.get(), counters[i].get(),
                 D3D12_RESOURCE_STATE_COPY_SOURCE,
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    list.get()->SetPipelineState(psos[1].get());
    list.get()->Dispatch(4, 1, 1);
    copy_readback(consumed.get(), payload_size, 16);
    for (UINT i = 0; i < 2; ++i)
      copy_readback(counters[i].get(),
                    payload_size + 16 + (i + 1) * counter_size, counter_size);
    if (!CheckHR("Close append/consume", list.get()->Close()))
      return false;
    if (!pass) {
      write(1, source_cpu);
      device->CopyDescriptorsSimple(1, cpu, source_cpu, heap_desc.Type);
      source_heap.reset();
    } else {
      auto no_counter = uav;
      no_counter.Buffer.CounterOffsetInBytes = 0;
      device->CreateUnorderedAccessView(payload.get(), nullptr, &no_counter,
                                        cpu);
      if (entry().texture_view_id) {
        std::cerr << "FAIL stale MSC counter after counterless replacement\n";
        return false;
      }
      write(0, cpu);
      if (entry().texture_view_id != original_entry.texture_view_id) {
        std::cerr << "FAIL counter view cache identity\n";
        return false;
      }
    }
    if (msc && !entry().texture_view_id)
      return false;
    auto *internal =
        static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list.get());
    std::vector<dxmt::SubmissionResourceUse> uses;
    std::vector<WMT::Reference<WMT::Resource>> resources;
    if (!CheckHR("Collect counter submission",
                 internal->CollectResourceUsesForSubmission(uses, resources)))
      return false;
    const auto native =
        static_cast<dxmt::MTLD3D12Resource *>(counters[1 - pass].get())
            ->buffer->current()
            ->buffer()
            .handle;
    const auto read_write = WMTResourceUsageRead | WMTResourceUsageWrite;
    if (!std::any_of(uses.begin(), uses.end(), [&](const auto &use) {
          return use.resource.handle == native &&
                 (use.usage & read_write) == read_write;
        })) {
      std::cerr << "FAIL missing latest counter submission residency\n";
      return false;
    }
    if (msc) {
      // This fixture has no UAV textures except the indirect counter view.
      // Check both compute encoders and retained native resources, without
      // extending the DLL interface to expose the internal view cache.
      const obj_handle_t buffers[] = {
          static_cast<dxmt::MTLD3D12Resource *>(payload.get())
              ->buffer->current()
              ->buffer()
              .handle,
          static_cast<dxmt::MTLD3D12Resource *>(consumed.get())
              ->buffer->current()
              ->buffer()
              .handle,
          native};
      UINT counter_texture_uses = 0;
      for (const auto &use : uses) {
        if ((use.usage & read_write) != read_write ||
            std::find(std::begin(buffers), std::end(buffers),
                      use.resource.handle) != std::end(buffers))
          continue;
        const bool retained = std::any_of(
            resources.begin(), resources.end(), [&](const auto &resource) {
              return resource.handle == use.resource.handle;
            });
        if (!retained)
          return false;
        counter_texture_uses++;
      }
      if (counter_texture_uses != 2) {
        std::cerr << "FAIL counter texture submission uses="
                  << counter_texture_uses << '\n';
        return false;
      }
    }
    uses.clear();
    resources.clear();
    ComPtr<ID3D12Fence> gate;
    if (pass &&
        (!CheckHR("Create counter gate",
                  device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                      IID_PPV_ARGS(gate.put()))) ||
         !CheckHR("Queue counter gate", queue.get()->Wait(gate.get(), 1))))
      return false;
    ID3D12CommandList *lists[] = {list.get()};
    queue.get()->ExecuteCommandLists(1, lists);
    if (pass) {
      list.reset();
      allocator.reset();
      heap.reset();
      payload.reset();
      consumed.reset();
      for (auto &counter : counters)
        counter.reset();
      for (auto &pso : psos)
        pso.reset();
      root.reset();
      if (!CheckHR("Release counter gate", gate.get()->Signal(1)))
        return false;
    }
    if (!WaitForQueue(device, queue.get()) ||
        !CheckHR("Map counter results",
                 readback.get()->Map(0, nullptr, &mapped)))
      return false;
    std::vector<uint32_t> values(readback_size / 4);
    std::memcpy(values.data(), mapped, readback_size);
    readback.get()->Unmap(0, nullptr);
    std::sort(values.begin() + first_element,
              values.begin() + first_element + 4);
    std::sort(values.begin() + payload_size / 4,
              values.begin() + payload_size / 4 + 4);
    std::vector<uint32_t> expected(values.size(), guard);
    for (UINT i = 0; i < 4; ++i) {
      expected[first_element + i] = 100 + i;
      expected[payload_size / 4 + i] = 100 + i;
    }
    for (UINT i = 0; i < 3; ++i)
      expected[(payload_size + 16 + i * counter_size + counter_offset) / 4] =
          i == 0 ? 4 : 0;
    if (wrong_expected)
      expected[first_element]++;
    if (values != expected) {
      for (size_t i = 0; i < values.size(); ++i)
        if (values[i] != expected[i]) {
          std::cerr << "FAIL counter GPU word=" << i << " actual=" << values[i]
                    << " expected=" << expected[i] << " pass=" << pass << '\n';
          break;
        }
      return false;
    }
  }
  std::cout << (msc ? "MSC" : "AIRCONV")
            << " Append/Consume GPU payload/counters/guards passed offset="
            << counter_offset << " first=" << first_element
            << " copy/mutation/reuse/pending-release passed\n";
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if ((argc == 6 || argc == 7) && std::strcmp(argv[1], "--counter-pair") == 0) {
    const UINT offset = std::strtoul(argv[4], nullptr, 10);
    const UINT first = std::strtoul(argv[5], nullptr, 10);
    if (offset > 8192 || offset % D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT ||
        first > 8 ||
        (argc == 7 && std::strcmp(argv[6], "--wrong-expected") != 0))
      return 2;
    ComPtr<ID3D12Device> device;
    if (!CheckHR("D3D12CreateDevice",
                 D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                   IID_PPV_ARGS(device.put()))))
      return 1;
    return TestAppendConsume(device.get(), argv[2], argv[3], offset, first,
                             argc == 7)
               ? 0
               : 1;
  }
  if (argc > 2 || (argc == 2 && std::strcmp(argv[1], "--clear-residency-override") != 0))
    return 2;
  if (argc == 2)
    SetEnvironmentVariableA("DXMT_AIRCONV_COMPUTE_RESIDENCY", nullptr);

  std::vector<uint8_t> shader_bytecode, array_bytecode;
  if (!CompileComputeShader(shader_bytecode, array_bytecode))
    return 1;
  if (!TestAirconvErrorOwnership(shader_bytecode))
    return 1;
  ID3D12Device *device = nullptr;
  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return 1;
  const D3D12_SHADER_BYTECODE shader = {shader_bytecode.data(), shader_bytecode.size()};
  bool automatic_fallback = true;
  for (bool directly_indexed : {true, false}) {
    ComPtr<ID3D12RootSignature> fallback_root;
    ComPtr<ID3D12PipelineState> fallback_pipeline;
    D3D12_COMPUTE_PIPELINE_STATE_DESC fallback_desc = {};
    const char *name = directly_indexed ? "direct-indexed" : "ambiguous";
    bool valid = CreateRootSignature(device, fallback_root.put(), false, directly_indexed, !directly_indexed);
    fallback_desc.pRootSignature = fallback_root.get();
    fallback_desc.CS = shader;
    valid = valid && SUCCEEDED(device->CreateComputePipelineState(&fallback_desc, IID_PPV_ARGS(fallback_pipeline.put())));
    if (valid) {
      auto *state = static_cast<dxmt::MTLD3D12PipelineState *>(fallback_pipeline.get());
      valid = !state->descriptor_footprint_exact && state->descriptor_footprint.empty();
    }
    if (!valid)
      std::cerr << "automatic " << name << " footprint fallback failed\n";
    else
      std::cout << "automatic " << name << " footprint fallback passed\n";
    automatic_fallback = automatic_fallback && valid;
  }
  bool passed = automatic_fallback && TestCounterResidency(device) &&
                RunTest(device, shader);
  if (passed && !array_bytecode.empty()) {
    const D3D12_SHADER_BYTECODE array_shader = {array_bytecode.data(), array_bytecode.size()};
    passed = RunTest(device, array_shader, true) && RunTest(device, array_shader, true, true);
  }
  device->Release();
  return passed ? 0 : 1;
}

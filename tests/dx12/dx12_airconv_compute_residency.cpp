#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
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

bool CompileComputeShader(std::vector<uint8_t> &bytecode) {
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

  const auto *data = static_cast<const uint8_t *>(shader->GetBufferPointer());
  bytecode.assign(data, data + shader->GetBufferSize());
  if (errors)
    errors->Release();
  shader->Release();
  FreeLibrary(compiler);
  return !bytecode.empty();
}

bool CreateRootSignature(ID3D12Device *device, ID3D12RootSignature **root_signature) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 1;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER parameters[3] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &ranges[0];
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &ranges[1];
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[2].Descriptor.ShaderRegister = 0;
  parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC description = {};
  description.NumParameters = 3;
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
    ID3D12DescriptorHeap *descriptor_heap, UINT descriptor_stride, ID3D12Resource *config
) {
  ID3D12DescriptorHeap *heaps[] = {descriptor_heap};
  list->SetDescriptorHeaps(1, heaps);
  list->SetPipelineState(pipeline);
  list->SetComputeRootSignature(root_signature);
  auto srv_table = descriptor_heap->GetGPUDescriptorHandleForHeapStart();
  auto uav_table = srv_table;
  uav_table.ptr += descriptor_stride;
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

bool RunTest(ID3D12Device *device, const D3D12_SHADER_BYTECODE &shader) {
  constexpr UINT kElementCount = 4;
  constexpr UINT64 kBufferSize = kElementCount * sizeof(uint32_t);
  static constexpr std::array<uint32_t, kElementCount> input_values = {11, 22, 33, 44};
  static constexpr std::array<uint32_t, kElementCount> replacement_input_values = {101, 202, 303, 404};
  static constexpr std::array<uint32_t, kElementCount> expected_values = {106, 207, 308, 409};
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
  if (!CreateRootSignature(device, root_signature.put()))
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
  descriptor_heap_description.NumDescriptors = 2;
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
  device->CreateShaderResourceView(input.get(), &srv, descriptor_heap.get()->GetCPUDescriptorHandleForHeapStart());

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kElementCount;
  uav.Buffer.StructureByteStride = sizeof(uint32_t);
  auto uav_handle = descriptor_heap.get()->GetCPUDescriptorHandleForHeapStart();
  uav_handle.ptr += descriptor_stride;
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

  SetRootBindings(list_a.get(), pipeline.get(), root_signature.get(), descriptor_heap.get(), descriptor_stride, config.get());
  list_a.get()->Dispatch(kElementCount, 1, 1);
  Transition(list_a.get(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list_a.get()->CopyBufferRegion(scratch.get(), 0, output.get(), 0, kBufferSize);
  Transition(list_a.get(), output.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  list_a.get()->Dispatch(kElementCount, 1, 1);
  if (!CheckHR("Close command list A", list_a.get()->Close()))
    return false;

  SetRootBindings(list_b.get(), pipeline.get(), root_signature.get(), descriptor_heap.get(), descriptor_stride, config.get());
  D3D12_RESOURCE_BARRIER uav_barrier = {};
  uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uav_barrier.UAV.pResource = output.get();
  list_b.get()->ResourceBarrier(1, &uav_barrier);
  list_b.get()->Dispatch(kElementCount, 1, 1);
  Transition(list_b.get(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list_b.get()->CopyBufferRegion(readback.get(), 0, output.get(), 0, kBufferSize);
  if (!CheckHR("Close command list B", list_b.get()->Close()))
    return false;

  // Root Signature 1.0 makes descriptor-table entries volatile. Rewriting the
  // SRV after both lists close but before ExecuteCommandLists changes what they use.
  device->CreateShaderResourceView(
      replacement_input.get(), &srv, descriptor_heap.get()->GetCPUDescriptorHandleForHeapStart()
  );

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
  std::cout << "AIRCONV compute residency dispatch/readback passed\n";
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 2 || (argc == 2 && std::strcmp(argv[1], "--clear-residency-override") != 0))
    return 2;
  if (argc == 2)
    SetEnvironmentVariableA("DXMT_AIRCONV_COMPUTE_RESIDENCY", nullptr);

  std::vector<uint8_t> shader_bytecode;
  if (!CompileComputeShader(shader_bytecode))
    return 1;
  if (!TestAirconvErrorOwnership(shader_bytecode))
    return 1;
  ID3D12Device *device = nullptr;
  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return 1;
  const D3D12_SHADER_BYTECODE shader = {shader_bytecode.data(), shader_bytecode.size()};
  const bool passed = RunTest(device, shader);
  device->Release();
  return passed ? 0 : 1;
}

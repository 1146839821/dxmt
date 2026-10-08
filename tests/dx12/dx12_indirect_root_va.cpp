#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
template <typename T> struct Owned {
  T *p = nullptr;
  ~Owned() { if (p) p->Release(); }
  T *operator->() const { return p; }
};
void Check(HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << '\n';
    throw std::runtime_error("D3D12 operation failed");
  }
}
void Run(const void *consumer, size_t consumer_size, ID3DBlob *producer, bool remap = false, bool direct = false) {
  Owned<ID3D12Device> device;
  Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)));
  Owned<ID3D12Heap> alias_heaps[6];
  unsigned alias_count = 0;
  auto buffer = [&](Owned<ID3D12Resource> &resource, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                    bool uav, bool placed = false) {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = heap;
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = 256;
    d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    if (placed) {
      if (alias_count >= 6) throw std::runtime_error("too many alias targets");
      D3D12_HEAP_DESC hd = {};
      hd.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      hd.Properties = hp; hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
      Check(device->CreateHeap(&hd, IID_PPV_ARGS(&alias_heaps[alias_count].p)));
      Check(device->CreatePlacedResource(alias_heaps[alias_count++].p, 0, &d, state, nullptr, IID_PPV_ARGS(&resource.p)));
    } else Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&resource.p)));
  };
  auto root = [&](D3D12_ROOT_PARAMETER *params, UINT count, Owned<ID3D12RootSignature> &result) {
    D3D12_ROOT_SIGNATURE_DESC d = {count, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    Owned<ID3DBlob> blob;
    Check(D3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &blob.p, nullptr));
    Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&result.p)));
  };
  auto pipeline = [&](ID3D12RootSignature *rs, const void *shader, size_t size, Owned<ID3D12PipelineState> &pso) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC d = {}; d.pRootSignature = rs; d.CS = {shader, size};
    Check(device->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso.p)));
  };
  D3D12_ROOT_PARAMETER params[4] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[3].Constants = {1, 0, 1};
  Owned<ID3D12RootSignature> rs, generator_rs;
  root(params, 4, rs);
  D3D12_ROOT_PARAMETER generator_params[2] = {};
  generator_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  generator_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  root(generator_params, 2, generator_rs);
  Owned<ID3D12PipelineState> pso, generator_pso;
  pipeline(rs.p, consumer, consumer_size, pso);
  pipeline(generator_rs.p, producer->GetBufferPointer(), producer->GetBufferSize(), generator_pso);
  D3D12_INDIRECT_ARGUMENT_DESC args[5] = {};
  args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT; args[0].Constant = {3, 0, 1};
  args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW; args[1].ConstantBufferView.RootParameterIndex = 0;
  args[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW; args[2].ShaderResourceView.RootParameterIndex = 1;
  args[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW; args[3].UnorderedAccessView.RootParameterIndex = 2;
  args[4].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
  D3D12_COMMAND_SIGNATURE_DESC sd = {40, 5, args, 0};
  Owned<ID3D12CommandSignature> signature;
  Check(device->CreateCommandSignature(&sd, rs.p, IID_PPV_ARGS(&signature.p)));
  Owned<ID3D12Resource> source, arguments, output[2], readback;
  buffer(source, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false);
  buffer(arguments, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
  for (auto &resource : output)
    buffer(resource, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, remap);
  buffer(readback, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, false);
  Owned<ID3D12Resource> late_cbv[2], late_srv[2];
  if (direct) for (unsigned i = 0; i < 2; ++i) {
    buffer(late_cbv[i], D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, remap);
    buffer(late_srv[i], D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, remap);
  }
  D3D12_COMMAND_QUEUE_DESC qd = {};
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue.p)));
  Check(device->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&allocator.p)));
  Check(device->CreateCommandList(0, qd.Type, allocator.p, generator_pso.p, IID_PPV_ARGS(&list.p)));
  list->SetComputeRootSignature(generator_rs.p);
  list->SetComputeRootShaderResourceView(0, source->GetGPUVirtualAddress());
  list->SetComputeRootUnorderedAccessView(1, arguments->GetGPUVirtualAddress());
  list->Dispatch(1, 1, 1);
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {arguments.p, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT};
  list->ResourceBarrier(1, &barrier);
  list->SetPipelineState(pso.p); list->SetComputeRootSignature(rs.p);
  list->SetComputeRootConstantBufferView(0, 0);
  list->SetComputeRootShaderResourceView(1, 0);
  list->SetComputeRootUnorderedAccessView(2, 0);
  list->SetComputeRoot32BitConstant(3, 63, 0);
  if (remap) {
    D3D12_RESOURCE_BARRIER alias = {};
    alias.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    list->ResourceBarrier(1, &alias);
  }
  if (direct) for (unsigned i = 0; i < 2; ++i) {
    list->SetComputeRootConstantBufferView(0, late_cbv[i]->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(1, late_srv[i]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(2, output[i]->GetGPUVirtualAddress());
    list->SetComputeRoot32BitConstant(3, i, 0);
    list->Dispatch(1, 1, 1);
  } else list->ExecuteIndirect(signature.p, 2, arguments.p, 0, nullptr, 0);
  if (remap) {
    // Reactivate the recorded copy-source aliases after the replacement UAVs.
    D3D12_RESOURCE_BARRIER alias = {};
    alias.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    list->ResourceBarrier(1, &alias);
  }
  for (unsigned i = 0; i < 2; ++i) {
    barrier.Transition = {output[i].p, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    list->CopyBufferRegion(readback.p, i * 4, output[i].p, i * 4, 4);
  }
  Check(list->Close());
  // Indirect targets did not exist during recording; direct targets did.
  // CPU arguments are never the ExecuteIndirect buffer.
  UINT command_words[20] = {};
  for (unsigned i = 0; i < 2; ++i) {
    if (!direct) {
      buffer(late_cbv[i], D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, remap);
      buffer(late_srv[i], D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, remap);
    }
    void *mapped = nullptr;
    const UINT value = (i + 1) * 100, input = i ? 9 : 7;
    Check(late_cbv[i]->Map(0, nullptr, &mapped)); std::memcpy(mapped, &value, 4); late_cbv[i]->Unmap(0, nullptr);
    Check(late_srv[i]->Map(0, nullptr, &mapped)); std::memcpy(mapped, &input, 4); late_srv[i]->Unmap(0, nullptr);
    auto *bytes = reinterpret_cast<char *>(command_words) + i * 40;
    std::memcpy(bytes, &i, 4);
    const UINT64 va[] = {late_cbv[i]->GetGPUVirtualAddress(), late_srv[i]->GetGPUVirtualAddress(), output[i]->GetGPUVirtualAddress()};
    std::memcpy(bytes + 4, va, sizeof(va));
    const UINT dispatch[] = {1, 1, 1}; std::memcpy(bytes + 28, dispatch, sizeof(dispatch));
  }
  if (remap) {
    ID3D12Resource **targets[] = {&output[0].p, &output[1].p,
        &late_cbv[0].p, &late_srv[0].p, &late_cbv[1].p, &late_srv[1].p};
    if (alias_count != 6) throw std::runtime_error("alias target count mismatch");
    for (unsigned i = 0; i < 6; ++i) {
      auto *old = *targets[i];
      const auto desc = old->GetDesc();
      const auto va = old->GetGPUVirtualAddress();
      const auto state = i < 2 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_GENERIC_READ;
      ID3D12Resource *replacement = nullptr;
      Check(device->CreatePlacedResource(alias_heaps[i].p, 0, &desc, state, nullptr, IID_PPV_ARGS(&replacement)));
      if (replacement->GetGPUVirtualAddress() != va) {
        replacement->Release(); throw std::runtime_error("alias VA changed");
      }
      *targets[i] = replacement;
      old->Release();
    }
    // Initialize only the new target generation, after old owners unregister.
    for (unsigned i = 0; i < 2; ++i) {
      void *data = nullptr;
      const UINT value = (i + 1) * 300, input = i ? 19 : 17;
      Check(late_cbv[i]->Map(0, nullptr, &data)); std::memcpy(data, &value, 4); late_cbv[i]->Unmap(0, nullptr);
      Check(late_srv[i]->Map(0, nullptr, &data)); std::memcpy(data, &input, 4); late_srv[i]->Unmap(0, nullptr);
    }
  }
  void *mapped = nullptr;
  Check(source->Map(0, nullptr, &mapped)); std::memcpy(mapped, command_words, sizeof(command_words)); source->Unmap(0, nullptr);
  Owned<ID3D12Fence> gate, complete;
  Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate.p)));
  Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&complete.p)));
  Check(queue->Wait(gate.p, 1));
  ID3D12CommandList *submitted[] = {list.p}; queue->ExecuteCommandLists(1, submitted);
  Check(queue->Signal(complete.p, 1));
  for (unsigned i = 0; i < 2; ++i) {
    late_cbv[i].p->Release(); late_cbv[i].p = nullptr;
    late_srv[i].p->Release(); late_srv[i].p = nullptr;
  }
  Check(gate->Signal(1));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) throw std::runtime_error("CreateEvent failed");
  auto hr = complete->SetEventOnCompletion(1, event);
  const auto wait = SUCCEEDED(hr) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event); Check(hr);
  if (wait != WAIT_OBJECT_0) throw std::runtime_error("GPU wait failed");
  D3D12_RANGE range = {0, 8}; Check(readback->Map(0, &range, &mapped));
  UINT values[2]; std::memcpy(values, mapped, sizeof(values));
  D3D12_RANGE empty = {}; readback->Unmap(0, &empty);
  if (values[0] != (remap ? 317u : 107u) || values[1] != (remap ? 619u : 209u)) {
    std::cerr << "readback=" << values[0] << ',' << values[1] << '\n';
    throw std::runtime_error("GPU-selected root VA mismatch");
  }
  std::cout << "CBV/SRV/UAV roots and gated lifetime: "
            << values[0] << ',' << values[1] << " remap=" << remap << " direct=" << direct << " PASS\n";
}
} // namespace

int main(int argc, char **argv) {
  const bool direct = argc > 1 && !std::strcmp(argv[argc - 1], "--direct-remap");
  const bool remap = direct || (argc > 1 && !std::strcmp(argv[argc - 1], "--remap"));
  const int shader_argc = argc - (remap ? 1 : 0);
  if (shader_argc > 2) return 2;
  HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
  if (!compiler) return 2;
  auto compile = reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile) { FreeLibrary(compiler); return 2; }
  int result = 1;
  try {
    const char generator[] = "ByteAddressBuffer source : register(t0); RWByteAddressBuffer generated : register(u0);"
      "[numthreads(1,1,1)] void main() { for(uint i=0; i<20; ++i) generated.Store(i*4, source.Load(i*4)); }";
    const char consumer[] = "cbuffer Values : register(b0) { uint value; }; StructuredBuffer<uint> input : register(t0);"
      "RWStructuredBuffer<uint> output : register(u0); cbuffer Position : register(b1) {uint slot;}"
      "[numthreads(1,1,1)] void main() {output[slot] = value + input[0];}";
    Owned<ID3DBlob> producer, shader, errors;
    Check(compile(generator, sizeof(generator) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &producer.p, &errors.p));
    if (shader_argc == 2) {
      std::ifstream file(argv[1], std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
      if (bytes.empty()) throw std::runtime_error("empty shader file");
      Run(bytes.data(), bytes.size(), producer.p, remap, direct);
    } else {
      Check(compile(consumer, sizeof(consumer) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &shader.p, nullptr));
      Run(shader->GetBufferPointer(), shader->GetBufferSize(), producer.p, remap, direct);
    }
    result = 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; }
  FreeLibrary(compiler); return result;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

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
D3D12_RESOURCE_DESC Buffer(UINT64 bytes) {
  D3D12_RESOURCE_DESC d = {};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
  d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return d;
}
void Run(unsigned mode, ID3DBlob *shader) {
  Owned<ID3D12Device> device;
  Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)));
  D3D12_ROOT_PARAMETER params[2] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants = {0, 0, 3};
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  Owned<ID3DBlob> root_blob;
  Check(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob.p, nullptr));
  Owned<ID3D12RootSignature> root;
  Check(device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                    IID_PPV_ARGS(&root.p)));
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
  pd.pRootSignature = root.p;
  pd.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  Owned<ID3D12PipelineState> pso;
  Check(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso.p)));
  D3D12_INDIRECT_ARGUMENT_DESC args[2] = {};
  args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
  args[0].Constant = {0, 1, 1}; // Reset one odd-offset DWORD, not the whole parameter.
  args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
  D3D12_COMMAND_SIGNATURE_DESC sd = {16, 2, args, 0};
  Owned<ID3D12CommandSignature> signature;
  Check(device->CreateCommandSignature(&sd, root.p, IID_PPV_ARGS(&signature.p)));
  Owned<ID3D12Resource> upload, output, readback;
  D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
  auto ud = Buffer(256);
  Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &ud,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload.p)));
  void *mapping = nullptr;
  Check(upload->Map(0, nullptr, &mapping));
  const UINT data[] = {99, 1, 1, 1, 0}; // Last DWORD is a zero GPU count.
  std::memcpy(mapping, data, sizeof(data));
  upload->Unmap(0, nullptr);
  hp.Type = D3D12_HEAP_TYPE_DEFAULT;
  auto od = Buffer(256); od.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &od,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output.p)));
  hp.Type = D3D12_HEAP_TYPE_READBACK;
  Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &ud,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback.p)));
  D3D12_COMMAND_QUEUE_DESC qd = {}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue.p)));
  Check(device->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&allocator.p)));
  Check(device->CreateCommandList(0, qd.Type, allocator.p, pso.p, IID_PPV_ARGS(&list.p)));
  list->SetComputeRootSignature(root.p);
  list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
  UINT initial[] = {0, 55, 7};
  list->SetComputeRoot32BitConstants(0, 3, initial, 0);
  list->Dispatch(1, 1, 1); // Initialize output[0] to 62, independently of allocation contents.
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; barrier.UAV.pResource = output.p;
  list->ResourceBarrier(1, &barrier);
  list->ExecuteIndirect(signature.p, mode == 1 ? 0 : 1, upload.p, 0,
                        mode == 2 ? upload.p : nullptr, mode == 2 ? 16 : 0);
  list->ResourceBarrier(1, &barrier);
  list->SetComputeRoot32BitConstant(0, 1, 0); // Leave the reset value and preserved keep untouched.
  list->Dispatch(1, 1, 1);
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {output.p, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
  list->ResourceBarrier(1, &barrier);
  list->CopyBufferRegion(readback.p, 0, output.p, 0, 8);
  Check(list->Close());
  ID3D12CommandList *submitted[] = {list.p};
  queue->ExecuteCommandLists(1, submitted);
  Owned<ID3D12Fence> fence;
  Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.p)));
  Check(queue->Signal(fence.p, 1));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) throw std::runtime_error("CreateEvent failed");
  auto hr = fence->SetEventOnCompletion(1, event);
  auto wait = SUCCEEDED(hr) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event);
  Check(hr);
  if (wait != WAIT_OBJECT_0) throw std::runtime_error("GPU wait failed");
  D3D12_RANGE range = {0, 8};
  Check(readback->Map(0, &range, &mapping));
  UINT result[2]; std::memcpy(result, mapping, sizeof(result));
  D3D12_RANGE empty = {}; readback->Unmap(0, &empty);
  if (result[0] != (mode == 0 ? 106u : 62u) || result[1] != 7) {
    std::cerr << "mode=" << mode << " readback=" << result[0] << ',' << result[1] << '\n';
    throw std::runtime_error("indirect reset/inheritance mismatch");
  }
  std::cout << "mode=" << mode << " indirect=" << result[0] << " after=" << result[1] << " PASS\n";
}
} // namespace

int main() {
  HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
  if (!compiler) return 2;
  auto compile = reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile) { FreeLibrary(compiler); return 2; }
  int result = 1;
  try {
    const char source[] = "cbuffer C : register(b0) { uint slot; uint value; uint keep; };"
                          "RWStructuredBuffer<uint> output : register(u0);"
                          "[numthreads(1,1,1)] void main() { output[slot] = value + keep; }";
    Owned<ID3DBlob> shader, errors;
    Check(compile(source, sizeof(source) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0",
                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader.p, &errors.p));
    for (unsigned mode = 0; mode != 3; ++mode) Run(mode, shader.p);
    result = 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; }
  FreeLibrary(compiler);
  return result;
}

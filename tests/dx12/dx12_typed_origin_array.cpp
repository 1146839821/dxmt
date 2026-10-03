#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <cstring>

template <typename T> struct Owned {
  T *p = nullptr;
  ~Owned() { if (p) p->Release(); }
};

int main(int argc, char **argv) {
  bool dynamic = false;
  const bool nonuniform = argc == 3 && (!std::strcmp(argv[2], "--nonuniform") ||
      !std::strcmp(argv[2], "--nonuniform-static"));
  for (const char *mode : {"--dynamic0", "--dynamic1", "--dynamic-static0", "--dynamic-static1",
      "--dynamic-unused", "--dynamic-static-unused"})
    dynamic |= argc == 3 && !std::strcmp(argv[2], mode);
  dynamic |= nonuniform;
  const bool partial = dynamic && std::strstr(argv[2], "unused");
  const UINT selected = dynamic && !partial && argv[2][std::strlen(argv[2]) - 1] == '0' ? 0 : 1;
  const bool static_ranges = argc == 3 && (!std::strcmp(argv[2], "--static") ||
      (dynamic && std::strstr(argv[2], "static")));
  if (argc != 2 && !static_ranges && !dynamic) return 1;
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<char> shader((std::istreambuf_iterator<char>(file)), {});
  if (shader.empty()) return 1;
  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Owned<ID3D12RootSignature> root;
  Owned<ID3D12PipelineState> pso;
  Owned<ID3D12DescriptorHeap> heap;
  Owned<ID3D12Resource> upload, output, readback;
  Owned<ID3D12Fence> fence;
  Owned<ID3DBlob> blob;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)))) return 1;
  D3D12_COMMAND_QUEUE_DESC q = {};
  if (FAILED(device.p->CreateCommandQueue(&q, IID_PPV_ARGS(&queue.p))) ||
      FAILED(device.p->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.p)))) return 1;
  D3D12_DESCRIPTOR_RANGE ranges[2] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 3, 0, 0},
      {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 5, 0, 2}};
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable = {2, ranges};
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants = {0, 0, 1};
  D3D12_ROOT_SIGNATURE_DESC rd = {dynamic ? 2u : 1u, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  HRESULT serialized;
  if (static_ranges) {
    D3D12_DESCRIPTOR_RANGE1 ranges1[2] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 5, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 2}};
    D3D12_ROOT_PARAMETER1 parameters1[2] = {};
    parameters1[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters1[0].DescriptorTable = {2, ranges1};
    parameters1[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters1[1].Constants = {0, 0, 1};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned = {};
    versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned.Desc_1_1 = {dynamic ? 2u : 1u, parameters1, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    serialized = D3D12SerializeVersionedRootSignature(&versioned, &blob.p, nullptr);
  } else serialized = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob.p, nullptr);
  if (FAILED(serialized) ||
      FAILED(device.p->CreateRootSignature(0, blob.p->GetBufferPointer(), blob.p->GetBufferSize(), IID_PPV_ARGS(&root.p)))) return 1;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
  pd.pRootSignature = root.p; pd.CS = {shader.data(), shader.size()};
  if (FAILED(device.p->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso.p)))) return 1;
  D3D12_RESOURCE_DESC bd = {};
  bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = 256;
  bd.Height = bd.DepthOrArraySize = bd.MipLevels = bd.SampleDesc.Count = 1;
  bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  auto buffer = [&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state, ID3D12Resource **resource) {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = type; hp.CreationNodeMask = hp.VisibleNodeMask = 1;
    return SUCCEEDED(device.p->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, state, nullptr,
        IID_PPV_ARGS(resource)));
  };
  if (!buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, &upload.p) ||
      !buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, &readback.p)) return 1;
  bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, &output.p)) return 1;
  void *mapped = nullptr;
  if (FAILED(upload.p->Map(0, nullptr, &mapped))) return 1;
  auto *words = static_cast<UINT *>(mapped);
  for (unsigned i = 0; i < 64; ++i) words[i] = 0xcafe1234;
  words[0] = 11; words[1] = 41; words[2] = 99;
  upload.p->Unmap(0, nullptr);
  D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4,
      D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  if (FAILED(device.p->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap.p)))) return 1;
  const auto increment = device.p->GetDescriptorHandleIncrementSize(hd.Type);
  auto cpu = heap.p->GetCPUDescriptorHandleForHeapStart(); cpu.ptr += increment;
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.FirstElement = 1; srv.Buffer.NumElements = 1;
  device.p->CreateShaderResourceView(upload.p, &srv, cpu);
  cpu.ptr += 2 * increment;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.FirstElement = 3; uav.Buffer.NumElements = 1;
  device.p->CreateUnorderedAccessView(output.p, nullptr, &uav, cpu);
  if (dynamic && !partial) {
    cpu = heap.p->GetCPUDescriptorHandleForHeapStart();
    srv.Buffer.FirstElement = 0;
    device.p->CreateShaderResourceView(upload.p, &srv, cpu);
    cpu.ptr += 2 * increment;
    uav.Buffer.FirstElement = 4;
    device.p->CreateUnorderedAccessView(output.p, nullptr, &uav, cpu);
  }
  if (FAILED(device.p->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.p, pso.p, IID_PPV_ARGS(&list.p)))) return 1;
  list.p->CopyBufferRegion(output.p, 0, upload.p, 0, 256);
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {output.p, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
      D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
  list.p->ResourceBarrier(1, &barrier);
  list.p->SetDescriptorHeaps(1, &heap.p);
  list.p->SetComputeRootSignature(root.p);
  list.p->SetComputeRootDescriptorTable(0, heap.p->GetGPUDescriptorHandleForHeapStart());
  if (dynamic) list.p->SetComputeRoot32BitConstant(1, selected, 0);
  list.p->Dispatch(1, 1, 1);
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list.p->ResourceBarrier(1, &barrier);
  list.p->CopyBufferRegion(readback.p, 0, output.p, 0, 256);
  if (FAILED(list.p->Close()) || FAILED(device.p->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.p)))) return 1;
  ID3D12CommandList *commands[] = {list.p}; queue.p->ExecuteCommandLists(1, commands);
  if (FAILED(queue.p->Signal(fence.p, 1))) return 1;
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) return 1;
  const bool done = SUCCEEDED(fence.p->SetEventOnCompletion(1, event)) && WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
  CloseHandle(event);
  if (!done || FAILED(readback.p->Map(0, nullptr, &mapped))) return 1;
  words = static_cast<UINT *>(mapped);
  bool ok = true;
  for (unsigned i = 0; i < 64; ++i) {
    const UINT expected = i == 0 ? 11 : i == 1 ? 41 : i == 2 ? 99 :
        nonuniform && i == 4 ? 28 :
        i == (selected ? 3u : 4u) ? (selected ? 58u : 28u) : 0xcafe1234;
    if (words[i] != expected) { std::cerr << "word " << i << " actual=" << words[i] << " expected=" << expected << '\n'; ok = false; }
  }
  readback.p->Unmap(0, nullptr);
  if (!ok) return 1;
  std::cout << "typed array origin/count full-buffer readback passed: " <<
      (argc == 3 ? argv[2] : "--volatile") << '\n';
  return 0;
}

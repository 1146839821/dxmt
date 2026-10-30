#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "d3d12_device.hpp"
#include <cstring>
#include <iostream>

template <typename T> struct Owned {
  T *p = nullptr;
  ~Owned() {
    if (p)
      p->Release();
  }
  void reset() {
    if (p)
      p->Release();
    p = nullptr;
  }
};

int main(int argc, char **argv) {
  const bool invalid = argc == 2 && !std::strcmp(argv[1], "--invalid");
  const bool untracked = argc == 2 && !std::strcmp(argv[1], "--untracked");
  const bool late = argc == 2 && !std::strcmp(argv[1], "--late");
  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator, reset_allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Owned<ID3D12DescriptorHeap> first, second;
  Owned<ID3D12Fence> gate, completed;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device.p))))
    return 1;
  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(
          device.p->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue.p))) ||
      FAILED(device.p->CreateCommandAllocator(queue_desc.Type,
                                              IID_PPV_ARGS(&allocator.p))) ||
      FAILED(device.p->CreateCommandAllocator(
          queue_desc.Type, IID_PPV_ARGS(&reset_allocator.p))) ||
      FAILED(device.p->CreateCommandList(0, queue_desc.Type, allocator.p,
                                         nullptr, IID_PPV_ARGS(&list.p))) ||
      FAILED(device.p->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&gate.p))) ||
      FAILED(device.p->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&completed.p))))
    return 1;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  heap_desc.NumDescriptors = 2;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(
          device.p->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&first.p))) ||
      FAILED(
          device.p->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&second.p))))
    return 1;
  auto *first_impl =
      static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(first.p);
  auto *second_impl =
      static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(second.p);
  if (FAILED(first_impl->PrepareSamplerLODSidecar()) ||
      FAILED(second_impl->PrepareSamplerLODSidecar()))
    return 1;
  auto first_address = first_impl->GetSamplerLODSidecarAddress(
      first.p->GetGPUDescriptorHandleForHeapStart());
  auto second_address = second_impl->GetSamplerLODSidecarAddress(
      second.p->GetGPUDescriptorHandleForHeapStart());
  if (!first_address || !second_address || first_address == second_address)
    return 1;
  ID3D12DescriptorHeap *heaps[] = {first.p};
  list.p->SetDescriptorHeaps(1, heaps);
  auto *internal_list =
      static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list.p);
  if (internal_list->TrackSamplerLODSidecar(
          second_impl, second.p->GetGPUDescriptorHandleForHeapStart()) !=
      E_INVALIDARG)
    return 1;
  if (!untracked &&
      FAILED(internal_list->TrackSamplerLODSidecar(
          first_impl, first.p->GetGPUDescriptorHandleForHeapStart())))
    return 1;
  if (!untracked &&
      FAILED(internal_list->TrackSamplerLODSidecar(
          first_impl, first.p->GetGPUDescriptorHandleForHeapStart())))
    return 1;
  heaps[0] = second.p;
  list.p->SetDescriptorHeaps(1, heaps);
  if (!untracked &&
      FAILED(internal_list->TrackSamplerLODSidecar(
          second_impl, second.p->GetGPUDescriptorHandleForHeapStart())))
    return 1;
  if (FAILED(list.p->Close()))
    return 1;
  if (invalid || untracked || late) {
    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    if (!late)
      SetEnvironmentVariableA("DXMT_TEST_FAIL_SAMPLER_LOD", "1");
    device.p->CreateSampler(&sampler,
                            first.p->GetCPUDescriptorHandleForHeapStart());
    SetEnvironmentVariableA("DXMT_TEST_FAIL_SAMPLER_LOD", nullptr);
    if ((!first_impl->GetSamplerLODSidecarAddress(
            first.p->GetGPUDescriptorHandleForHeapStart())) != !late)
      return 1;
    ID3D12CommandList *lists[] = {list.p};
    queue.p->ExecuteCommandLists(1, lists);
    const auto reason = device.p->GetDeviceRemovedReason();
    std::cout << "device_reason=0x" << std::hex
              << static_cast<unsigned long>(reason) << std::dec << "\n";
    return reason == (invalid ? E_FAIL : S_OK) ? 0 : 1;
  }
  if (FAILED(queue.p->Wait(gate.p, 1)))
    return 1;
  struct GateGuard {
    ID3D12Fence *fence;
    ~GateGuard() { fence->Signal(1); }
  } gate_guard{gate.p};
  ID3D12CommandList *lists[] = {list.p};
  queue.p->ExecuteCommandLists(1, lists);
  if (FAILED(list.p->Reset(reset_allocator.p, nullptr)))
    return 1;
  list.reset();
  first.reset();
  second.reset();
  auto *internal_device = static_cast<dxmt::MTLD3D12Device *>(device.p);
  uint64_t offset = 0;
  const bool retained =
      internal_device->LookupBufferByVA(first_address, &offset) &&
      internal_device->LookupBufferByVA(second_address, &offset);
  if (FAILED(gate.p->Signal(1)) || FAILED(queue.p->Signal(completed.p, 1)))
    return 1;
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    return 1;
  const HRESULT hr = completed.p->SetEventOnCompletion(1, event);
  const DWORD wait_result =
      SUCCEEDED(hr) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event);
  std::cout << "retained_before_completion=" << retained
            << " wait_result=" << wait_result << "\n";
  if (!retained || wait_result != WAIT_OBJECT_0 ||
      FAILED(device.p->GetDeviceRemovedReason()))
    return 1;
  if (internal_device->LookupBufferByVA(first_address, &offset) ||
      internal_device->LookupBufferByVA(second_address, &offset))
    return 1;
  std::cout << "sampler heap submission lifetime passed\n";
  return 0;
}

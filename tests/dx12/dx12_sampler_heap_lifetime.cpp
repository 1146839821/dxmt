#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "d3d12_device.hpp"
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

int main() {
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
  heaps[0] = second.p;
  list.p->SetDescriptorHeaps(1, heaps);
  if (FAILED(list.p->Close()) || FAILED(queue.p->Wait(gate.p, 1)))
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

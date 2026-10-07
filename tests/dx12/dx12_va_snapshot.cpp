#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "d3d12_device.hpp"
#include <algorithm>
#include <iostream>

template <typename T> struct Owned {
  T *p = nullptr;
  ~Owned() { if (p) p->Release(); }
};

int main() {
  Owned<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)))) return 1;
  auto internal = static_cast<dxmt::MTLD3D12Device *>(device.p);
  D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC d = {};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = 256;
  d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Owned<ID3D12Resource> first, second;
  for (auto result : {&first, &second})
    if (FAILED(device.p->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&result->p)))) return 1;
  const auto va = first.p->GetGPUVirtualAddress();
  uint64_t offset = 0;
  auto allocation = internal->LookupBufferByVA(va, &offset);
  if (!allocation || offset) return 1;
  auto first_owner = static_cast<dxmt::MTLD3D12Resource *>(first.p);
  auto second_owner = static_cast<dxmt::MTLD3D12Resource *>(second.p);
  bool ok = true;
  auto retained = internal->SnapshotBufferByVA(va + 17, &offset);
  ok &= retained.ptr() == allocation && offset == 17;
  offset = 99;
  ok &= !internal->SnapshotBufferByVA(0, &offset) && offset == 0;
  ok &= !internal->SnapshotBufferByVA(va, nullptr);
  for (auto new_owner : {second_owner, static_cast<dxmt::MTLD3D12Resource *>(nullptr)}) {
    ok &= SUCCEEDED(internal->RegisterResidencyAndVA(allocation, new_owner));
    ok &= SUCCEEDED(internal->UnregisterResidencyAndVA(allocation, first_owner));
    ok &= internal->LookupBufferByVA(va, &offset) == allocation && offset == 0;
    ok &= internal->LookupResourceByVA(va, &offset) == new_owner;
    if (new_owner) {
      ok &= SUCCEEDED(internal->UnregisterResidencyAndVA(allocation, nullptr));
      ok &= internal->LookupBufferByVA(va, &offset) == allocation;
    }
    // Restore the public resource's real registration before any cleanup.
    ok &= SUCCEEDED(internal->RegisterResidencyAndVA(allocation, first_owner));
  }
  std::vector<dxmt::Rc<dxmt::BufferAllocation>> snapshot;
  ok &= SUCCEEDED(internal->SnapshotRegisteredBuffers(snapshot));
  auto found = std::find_if(snapshot.begin(), snapshot.end(), [&](const auto &entry) { return entry.ptr() == allocation; });
  ok &= found != snapshot.end();
  first.p->Release(); first.p = nullptr;
  ok &= internal->LookupBufferByVA(va, &offset) == nullptr;
  ok &= !internal->SnapshotBufferByVA(va, &offset) && offset == 0;
  ok &= retained && retained->gpuAddress() == va && retained->length() >= 256;
  if (found != snapshot.end()) ok &= (*found)->gpuAddress() == va && (*found)->length() >= 256;
  if (!ok) { std::cerr << "VA owner/snapshot regression failed\n"; return 1; }
  std::cout << "same-allocation owner replacement, stale unregister and retained snapshot PASS\n";
  return 0;
}

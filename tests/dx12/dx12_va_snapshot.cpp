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
  D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0,
                                  D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
  D3D12_ROOT_PARAMETER params[5] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants = {0, 0, 3};
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable = {1, &range};
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[4].Descriptor.ShaderRegister = 1;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {5, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  Owned<ID3DBlob> root_blob;
  Owned<ID3D12RootSignature> root;
  if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob.p, nullptr)) ||
      FAILED(device.p->CreateRootSignature(0, root_blob.p->GetBufferPointer(), root_blob.p->GetBufferSize(),
                                           IID_PPV_ARGS(&root.p)))) return 1;
  ok &= static_cast<dxmt::MTLD3D12RootSignature *>(root.p)->RootBufferQwordMask ==
        ((uint64_t(1) << 3) | (uint64_t(1) << 4) | (uint64_t(1) << 5));
  dxmt::EncoderData feedback = {};
  feedback.root_feedback_vas = {va + 17, va + 255};
  ok &= feedback.NeedsRootFeedbackInterval(va, 256);
  ok &= feedback.NeedsRootFeedbackInterval(va + 16, 2); // overlapping interval stays eligible
  ok &= !feedback.NeedsRootFeedbackInterval(va + 256, 256);
  ok &= !feedback.NeedsRootFeedbackInterval(va, 0);
  feedback.root_feedback_vas = {UINT64_MAX - 1};
  ok &= feedback.NeedsRootFeedbackInterval(UINT64_MAX - 2, 2);
  ok &= !feedback.NeedsRootFeedbackInterval(UINT64_MAX, 2); // no end-address overflow
  feedback.indirect_root_va = true;
  ok &= feedback.NeedsRootFeedbackInterval(0, 1);
  dxmt::EncoderData bounded = {};
  bounded.CaptureRootFeedbackVA(0);
  for (uint64_t address = 1; address <= 64; ++address) {
    bounded.CaptureRootFeedbackVA(address);
    bounded.CaptureRootFeedbackVA(address);
  }
  ok &= bounded.root_feedback_vas_known && bounded.root_feedback_vas.size() == 64;
  bounded.CaptureRootFeedbackVA(65);
  bounded.CaptureRootFeedbackVA(66);
  ok &= !bounded.root_feedback_vas_known && bounded.root_feedback_vas.empty();
  ok &= bounded.NeedsRootFeedbackInterval(1000, 1);
  feedback.indirect_root_va = false;
  feedback.root_feedback_vas_known = false;
  ok &= feedback.NeedsRootFeedbackInterval(0, 1);
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
  // Public placed-resource aliases, unlike an internal owner-only swap, may
  // produce distinct allocations at exactly the same GPU virtual address.
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Properties = hp;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  Owned<ID3D12Heap> heap;
  Owned<ID3D12Resource> alias_first, alias_second;
  if (FAILED(device.p->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap.p)))) return 1;
  if (FAILED(device.p->CreatePlacedResource(heap.p, 0, &d, D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr, IID_PPV_ARGS(&alias_first.p)))) return 1;
  const auto alias_va = alias_first.p->GetGPUVirtualAddress();
  auto old_alias = internal->SnapshotBufferByVA(alias_va, &offset);
  if (FAILED(device.p->CreatePlacedResource(heap.p, 0, &d, D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr, IID_PPV_ARGS(&alias_second.p)))) return 1;
  auto new_alias = internal->SnapshotBufferByVA(alias_second.p->GetGPUVirtualAddress(), &offset);
  const bool same_address = alias_va == alias_second.p->GetGPUVirtualAddress();
  const bool distinct_allocation = old_alias.ptr() != new_alias.ptr();
  std::cout << "placed aliases same_address=" << same_address
            << " distinct_allocation=" << distinct_allocation << "\n";
  ok &= same_address && distinct_allocation;
  if (same_address) {
    ok &= distinct_allocation && old_alias && new_alias;
    alias_first.p->Release(); alias_first.p = nullptr;
    ok &= internal->SnapshotBufferByVA(alias_va, &offset).ptr() == new_alias.ptr();
    std::vector<dxmt::Rc<dxmt::BufferAllocation>> current;
    ok &= SUCCEEDED(internal->SnapshotRegisteredBuffers(current));
    dxmt::EncoderData fixed = {}; fixed.root_feedback_vas = {alias_va};
    current.erase(std::remove_if(current.begin(), current.end(), [&](const auto &entry) {
      return !fixed.NeedsRootFeedbackInterval(entry->gpuAddress(), entry->length());
    }), current.end());
    ok &= std::any_of(current.begin(), current.end(), [&](const auto &entry) { return entry.ptr() == new_alias.ptr(); });
    ok &= std::none_of(current.begin(), current.end(), [&](const auto &entry) { return entry.ptr() == old_alias.ptr(); });
    ok &= internal->LookupResourceByVA(alias_va, &offset) ==
        static_cast<dxmt::MTLD3D12Resource *>(alias_second.p);
    ok &= old_alias->gpuAddress() == alias_va;
  }
  if (!ok) { std::cerr << "VA owner/snapshot regression failed\n"; return 1; }
  std::cout << "VA owner replacement, same-address placed aliases and retained snapshot PASS\n";
  return 0;
}

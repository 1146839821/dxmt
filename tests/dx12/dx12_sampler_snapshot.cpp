#include "d3d12_device.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

template <typename T> struct ReleaseCOM {
  void operator()(T *value) const { if (value) value->Release(); }
};
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;

int main() {
  ID3D12Device *raw_device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)))) return 1;
  OwnedCOM<ID3D12Device> device(raw_device);
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2,
      D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  ID3D12DescriptorHeap *raw_source = nullptr, *raw_destination = nullptr;
  if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&raw_source)))) return 1;
  OwnedCOM<ID3D12DescriptorHeap> source(raw_source);
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&raw_destination)))) return 1;
  OwnedCOM<ID3D12DescriptorHeap> destination(raw_destination);
  auto *source_heap = static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(source.get());
  auto *destination_heap = static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(destination.get());
  D3D12_SAMPLER_DESC desc = {};
  desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
  desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.MinLOD = .75f; desc.MaxLOD = 3.25f; desc.MipLODBias = 1.5f;
  if (FAILED(source_heap->AddSampler(0, &desc))) return 1;
  std::vector<dxmt::SamplerDescriptorSnapshot> recorded;
  source_heap->ResolveSamplers({0, 1, 99}, recorded);
  if (recorded.size() != 3 || !recorded[0].sampler || recorded[1].sampler || recorded[2].sampler ||
      std::memcmp(&desc, &recorded[0].descriptor, sizeof(desc))) return 1;
  device->CopyDescriptorsSimple(1, destination->GetCPUDescriptorHandleForHeapStart(),
      source->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  std::vector<dxmt::SamplerDescriptorSnapshot> copied;
  destination_heap->ResolveSamplers({0}, copied);
  if (!copied[0].sampler || copied[0].sampler.ptr() != recorded[0].sampler.ptr() ||
      std::memcmp(&desc, &copied[0].descriptor, sizeof(desc))) return 1;
  auto replacement = desc;
  replacement.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  replacement.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  replacement.MinLOD = 2; replacement.MaxLOD = 4;
  if (FAILED(source_heap->AddSampler(0, &replacement))) return 1;
  std::vector<dxmt::SamplerDescriptorSnapshot> live;
  source_heap->ResolveSamplers({0}, live);
  if (!live[0].sampler || live[0].sampler.ptr() == recorded[0].sampler.ptr() ||
      std::memcmp(&replacement, &live[0].descriptor, sizeof(desc)) ||
      std::memcmp(&desc, &recorded[0].descriptor, sizeof(desc))) return 1;
  // Exercise preservation across the existing opt-in AIR point surrogate.
  if (!SetEnvironmentVariableW(L"DXMT_ENABLE_AIR_MINMAX_DYNAMIC", L"1")) return 1;
  auto reduction = desc;
  reduction.Filter = D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR;
  if (FAILED(source_heap->AddSampler(0, &reduction))) return 1;
  source_heap->ResolveSamplers({0}, live);
  if (!live[0].sampler || live[0].msc.gpu_va ||
      std::memcmp(&reduction, &live[0].descriptor, sizeof(reduction))) return 1;
  device->CopyDescriptorsSimple(1, destination->GetCPUDescriptorHandleForHeapStart(),
      source->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  destination_heap->ResolveSamplers({0}, copied);
  if (!copied[0].sampler || copied[0].sampler.ptr() != live[0].sampler.ptr() ||
      std::memcmp(&reduction, &copied[0].descriptor, sizeof(reduction))) return 1;
  // Unsupported descriptor invalidates both object and original descriptor.
  replacement.Filter = D3D12_FILTER_MINIMUM_ANISOTROPIC;
  if (source_heap->AddSampler(0, &replacement) != E_NOTIMPL) return 1;
  source_heap->ResolveSamplers({0}, live);
  const D3D12_SAMPLER_DESC empty = {};
  if (live[0].sampler || live[0].msc.gpu_va || std::memcmp(&empty, &live[0].descriptor, sizeof(empty))) return 1;
  device->CopyDescriptorsSimple(1, destination->GetCPUDescriptorHandleForHeapStart(),
      source->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  destination_heap->ResolveSamplers({0}, copied);
  if (copied[0].sampler || std::memcmp(&empty, &copied[0].descriptor, sizeof(empty))) return 1;
  if (!recorded[0].sampler->sampler_state || recorded[0].sampler->lod_bias != desc.MipLODBias) return 1;
  std::puts("sampler original descriptor snapshot/copy/overwrite/invalidate PASS (not dispatch acceptance)");
  return 0;
}

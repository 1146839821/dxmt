#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "d3d12_device.hpp"

#include <iostream>
#include <cstring>
#include <thread>
#include <vector>

template <typename T> struct Owned {
  T *p = nullptr;
  ~Owned() {
    if (p)
      p->Release();
  }
};

static bool check_range(ID3D12DescriptorHeap *heap, UINT index, bool valid,
                        float min, float max) {
  dxmt::D3D12SamplerLODRange range{};
  const bool actual = static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(heap)
                          ->GetSamplerLODRange(index, range);
  return actual == valid &&
         (!valid || (range.minimum == min && range.maximum == max));
}

int main() {
  Owned<ID3D12Device> device;
  Owned<ID3D12DescriptorHeap> cpu, gpu;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device.p))))
    return 1;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  hd.NumDescriptors = 4;
  if (FAILED(device.p->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&cpu.p))))
    return 1;
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device.p->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gpu.p))))
    return 1;
  const UINT stride = device.p->GetDescriptorHandleIncrementSize(hd.Type);
  auto src = cpu.p->GetCPUDescriptorHandleForHeapStart();
  auto dst = gpu.p->GetCPUDescriptorHandleForHeapStart();
  D3D12_SAMPLER_DESC desc{};
  desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  desc.AddressU = desc.AddressV = desc.AddressW =
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.MaxAnisotropy = 1;
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  desc.MinLOD = 0.25f;
  desc.MaxLOD = 2.75f;
  device.p->CreateSampler(&desc, src);
  device.p->CopyDescriptorsSimple(1, dst, src, hd.Type);
  auto source = src;
  source.ptr += stride;
  device.p->CreateSampler(&desc, source);
  auto *cpu_impl = static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(cpu.p);
  auto *gpu_impl = static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(gpu.p);
  if (cpu_impl->PrepareSamplerLODSidecar() != E_INVALIDARG ||
      FAILED(gpu_impl->PrepareSamplerLODSidecar()))
    return 1;
  auto gpu_handle = gpu.p->GetGPUDescriptorHandleForHeapStart();
  const auto sidecar_base = gpu_impl->GetSamplerLODSidecarAddress(gpu_handle);
  if (!sidecar_base || !gpu_impl->GetSamplerLODSidecarBuffer())
    return 1;
  auto read_sidecar = [&](UINT index) {
    auto handle = gpu_handle;
    handle.ptr += index * stride;
    const auto address = gpu_impl->GetSamplerLODSidecarAddress(handle);
    uint64_t offset = 0;
    auto *allocation =
        static_cast<dxmt::MTLD3D12Device *>(device.p)->LookupBufferByVA(
            address, &offset);
    dxmt::D3D12SamplerLODSidecarEntry entry{};
    if (allocation)
      std::memcpy(&entry,
                  static_cast<const uint8_t *>(allocation->mappedMemory(0)) +
                      offset,
                  sizeof(entry));
    return entry;
  };
  auto invalid_handle = gpu_handle;
  invalid_handle.ptr += 4 * stride;
  if (gpu_impl->GetSamplerLODSidecarAddress(invalid_handle))
    return 1;
  invalid_handle = gpu_handle;
  ++invalid_handle.ptr;
  if (gpu_impl->GetSamplerLODSidecarAddress(invalid_handle))
    return 1;
  dxmt::D3D12SamplerLODSidecarEntry first{}, copied{}, again{};
  if (!gpu_impl->GetSamplerLODSidecarEntry(0, copied) || !copied.sampler ||
      copied.minimum != .25f || copied.maximum != 2.75f)
    return 1;
  if (!cpu_impl->GetSamplerLODSidecarEntry(1, first) || !first.sampler ||
      first.minimum != .25f || first.maximum != 2.75f ||
      !cpu_impl->GetSamplerLODSidecarEntry(1, again) ||
      again.sampler != first.sampler ||
      cpu_impl->GetSamplerLODSidecarEntry(4, again))
    return 1;
  auto destination = dst;
  destination.ptr += 2 * stride;
  device.p->CopyDescriptorsSimple(1, destination, source, hd.Type);
  if (!gpu_impl->GetSamplerLODSidecarEntry(2, copied) ||
      copied.sampler != first.sampler)
    return 1;
  auto stored = read_sidecar(2);
  if (stored.sampler != first.sampler || stored.minimum != .25f ||
      stored.maximum != 2.75f)
    return 1;
  if (!check_range(cpu.p, 1, true, .25f, 2.75f) ||
      !check_range(gpu.p, 2, true, .25f, 2.75f) ||
      !check_range(gpu.p, 4, false, 0, 0) ||
      !check_range(gpu.p, 1, false, 0, 0))
    return 1;
  desc.MinLOD = 1.5f;
  desc.MaxLOD = 5.5f;
  device.p->CreateSampler(&desc, source);
  if (!cpu_impl->GetSamplerLODSidecarEntry(1, again) || !again.sampler ||
      again.minimum != 1.5f || again.maximum != 5.5f)
    return 1;
  if (!check_range(gpu.p, 2, true, .25f, 2.75f))
    return 1;
  device.p->CopyDescriptorsSimple(1, destination, source, hd.Type);
  if (!gpu_impl->GetSamplerLODSidecarEntry(2, copied) ||
      copied.sampler != again.sampler)
    return 1;
  stored = read_sidecar(2);
  if (stored.sampler != again.sampler || stored.minimum != 1.5f ||
      stored.maximum != 5.5f ||
      gpu_impl->GetSamplerLODSidecarAddress(gpu_handle) != sidecar_base)
    return 1;
  if (!check_range(gpu.p, 2, true, 1.5f, 5.5f))
    return 1;
  if (!gpu_impl->GetSamplerLODSidecarEntry(2, copied) ||
      copied.sampler != again.sampler)
    return 1;
  cpu.p->Release();
  cpu.p = nullptr;
  if (!check_range(gpu.p, 2, true, 1.5f, 5.5f))
    return 1;
  desc.MinLOD = 2.0f;
  desc.MaxLOD = 6.0f;
  device.p->CreateSampler(&desc, destination);
  stored = read_sidecar(2);
  if (!stored.sampler || stored.minimum != 2.0f || stored.maximum != 6.0f ||
      gpu_impl->GetSamplerLODSidecarAddress(gpu_handle) != sidecar_base)
    return 1;
  if (FAILED(gpu_impl->PrepareSamplerLODSidecar()) ||
      gpu_impl->GetSamplerLODSidecarAddress(gpu_handle) != sidecar_base ||
      read_sidecar(2).sampler != stored.sampler)
    return 1;

  D3D12_STATIC_SAMPLER_DESC stat{};
  stat.Filter = desc.Filter;
  stat.AddressU = desc.AddressU;
  stat.AddressV = desc.AddressV;
  stat.AddressW = desc.AddressW;
  stat.MaxAnisotropy = 1;
  stat.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  stat.MinLOD = .5f;
  stat.MaxLOD = 3.5f;
  stat.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  root_desc.NumStaticSamplers = 1;
  root_desc.pStaticSamplers = &stat;
  Owned<ID3DBlob> blob, error;
  if (FAILED(D3D12SerializeRootSignature(
          &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob.p, &error.p)))
    return 1;
  Owned<ID3D12RootSignature> root;
  if (FAILED(device.p->CreateRootSignature(0, blob.p->GetBufferPointer(),
                                           blob.p->GetBufferSize(),
                                           IID_PPV_ARGS(&root.p))))
    return 1;
  auto *internal = static_cast<dxmt::MTLD3D12RootSignature *>(root.p);
  if (internal->NumStaticSamplers != 1 || !internal->StaticSamplerLODRanges ||
      internal->StaticSamplerLODRanges[0].minimum != .5f ||
      internal->StaticSamplerLODRanges[0].maximum != 3.5f)
    return 1;
  dxmt::D3D12SamplerLODSidecarEntry static_entry{}, static_again{};
  std::vector<dxmt::D3D12SamplerLODSidecarEntry> concurrent_entries(8);
  std::vector<std::thread> workers;
  std::vector<int> succeeded(8);
  for (size_t i = 0; i < concurrent_entries.size(); ++i)
    workers.emplace_back([&, i] {
      succeeded[i] =
          internal->GetStaticSamplerLODSidecarEntry(0, concurrent_entries[i]);
    });
  for (auto &worker : workers)
    worker.join();
  for (size_t i = 0; i < concurrent_entries.size(); ++i)
    if (!succeeded[i] || !concurrent_entries[i].sampler ||
        concurrent_entries[i].sampler != concurrent_entries[0].sampler)
      return 1;
  if (!internal->GetStaticSamplerLODSidecarEntry(0, static_entry) ||
      !static_entry.sampler || static_entry.minimum != .5f ||
      static_entry.maximum != 3.5f ||
      !internal->GetStaticSamplerLODSidecarEntry(0, static_again) ||
      static_again.sampler != static_entry.sampler ||
      internal->GetStaticSamplerLODSidecarEntry(1, static_again))
    return 1;
  gpu.p->Release();
  gpu.p = nullptr;
  uint64_t released_offset = 0;
  if (static_cast<dxmt::MTLD3D12Device *>(device.p)->LookupBufferByVA(
          sidecar_base, &released_offset))
    return 1;
  std::cout << "sampler LOD metadata passed\n";
  return 0;
}

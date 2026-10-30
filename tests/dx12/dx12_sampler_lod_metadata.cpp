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
  auto source = src;
  source.ptr += stride;
  device.p->CreateSampler(&desc, source);
  auto destination = dst;
  destination.ptr += 2 * stride;
  device.p->CopyDescriptorsSimple(1, destination, source, hd.Type);
  if (!check_range(cpu.p, 1, true, .25f, 2.75f) ||
      !check_range(gpu.p, 2, true, .25f, 2.75f) ||
      !check_range(gpu.p, 4, false, 0, 0) ||
      !check_range(gpu.p, 1, false, 0, 0))
    return 1;
  desc.MinLOD = 1.5f;
  desc.MaxLOD = 5.5f;
  device.p->CreateSampler(&desc, source);
  if (!check_range(gpu.p, 2, true, .25f, 2.75f))
    return 1;
  device.p->CopyDescriptorsSimple(1, destination, source, hd.Type);
  if (!check_range(gpu.p, 2, true, 1.5f, 5.5f))
    return 1;
  cpu.p->Release();
  cpu.p = nullptr;
  if (!check_range(gpu.p, 2, true, 1.5f, 5.5f))
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
  std::cout << "sampler LOD metadata passed\n";
  return 0;
}

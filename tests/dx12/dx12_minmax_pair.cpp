#include "d3d12_device.hpp"
#include "d3d12_minmax_binding.hpp"
#include "log/log.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

dxmt::Logger dxmt::Logger::s_instance("dx12_minmax_pair");

template <typename T> struct ReleaseCOM {
  void operator()(T *value) const { if (value) value->Release(); }
};
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;

int main() {
  ID3D12Device *raw_device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)))) return 1;
  OwnedCOM<ID3D12Device> device(raw_device);
  auto metal = static_cast<dxmt::MTLD3D12Device *>(device.get())->GetMTLDevice();
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 4; desc.Height = 4; desc.DepthOrArraySize = 1; desc.MipLevels = 2;
  desc.Format = DXGI_FORMAT_R32_FLOAT; desc.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES properties = {}; properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  ID3D12Resource *raw_texture = nullptr;
  if (FAILED(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&raw_texture)))) return 1;
  OwnedCOM<ID3D12Resource> resource(raw_texture);
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1,
      D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  ID3D12DescriptorHeap *raw_heap = nullptr;
  if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&raw_heap)))) return 1;
  OwnedCOM<ID3D12DescriptorHeap> heap(raw_heap);
  auto *implementation = static_cast<dxmt::MTLD3D12DescriptorHeap *>(heap.get());
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = desc.Format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 2; srv.Texture2D.ResourceMinLODClamp = .75f;
  device->CreateShaderResourceView(resource.get(), &srv, heap->GetCPUDescriptorHandleForHeapStart());
  std::vector<dxmt::ShaderVisibleDescriptorSnapshot> snapshots;
  implementation->ResolveDescriptors({0}, snapshots);
  if (snapshots.size() != 1) return 1;
  auto captured = snapshots[0];
  D3D12_SAMPLER_DESC sampler = {};
  sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MinLOD = .25f; sampler.MaxLOD = 1.25f; sampler.MipLODBias = 2;
  dxmt::D3D12MinMaxPairBinding binding;
  for (uint32_t reduction : {0u, 2u, 3u}) {
    sampler.Filter = static_cast<D3D12_FILTER>(0x15 | (reduction << 7));
    if (FAILED(dxmt::PrepareD3D12MinMaxPairBinding(metal, captured, sampler, binding)) ||
        !binding.point_sampler || !binding.ordinary_sampler ||
        !binding.point_descriptor.gpu_va || !binding.ordinary_descriptor.gpu_va ||
        binding.texture.msc_texture_view.handle != captured.msc_texture_view.handle ||
        binding.texture_descriptor.texture_view_id != captured.msc_descriptor.texture_view_id ||
        binding.texture_descriptor.metadata || binding.point_descriptor.metadata || binding.ordinary_descriptor.metadata ||
        binding.state.flags != (7u | (reduction ? 32u : 0u) | (reduction == 3 ? 8u : 0u)) ||
        binding.state.min_lod != .25f || binding.state.max_lod != 1.25f ||
        binding.state.resource_clamp != .75f || binding.state.default_components != 8 ||
        binding.state.address_u != 3 || binding.state.address_v != 3 || binding.state.reserved ||
        binding.point_sampler->lod_bias || binding.ordinary_sampler->lod_bias) return 1;
  }
  auto before = binding;
  for (uint32_t bad = 0; bad < 5; ++bad) {
    auto invalid = captured;
    auto invalid_sampler = sampler;
    if (bad == 0) invalid.descriptor.type = dxmt::ShaderVisibleDescriptorType::Null;
    if (bad == 1) invalid.descriptor.SRVTexture.default_components = 0;
    if (bad == 2) invalid.descriptor.SRVTexture.resource_min_lod_clamp = std::numeric_limits<float>::quiet_NaN();
    if (bad == 3) invalid.msc_texture_view = nullptr;
    if (bad == 4) invalid_sampler.Filter = D3D12_FILTER_MAXIMUM_ANISOTROPIC;
    if (SUCCEEDED(dxmt::PrepareD3D12MinMaxPairBinding(metal, invalid, invalid_sampler, binding)) ||
        binding.point_sampler.ptr() != before.point_sampler.ptr() ||
        binding.ordinary_sampler.ptr() != before.ordinary_sampler.ptr() ||
        std::memcmp(&binding.state, &before.state, sizeof(binding.state))) return 1;
  }
  {
    srv.Texture2D.MostDetailedMip = 1; srv.Texture2D.MipLevels = 1;
    srv.Texture2D.ResourceMinLODClamp = .25f;
    device->CreateShaderResourceView(resource.get(), &srv, heap->GetCPUDescriptorHandleForHeapStart());
    std::vector<dxmt::ShaderVisibleDescriptorSnapshot> subset_snapshots;
    implementation->ResolveDescriptors({0}, subset_snapshots);
    dxmt::D3D12MinMaxPairBinding subset;
    const auto subset_hr = dxmt::PrepareD3D12MinMaxPairBinding(metal, subset_snapshots[0], sampler, subset);
    if (FAILED(subset_hr)) {
      std::printf("subset preparation failed %08lx clamp=%g defaults=%llu\n", (unsigned long)subset_hr,
          subset_snapshots[0].descriptor.SRVTexture.resource_min_lod_clamp,
          (unsigned long long)subset_snapshots[0].descriptor.SRVTexture.default_components); return 1;
    }
    WMT::Texture native = subset.texture.msc_texture_view;
    if (native.width() != 2 || native.mipmapLevelCount() != 1 || subset.state.resource_clamp != -.75f ||
        subset.texture_descriptor.texture_view_id == binding.texture_descriptor.texture_view_id) {
      std::printf("subset width=%llu mips=%llu clamp=%g id=%llu original=%llu\n",
          (unsigned long long)native.width(), (unsigned long long)native.mipmapLevelCount(), subset.state.resource_clamp,
          (unsigned long long)subset.texture_descriptor.texture_view_id,
          (unsigned long long)binding.texture_descriptor.texture_view_id); return 1;
    }
  }
  {
    auto integer_desc = desc; integer_desc.Format = DXGI_FORMAT_R32_UINT;
    ID3D12Resource *raw_integer = nullptr;
    if (FAILED(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &integer_desc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&raw_integer)))) return 1;
    OwnedCOM<ID3D12Resource> integer(raw_integer);
    auto integer_srv = srv; integer_srv.Format = integer_desc.Format;
    device->CreateShaderResourceView(integer.get(), &integer_srv, heap->GetCPUDescriptorHandleForHeapStart());
    std::vector<dxmt::ShaderVisibleDescriptorSnapshot> integer_snapshots;
    implementation->ResolveDescriptors({0}, integer_snapshots);
    if (dxmt::PrepareD3D12MinMaxPairBinding(metal, integer_snapshots[0], sampler, binding) != E_NOTIMPL ||
        binding.texture_descriptor.texture_view_id != before.texture_descriptor.texture_view_id) {
      std::puts("integer rejection/publication failed"); return 1;
    }
  }
  srv.Texture2D.MostDetailedMip = 0; srv.Texture2D.MipLevels = 2;
  srv.Texture2D.ResourceMinLODClamp = 0;
  device->CreateShaderResourceView(resource.get(), &srv, heap->GetCPUDescriptorHandleForHeapStart());
  resource.reset(); heap.reset(); snapshots.clear(); captured = {}; before = {};
  WMT::Texture native = binding.texture.msc_texture_view;
  if (!native || native.width() != 4 || native.mipmapLevelCount() != 2 || binding.state.resource_clamp != .75f ||
      !binding.point_sampler->sampler_state || !binding.ordinary_sampler->sampler_state) return 1;
  std::puts("MinMax native pair/state/lifetime PASS: 3 modes + subset + 6 rejects (not dispatch acceptance)");
  return 0;
}

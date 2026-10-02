#include "d3d12_sampler.hpp"
#include "log/log.hpp"

#include <array>
#include <iostream>
#include <cstring>
#include <limits>

dxmt::Logger dxmt::Logger::s_instance("dx12_sampler_filter");

int main() {
  constexpr std::array<unsigned, 9> base_filters = {0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15, 0x55};
  unsigned cases = 0;
  for (unsigned reduction = 0; reduction < 4; ++reduction) {
    for (auto base : base_filters) {
      const auto filter = static_cast<D3D12_FILTER>(base | (reduction << 7));
      D3D12_SAMPLER_DESC dynamic_desc = {};
      dynamic_desc.Filter = filter;
      dynamic_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
      dynamic_desc.MaxAnisotropy = 16;
      D3D12_STATIC_SAMPLER_DESC static_desc = {};
      static_desc.Filter = filter;
      static_desc.ComparisonFunc = dynamic_desc.ComparisonFunc;
      static_desc.MaxAnisotropy = dynamic_desc.MaxAnisotropy;
      WMTSamplerInfo dynamic_info = {}, static_info = {};
      dynamic_info.support_argument_buffers = static_info.support_argument_buffers = true;
      dynamic_info.max_anisotroy = static_info.max_anisotroy = 16;
      dynamic_info.gpu_resource_id = static_info.gpu_resource_id = ~0ull;
      const HRESULT expected = reduction >= 2 ? E_NOTIMPL : S_OK;
      const HRESULT dynamic_hr = dxmt::PopulateWMTSamplerInfo({}, dynamic_info, dynamic_desc);
      const HRESULT static_hr = dxmt::PopulateWMTSamplerInfo({}, static_info, static_desc);
      if (dynamic_hr != expected || static_hr != expected) {
        std::cerr << "filter 0x" << std::hex << filter << " returned " << dynamic_hr << "/" << static_hr << "\n";
        return 1;
      }
      if (expected == S_OK) {
        const auto compare = reduction == 1 ? WMTCompareFunctionLess : WMTCompareFunctionNever;
        if (dynamic_info.compare_function != compare || static_info.compare_function != compare ||
            !dynamic_info.support_argument_buffers || !static_info.support_argument_buffers) {
          std::cerr << "ordinary/comparison sampler regression\n";
          return 1;
        }
        for (const auto *info : {&dynamic_info, &static_info}) {
          if (info->min_filter != ((base & 0x10) ? WMTSamplerMinMagFilterLinear : WMTSamplerMinMagFilterNearest) ||
              info->mag_filter != ((base & 0x04) ? WMTSamplerMinMagFilterLinear : WMTSamplerMinMagFilterNearest) ||
              info->mip_filter != ((base & 0x01) ? WMTSamplerMipFilterLinear : WMTSamplerMipFilterNearest) ||
              info->max_anisotroy != (base == 0x55 ? 16u : 1u) || info->gpu_resource_id) {
            std::cerr << "filter/anisotropy conversion regression\n";
            return 1;
          }
        }
      } else if (dynamic_info.support_argument_buffers || static_info.support_argument_buffers ||
                 dynamic_info.max_anisotroy || static_info.max_anisotroy ||
                 dynamic_info.gpu_resource_id || static_info.gpu_resource_id) {
        std::cerr << "unsupported reduction populated a usable sampler\n";
        return 1;
      }
      cases += 2;
    }
  }
  unsigned private_cases = 0;
  for (unsigned reduction : {0u, 2u, 3u}) {
    for (unsigned base : {0u, 1u, 4u, 5u, 16u, 17u, 20u, 21u}) {
      D3D12_SAMPLER_DESC desc = {};
      desc.Filter = static_cast<D3D12_FILTER>(base | (reduction << 7));
      desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
      desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
      desc.MinLOD = 1.25f; desc.MaxLOD = .75f; desc.MipLODBias = 2.f;
      WMTSamplerInfo point = {}, ordinary = {};
      dxmt_msc_minmax_state state = {};
      const uint32_t flags = ((base & 16) ? 1u : 0u) | ((base & 4) ? 2u : 0u) | ((base & 1) ? 4u : 0u) |
          (reduction ? DXMT_MSC_MINMAX_ENABLED : 0u) | (reduction == 3 ? 8u : 0u);
      if (FAILED(dxmt::PrepareD3D12MinMaxSamplerInfo({}, desc, point, ordinary, state)) ||
          state.flags != flags || state.min_lod != desc.MinLOD || state.max_lod != desc.MaxLOD ||
          state.address_u != desc.AddressU || state.address_v != desc.AddressV || state.resource_clamp ||
          state.default_components || state.reserved || !point.support_argument_buffers ||
          !ordinary.support_argument_buffers || point.lod_min_clamp || ordinary.lod_min_clamp ||
          point.lod_max_clamp != D3D12_FLOAT32_MAX || ordinary.lod_max_clamp != D3D12_FLOAT32_MAX ||
          point.min_filter != WMTSamplerMinMagFilterNearest || point.mag_filter != WMTSamplerMinMagFilterNearest ||
          point.mip_filter != WMTSamplerMipFilterNearest ||
          ordinary.min_filter != ((base & 16) ? WMTSamplerMinMagFilterLinear : WMTSamplerMinMagFilterNearest) ||
          ordinary.mag_filter != ((base & 4) ? WMTSamplerMinMagFilterLinear : WMTSamplerMinMagFilterNearest) ||
          ordinary.mip_filter != ((base & 1) ? WMTSamplerMipFilterLinear : WMTSamplerMipFilterNearest)) return 1;
      ++private_cases;
    }
  }
  for (unsigned bad = 0; bad < 6; ++bad) {
    D3D12_SAMPLER_DESC desc = {};
    desc.Filter = D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR;
    desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.MaxLOD = 10;
    if (bad == 0) desc.Filter = D3D12_FILTER_MINIMUM_ANISOTROPIC;
    if (bad == 1) desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    if (bad == 2) desc.MinLOD = std::numeric_limits<float>::quiet_NaN();
    if (bad == 3) desc.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(0);
    if (bad == 4) { desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER; desc.BorderColor[0] = .5f; }
    if (bad == 5) desc.Filter = static_cast<D3D12_FILTER>(0x200);
    WMTSamplerInfo point = {}, ordinary = {};
    point.gpu_resource_id = 123; ordinary.gpu_resource_id = 456;
    dxmt_msc_minmax_state state = {}; state.reserved = 789;
    const auto before_point = point, before_ordinary = ordinary;
    const auto before_state = state;
    if (SUCCEEDED(dxmt::PrepareD3D12MinMaxSamplerInfo({}, desc, point, ordinary, state)) ||
        std::memcmp(&point, &before_point, sizeof(point)) || std::memcmp(&ordinary, &before_ordinary, sizeof(ordinary)) ||
        std::memcmp(&state, &before_state, sizeof(state))) return 1;
  }
  std::cout << "D3D12 sampler filter contracts passed: " << cases << "; private MinMax: " << private_cases << " + 6 rejects\n";
  return 0;
}

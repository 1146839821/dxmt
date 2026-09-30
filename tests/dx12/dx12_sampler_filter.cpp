#include "d3d12_sampler.hpp"
#include "log/log.hpp"

#include <array>
#include <iostream>

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
  std::cout << "D3D12 sampler filter contracts passed: " << cases << "\n";
  return 0;
}

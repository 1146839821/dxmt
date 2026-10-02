#pragma once

#include <d3d12.h>
#include "Metal.hpp"

namespace dxmt {

uint32_t GetAIRSamplerReductionFlags(D3D12_FILTER filter);

HRESULT PopulateWMTSamplerInfo(WMT::Device device, WMTSamplerInfo &info, const D3D12_STATIC_SAMPLER_DESC &desc);
HRESULT PopulateWMTSamplerInfo(WMT::Device device, WMTSamplerInfo &info, const D3D12_SAMPLER_DESC &desc);

} // namespace dxmt

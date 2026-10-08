#pragma once

#include <d3d12.h>
#include "Metal.hpp"
#include "msc_minmax_abi.h"

namespace dxmt {

uint32_t GetAIRSamplerReductionFlags(D3D12_FILTER filter);

HRESULT PopulateWMTSamplerInfo(WMT::Device device, WMTSamplerInfo &info, const D3D12_STATIC_SAMPLER_DESC &desc);
HRESULT PopulateWMTSamplerInfo(WMT::Device device, WMTSamplerInfo &info, const D3D12_SAMPLER_DESC &desc);

// Private SampleLevel lowering contract. Native clamps/bias are removed;
// sampler clamps are applied in shader before resource clamp. No admission gate.
// Failure leaves all outputs unchanged. Texture-dependent state is filled later.
HRESULT PrepareD3D12MinMaxSamplerInfo(WMT::Device device, const D3D12_SAMPLER_DESC &desc,
    WMTSamplerInfo &point, WMTSamplerInfo &ordinary, dxmt_msc_minmax_state &state);

} // namespace dxmt

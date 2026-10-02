#pragma once

#include "d3d12_descriptor_heap.hpp"
#include "d3d12_minmax.hpp"

namespace dxmt {

// Submission/recording-owned pair. All descriptor handles have strong owners.
// Texture residency must still be declared by the consuming encoder.
struct D3D12MinMaxPairBinding {
  ShaderVisibleDescriptorSnapshot texture;
  Rc<Sampler> point_sampler;
  Rc<Sampler> ordinary_sampler;
  dxmt_msc_descriptor_entry texture_descriptor = {};
  dxmt_msc_descriptor_entry point_descriptor = {};
  dxmt_msc_descriptor_entry ordinary_descriptor = {};
  dxmt_msc_minmax_state state = {};
};

// No heap lookup: callers supply recording/static or submission/live snapshots.
// Exact captured native SRV view, zero private texture clamp and sampler bias.
// Failure leaves the output unchanged; does not enable command-list selection.
HRESULT PrepareD3D12MinMaxPairBinding(WMT::Device device, const ShaderVisibleDescriptorSnapshot &texture,
    const D3D12_SAMPLER_DESC &sampler, D3D12MinMaxPairBinding &binding);

} // namespace dxmt

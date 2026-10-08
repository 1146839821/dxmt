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

struct D3D12MinMaxPairSlot {
  UINT texture_index = 0;
  UINT sampler_index = 0;
  D3D12_DESCRIPTOR_RANGE_FLAGS texture_flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
  D3D12_DESCRIPTOR_RANGE_FLAGS sampler_flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
  bool static_sampler = false;
  D3D12_SAMPLER_DESC static_sampler_descriptor = {};
};

struct D3D12MinMaxPairObservation {
  D3D12MinMaxPairSlot slot;
  ShaderVisibleDescriptorSnapshot texture;
  SamplerDescriptorSnapshot sampler;
};

// Indices/flags must come from the resolved application root. Static root
// samplers never require a sampler heap. Outputs are atomic on failure.
HRESULT RecordD3D12MinMaxPairs(MTLD3D12DescriptorHeap *textures, MTLD3D12SamplerDescriptorHeap *samplers,
    const std::vector<D3D12MinMaxPairSlot> &slots, std::vector<D3D12MinMaxPairObservation> &observations);
HRESULT MaterializeD3D12MinMaxPairs(WMT::Device device, MTLD3D12DescriptorHeap *textures,
    MTLD3D12SamplerDescriptorHeap *samplers, const std::vector<D3D12MinMaxPairObservation> &observations,
    std::vector<D3D12MinMaxPairBinding> &bindings);

} // namespace dxmt

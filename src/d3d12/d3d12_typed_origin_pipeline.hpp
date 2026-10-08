#pragma once

#include "Metal.hpp"
#include "d3d12_typed_origin.hpp"
#include "d3d12_private_graphics_pipeline.hpp"

namespace dxmt {

// Immutable PSO-owned artifact; retain the application PSO while using it.
// Submission storage is separate from this immutable binding ABI.
struct D3D12TypedOriginBindingVariant {
  D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12TypedOriginRoot root;
  std::vector<dxmt_msc_typed_origin_binding> bindings;
  std::vector<D3D12TypedOriginBindingLocation> locations;
  // Present application stages, including stages without typed buffers.
  // Empty retains native ALL/VS/PS capture and the compute contract.
  std::vector<D3D12_SHADER_VISIBILITY> active_graphics_stages;

  bool CapturesTableStage(D3D12_SHADER_VISIBILITY stage) const {
    if (stage == D3D12_SHADER_VISIBILITY_ALL) return true;
    if (visibility != D3D12_SHADER_VISIBILITY_PIXEL) return false;
    if (active_graphics_stages.empty())
      return stage == D3D12_SHADER_VISIBILITY_VERTEX || stage == D3D12_SHADER_VISIBILITY_PIXEL;
    for (auto active : active_graphics_stages) if (active == stage) return true;
    return false;
  }
};

struct D3D12TypedOriginComputeVariant : D3D12TypedOriginBindingVariant {
  WMT::Reference<WMT::ComputePipelineState> pso;
  WMTSize threadgroup_size = {};
};

struct D3D12TypedOriginGraphicsVariant : D3D12TypedOriginBindingVariant, D3D12PrivateGraphicsPipeline {
  // PIXEL on the binding base selects native graphics table capture (ALL/VS/PS).
  D3D12TypedOriginGraphicsVariant() { visibility = D3D12_SHADER_VISIBILITY_PIXEL; }
  // Native records are VS then PS, with stage-local indexing. Emulation uses
  // global intervals and one TLAB for all stages instead.
  uint32_t vertex_binding_count = 0;

  bool UsesSharedOriginRecords() const { return geometry || tessellation; }
};

} // namespace dxmt

#pragma once

#include "Metal.hpp"
#include "d3d12_typed_origin.hpp"

namespace dxmt {

// Immutable PSO-owned artifact; retain the application PSO while using it.
// Submission storage is separate from this immutable binding ABI.
struct D3D12TypedOriginBindingVariant {
  D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12TypedOriginRoot root;
  std::vector<dxmt_msc_typed_origin_binding> bindings;
  std::vector<D3D12TypedOriginBindingLocation> locations;
};

struct D3D12TypedOriginComputeVariant : D3D12TypedOriginBindingVariant {
  WMT::Reference<WMT::ComputePipelineState> pso;
  WMTSize threadgroup_size = {};
};

struct D3D12TypedOriginGraphicsVariant : D3D12TypedOriginBindingVariant {
  D3D12TypedOriginGraphicsVariant() { visibility = D3D12_SHADER_VISIBILITY_PIXEL; }
  WMT::Reference<WMT::RenderPipelineState> pso;
};

} // namespace dxmt

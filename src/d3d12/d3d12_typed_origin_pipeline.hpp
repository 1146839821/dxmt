#pragma once

#include "Metal.hpp"
#include "d3d12_typed_origin.hpp"

namespace dxmt {

// Immutable PSO-owned artifact; retain the application PSO while using it.
// This is not yet selected by command-list encoding.
struct D3D12TypedOriginComputeVariant {
  WMT::Reference<WMT::ComputePipelineState> pso;
  WMTSize threadgroup_size = {};
  D3D12TypedOriginRoot root;
  std::vector<dxmt_msc_typed_origin_binding> bindings;
  std::vector<D3D12TypedOriginBindingLocation> locations;
};

} // namespace dxmt

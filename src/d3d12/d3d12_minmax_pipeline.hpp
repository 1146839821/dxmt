#pragma once

#include "Metal.hpp"
#include "d3d12_minmax.hpp"

namespace dxmt {

// Immutable PSO-owned artifact. Retain the application PSO while borrowing it.
// Command-list selection additionally requires coherent private descriptors/state.
struct D3D12MinMaxComputeVariant {
  WMT::Reference<WMT::ComputePipelineState> pso;
  WMTSize threadgroup_size = {};
  D3D12MinMaxRoot root;
  std::vector<dxmt_msc_minmax_binding> bindings;
  std::vector<D3D12MinMaxPairLocation> locations;
};

} // namespace dxmt

#pragma once

#include "Metal.hpp"
#include "d3d12_minmax.hpp"

namespace dxmt {

// Immutable PSO-owned artifact. Retain the application PSO while borrowing it.
// Command-list selection additionally requires coherent private descriptors/state.
struct D3D12MinMaxBindingVariant {
  D3D12MinMaxShaderStage stage = D3D12MinMaxShaderStage::Compute;
  D3D12MinMaxRoot root;
  std::vector<dxmt_msc_minmax_binding> bindings;
  std::vector<D3D12MinMaxPairLocation> locations;
  // Empty retains the single-stage contract; graphics can label each interval.
  std::vector<D3D12MinMaxShaderStage> binding_stages;
  // All application stages, including stages without reduction samples.
  // Empty preserves the legacy ordinary VS/PS capture contract.
  std::vector<D3D12MinMaxShaderStage> active_graphics_stages;
};

struct D3D12MinMaxComputeVariant : D3D12MinMaxBindingVariant {
  WMT::Reference<WMT::ComputePipelineState> pso;
  WMTSize threadgroup_size = {};
};

struct D3D12MinMaxGraphicsVariant : D3D12MinMaxBindingVariant {
  D3D12MinMaxGraphicsVariant() { stage = D3D12MinMaxShaderStage::Pixel; }
  WMT::Reference<WMT::RenderPipelineState> pso;
  bool geometry = false;
  bool tessellation = false;
  WMTMSCGeometryPipelineConfig geometry_config = {};
  WMTMSCTessellationPipelineConfig tessellation_config = {};
};

} // namespace dxmt

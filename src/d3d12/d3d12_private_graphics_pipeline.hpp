#pragma once

#include "Metal.hpp"

namespace dxmt {

// A private shader transformation can change companion dispatch reflection.
// Keep its PSO and draw configuration together, independently of its binding ABI.
struct D3D12PrivateGraphicsPipeline {
  WMT::Reference<WMT::RenderPipelineState> pso;
  bool geometry = false;
  bool tessellation = false;
  WMTMSCGeometryPipelineConfig geometry_config = {};
  WMTMSCTessellationPipelineConfig tessellation_config = {};
};

} // namespace dxmt

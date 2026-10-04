#pragma once
#include <string>
#include <vector>
#include "../msc_minmax_abi.h"

namespace llvm { class CallInst; class Value; class Module; }
namespace dxmt::dxil {
// Values are supplied by the caller's sampler/resource binding lowering, not
// read from MSC's 24-byte descriptor. Flags match the private reduction filter
// bits 0..3: min-linear, mag-linear, mip-linear, maximum.
struct ReductionSampleState {
  llvm::Value *flags;
  llvm::Value *min_lod;
  llvm::Value *max_lod;
  llvm::Value *resource_clamp;
  llvm::Value *default_components;
  llvm::Value *point_texture;
  llvm::Value *address_u;
  llvm::Value *address_v;
  llvm::Value *address_w = nullptr;
};

// Private-module transformation. Caller must prove a float Texture1D/Texture2D/Texture3D
// or corresponding array handle, matching spatial_dimensions (1..3),
// finite coordinates and a point, unbiased/unclamped sampler with the original
// address/border modes. Only component extracts (no residency/status) are
// accepted. LLVM verification does not replace regenerated DXIL validation.
// point_texture must reference the SAME view with its MSC resource clamp zero;
// reusing a nonzero-clamp descriptor would re-clamp each generated integer tap.
// Address values use D3D's 1..5 encoding and must be validated by the caller.
// No production admission may use this until binding/provenance is connected.
// Explicit cube mode requires spatial_dimensions=2, finite nonzero xyz and
// zero/undefined offsets. It preserves the Cube/CubeArray handle and layer,
// reconstructs face-interior directions, and does not open binding admission.
bool LowerReductionSampleLevel(llvm::CallInst &sample,
    const ReductionSampleState &state, std::string &error, unsigned spatial_dimensions = 2,
    bool cube = false);

// Emit the view-relative isotropic LOD before a qualified float 1D/2D/3D (or array)
// SampleGrad. Mirrors AIR's normalized major-axis algorithm. The caller still
// applies sampler bias/clamps and instruction/resource clamps in API order.
// This helper does not rewrite or admit the sampling operation itself.
// Cube qualification uses two projected axes, three direction/gradient operands
// and a common side width. This does not admit Cube binding or lower its taps.
llvm::Value *CreateReductionGradientLOD(llvm::CallInst &sample, std::string &error,
    unsigned spatial_dimensions = 2, bool cube = false);

// Qualify float 1D/2D/3D/Cube (or array) SampleLevel/SampleGrad pairs and pixel-only
// Sample/SampleBias, append private tN/sN and
// b0 in DXMT_MSC_MINMAX_SPACE, and guard reduction with the runtime enabled bit.
// Returned pair ordinal N selects its point texture/sampler and 32-byte CBV
// state; sampler N+pair_count is an unclamped ordinary-filter sampler. Both
// branches apply sampler/resource LOD semantics explicitly; gradients also
// apply runtime bias and instruction clamp. Rejecting a private
// module never publishes an artifact.
// The application root must be augmented/reflected and the regenerated DXIL
// container fully validated before MSC compilation. No runtime admission here.
bool LowerReductionSamplerBindings(llvm::Module &module,
    std::vector<dxmt_msc_minmax_binding> &bindings, std::string &error,
    unsigned pair_offset = 0, unsigned pair_count = 0);
}

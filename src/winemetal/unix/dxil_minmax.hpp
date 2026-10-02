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
};

// Private-module transformation. Caller must prove a float Texture2D handle,
// finite coordinates and a point, unbiased/unclamped sampler with the original
// address/border modes. Only component extracts (no residency/status) are
// accepted. LLVM verification does not replace regenerated DXIL validation.
// point_texture must reference the SAME view with its MSC resource clamp zero;
// reusing a nonzero-clamp descriptor would re-clamp each generated integer tap.
// Address values use D3D's 1..5 encoding and must be validated by the caller.
// No production admission may use this until binding/provenance is connected.
bool LowerReductionSampleLevel2D(llvm::CallInst &sample,
    const ReductionSampleState &state, std::string &error);

// Emit the view-relative isotropic LOD before a qualified float Texture2D
// SampleGrad. Mirrors AIR's normalized major-axis algorithm. The caller still
// applies sampler bias/clamps and instruction/resource clamps in API order.
// This helper does not rewrite or admit the sampling operation itself.
llvm::Value *CreateReductionGradientLOD2D(llvm::CallInst &sample, std::string &error);

// Qualify finite legacy Texture2D/SamplerState pairs, append private tN/sN and
// b0 in DXMT_MSC_MINMAX_SPACE, and guard reduction with the runtime enabled bit.
// Returned pair ordinal N selects its point texture/sampler and 32-byte CBV
// state; sampler N+pair_count is an unclamped ordinary-filter sampler. Both
// branches apply sampler/resource LOD semantics explicitly. Rejecting a private
// module never publishes an artifact.
// The application root must be augmented/reflected and the regenerated DXIL
// container fully validated before MSC compilation. No runtime admission here.
bool LowerReductionSamplerBindings(llvm::Module &module,
    std::vector<dxmt_msc_minmax_binding> &bindings, std::string &error);
}

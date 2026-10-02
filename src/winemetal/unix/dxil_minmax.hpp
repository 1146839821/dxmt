#pragma once
#include <string>

namespace llvm { class CallInst; class Value; }
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
}

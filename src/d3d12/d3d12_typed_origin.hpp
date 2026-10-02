#pragma once

#include "d3d12.h"
#include "metalirconverter_thunks.h"
#include <string>
#include <vector>

namespace dxmt {

// Private shader preparation, not an enabled binding ABI. The caller must
// reserve b0/space1 in the compiler root and supply one 16-byte record per
// binding before passing this shader to MSC. Application shader/root identity
// remains separate from this artifact's transformed identity.
struct D3D12TypedOriginShader {
  static constexpr uint32_t kLoweringVersion = 1;
  std::vector<uint8_t> bytecode;
  std::vector<dxmt_msc_typed_origin_binding> bindings;
};

// The selected DXC directory must be an absolute Windows drive path. Both
// compiler and validator are loaded from that directory; no backend fallback.
// Currently accepts the verified SM6.0 compute container envelope. On failure
// the caller's artifact is unchanged.
HRESULT PrepareD3D12TypedOriginShader(
    const D3D12_SHADER_BYTECODE &shader, const wchar_t *dxc_directory,
    D3D12TypedOriginShader &prepared, std::string &diagnostics);

} // namespace dxmt

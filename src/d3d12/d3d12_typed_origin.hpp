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

struct D3D12TypedOriginRoot {
  static constexpr uint32_t kBindingVersion = 1;
  std::vector<uint8_t> bytecode;
  // Application parameter indices are preserved; the hidden CBV is appended
  // and a reflected static-sampler entry, if present, remains last.
  std::vector<dxmt_msc_root_parameter_layout> layouts;
  uint64_t argument_buffer_size = 0;
  uint32_t application_parameter_count = 0;
  uint32_t application_cost = 0;
  uint32_t hidden_parameter_index = 0;
};

// Accepts a trusted, decoded RS1.1 descriptor (including the existing RS1.0
// deserializer's volatile conversion). Owns serialized bytes and reflected
// layout; never retains input pointers. Failure leaves output unchanged.
HRESULT PrepareD3D12TypedOriginRoot(
    const D3D12_ROOT_SIGNATURE_DESC1 &application, D3D12TypedOriginRoot &prepared,
    std::string &diagnostics);

// The selected DXC directory must be an absolute Windows drive path. Both
// compiler and validator are loaded from that directory; no backend fallback.
// Currently accepts the verified SM6.0 compute container envelope. On failure
// the caller's artifact is unchanged.
HRESULT PrepareD3D12TypedOriginShader(
    const D3D12_SHADER_BYTECODE &shader, const wchar_t *dxc_directory,
    D3D12TypedOriginShader &prepared, std::string &diagnostics);

} // namespace dxmt

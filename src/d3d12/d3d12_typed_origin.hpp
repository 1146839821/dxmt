#pragma once

#include "d3d12.h"
#include "metalirconverter_thunks.h"
#include "d3d12_compiler_root.hpp"
#include <string>
#include <vector>

namespace dxmt {

// S_OK identifies typed buffers using the selected or deployed compiler;
// S_FALSE leaves an ordinary shader on its native path. An explicitly selected
// unavailable compiler fails rather than silently disabling correction.
HRESULT SelectD3D12TypedOriginCompiler(const D3D12_SHADER_BYTECODE &shader, std::wstring &directory,
    const wchar_t *selected_directory = nullptr);

// Private shader preparation. The caller must
// reserve b0/space1 in the compiler root and supply one 16-byte record per
// binding before passing this shader to MSC. Application shader/root identity
// remains separate from this artifact's transformed identity.
struct D3D12TypedOriginShader {
  static constexpr uint32_t kLoweringVersion = 7;
  D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
  uint32_t record_offset = 0;
  uint32_t record_count = 0;
  std::vector<uint8_t> bytecode;
  std::vector<uint8_t> application_root_signature;
  std::vector<dxmt_msc_typed_origin_binding> bindings;
};

struct D3D12TypedOriginRoot : D3D12CompilerRoot {};

struct D3D12TypedOriginBindingLocation {
  uint32_t parameter_index = 0;
  uint32_t table_offset = 0;
  D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
};

// Resolve shader resource identities to application descriptor-table slots.
// No GPU heap handle is resolved here; static/volatile observation stays with
// command recording/submission. Failure leaves locations unchanged.
HRESULT ResolveD3D12TypedOriginBindings(
    const D3D12TypedOriginRoot &root, const std::vector<dxmt_msc_typed_origin_binding> &bindings,
    std::vector<D3D12TypedOriginBindingLocation> &locations, std::string &diagnostics,
    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

// Accepts a trusted, decoded RS1.1 descriptor (including the existing RS1.0
// deserializer's volatile conversion). Owns serialized bytes and reflected
// layout; never retains input pointers. Failure leaves output unchanged.
HRESULT PrepareD3D12TypedOriginRoot(
    const D3D12_ROOT_SIGNATURE_DESC1 &application, D3D12TypedOriginRoot &prepared,
    std::string &diagnostics);

// The selected DXC directory must be an absolute Windows drive path. Both
// compiler and validator are loaded from that directory; no backend fallback.
// Accepts bounded SM6.0-6.6 compute and VS/PS/GS/HS/DS envelopes. A nonzero
// record_count selects a shared CBV interval; zero preserves stage-local layout.
// On failure
// the caller's artifact is unchanged.
HRESULT PrepareD3D12TypedOriginShader(
    const D3D12_SHADER_BYTECODE &shader, const wchar_t *dxc_directory,
    D3D12TypedOriginShader &prepared, std::string &diagnostics,
    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL,
    uint32_t record_offset = 0, uint32_t record_count = 0);

} // namespace dxmt

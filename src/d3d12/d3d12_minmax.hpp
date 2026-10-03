#pragma once
#include "d3d12.h"
#include "msc_minmax_abi.h"
#include "d3d12_compiler_root.hpp"
#include <string>
#include <vector>

namespace dxmt {
enum class D3D12MinMaxShaderStage { Compute, Pixel, Vertex };
// Owned compiler artifact, not an admitted PSO. The caller must augment and
// reflect the root and provide the private state/point/ordinary bindings.
struct D3D12MinMaxShader {
  static constexpr uint32_t kLoweringVersion = DXMT_MSC_MINMAX_VERSION;
  D3D12MinMaxShaderStage stage = D3D12MinMaxShaderStage::Compute;
  std::vector<uint8_t> bytecode;
  std::vector<dxmt_msc_minmax_binding> bindings;
  // This stage owns [pair_offset, pair_offset + bindings.size()) in the shared layout.
  uint32_t pair_offset = 0;
  uint32_t pair_count = 0;
};
struct D3D12MinMaxRoot {
  D3D12CompilerRoot layout;
  uint32_t pair_count = 0;
};
struct D3D12MinMaxLocation {
  uint32_t parameter_index = UINT32_MAX;
  uint32_t table_offset = 0;
  D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
  uint32_t static_sampler_index = UINT32_MAX;
};
struct D3D12MinMaxPairLocation {
  D3D12MinMaxLocation texture;
  D3D12MinMaxLocation sampler;
};
// Reserve space 2 and four DWORDs. Append b0, t[0,N), s[0,2N) in that order.
// Trusted decoded RS1.1 input; RS1.0 uses the existing volatile conversion.
// Neither call publishes an artifact on failure or resolves GPU heap handles.
HRESULT PrepareD3D12MinMaxRoot(const D3D12_ROOT_SIGNATURE_DESC1 &application, uint32_t pair_count,
    D3D12MinMaxRoot &prepared, std::string &diagnostics);
HRESULT ResolveD3D12MinMaxBindings(const D3D12MinMaxRoot &root,
    const std::vector<dxmt_msc_minmax_binding> &bindings,
    std::vector<D3D12MinMaxPairLocation> &locations, std::string &diagnostics,
    D3D12MinMaxShaderStage stage = D3D12MinMaxShaderStage::Compute);
// Selected absolute Windows DXC directory; validated SM6.0..6.6 compute/pixel/vertex envelope.
// Zero pair_count retains a local layout; otherwise use the given shared interval.
// No fallback. Failure leaves the artifact unchanged.
HRESULT PrepareD3D12MinMaxShader(const D3D12_SHADER_BYTECODE &shader, const wchar_t *dxc_directory,
    D3D12MinMaxShader &prepared, std::string &diagnostics,
    D3D12MinMaxShaderStage stage = D3D12MinMaxShaderStage::Compute,
    uint32_t pair_offset = 0, uint32_t pair_count = 0);
}

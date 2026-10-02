#pragma once
#include "d3d12.h"
#include "msc_minmax_abi.h"
#include <string>
#include <vector>

namespace dxmt {
// Owned compiler artifact, not an admitted PSO. The caller must augment and
// reflect the root and provide the private state/point/ordinary bindings.
struct D3D12MinMaxShader {
  static constexpr uint32_t kLoweringVersion = DXMT_MSC_MINMAX_VERSION;
  std::vector<uint8_t> bytecode;
  std::vector<dxmt_msc_minmax_binding> bindings;
};
// Selected absolute Windows DXC directory; validated SM6.0 compute envelope.
// No fallback. Failure leaves the artifact unchanged.
HRESULT PrepareD3D12MinMaxShader(const D3D12_SHADER_BYTECODE &shader, const wchar_t *dxc_directory,
    D3D12MinMaxShader &prepared, std::string &diagnostics);
}

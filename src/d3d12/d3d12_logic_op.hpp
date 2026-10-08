#pragma once
#include "d3d12.h"
#include <array>
#include <string>
#include <vector>

namespace dxmt {
struct D3D12LogicOpShader {
  static constexpr uint32_t kLoweringVersion = 1;
  uint32_t framebuffer_space = UINT32_MAX;
  std::vector<uint8_t> bytecode;
  std::vector<uint8_t> application_root_signature;
};
// Compiler-only preparation; caller must qualify target formats/device, check
// the returned feature space against its root and specialize MSC/cache state.
// Failure leaves the caller's artifact unchanged. No backend fallback.
HRESULT PrepareD3D12LogicOpShader(const D3D12_SHADER_BYTECODE &shader,
    const wchar_t *dxc_directory, uint32_t operation,
    const std::array<std::array<uint32_t, 4>, 8> &component_bits,
    D3D12LogicOpShader &prepared, std::string &diagnostics);
}

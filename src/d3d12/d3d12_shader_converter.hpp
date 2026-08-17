#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "d3d12.h"
#include "metalirconverter_thunks.h"

namespace dxmt {

enum class D3D12ShaderBackend {
  Airconv,
  MetalShaderConverter,
  Unsupported,
};

struct D3D12ConvertedShader {
  std::vector<uint8_t> metallib;
  std::string entry_point;
  std::array<uint32_t, 3> threadgroup_size = {};
  D3D12ShaderBackend backend = D3D12ShaderBackend::Airconv;
};

D3D12ShaderBackend
DetectD3D12ShaderBackend(const D3D12_SHADER_BYTECODE &shader);

HRESULT
ConvertD3D12ComputeShader(
    const D3D12_SHADER_BYTECODE &shader, D3D12ConvertedShader &converted, const void *root_signature = nullptr,
    size_t root_signature_size = 0
);

} // namespace dxmt

#include "d3d12_shader_converter.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#include "DXBCParser/BlobContainer.h"
#include "log/log.hpp"
#include "metalirconverter_thunks.h"

namespace dxmt {

namespace {

constexpr uint32_t
MakeFourCC(char a, char b, char c, char d) {
  return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kDXILFourCC = MakeFourCC('D', 'X', 'I', 'L');

bool
HasDXBCHeader(const D3D12_SHADER_BYTECODE &shader) {
  if (!shader.pShaderBytecode || shader.BytecodeLength < sizeof(uint32_t))
    return false;

  uint32_t fourcc = 0;
  memcpy(&fourcc, shader.pShaderBytecode, sizeof(fourcc));
  return fourcc == MakeFourCC('D', 'X', 'B', 'C');
}

void
LogMSCFailure(const dxmt_msc_compile_dxil_params &params, int result) {
  const char *message = params.error_message ? params.error_message : "";
  ERR("DXIL compute conversion failed, result=", result, " code=", params.error_code, " message=", message);
}

int
CompileDXIL(const D3D12_SHADER_BYTECODE &shader, const void *root_signature, size_t root_signature_size, void *metallib,
           size_t metallib_capacity, char *entry_point,
           size_t entry_point_capacity, size_t *metallib_size, size_t *entry_point_size,
           std::array<uint32_t, 3> *threadgroup_size, char *error_message, size_t error_message_capacity) {
  dxmt_msc_compile_dxil_params params = {};
  params.dxil = shader.pShaderBytecode;
  params.dxil_size = shader.BytecodeLength;
  params.stage = DXMT_MSC_STAGE_COMPUTE;
  params.root_signature = root_signature;
  params.root_signature_size = root_signature_size;
  params.metallib = metallib;
  params.metallib_capacity = metallib_capacity;
  params.entry_point_out = entry_point;
  params.entry_point_capacity = entry_point_capacity;
  params.error_message = error_message;
  params.error_message_capacity = error_message_capacity;

  int result = DXMTMSCCompileDXIL(&params);
  if (metallib_size)
    *metallib_size = params.metallib_size;
  if (entry_point_size)
    *entry_point_size = params.entry_point_size;
  if (threadgroup_size) {
    (*threadgroup_size)[0] = params.threadgroup_size[0];
    (*threadgroup_size)[1] = params.threadgroup_size[1];
    (*threadgroup_size)[2] = params.threadgroup_size[2];
  }
  if (result != DXMT_MSC_SUCCESS)
    LogMSCFailure(params, result);
  return result;
}

} // namespace

D3D12ShaderBackend
DetectD3D12ShaderBackend(const D3D12_SHADER_BYTECODE &shader) {
  if (!HasDXBCHeader(shader))
    return D3D12ShaderBackend::Airconv;
  if (shader.BytecodeLength > std::numeric_limits<uint32_t>::max())
    return D3D12ShaderBackend::Unsupported;

  microsoft::CDXBCParser parser;
  if (FAILED(parser.ReadDXBC(shader.pShaderBytecode, static_cast<uint32_t>(shader.BytecodeLength))))
    return D3D12ShaderBackend::Unsupported;

  for (uint32_t i = 0; i < parser.GetBlobCount(); i++) {
    if (parser.GetBlobFourCC(i) == kDXILFourCC)
      return D3D12ShaderBackend::MetalShaderConverter;
  }
  return D3D12ShaderBackend::Airconv;
}

HRESULT
ConvertD3D12ComputeShader(
    const D3D12_SHADER_BYTECODE &shader, D3D12ConvertedShader &converted, const void *root_signature,
    size_t root_signature_size
) {
  if (DetectD3D12ShaderBackend(shader) != D3D12ShaderBackend::MetalShaderConverter)
    return E_INVALIDARG;

  if (DXMTMSCIsAvailable() != 1) {
    ERR("DXIL detected but Metal Shader Converter is unavailable");
    return E_FAIL;
  }

  char error_message[1024] = {};
  size_t metallib_size = 0;
  size_t entry_point_size = 0;
  std::array<uint32_t, 3> threadgroup_size = {};

  int result = CompileDXIL(
      shader, root_signature, root_signature_size, nullptr, 0, nullptr, 0, &metallib_size, &entry_point_size,
      &threadgroup_size, error_message, sizeof(error_message)
  );
  if (result != DXMT_MSC_SUCCESS)
    return E_FAIL;
  if (!metallib_size || !entry_point_size)
    return E_FAIL;

  converted.metallib.resize(metallib_size);
  std::vector<char> entry_point(entry_point_size);
  error_message[0] = '\0';

  result = CompileDXIL(
      shader, root_signature, root_signature_size, converted.metallib.data(), converted.metallib.size(),
      entry_point.data(), entry_point.size(), &metallib_size, &entry_point_size, &threadgroup_size, error_message,
      sizeof(error_message)
  );
  if (result != DXMT_MSC_SUCCESS)
    return E_FAIL;

  if (entry_point_size == 0 || entry_point[entry_point_size - 1] != '\0') {
    ERR("DXIL conversion returned an invalid entry point");
    return E_FAIL;
  }

  converted.entry_point.assign(entry_point.data(), entry_point_size - 1);
  converted.threadgroup_size = threadgroup_size;
  converted.backend = D3D12ShaderBackend::MetalShaderConverter;
  return S_OK;
}

} // namespace dxmt

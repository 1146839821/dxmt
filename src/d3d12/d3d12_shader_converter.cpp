#include "d3d12_shader_converter.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "DXBCParser/BlobContainer.h"
#include "log/log.hpp"
#include "metalirconverter_thunks.h"
#include "sha1/sha1_util.hpp"

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

// This cache is process-local, but the key still encodes every converter input
// that can change the generated metallib. Bump the version when the ABI or
// converter defaults change.
constexpr uint32_t kMSCConversionCacheVersion = 1;
constexpr uint32_t kMSCConverterAPIVersion = 0x040001;
constexpr uint32_t kMSCMetalTargetVersion = 0;
constexpr uint32_t kMSCCompileFlags = 0;
constexpr uint32_t kMSCBindingLayoutVersion = 1;

struct MSCConversionCache {
  std::shared_mutex mutex;
  std::unordered_map<Sha1Digest, D3D12ConvertedShader> entries;
};

MSCConversionCache &
GetMSCConversionCache() {
  static MSCConversionCache cache;
  return cache;
}

Sha1Digest
MakeMSCConversionCacheKey(
    const D3D12_SHADER_BYTECODE &shader, uint32_t stage, const void *root_signature, size_t root_signature_size
) {
  Sha1HashState hash;
  hash.update(kMSCConversionCacheVersion);
  hash.update(kMSCConverterAPIVersion);
  hash.update(kMSCMetalTargetVersion);
  hash.update(kMSCCompileFlags);
  hash.update(kMSCBindingLayoutVersion);
  hash.update(stage);
  uint64_t shader_size = shader.BytecodeLength;
  hash.update(shader_size);
  hash.update(shader.pShaderBytecode, shader.BytecodeLength);
  uint64_t root_size = root_signature ? root_signature_size : 0;
  hash.update(root_size);
  if (root_size)
    hash.update(root_signature, root_signature_size);
  return hash.final();
}

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
  ERR("DXIL conversion failed, result=", result, " code=", params.error_code, " message=", message);
}

int
CompileDXIL(
    const D3D12_SHADER_BYTECODE &shader, uint32_t stage, const void *root_signature, size_t root_signature_size,
    void *metallib, size_t metallib_capacity, char *entry_point, size_t entry_point_capacity, size_t *metallib_size,
    size_t *entry_point_size, std::array<uint32_t, 3> *threadgroup_size, char *error_message,
    size_t error_message_capacity
) {
  dxmt_msc_compile_dxil_params params = {};
  params.dxil = shader.pShaderBytecode;
  params.dxil_size = shader.BytecodeLength;
  params.stage = stage;
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
ConvertD3D12Shader(
    const D3D12_SHADER_BYTECODE &shader, uint32_t stage, D3D12ConvertedShader &converted, const void *root_signature,
    size_t root_signature_size
) {
  if (DetectD3D12ShaderBackend(shader) != D3D12ShaderBackend::MetalShaderConverter)
    return E_INVALIDARG;

  if (DXMTMSCIsAvailable() != 1) {
    ERR("DXIL detected but Metal Shader Converter is unavailable");
    return E_FAIL;
  }

  auto cache_key = MakeMSCConversionCacheKey(shader, stage, root_signature, root_signature_size);
  auto &cache = GetMSCConversionCache();
  {
    std::shared_lock<std::shared_mutex> lock(cache.mutex);
    auto cached = cache.entries.find(cache_key);
    if (cached != cache.entries.end()) {
      converted = cached->second;
      DEBUG("MSC shader conversion cache hit");
      return S_OK;
    }
  }

  char error_message[1024] = {};
  size_t metallib_size = 0;
  size_t entry_point_size = 0;
  std::array<uint32_t, 3> threadgroup_size = {};

  int result = CompileDXIL(
      shader, stage, root_signature, root_signature_size, nullptr, 0, nullptr, 0, &metallib_size, &entry_point_size,
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
      shader, stage, root_signature, root_signature_size, converted.metallib.data(), converted.metallib.size(),
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

  {
    std::unique_lock<std::shared_mutex> lock(cache.mutex);
    cache.entries.emplace(cache_key, converted);
  }
  return S_OK;
}

HRESULT
ConvertD3D12ComputeShader(
    const D3D12_SHADER_BYTECODE &shader, D3D12ConvertedShader &converted, const void *root_signature,
    size_t root_signature_size
) {
  return ConvertD3D12Shader(shader, DXMT_MSC_STAGE_COMPUTE, converted, root_signature, root_signature_size);
}

} // namespace dxmt

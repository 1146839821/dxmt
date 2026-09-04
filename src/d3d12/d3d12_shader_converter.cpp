#include "d3d12_shader_converter.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "DXBCParser/BlobContainer.h"
#include "dxmt_shader_cache.hpp"
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
constexpr uint32_t kMSCConversionCacheVersion = 5;
constexpr uint32_t kMSCConverterAPIVersion = 0x040001;
constexpr uint32_t kMSCMetalTargetVersion = 0;
constexpr uint32_t kMSCCompileFlags = 0;
constexpr uint32_t kMSCBindingLayoutVersion = 1;
constexpr char kMSCConversionCacheNamespace[] = "dxmt-msc-conversion";
constexpr char kMSCEntryPointPolicy[] = "auto-from-dxil";
constexpr uint32_t kMSCSerializedCacheMagic = MakeFourCC('M', 'S', 'C', 'C');
constexpr uint64_t kMSCSerializedCacheLimit = 256ull * 1024ull * 1024ull;

struct MSCSerializedCacheHeader {
  uint32_t magic;
  uint32_t version;
  uint64_t metallib_size;
  uint64_t stage_in_metallib_size;
  uint64_t entry_point_size;
  uint32_t threadgroup_size[3];
  dxmt_msc_shader_reflection reflection;
};

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
    const D3D12_SHADER_BYTECODE &shader, uint32_t stage, const void *root_signature, size_t root_signature_size,
    const dxmt_msc_input_layout *input_layout, uint32_t compile_flags
) {
  Sha1HashState hash;
  hash.update(kMSCConversionCacheNamespace, sizeof(kMSCConversionCacheNamespace) - 1);
  hash.update(kMSCEntryPointPolicy, sizeof(kMSCEntryPointPolicy) - 1);
  hash.update(kMSCConversionCacheVersion);
  hash.update(kMSCConverterAPIVersion);
  hash.update(kMSCMetalTargetVersion);
  hash.update(kMSCCompileFlags);
  hash.update(kMSCBindingLayoutVersion);
  hash.update(stage);
  compile_flags |= input_layout ? DXMT_MSC_COMPILE_FLAG_SYNTHESIZE_STAGE_IN : 0;
  hash.update(compile_flags);
  if (input_layout)
    hash.update(input_layout, sizeof(*input_layout));
  uint64_t shader_size = shader.BytecodeLength;
  hash.update(shader_size);
  hash.update(Sha1HashState::compute(shader.pShaderBytecode, shader.BytecodeLength));
  uint64_t root_size = root_signature ? root_signature_size : 0;
  hash.update(root_size);
  Sha1Digest root_signature_hash = {};
  if (root_size)
    root_signature_hash = Sha1HashState::compute(root_signature, root_signature_size);
  hash.update(root_signature_hash);
  return hash.final();
}

bool
DeserializeMSCConversionCache(const uint8_t *data, size_t data_size, D3D12ConvertedShader &converted) {
  if (!data || data_size < sizeof(MSCSerializedCacheHeader))
    return false;

  MSCSerializedCacheHeader header;
  memcpy(&header, data, sizeof(header));
  if (header.magic != kMSCSerializedCacheMagic || header.version != kMSCConversionCacheVersion)
    return false;
  if (header.metallib_size > kMSCSerializedCacheLimit || header.stage_in_metallib_size > kMSCSerializedCacheLimit ||
      header.entry_point_size > kMSCSerializedCacheLimit)
    return false;

  uint64_t payload_size = header.metallib_size;
  if (payload_size > UINT64_MAX - header.stage_in_metallib_size)
    return false;
  payload_size += header.stage_in_metallib_size;
  if (payload_size > UINT64_MAX - header.entry_point_size)
    return false;
  payload_size += header.entry_point_size;
  if (payload_size > data_size - sizeof(header))
    return false;

  size_t offset = sizeof(header);
  converted.metallib.assign(data + offset, data + offset + static_cast<size_t>(header.metallib_size));
  offset += static_cast<size_t>(header.metallib_size);
  converted.stage_in_metallib.assign(
      data + offset, data + offset + static_cast<size_t>(header.stage_in_metallib_size)
  );
  offset += static_cast<size_t>(header.stage_in_metallib_size);
  converted.entry_point.assign(reinterpret_cast<const char *>(data + offset),
                               static_cast<size_t>(header.entry_point_size));
  converted.threadgroup_size = {
      header.threadgroup_size[0], header.threadgroup_size[1], header.threadgroup_size[2]
  };
  converted.reflection = header.reflection;
  converted.backend = D3D12ShaderBackend::MetalShaderConverter;
  return !converted.metallib.empty() && !converted.entry_point.empty();
}

std::vector<uint8_t>
SerializeMSCConversionCache(const D3D12ConvertedShader &converted) {
  if (converted.metallib.empty() || converted.entry_point.empty() ||
      converted.metallib.size() > kMSCSerializedCacheLimit ||
      converted.stage_in_metallib.size() > kMSCSerializedCacheLimit ||
      converted.entry_point.size() > kMSCSerializedCacheLimit)
    return {};

  MSCSerializedCacheHeader header = {};
  header.magic = kMSCSerializedCacheMagic;
  header.version = kMSCConversionCacheVersion;
  header.metallib_size = converted.metallib.size();
  header.stage_in_metallib_size = converted.stage_in_metallib.size();
  header.entry_point_size = converted.entry_point.size();
  header.threadgroup_size[0] = converted.threadgroup_size[0];
  header.threadgroup_size[1] = converted.threadgroup_size[1];
  header.threadgroup_size[2] = converted.threadgroup_size[2];
  header.reflection = converted.reflection;

  uint64_t total_size = sizeof(header);
  if (header.metallib_size > UINT64_MAX - total_size)
    return {};
  total_size += header.metallib_size;
  if (header.stage_in_metallib_size > UINT64_MAX - total_size)
    return {};
  total_size += header.stage_in_metallib_size;
  if (header.entry_point_size > UINT64_MAX - total_size)
    return {};
  total_size += header.entry_point_size;
  if (total_size > kMSCSerializedCacheLimit || total_size > SIZE_MAX)
    return {};

  std::vector<uint8_t> result(static_cast<size_t>(total_size));
  memcpy(result.data(), &header, sizeof(header));
  size_t offset = sizeof(header);
  memcpy(result.data() + offset, converted.metallib.data(), converted.metallib.size());
  offset += converted.metallib.size();
  memcpy(result.data() + offset, converted.stage_in_metallib.data(), converted.stage_in_metallib.size());
  offset += converted.stage_in_metallib.size();
  memcpy(result.data() + offset, converted.entry_point.data(), converted.entry_point.size());
  return result;
}

bool
LoadPersistentMSCConversion(const Sha1Digest &key, D3D12ConvertedShader &converted) {
  auto &cache = ShaderCache::getInstance(WMTMetalVersionMax);
  auto reader = cache.getReader();
  if (!reader)
    return false;

  auto data = reader->get(key);
  if (!data)
    return false;
  uint64_t data_size = data.size();
  if (!data_size || data_size > kMSCSerializedCacheLimit || data_size > SIZE_MAX)
    return false;

  std::vector<uint8_t> serialized(static_cast<size_t>(data_size));
  if (data.copy(serialized.data(), data_size) != data_size)
    return false;
  return DeserializeMSCConversionCache(serialized.data(), serialized.size(), converted);
}

void
StorePersistentMSCConversion(const Sha1Digest &key, const D3D12ConvertedShader &converted) {
  auto serialized = SerializeMSCConversionCache(converted);
  if (serialized.empty())
    return;

  auto &cache = ShaderCache::getInstance(WMTMetalVersionMax);
  auto writer = cache.getWriter();
  if (!writer)
    return;
  auto data = WMT::MakeDispatchData(serialized.data(), serialized.size());
  if (data)
    writer->set(key, data);
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
  const dxmt_msc_input_layout *input_layout, uint32_t compile_flags,
  void *metallib, size_t metallib_capacity, char *entry_point, size_t entry_point_capacity, size_t *metallib_size,
  size_t *entry_point_size, std::array<uint32_t, 3> *threadgroup_size, void *stage_in_metallib,
  size_t stage_in_metallib_capacity, size_t *stage_in_metallib_size, dxmt_msc_shader_reflection *reflection,
  char *error_message, size_t error_message_capacity
) {
  dxmt_msc_compile_dxil_params params = {};
  params.dxil = shader.pShaderBytecode;
  params.dxil_size = shader.BytecodeLength;
  params.stage = stage;
  params.reserved = compile_flags;
  if (input_layout)
    params.input_layout = *input_layout;
  params.root_signature = root_signature;
  params.root_signature_size = root_signature_size;
  params.metallib = metallib;
  params.metallib_capacity = metallib_capacity;
  params.stage_in_metallib = stage_in_metallib;
  params.stage_in_metallib_capacity = stage_in_metallib_capacity;
  params.entry_point_out = entry_point;
  params.entry_point_capacity = entry_point_capacity;
  params.error_message = error_message;
  params.error_message_capacity = error_message_capacity;

  int result = DXMTMSCCompileDXIL(&params);
  if (metallib_size)
    *metallib_size = params.metallib_size;
  if (entry_point_size)
    *entry_point_size = params.entry_point_size;
  if (stage_in_metallib_size)
    *stage_in_metallib_size = params.stage_in_metallib_size;
  if (threadgroup_size) {
    (*threadgroup_size)[0] = params.threadgroup_size[0];
    (*threadgroup_size)[1] = params.threadgroup_size[1];
    (*threadgroup_size)[2] = params.threadgroup_size[2];
  }
  if (reflection)
    *reflection = params.reflection;
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
    size_t root_signature_size, const dxmt_msc_input_layout *input_layout, uint32_t compile_flags
) {
  if (DetectD3D12ShaderBackend(shader) != D3D12ShaderBackend::MetalShaderConverter)
    return E_INVALIDARG;

  if (DXMTMSCIsAvailable() != 1) {
    ERR("DXIL detected but Metal Shader Converter is unavailable");
    return E_FAIL;
  }

  compile_flags |= input_layout ? DXMT_MSC_COMPILE_FLAG_SYNTHESIZE_STAGE_IN : 0;
  auto cache_key = MakeMSCConversionCacheKey(
      shader, stage, root_signature, root_signature_size, input_layout, compile_flags
  );
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
  if (LoadPersistentMSCConversion(cache_key, converted)) {
    std::unique_lock<std::shared_mutex> lock(cache.mutex);
    cache.entries.emplace(cache_key, converted);
    DEBUG("MSC shader persistent cache hit");
    return S_OK;
  }

  char error_message[1024] = {};
  size_t metallib_size = 0;
  size_t stage_in_metallib_size = 0;
  size_t entry_point_size = 0;
  std::array<uint32_t, 3> threadgroup_size = {};
  dxmt_msc_shader_reflection reflection = {};

  int result = CompileDXIL(
      shader, stage, root_signature, root_signature_size, input_layout, compile_flags, nullptr, 0, nullptr, 0,
      &metallib_size, &entry_point_size, &threadgroup_size, nullptr, 0, &stage_in_metallib_size, &reflection,
      error_message, sizeof(error_message)
  );
  if (result != DXMT_MSC_SUCCESS)
    return E_FAIL;
  if (!metallib_size || !entry_point_size)
    return E_FAIL;

  converted.metallib.resize(metallib_size);
  converted.stage_in_metallib.resize(stage_in_metallib_size);
  std::vector<char> entry_point(entry_point_size);
  error_message[0] = '\0';

  result = CompileDXIL(
      shader, stage, root_signature, root_signature_size, input_layout, compile_flags, converted.metallib.data(),
      converted.metallib.size(), entry_point.data(), entry_point.size(), &metallib_size, &entry_point_size,
      &threadgroup_size, converted.stage_in_metallib.data(), converted.stage_in_metallib.size(),
      &stage_in_metallib_size, &reflection, error_message, sizeof(error_message)
  );
  if (result != DXMT_MSC_SUCCESS)
    return E_FAIL;

  if (entry_point_size == 0 || entry_point[entry_point_size - 1] != '\0') {
    ERR("DXIL conversion returned an invalid entry point");
    return E_FAIL;
  }

  converted.entry_point.assign(entry_point.data(), entry_point_size - 1);
  converted.threadgroup_size = threadgroup_size;
  converted.reflection = reflection;
  converted.backend = D3D12ShaderBackend::MetalShaderConverter;

  {
    std::unique_lock<std::shared_mutex> lock(cache.mutex);
    cache.entries.emplace(cache_key, converted);
  }
  StorePersistentMSCConversion(cache_key, converted);
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

#include "d3d12_shader_converter.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <strings.h>
#include <unordered_map>

#include "DXBCParser/BlobContainer.h"
#include "DXBCParser/DXBCUtils.h"
#include "d3d12_msc_capabilities.hpp"
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
constexpr uint32_t kMSCConversionCacheVersion = 8;
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
    const dxmt_msc_input_layout *input_layout, uint32_t compile_flags,
    const DXMTMSCCapabilities *msc_capabilities
) {
  Sha1HashState hash;
  hash.update(kMSCConversionCacheNamespace, sizeof(kMSCConversionCacheNamespace) - 1);
  hash.update(kMSCEntryPointPolicy, sizeof(kMSCEntryPointPolicy) - 1);
  hash.update(kMSCConversionCacheVersion);
  hash.update(kMSCConverterAPIVersion);
  hash.update(kMSCMetalTargetVersion);
  hash.update(kMSCCompileFlags);
  hash.update(kMSCBindingLayoutVersion);
  const uint32_t has_capability_snapshot = msc_capabilities ? 1u : 0u;
  hash.update(has_capability_snapshot);
  uint32_t compiler_minimum_gpu_family = 0;
  uint32_t compiler_minimum_os_major = 0;
  uint32_t compiler_minimum_os_minor = 0;
  uint32_t compiler_minimum_os_patch = 0;
  uint32_t compiler_compatibility_flags = DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP;
  uint32_t compiler_validation_flags = 0;
  uint8_t compiler_ignore_debug_information = 0;
  if (msc_capabilities) {
    /* The same DXIL can produce a different metallib when the runtime ABI,
     * optional symbol set, OS, or Metal GPU target changes. Keep those
     * dimensions in the persistent key even when the current compiler path
     * does not yet promote every reported capability. */
    hash.update(msc_capabilities->ir_version_major);
    hash.update(msc_capabilities->ir_version_minor);
    hash.update(msc_capabilities->ir_version_patch);
    hash.update(msc_capabilities->runtime_symbols);
    hash.update(msc_capabilities->os_major);
    hash.update(msc_capabilities->os_minor);
    hash.update(msc_capabilities->os_patch);
    hash.update(msc_capabilities->highest_apple_gpu_family);
    hash.update(static_cast<uint8_t>(msc_capabilities->core_converter));
    hash.update(static_cast<uint8_t>(msc_capabilities->argument_buffers_tier2));
    compiler_minimum_gpu_family = msc_capabilities->compiler_minimum_gpu_family;
    compiler_minimum_os_major = msc_capabilities->compiler_minimum_os_major;
    compiler_minimum_os_minor = msc_capabilities->compiler_minimum_os_minor;
    compiler_minimum_os_patch = msc_capabilities->compiler_minimum_os_patch;
    compiler_compatibility_flags = msc_capabilities->compiler_compatibility_flags;
    compiler_validation_flags = msc_capabilities->compiler_validation_flags;
    compiler_ignore_debug_information = msc_capabilities->compiler_ignore_debug_information;
  }
  hash.update(compiler_minimum_gpu_family);
  hash.update(compiler_minimum_os_major);
  hash.update(compiler_minimum_os_minor);
  hash.update(compiler_minimum_os_patch);
  hash.update(compiler_compatibility_flags);
  hash.update(compiler_validation_flags);
  hash.update(compiler_ignore_debug_information);
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
HasOutputSemantic(const D3D12_SHADER_BYTECODE &shader, const char *semantic_name) {
  if (!shader.pShaderBytecode || !shader.BytecodeLength || !semantic_name)
    return false;

  microsoft::CDXBCParser container;
  if (FAILED(container.ReadDXBC(shader.pShaderBytecode, static_cast<uint32_t>(shader.BytecodeLength)))) {
    return false;
  }

  microsoft::CSignatureParser signature_parser;
  if (SUCCEEDED(microsoft::DXBCGetOutputSignature(shader.pShaderBytecode, &signature_parser))) {
    const microsoft::D3D11_SIGNATURE_PARAMETER *parameters = nullptr;
    const UINT parameter_count = signature_parser.GetParameters(&parameters);
    for (UINT i = 0; i < parameter_count; i++) {
      if (parameters[i].SemanticName && strcasecmp(parameters[i].SemanticName, semantic_name) == 0)
        return true;
    }
  }

  const size_t semantic_length = std::strlen(semantic_name);
  for (uint32_t i = 0; i < container.GetBlobCount(); i++) {
    const auto fourcc = container.GetBlobFourCC(i);
    if (fourcc != microsoft::DXBC_OutputSignature &&
        fourcc != microsoft::DXBC_OutputSignature11_1 &&
        fourcc != microsoft::DXBC_OutputSignature5)
      continue;

    const auto *data = static_cast<const uint8_t *>(container.GetBlob(i));
    const size_t data_size = container.GetBlobSize(i);
    if (!data || semantic_length == 0 || data_size <= semantic_length)
      continue;
    for (size_t offset = 0; offset + semantic_length < data_size; offset++) {
      if (data[offset + semantic_length] != '\0')
        continue;
      bool matches = true;
      for (size_t j = 0; j < semantic_length; j++) {
        if (std::tolower(static_cast<unsigned char>(data[offset + j])) !=
            std::tolower(static_cast<unsigned char>(semantic_name[j]))) {
          matches = false;
          break;
        }
      }
      if (matches)
        return true;
    }
  }
  return false;
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
  char *error_message, size_t error_message_capacity, const DXMTMSCCapabilities *msc_capabilities
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
  params.compatibility_flags = DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP;
  if (msc_capabilities) {
    params.minimum_gpu_family = msc_capabilities->compiler_minimum_gpu_family;
    params.minimum_os_major = msc_capabilities->compiler_minimum_os_major;
    params.minimum_os_minor = msc_capabilities->compiler_minimum_os_minor;
    params.minimum_os_patch = msc_capabilities->compiler_minimum_os_patch;
    params.compatibility_flags = msc_capabilities->compiler_compatibility_flags;
    params.validation_flags = msc_capabilities->compiler_validation_flags;
    params.ignore_debug_information = msc_capabilities->compiler_ignore_debug_information;
  }

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

D3D12ShaderClassification
ClassifyD3D12Shader(const D3D12_SHADER_BYTECODE &shader) {
  D3D12ShaderClassification classification;
  if (shader.BytecodeLength > std::numeric_limits<uint32_t>::max()) {
    classification.validation_hr = E_INVALIDARG;
    return classification;
  }

  microsoft::CDXBCParser parser;
  classification.validation_hr =
      parser.ReadDXBC(shader.pShaderBytecode, static_cast<uint32_t>(shader.BytecodeLength));
  if (FAILED(classification.validation_hr))
    return classification;

  classification.backend = D3D12ShaderBackend::Airconv;
  const UINT root_signature_blob = parser.FindNextMatchingBlob(microsoft::DXBC_RootSignature, 0);
  if (root_signature_blob != DXBC_BLOB_NOT_FOUND) {
    classification.embedded_root_signature = parser.GetBlob(root_signature_blob);
    classification.embedded_root_signature_size = parser.GetBlobSize(root_signature_blob);
  }
  for (uint32_t i = 0; i < parser.GetBlobCount(); i++) {
    if (parser.GetBlobFourCC(i) == kDXILFourCC) {
      classification.backend = D3D12ShaderBackend::MetalShaderConverter;
      break;
    }
  }
  if (classification.backend == D3D12ShaderBackend::MetalShaderConverter) {
    classification.uses_unsupported_stencil_ref = HasOutputSemantic(shader, "SV_StencilRef");
  }
  return classification;
}

D3D12ShaderBackend
DetectD3D12ShaderBackend(const D3D12_SHADER_BYTECODE &shader) {
  return ClassifyD3D12Shader(shader).backend;
}

D3D12AirconvError::~D3D12AirconvError() {
  reset();
}

sm50_error_t *
D3D12AirconvError::out() {
  reset();
  return &handle_;
}

bool
D3D12AirconvError::has_value() const {
  return !!handle_;
}

std::string
D3D12AirconvError::message() const {
  return has_value() ? SM50GetErrorMessageString(handle_) : std::string();
}

void
D3D12AirconvError::reset() {
  if (has_value()) {
    SM50FreeError(handle_);
    handle_ = {};
  }
}

D3D12AirconvShader::~D3D12AirconvShader() {
  reset();
}

HRESULT
D3D12AirconvShader::Initialize(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    MTL_SHADER_REFLECTION *reflection, const char *stage_name
) {
  return InitializeD3D12AirconvShader(shader, classification, *this, reflection, stage_name);
}

sm50_shader_t *
D3D12AirconvShader::out() {
  reset();
  return &handle_;
}

sm50_shader_t
D3D12AirconvShader::get() const {
  return handle_;
}

void
D3D12AirconvShader::reset() {
  if (handle_) {
    SM50Destroy(handle_);
    handle_ = {};
  }
}

D3D12AirconvBitcode::~D3D12AirconvBitcode() {
  reset();
}

sm50_bitcode_t *
D3D12AirconvBitcode::out() {
  reset();
  return &handle_;
}

sm50_bitcode_t
D3D12AirconvBitcode::get() const {
  return handle_;
}

void
D3D12AirconvBitcode::reset() {
  if (handle_) {
    SM50DestroyBitcode(handle_);
    handle_ = {};
  }
}

HRESULT
GetD3D12EmbeddedRootSignature(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    const void **root_signature, size_t *root_signature_size
) {
  if (!root_signature || !root_signature_size)
    return E_POINTER;
  *root_signature = nullptr;
  *root_signature_size = 0;
  if (FAILED(classification.validation_hr))
    return classification.validation_hr;
  if (!classification.embedded_root_signature || !classification.embedded_root_signature_size)
    return E_FAIL;
  *root_signature = classification.embedded_root_signature;
  *root_signature_size = classification.embedded_root_signature_size;
  return S_OK;
}

HRESULT
InitializeD3D12AirconvRootSignature(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    const void *explicit_root_signature, size_t explicit_root_signature_size,
    SM50_SHADER_ROOT_SIGNATURE_DATA &root_signature
) {
  root_signature = {};
  root_signature.type = SM50_SHADER_ROOT_SIGNATURE;
  if (classification.backend != D3D12ShaderBackend::Airconv)
    return E_INVALIDARG;
  if (explicit_root_signature || explicit_root_signature_size) {
    if (!explicit_root_signature || !explicit_root_signature_size)
      return E_INVALIDARG;
    root_signature.bytecode = explicit_root_signature;
    root_signature.bytecode_length = explicit_root_signature_size;
    return S_OK;
  }

  const void *embedded_root_signature = nullptr;
  size_t embedded_root_signature_size = 0;
  HRESULT hr = GetD3D12EmbeddedRootSignature(
      shader, classification, &embedded_root_signature, &embedded_root_signature_size
  );
  if (FAILED(hr))
    return hr;

  // AIRCONV's root-signature parser accepts the complete DXBC container and
  // extracts its embedded RTS0 blob.  Keep the validated container alive for
  // the duration of compilation rather than passing only the raw blob.
  root_signature.bytecode = shader.pShaderBytecode;
  root_signature.bytecode_length = shader.BytecodeLength;
  return S_OK;
}

HRESULT
InitializeD3D12AirconvShader(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    D3D12AirconvShader &airconv_shader, MTL_SHADER_REFLECTION *reflection, const char *stage_name
) {
  if (FAILED(classification.validation_hr))
    return classification.validation_hr;
  if (classification.backend != D3D12ShaderBackend::Airconv)
    return E_INVALIDARG;
  if (!stage_name)
    return E_INVALIDARG;

  D3D12AirconvError error;
  int result = SM50Initialize(
      shader.pShaderBytecode, shader.BytecodeLength, airconv_shader.out(), reflection, error.out()
  );
  if (!result)
    return S_OK;

  const auto message = error.message();
  ERR(
      "Failed to initialize AIRCONV ", stage_name, " shader: ",
      message.empty() ? "unknown error" : message
  );
  airconv_shader.reset();
  return E_FAIL;
}

HRESULT
ConvertD3D12Shader(
    const D3D12ShaderClassification &classification, const D3D12_SHADER_BYTECODE &shader, uint32_t stage,
    D3D12ConvertedShader &converted, const void *root_signature, size_t root_signature_size,
    const dxmt_msc_input_layout *input_layout, uint32_t compile_flags,
    const DXMTMSCCapabilities *msc_capabilities
) {
  if (FAILED(classification.validation_hr))
    return classification.validation_hr;
  if (classification.backend != D3D12ShaderBackend::MetalShaderConverter)
    return E_INVALIDARG;
  if (stage == DXMT_MSC_STAGE_FRAGMENT && classification.uses_unsupported_stencil_ref) {
    ERR("DXIL pixel shader uses unsupported SV_StencilRef output");
    return E_NOTIMPL;
  }

  if (msc_capabilities ? !msc_capabilities->core_converter : DXMTMSCIsAvailable() != 1) {
    ERR("DXIL detected but Metal Shader Converter is unavailable");
    return E_FAIL;
  }

  compile_flags |= input_layout ? DXMT_MSC_COMPILE_FLAG_SYNTHESIZE_STAGE_IN : 0;
  auto cache_key = MakeMSCConversionCacheKey(
      shader, stage, root_signature, root_signature_size, input_layout, compile_flags, msc_capabilities
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
      error_message, sizeof(error_message), msc_capabilities
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
      &stage_in_metallib_size, &reflection, error_message, sizeof(error_message), msc_capabilities
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
ConvertD3D12Shader(
    const D3D12_SHADER_BYTECODE &shader, uint32_t stage, D3D12ConvertedShader &converted, const void *root_signature,
    size_t root_signature_size, const dxmt_msc_input_layout *input_layout, uint32_t compile_flags,
    const DXMTMSCCapabilities *msc_capabilities
) {
  auto classification = ClassifyD3D12Shader(shader);
  return ConvertD3D12Shader(
      classification, shader, stage, converted, root_signature, root_signature_size, input_layout, compile_flags,
      msc_capabilities
  );
}

HRESULT
ConvertD3D12ComputeShader(
    const D3D12ShaderClassification &classification, const D3D12_SHADER_BYTECODE &shader,
    D3D12ConvertedShader &converted, const void *root_signature, size_t root_signature_size,
    const DXMTMSCCapabilities *msc_capabilities
) {
  return ConvertD3D12Shader(
      classification, shader, DXMT_MSC_STAGE_COMPUTE, converted, root_signature, root_signature_size, nullptr, 0,
      msc_capabilities
  );
}

HRESULT
ConvertD3D12ComputeShader(
    const D3D12_SHADER_BYTECODE &shader, D3D12ConvertedShader &converted, const void *root_signature,
    size_t root_signature_size, const DXMTMSCCapabilities *msc_capabilities
) {
  auto classification = ClassifyD3D12Shader(shader);
  return ConvertD3D12ComputeShader(
      classification, shader, converted, root_signature, root_signature_size, msc_capabilities
  );
}

} // namespace dxmt

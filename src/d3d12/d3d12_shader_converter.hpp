#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "d3d12.h"
#include "metalirconverter_thunks.h"
#include "airconv_public.h"

namespace dxmt {

struct DXMTMSCCapabilities;

enum class D3D12ShaderBackend {
  None,
  Airconv,
  MetalShaderConverter,
  Unsupported,
};

enum class D3D12ShaderExecutableFamily {
  None,
  LegacyTokenized,
  DXIL,
  Ambiguous,
  Unsupported,
};

enum class D3D12ShaderKind {
  Unknown,
  Pixel,
  Vertex,
  Geometry,
  Hull,
  Domain,
  Compute,
  Library,
  RayGeneration,
  Intersection,
  AnyHit,
  ClosestHit,
  Miss,
  Callable,
  Mesh,
  Amplification,
  Node,
};

struct D3D12ConvertedShader {
  std::vector<uint8_t> metallib;
  std::vector<uint8_t> stage_in_metallib;
  std::string entry_point;
  std::array<uint32_t, 3> threadgroup_size = {};
  dxmt_msc_shader_reflection reflection = {};
  D3D12ShaderBackend backend = D3D12ShaderBackend::None;
};

D3D12ShaderBackend
DetectD3D12ShaderBackend(const D3D12_SHADER_BYTECODE &shader);

struct D3D12ShaderClassification {
  D3D12ShaderBackend backend = D3D12ShaderBackend::None;
  D3D12ShaderExecutableFamily executable_family = D3D12ShaderExecutableFamily::Unsupported;
  D3D12ShaderKind shader_kind = D3D12ShaderKind::Unknown;
  HRESULT validation_hr = E_INVALIDARG;
  bool has_legacy_shdr = false;
  bool has_legacy_shex = false;
  bool has_dxil = false;
  bool uses_unsupported_view_id = false;
  bool uses_unsupported_attribute_at_vertex = false;
  bool uses_unsupported_stencil_ref = false;
  bool uses_unsupported_shading_rate = false;
  bool uses_unsupported_denorm_mode = false;
  bool uses_unsupported_pack_unpack = false;
  bool uses_unsupported_append_consume = false;
  bool uses_unsupported_sampler_feedback = false;
  bool uses_unsupported_ray_payload_qualifiers = false;
  bool uses_unsupported_compute_derivative_shape = false;
  bool uses_unsupported_wave_size = false;
  bool uses_texture_load = false;
  uint64_t atomic64_feature_flags = 0;
  bool is_library_shader = false;
  const void *embedded_root_signature = nullptr;
  size_t embedded_root_signature_size = 0;
};

inline D3D12ShaderKind
DecodeD3D12ShaderKind(uint32_t program_version) {
  switch (program_version >> 16) {
  case 0:
    return D3D12ShaderKind::Pixel;
  case 1:
    return D3D12ShaderKind::Vertex;
  case 2:
    return D3D12ShaderKind::Geometry;
  case 3:
    return D3D12ShaderKind::Hull;
  case 4:
    return D3D12ShaderKind::Domain;
  case 5:
    return D3D12ShaderKind::Compute;
  case 6:
    return D3D12ShaderKind::Library;
  case 7:
    return D3D12ShaderKind::RayGeneration;
  case 8:
    return D3D12ShaderKind::Intersection;
  case 9:
    return D3D12ShaderKind::AnyHit;
  case 10:
    return D3D12ShaderKind::ClosestHit;
  case 11:
    return D3D12ShaderKind::Miss;
  case 12:
    return D3D12ShaderKind::Callable;
  case 13:
    return D3D12ShaderKind::Mesh;
  case 14:
    return D3D12ShaderKind::Amplification;
  case 15:
    return D3D12ShaderKind::Node;
  default:
    return D3D12ShaderKind::Unknown;
  }
}

inline HRESULT
ClassifyD3D12ShaderProgramVersion(D3D12ShaderClassification &classification, uint32_t program_version) {
  if (FAILED(classification.validation_hr))
    return classification.validation_hr;

  classification.shader_kind = DecodeD3D12ShaderKind(program_version);
  if (classification.shader_kind == D3D12ShaderKind::Unknown) {
    classification.backend = D3D12ShaderBackend::Unsupported;
    classification.validation_hr = E_INVALIDARG;
    return classification.validation_hr;
  }

  if (classification.executable_family == D3D12ShaderExecutableFamily::DXIL) {
    classification.backend = D3D12ShaderBackend::MetalShaderConverter;
    classification.is_library_shader = classification.shader_kind == D3D12ShaderKind::Library;
  } else if (classification.executable_family == D3D12ShaderExecutableFamily::LegacyTokenized) {
    classification.backend = D3D12ShaderBackend::Airconv;
    classification.is_library_shader = false;
  } else {
    classification.backend = D3D12ShaderBackend::Unsupported;
    classification.validation_hr = E_INVALIDARG;
    return classification.validation_hr;
  }

  return S_OK;
}

inline HRESULT
ValidateD3D12ShaderKind(const D3D12ShaderClassification &classification, D3D12ShaderKind expected_kind) {
  if (FAILED(classification.validation_hr))
    return classification.validation_hr;

  const bool valid_backend_family =
      (classification.backend == D3D12ShaderBackend::Airconv &&
       classification.executable_family == D3D12ShaderExecutableFamily::LegacyTokenized) ||
      (classification.backend == D3D12ShaderBackend::MetalShaderConverter &&
       classification.executable_family == D3D12ShaderExecutableFamily::DXIL);
  if (!valid_backend_family || expected_kind == D3D12ShaderKind::Unknown ||
      classification.shader_kind == D3D12ShaderKind::Unknown || classification.shader_kind != expected_kind)
    return E_INVALIDARG;
  return S_OK;
}

inline D3D12ShaderKind
D3D12ShaderKindForMSCStage(uint32_t stage) {
  switch (stage) {
  case DXMT_MSC_STAGE_VERTEX:
    return D3D12ShaderKind::Vertex;
  case DXMT_MSC_STAGE_FRAGMENT:
    return D3D12ShaderKind::Pixel;
  case DXMT_MSC_STAGE_COMPUTE:
    return D3D12ShaderKind::Compute;
  case DXMT_MSC_STAGE_HULL:
    return D3D12ShaderKind::Hull;
  case DXMT_MSC_STAGE_DOMAIN:
    return D3D12ShaderKind::Domain;
  case DXMT_MSC_STAGE_GEOMETRY:
    return D3D12ShaderKind::Geometry;
  case DXMT_MSC_STAGE_MESH:
    return D3D12ShaderKind::Mesh;
  case DXMT_MSC_STAGE_AMPLIFICATION:
    return D3D12ShaderKind::Amplification;
  default:
    return D3D12ShaderKind::Unknown;
  }
}

inline HRESULT
ValidateD3D12MSCShaderConversion(
    const D3D12ShaderClassification &classification, uint32_t stage, bool allow_library_shader
) {
  if (FAILED(classification.validation_hr))
    return classification.validation_hr;
  if (classification.backend != D3D12ShaderBackend::MetalShaderConverter ||
      classification.executable_family != D3D12ShaderExecutableFamily::DXIL)
    return E_INVALIDARG;

  if (allow_library_shader)
    return classification.shader_kind == D3D12ShaderKind::Library && classification.is_library_shader
               ? S_OK
               : E_INVALIDARG;

  if (classification.shader_kind == D3D12ShaderKind::Library || classification.is_library_shader)
    return E_INVALIDARG;
  const auto expected_kind = D3D12ShaderKindForMSCStage(stage);
  if (expected_kind == D3D12ShaderKind::Unknown)
    return E_INVALIDARG;
  return ValidateD3D12ShaderKind(classification, expected_kind);
}

inline D3D12ShaderClassification
AbsentD3D12ShaderClassification() {
  D3D12ShaderClassification classification;
  classification.backend = D3D12ShaderBackend::None;
  classification.executable_family = D3D12ShaderExecutableFamily::None;
  classification.validation_hr = S_OK;
  return classification;
}

D3D12ShaderClassification
ClassifyD3D12Shader(const D3D12_SHADER_BYTECODE &shader);

class D3D12AirconvError {
public:
  D3D12AirconvError() = default;
  D3D12AirconvError(const D3D12AirconvError &) = delete;
  D3D12AirconvError &operator=(const D3D12AirconvError &) = delete;
  ~D3D12AirconvError();

  sm50_error_t *out();
  bool has_value() const;
  std::string message() const;
  void reset();

private:
  sm50_error_t handle_ = {};
};

class D3D12AirconvShader {
public:
  D3D12AirconvShader() = default;
  D3D12AirconvShader(const D3D12AirconvShader &) = delete;
  D3D12AirconvShader &operator=(const D3D12AirconvShader &) = delete;
  ~D3D12AirconvShader();

  HRESULT Initialize(
      const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
      D3D12ShaderKind expected_kind, MTL_SHADER_REFLECTION *reflection, const char *stage_name
  );
  sm50_shader_t *out();
  sm50_shader_t get() const;
  void reset();

private:
  sm50_shader_t handle_ = {};
};

class D3D12AirconvBitcode {
public:
  D3D12AirconvBitcode() = default;
  D3D12AirconvBitcode(const D3D12AirconvBitcode &) = delete;
  D3D12AirconvBitcode &operator=(const D3D12AirconvBitcode &) = delete;
  ~D3D12AirconvBitcode();

  sm50_bitcode_t *out();
  sm50_bitcode_t get() const;
  void reset();

private:
  sm50_bitcode_t handle_ = {};
};

HRESULT
InitializeD3D12AirconvRootSignature(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    const void *explicit_root_signature, size_t explicit_root_signature_size,
    SM50_SHADER_ROOT_SIGNATURE_DATA &root_signature
);

HRESULT
GetD3D12EmbeddedRootSignature(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    const void **root_signature, size_t *root_signature_size
);

HRESULT
InitializeD3D12AirconvShader(
    const D3D12_SHADER_BYTECODE &shader, const D3D12ShaderClassification &classification,
    D3D12AirconvShader &airconv_shader, D3D12ShaderKind expected_kind, MTL_SHADER_REFLECTION *reflection,
    const char *stage_name
);

HRESULT
ConvertD3D12Shader(
    const D3D12ShaderClassification &classification, const D3D12_SHADER_BYTECODE &shader, uint32_t stage,
    D3D12ConvertedShader &converted, const void *root_signature = nullptr, size_t root_signature_size = 0,
    const dxmt_msc_input_layout *input_layout = nullptr, uint32_t compile_flags = 0,
    const DXMTMSCCapabilities *msc_capabilities = nullptr
);

HRESULT
ConvertD3D12LibraryShader(
    const D3D12ShaderClassification &classification, const D3D12_SHADER_BYTECODE &shader, uint32_t stage,
    const char *entry_point, D3D12ConvertedShader &converted, const void *root_signature = nullptr,
    size_t root_signature_size = 0, const void *local_root_signature = nullptr,
    size_t local_root_signature_size = 0, const DXMTMSCCapabilities *msc_capabilities = nullptr
);

HRESULT
ConvertD3D12Shader(
    const D3D12_SHADER_BYTECODE &shader, uint32_t stage, D3D12ConvertedShader &converted,
    const void *root_signature = nullptr, size_t root_signature_size = 0,
    const dxmt_msc_input_layout *input_layout = nullptr, uint32_t compile_flags = 0,
    const DXMTMSCCapabilities *msc_capabilities = nullptr
);

HRESULT
ConvertD3D12ComputeShader(
    const D3D12ShaderClassification &classification, const D3D12_SHADER_BYTECODE &shader,
    D3D12ConvertedShader &converted, const void *root_signature = nullptr, size_t root_signature_size = 0,
    const DXMTMSCCapabilities *msc_capabilities = nullptr
);

HRESULT
ConvertD3D12ComputeShader(
    const D3D12_SHADER_BYTECODE &shader, D3D12ConvertedShader &converted, const void *root_signature = nullptr,
    size_t root_signature_size = 0, const DXMTMSCCapabilities *msc_capabilities = nullptr
);

} // namespace dxmt

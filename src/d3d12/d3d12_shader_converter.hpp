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
  Airconv,
  MetalShaderConverter,
  Unsupported,
};

struct D3D12ConvertedShader {
  std::vector<uint8_t> metallib;
  std::vector<uint8_t> stage_in_metallib;
  std::string entry_point;
  std::array<uint32_t, 3> threadgroup_size = {};
  dxmt_msc_shader_reflection reflection = {};
  D3D12ShaderBackend backend = D3D12ShaderBackend::Airconv;
};

D3D12ShaderBackend
DetectD3D12ShaderBackend(const D3D12_SHADER_BYTECODE &shader);

struct D3D12ShaderClassification {
  D3D12ShaderBackend backend = D3D12ShaderBackend::Unsupported;
  HRESULT validation_hr = E_INVALIDARG;
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
  uint64_t atomic64_feature_flags = 0;
  bool is_library_shader = false;
  const void *embedded_root_signature = nullptr;
  size_t embedded_root_signature_size = 0;
};

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
      MTL_SHADER_REFLECTION *reflection, const char *stage_name
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
    D3D12AirconvShader &airconv_shader, MTL_SHADER_REFLECTION *reflection, const char *stage_name
);

HRESULT
ConvertD3D12Shader(
    const D3D12ShaderClassification &classification, const D3D12_SHADER_BYTECODE &shader, uint32_t stage,
    D3D12ConvertedShader &converted, const void *root_signature = nullptr, size_t root_signature_size = 0,
    const dxmt_msc_input_layout *input_layout = nullptr, uint32_t compile_flags = 0,
    const DXMTMSCCapabilities *msc_capabilities = nullptr
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

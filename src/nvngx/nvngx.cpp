#include "com/com_pointer.hpp"
#include "d3d11.h"
#include "d3d12.h"
#include "nvngx.hpp"
#include "log/log.hpp"
#include "nvngx_feature.hpp"
#include "nvngx_parameter.hpp"
#include "../d3d11/d3d11_interfaces.hpp"
#include "../d3d12/d3d12_interfaces.hpp"
#include "../dxgi/dxgi_interfaces.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace dxmt {
Logger Logger::s_instance("nvngx.log");

static bool
ngx_debug_enabled() {
  static const bool enabled = [] {
    const char *value = std::getenv("DXMT_NGX_DEBUG");
    return value && value[0] == '1';
  }();
  return enabled;
}

#define NGX_DEBUG(...)                                                                                                  \
  do {                                                                                                                 \
    if (ngx_debug_enabled())                                                                                            \
      DEBUG(__VA_ARGS__);                                                                                              \
  } while (0)

template <typename Resource>
struct TemporalParameters {
  Resource *color = nullptr;
  Resource *output = nullptr;
  Resource *depth = nullptr;
  Resource *motion_vector = nullptr;
  Resource *exposure = nullptr;
  uint32_t input_content_width = 0;
  uint32_t input_content_height = 0;
  int reset = 0;
  float motion_vector_scale_x = 1.0f;
  float motion_vector_scale_y = 1.0f;
  float jitter_offset_x = 0.0f;
  float jitter_offset_y = 0.0f;
  float pre_exposure = 0.0f;
};

static NVNGX_RESULT NVNGX_DLSS_GetOptimalSettingsCallback(NVNGXParameter *params);
static NVNGX_RESULT NVNGX_DLSS_GetStatsCallback(NVNGXParameter *params);

template <typename Resource>
NVNGX_RESULT
get_temporal_parameters(
    const ParametersImpl *parameters, const DLSSFeature &feature, TemporalParameters<Resource> &out
) {
  if (!parameters)
    return NVNGX_RESULT_INVALID_PARAMETER;

  if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Color, &out.color)) ||
      NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Output, &out.output)) ||
      NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Depth, &out.depth)) ||
      NVNGX_FAILED(parameters->Get(NVNGX_Parameter_MotionVectors, &out.motion_vector)))
    return NVNGX_RESULT_INVALID_PARAMETER;

  parameters->Get(NVNGX_Parameter_ExposureTexture, &out.exposure);

  if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &out.input_content_width)) &&
      NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Width, &out.input_content_width)))
    return NVNGX_RESULT_INVALID_PARAMETER;
  if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &out.input_content_height)) &&
      NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Height, &out.input_content_height)))
    return NVNGX_RESULT_INVALID_PARAMETER;

  out.reset = 0;
  parameters->Get(NVNGX_Parameter_Reset, &out.reset);
  parameters->Get(NVNGX_Parameter_MV_Scale_X, &out.motion_vector_scale_x);
  parameters->Get(NVNGX_Parameter_MV_Scale_Y, &out.motion_vector_scale_y);
  parameters->Get(NVNGX_Parameter_Jitter_Offset_X, &out.jitter_offset_x);
  parameters->Get(NVNGX_Parameter_Jitter_Offset_Y, &out.jitter_offset_y);
  parameters->Get(NVNGX_Parameter_DLSS_Pre_Exposure, &out.pre_exposure);
  return NVNGX_RESULT_OK;
}

static NVNGX_RESULT
create_dlss_feature(
    NVNGX_FEATURE feature, ParametersImpl *parameters, NVSDK_NGX_Handle **out_handle
) {
  if (!parameters || !out_handle)
    return NVNGX_RESULT_INVALID_PARAMETER;
  *out_handle = nullptr;
  if (feature != NVNGX_FEATURE_SUPERSAMPLING)
    return NVNGX_RESULT_FAIL;

  auto dlss = std::make_unique<DLSSFeature>();
  dlss->feature = feature;
  if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Width, &dlss->width)) ||
      NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Height, &dlss->height)) ||
      NVNGX_FAILED(parameters->Get(NVNGX_Parameter_OutWidth, &dlss->target_width)) ||
      NVNGX_FAILED(parameters->Get(NVNGX_Parameter_OutHeight, &dlss->target_height)))
    return NVNGX_RESULT_INVALID_PARAMETER;
  if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_PerfQualityValue, &dlss->quality)))
    dlss->quality = 0;
  if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Feature_Create_Flags, &dlss->flag)))
    dlss->flag = 0;
  if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Enable_Output_Subrects, &dlss->enable_output_subrects)))
    dlss->enable_output_subrects = 0;

  NGX_DEBUG("CreateFeature: DLSS input=", dlss->width, "x", dlss->height, " output=", dlss->target_width, "x",
            dlss->target_height, " flags=0x", std::hex, dlss->flag, std::dec);
  *out_handle = &dlss.release()->handle;
  return NVNGX_RESULT_OK;
}

static void
populate_capability_parameters(ParametersImpl &parameters) {
  parameters.Set("SuperSampling.Available", 1u);
  parameters.Set(NVSDK_NGX_EParameter_SuperSampling_Available, 1u);
  parameters.Set("SuperSampling.MinDriverVersionMajor", 0);
  parameters.Set("SuperSampling.MinDriverVersionMinor", 0);
  parameters.Set("SuperSampling.NeedsUpdatedDriver", 0);
  parameters.Set("SuperSampling.FeatureInitResult", 1u);
  parameters.Set("Snippet.OptLevel", 0);
  parameters.Set("Snippet.IsDevBranch", 0);
  parameters.Set(NVNGX_Parameter_DLSSOptimalSettingsCallback, (void *)&NVNGX_DLSS_GetOptimalSettingsCallback);
  parameters.Set("DLSSGetStatsCallback", (void *)&NVNGX_DLSS_GetStatsCallback);
  parameters.Set(NVNGX_Parameter_Sharpness, 0.0f);
  parameters.Set(NVNGX_Parameter_MV_Scale_X, 1.0f);
  parameters.Set(NVNGX_Parameter_MV_Scale_Y, 1.0f);
  parameters.Set("MV.Offset.X", 0.0f);
  parameters.Set("MV.Offset.Y", 0.0f);
  parameters.Set("DLSS.Exposure.Scale", 1.0f);
  parameters.Set(NVNGX_Parameter_PerfQualityValue, 2);
  parameters.Set("CreationNodeMask", 1);
  parameters.Set("VisibilityNodeMask", 1);
  parameters.Set(NVNGX_Parameter_DLSS_Enable_Output_Subrects, 1);
  parameters.Set("RTXValue", 0);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init_Ext(
    unsigned long long id, const wchar_t *path, ID3D11Device *device, const void *feature_info, unsigned int sdk_version
) {
  NGX_DEBUG("D3D11_Init_Ext: app=", id, " device=", device, " sdk=", sdk_version);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init(
    unsigned long long id, const wchar_t *path, ID3D11Device *device, const void *feature_info, unsigned int sdk_version
) {
  NGX_DEBUG("D3D11_Init: app=", id, " device=", device, " sdk=", sdk_version);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Init(
    unsigned long long id, const wchar_t *path, ID3D12Device *device, const void *feature_info, unsigned int sdk_version
) {
  NGX_DEBUG("D3D12_Init: app=", id, " device=", device, " sdk=", sdk_version);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Init_Ext(
    unsigned long long id, const wchar_t *path, ID3D12Device *device, const void *feature_info, unsigned int sdk_version
) {
  NGX_DEBUG("D3D12_Init_Ext: app=", id, " device=", device, " sdk=", sdk_version);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init_ProjectID(
    const char *project, unsigned int engine_type, const char *engine_version, const wchar_t *path,
    ID3D11Device *device, unsigned int sdk_version, const void *feature_info
) {
  NGX_DEBUG("D3D11_Init_ProjectID: project=", project, " device=", device, " sdk=", sdk_version);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init_with_ProjectID(
    const char *project, unsigned int engine_type, const char *engine_version, const wchar_t *path,
    ID3D11Device *device, const void *feature_info, unsigned int sdk_version
) {
  NGX_DEBUG("D3D11_Init_with_ProjectID: project=", project, " device=", device, " sdk=", sdk_version);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Init_ProjectID(
    const char *project, unsigned int engine_type, const char *engine_version, const wchar_t *path,
    ID3D12Device *device, const void *feature_info, unsigned int sdk_version
) {
  NGX_DEBUG("D3D12_Init_ProjectID: project=", project, " device=", device, " sdk=", sdk_version);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Init_with_ProjectID(
    const char *project, unsigned int engine_type, const char *engine_version, const wchar_t *path,
    ID3D12Device *device, const void *feature_info, unsigned int sdk_version
) {
  return NVSDK_NGX_D3D12_Init_ProjectID(project, engine_type, engine_version, path, device, feature_info, sdk_version);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_CreateFeature(
    ID3D11DeviceContext *context, unsigned int feature, NVNGXParameter *params, NVSDK_NGX_Handle **out_handle
) {
  if (!context || !params || !out_handle)
    return NVNGX_RESULT_INVALID_PARAMETER;

  auto parameters = static_cast<ParametersImpl *>(params);
  Com<IMTLD3D11ContextExt1> pCtxExt = nullptr;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(&pCtxExt))))
    return NVNGX_RESULT_INVALID_PARAMETER;
  if (feature == NVNGX_FEATURE_SUPERSAMPLING) {
    BOOL feature_supported = false;
    if (FAILED(pCtxExt->CheckFeatureSupport(MTL_FEATURE_METALFX_TEMPORAL_SCALER, &feature_supported, sizeof(feature_supported))))
      return NVNGX_RESULT_FEATURE_NOT_SUPPORTED;
    if (!feature_supported)
      return NVNGX_RESULT_FEATURE_NOT_SUPPORTED;
    return create_dlss_feature(static_cast<NVNGX_FEATURE>(feature), parameters, out_handle);
  }

  ERR("NVSDK_NGX_D3D11_CreateFeature: feature ", feature, " not handled");
  return NVNGX_RESULT_FAIL;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_CreateFeature(
    ID3D12GraphicsCommandList *context, unsigned int feature, NVNGXParameter *params, NVSDK_NGX_Handle **out_handle
) {
  if (!context || !params || !out_handle)
    return NVNGX_RESULT_INVALID_PARAMETER;

  Com<IMTLD3D12CommandListExt> pCtxExt = nullptr;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(&pCtxExt))))
    return NVNGX_RESULT_INVALID_PARAMETER;

  if (feature == NVNGX_FEATURE_SUPERSAMPLING) {
    BOOL feature_supported = false;
    if (FAILED(pCtxExt->CheckFeatureSupport(
            MTL_D3D12_FEATURE_METALFX_TEMPORAL_SCALER, &feature_supported, sizeof(feature_supported))) ||
        !feature_supported)
      return NVNGX_RESULT_FEATURE_NOT_SUPPORTED;
    return create_dlss_feature(static_cast<NVNGX_FEATURE>(feature), static_cast<ParametersImpl *>(params), out_handle);
  }

  ERR("NVSDK_NGX_D3D12_CreateFeature: feature ", feature, " not handled");
  return NVNGX_RESULT_FAIL;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_EvaluateFeature(
    ID3D11DeviceContext *context, const NVSDK_NGX_Handle *handle, const NVNGXParameter *params, void *callback
) {
  if (!context || !handle || !params)
    return NVNGX_RESULT_INVALID_PARAMETER;

  auto parameters = static_cast<const ParametersImpl *>(params);
  switch (static_cast<CommonFeature *>((void *)handle)->feature) {
  case NVNGX_FEATURE_SUPERSAMPLING: {
    auto dlss = static_cast<DLSSFeature *>((void *)handle);

    IMTLD3D11ContextExt *pCtxExt = nullptr;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&pCtxExt))))
      return NVNGX_RESULT_INVALID_PARAMETER;

    TemporalParameters<ID3D11Resource> temporal;
    auto result = get_temporal_parameters(parameters, *dlss, temporal);
    if (NVNGX_FAILED(result))
      return result;

    MTL_TEMPORAL_UPSCALE_D3D11_DESC desc = {};
    desc.InputContentWidth = temporal.input_content_width;
    desc.InputContentHeight = temporal.input_content_height;
    desc.Color = temporal.color;
    desc.Output = temporal.output;
    desc.Depth = temporal.depth;
    desc.MotionVector = temporal.motion_vector;
    desc.ExposureTexture = temporal.exposure;
    desc.DepthReversed = bool(dlss->flag & NVNGX_DLSS_FLAG_DEPTH_INVERTED);
    desc.AutoExposure = bool(dlss->flag & NVNGX_DLSS_FLAG_AUTO_EXPOSURE);
    desc.MotionVectorInDisplayRes = !bool(dlss->flag & NVNGX_DLSS_FLAG_MV_LOWRES);
    desc.JitterOffsetX = temporal.jitter_offset_x;
    desc.JitterOffsetY = temporal.jitter_offset_y;
    desc.InReset = temporal.reset;
    desc.MotionVectorScaleX = temporal.motion_vector_scale_x;
    desc.MotionVectorScaleY = temporal.motion_vector_scale_y;
    desc.PreExposure = temporal.pre_exposure;

    pCtxExt->TemporalUpscale(&desc);
    return NVNGX_RESULT_OK;
  }
  default:
    break;
  }
  return NVNGX_RESULT_FAIL;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_EvaluateFeature(
    ID3D12GraphicsCommandList *context, const NVSDK_NGX_Handle *handle, const NVNGXParameter *params, void *callback
) {
  if (!context || !handle || !params)
    return NVNGX_RESULT_INVALID_PARAMETER;

  auto parameters = static_cast<const ParametersImpl *>(params);
  auto *feature = static_cast<CommonFeature *>((void *)handle);
  if (feature->feature != NVNGX_FEATURE_SUPERSAMPLING)
    return NVNGX_RESULT_FAIL;

  auto dlss = static_cast<DLSSFeature *>(feature);
  TemporalParameters<ID3D12Resource> temporal;
  auto result = get_temporal_parameters(parameters, *dlss, temporal);
  if (NVNGX_FAILED(result))
    return result;

  Com<IMTLD3D12CommandListExt> pCtxExt = nullptr;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(&pCtxExt))))
    return NVNGX_RESULT_INVALID_PARAMETER;

  MTL_TEMPORAL_UPSCALE_D3D12_DESC desc = {};
  desc.InputContentWidth = temporal.input_content_width;
  desc.InputContentHeight = temporal.input_content_height;
  desc.Color = temporal.color;
  desc.Output = temporal.output;
  desc.Depth = temporal.depth;
  desc.MotionVector = temporal.motion_vector;
  desc.ExposureTexture = temporal.exposure;
  desc.DepthReversed = bool(dlss->flag & NVNGX_DLSS_FLAG_DEPTH_INVERTED);
  desc.AutoExposure = bool(dlss->flag & NVNGX_DLSS_FLAG_AUTO_EXPOSURE);
  desc.MotionVectorInDisplayRes = !bool(dlss->flag & NVNGX_DLSS_FLAG_MV_LOWRES);
  desc.InReset = temporal.reset;
  desc.MotionVectorScaleX = temporal.motion_vector_scale_x;
  desc.MotionVectorScaleY = temporal.motion_vector_scale_y;
  desc.JitterOffsetX = temporal.jitter_offset_x;
  desc.JitterOffsetY = temporal.jitter_offset_y;
  desc.PreExposure = temporal.pre_exposure;

  NGX_DEBUG("D3D12_EvaluateFeature: handle=", handle, " input=", desc.InputContentWidth, "x",
            desc.InputContentHeight, " reset=", desc.InReset, " mv_scale=", desc.MotionVectorScaleX, ",",
            desc.MotionVectorScaleY);
  return SUCCEEDED(pCtxExt->TemporalUpscale(&desc)) ? NVNGX_RESULT_OK : NVNGX_RESULT_PLATFORM_ERROR;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_EvaluateFeature_C(
    ID3D11DeviceContext *context, const NVSDK_NGX_Handle *handle, const NVNGXParameter *params, void *callback
) {
  return NVSDK_NGX_D3D11_EvaluateFeature(context, handle, params, callback);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_EvaluateFeature_C(
    ID3D12GraphicsCommandList *context, const NVSDK_NGX_Handle *handle, const NVNGXParameter *params, void *callback
) {
  return NVSDK_NGX_D3D12_EvaluateFeature(context, handle, params, callback);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Shutdown() {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Shutdown1(ID3D11Device *device) {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Shutdown() {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Shutdown1(ID3D12Device *device) {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetScratchBufferSize(unsigned int feature, const NVNGXParameter *params, size_t *out_size) {
  if (!out_size)
    return NVNGX_RESULT_INVALID_PARAMETER;
  *out_size = 0;
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_GetScratchBufferSize(unsigned int feature, const NVNGXParameter *params, size_t *out_size) {
  if (!out_size)
    return NVNGX_RESULT_INVALID_PARAMETER;
  *out_size = 0;
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_ReleaseFeature(NVSDK_NGX_Handle *handle) {
  if (!handle)
    return NVNGX_RESULT_INVALID_PARAMETER;
  switch (static_cast<CommonFeature *>((void *)handle)->feature) {
  case NVNGX_FEATURE_SUPERSAMPLING:
    delete static_cast<DLSSFeature *>((void *)handle);
    break;
  default:
    break;
  }
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle *handle) {
  if (!handle)
    return NVNGX_RESULT_INVALID_PARAMETER;
  switch (static_cast<CommonFeature *>((void *)handle)->feature) {
  case NVNGX_FEATURE_SUPERSAMPLING:
    delete static_cast<DLSSFeature *>((void *)handle);
    break;
  default:
    return NVNGX_RESULT_FEATURE_NOT_FOUND;
  }
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_DestroyParameters(NVNGXParameter *params) {
  if (!params)
    return NVNGX_RESULT_INVALID_PARAMETER;
  delete static_cast<ParametersImpl *>(params);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_AllocateParameters(NVNGXParameter **out_params) {
  if (!out_params)
    return NVNGX_RESULT_INVALID_PARAMETER;
  *out_params = new (std::nothrow) ParametersImpl();
  if (!*out_params)
    return NVNGX_RESULT_OUT_OF_GPU_MEMORY;
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_DestroyParameters(NVNGXParameter *params) {
  if (!params)
    return NVNGX_RESULT_INVALID_PARAMETER;
  delete static_cast<ParametersImpl *>(params);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_AllocateParameters(NVNGXParameter **out_params) {
  if (!out_params)
    return NVNGX_RESULT_INVALID_PARAMETER;
  *out_params = new (std::nothrow) ParametersImpl();
  if (!*out_params)
    return NVNGX_RESULT_OUT_OF_GPU_MEMORY;
  return NVNGX_RESULT_OK;
}

#define DEFINE_PARAMETER_SETTER(name, type)                                                                            \
  NVNGX_API void NVNGX_CONV NVSDK_NGX_Parameter_Set##name(                                                            \
      NVNGXParameter *parameters, const char *name, type value) {                                                     \
    if (parameters)                                                                                                    \
      parameters->Set(name, value);                                                                                    \
  }

DEFINE_PARAMETER_SETTER(ULL, unsigned long long)
DEFINE_PARAMETER_SETTER(F, float)
DEFINE_PARAMETER_SETTER(D, double)
DEFINE_PARAMETER_SETTER(UI, unsigned int)
DEFINE_PARAMETER_SETTER(I, int)
DEFINE_PARAMETER_SETTER(D3d11Resource, ID3D11Resource *)
DEFINE_PARAMETER_SETTER(D3d12Resource, ID3D12Resource *)
DEFINE_PARAMETER_SETTER(VoidPointer, void *)

#undef DEFINE_PARAMETER_SETTER

#define DEFINE_PARAMETER_GETTER(name, type)                                                                            \
  NVNGX_API NVNGX_RESULT NVNGX_CONV NVSDK_NGX_Parameter_Get##name(                                                    \
      NVNGXParameter *parameters, const char *name, type *value) {                                                    \
    if (!parameters || !value)                                                                                         \
      return NVNGX_RESULT_INVALID_PARAMETER;                                                                           \
    return parameters->Get(name, value);                                                                               \
  }

DEFINE_PARAMETER_GETTER(ULL, unsigned long long)
DEFINE_PARAMETER_GETTER(F, float)
DEFINE_PARAMETER_GETTER(D, double)
DEFINE_PARAMETER_GETTER(UI, unsigned int)
DEFINE_PARAMETER_GETTER(I, int)
DEFINE_PARAMETER_GETTER(D3d11Resource, ID3D11Resource *)
DEFINE_PARAMETER_GETTER(D3d12Resource, ID3D12Resource *)
DEFINE_PARAMETER_GETTER(VoidPointer, void *)

#undef DEFINE_PARAMETER_GETTER

NVNGX_API NVNGX_RESULT
NVSDK_NGX_UpdateFeature(const void *app_id, const unsigned int feature) {
  // do nothing
  return NVNGX_RESULT_OK;
}

static NVNGX_RESULT
NVNGX_DLSS_GetOptimalSettingsCallback(NVNGXParameter *params) {
  unsigned int width;
  unsigned int height;
  unsigned int out_width;
  unsigned int out_height;
  float scale = 0.0f;
  NVNGX_PERFQUALITY perf_quality_value;

  if (!params || params->Get(NVNGX_Parameter_Width, &width) != NVNGX_RESULT_OK ||
      params->Get(NVNGX_Parameter_Height, &height) != NVNGX_RESULT_OK ||
      params->Get(NVNGX_Parameter_PerfQualityValue, (int *)&perf_quality_value) != NVNGX_RESULT_OK)
    return NVNGX_RESULT_FAIL;

  switch (perf_quality_value) {
  case NVNGX_PERFQUALITY_ULTRA_PERFORMANCE:
    scale = 1.0f / 3.0f;
    break;
  case NVNGX_PERFQUALITY_MAXPERF:
    scale = 0.5f;
    break;
  case NVNGX_PERFQUALITY_MAXQUALITY:
    scale = 1.0f / 1.5f;
    break;
  case NVNGX_PERFQUALITY_ULTRA_QUALITY:
    scale = 1.0f / 1.3f;
    break;
  case NVNGX_PERFQUALITY_DLAA:
    scale = 1.0f;
    break;
  default:
    scale = 58.0f / 100.0f;
    break;
  }

  out_width = std::ceil(width * scale);
  out_height = std::ceil(height * scale);

  params->Set(NVNGX_Parameter_Scale, scale);
  params->Set(NVNGX_Parameter_SuperSampling_ScaleFactor, scale);
  params->Set(NVNGX_Parameter_OutWidth, out_width);
  params->Set(NVNGX_Parameter_OutHeight, out_height);
  params->Set(NVNGX_Parameter_Sharpness, 0.0f);

  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, (unsigned int)std::ceil(width / 3));
  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, (unsigned int)std::ceil(height / 3));

  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, width);
  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, height);

  params->Set(NVNGX_Parameter_DLSSMode, 1);

  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_DLAA, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_Quality, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_Balanced, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_Performance, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, (unsigned int)0);

  return NVNGX_RESULT_OK;
}

static NVNGX_RESULT
NVNGX_DLSS_GetStatsCallback(NVNGXParameter *params) {
  if (params)
    params->Set("SizeInBytes", 0ull);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetCapabilityParameters(NVNGXParameter **out_params) {
  if (!out_params)
    return NVNGX_RESULT_INVALID_PARAMETER;
  auto out_parameters = new (std::nothrow) ParametersImpl();
  if (!out_parameters)
    return NVNGX_RESULT_OUT_OF_GPU_MEMORY;
  populate_capability_parameters(*out_parameters);
  *out_params = out_parameters;
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_GetCapabilityParameters(NVNGXParameter **out_params) {
  return NVSDK_NGX_D3D11_GetCapabilityParameters(out_params);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetParameters(NVNGXParameter **out_params) {
  return NVSDK_NGX_D3D11_GetCapabilityParameters(out_params);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_GetParameters(NVNGXParameter **out_params) {
  return NVSDK_NGX_D3D12_GetCapabilityParameters(out_params);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetFeatureRequirements(
    IDXGIAdapter *adapter, const NVNGX_FeatureDiscoveryInfo *discovery_info,
    NVNGX_FeatureRequirement *requirement
) {
  if (!discovery_info || !requirement)
    return NVNGX_RESULT_INVALID_PARAMETER;
  if (discovery_info->FeatureID == NVNGX_FEATURE_SUPERSAMPLING) {
    requirement->FeatureSupported = NVNGX_FEATURE_SUPPORT_RESULT_SUPPORTED;
    requirement->MinHWArchitecture = 0;
    strcpy_s(requirement->MinOSVersion, "10.0.16299.0"); // v1709
    return NVNGX_RESULT_OK;
  }
  WARN("NVSDK_NGX_D3D11_GetFeatureRequirements: unsupported feature ", discovery_info->FeatureID);
  requirement->FeatureSupported = NVNGX_FEATURE_SUPPORT_RESULT_UNSUPPORTED;
  return NVNGX_RESULT_FAIL;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_GetFeatureRequirements(
    IDXGIAdapter *adapter, const NVNGX_FeatureDiscoveryInfo *discovery_info,
    NVNGX_FeatureRequirement *requirement
) {
  if (!discovery_info || !requirement)
    return NVNGX_RESULT_INVALID_PARAMETER;
  if (discovery_info->FeatureID == NVNGX_FEATURE_SUPERSAMPLING) {
    requirement->FeatureSupported = NVNGX_FEATURE_SUPPORT_RESULT_SUPPORTED;
    requirement->MinHWArchitecture = 0;
    strcpy_s(requirement->MinOSVersion, "10.0.16299.0");
    return NVNGX_RESULT_OK;
  }
  requirement->FeatureSupported = NVNGX_FEATURE_SUPPORT_RESULT_UNSUPPORTED;
  return NVNGX_RESULT_FAIL;
}

NVNGX_API const wchar_t *NVNGX_CONV
GetNGXResultAsString(NVNGX_RESULT result) {
  switch (result) {
  case NVNGX_RESULT_OK:
    return L"NVSDK_NGX_Result_Success";
  case NVNGX_RESULT_FEATURE_NOT_SUPPORTED:
    return L"NVSDK_NGX_Result_FAIL_FeatureNotSupported";
  case NVNGX_RESULT_INVALID_PARAMETER:
    return L"NVSDK_NGX_Result_FAIL_InvalidParameter";
  case NVNGX_RESULT_FEATURE_NOT_FOUND:
    return L"NVSDK_NGX_Result_FAIL_FeatureNotFound";
  case NVNGX_RESULT_PLATFORM_ERROR:
    return L"NVSDK_NGX_Result_FAIL_PlatformError";
  default:
    return L"NVSDK_NGX_Result_Fail";
  }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason,
                               LPVOID reserved) {
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  DisableThreadLibraryCalls(instance);
  return TRUE;
}

}; // namespace dxmt

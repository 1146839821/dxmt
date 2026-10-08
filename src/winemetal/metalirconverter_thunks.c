#define WINEMETAL_API

#ifdef _WIN32
#undef WINEMETAL_API
#define WINEMETAL_API __declspec(dllexport)
#endif

#include "wineunixlib.h"
#include "metalirconverter_thunks.h"
#include "msc_minmax_abi.h"

WINEMETAL_API int
DXMTMSCIsAvailable(void) {
  struct {
    int32_t ret;
  } params;
  params.ret = 0;

  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_is_available, &params);
  if (status)
    return -1;
  return params.ret;
}

WINEMETAL_API int
DXMTMSCGetCapabilities(struct dxmt_msc_capabilities *capabilities) {
  if (!capabilities)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;

  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_get_capabilities, capabilities);
  if (status)
    return -1;
  return capabilities->ret;
}

WINEMETAL_API int
DXMTMSCCompileDXIL(struct dxmt_msc_compile_dxil_params *params) {
  if (!params)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;

  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_compile_dxil, params);
  if (status)
    return -1;
  return params->ret;
}

WINEMETAL_API int
DXMTMSCCompileDXILWithSampleMask(struct dxmt_msc_compile_dxil_params *params, uint32_t sample_mask) {
  if (!params)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  struct dxmt_msc_compile_sample_mask_params call = {params, sample_mask, DXMT_MSC_ERROR_UNSUPPORTED_FEATURE};
  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_compile_dxil_with_sample_mask, &call);
  return status ? DXMT_MSC_ERROR_UNSUPPORTED_FEATURE : call.ret;
}

WINEMETAL_API int
DXMTMSCSynthesizeRayDispatch(struct dxmt_msc_synthesize_ray_dispatch_params *params) {
  if (!params)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;

  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_synthesize_ray_dispatch, params);
  if (status)
    return -1;
  return params->ret;
}

WINEMETAL_API int
DXMTMSCSynthesizeRayIntersection(struct dxmt_msc_synthesize_ray_intersection_params *params) {
  if (!params)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;

  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_synthesize_ray_intersection, params);
  if (status)
    return -1;
  return params->ret;
}

WINEMETAL_API int
DXMTMSCGetRootSignatureLayout(struct dxmt_msc_get_root_layout_params *params) {
  if (!params)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;

  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_get_root_layout, params);
  if (status)
    return -1;
  return params->ret;
}

WINEMETAL_API int
DXMTMSCLowerTypedBufferOrigins(struct dxmt_msc_lower_typed_origins_params *params) {
  if (!params)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  params->ir_size = 0;
  params->binding_count = 0;
#if UINTPTR_MAX == UINT32_MAX
  if (params->bitcode > UINT32_MAX || params->ir > UINT32_MAX || params->bindings > UINT32_MAX ||
      params->bitcode_size > UINT32_MAX - params->bitcode ||
      params->ir_capacity > UINT32_MAX - params->ir ||
      (uint64_t)params->binding_capacity * sizeof(struct dxmt_msc_typed_origin_binding) > UINT32_MAX - params->bindings)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
#endif
  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_lower_typed_origins, params);
  if (status)
    return -1;
  return params->ret;
}

WINEMETAL_API int
DXMTMSCLowerReductionSamplers(struct dxmt_msc_lower_reduction_samplers_params *params) {
  if (!params)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  if (dxmt_msc_reduction_params_alias(params))
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  params->ir_size = 0;
  params->binding_count = 0;
#if UINTPTR_MAX == UINT32_MAX
  if (params->bitcode > UINT32_MAX || params->ir > UINT32_MAX || params->bindings > UINT32_MAX ||
      params->bitcode_size > UINT32_MAX - params->bitcode ||
      params->ir_capacity > UINT32_MAX - params->ir ||
      (uint64_t)params->binding_capacity * sizeof(struct dxmt_msc_minmax_binding) > UINT32_MAX - params->bindings)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
#endif
  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_lower_reduction_samplers, params);
  if (status)
    return -1;
  return params->ret;
}

WINEMETAL_API int
DXMTMSCLowerLogicOutputs(struct dxmt_msc_lower_logic_outputs_params *params) {
  if (!params || dxmt_msc_logic_params_alias(params)) return DXMT_MSC_ERROR_INVALID_ARGUMENT;
#if UINTPTR_MAX == UINT32_MAX
  if (params->bitcode > UINT32_MAX || params->ir > UINT32_MAX ||
      params->bitcode_size > UINT32_MAX - params->bitcode ||
      params->ir_capacity > UINT32_MAX - params->ir) return DXMT_MSC_ERROR_INVALID_ARGUMENT;
#endif
  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_lower_logic_outputs, params);
  if (status) return -1;
  return params->ret;
}

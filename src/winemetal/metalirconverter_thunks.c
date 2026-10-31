#define WINEMETAL_API

#ifdef _WIN32
#undef WINEMETAL_API
#define WINEMETAL_API __declspec(dllexport)
#endif

#include "wineunixlib.h"
#include "metalirconverter_thunks.h"
#include <string.h>

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
DXMTMSCGetIdentityV1(struct dxmt_msc_identity_v1 *identity) {
  if (!identity || identity->size != sizeof(*identity) || identity->version != DXMT_MSC_IDENTITY_VERSION_1 ||
      identity->reserved)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  memset(identity->sha256, 0, sizeof(identity->sha256));
  identity->ret = DXMT_MSC_ERROR_UNAVAILABLE;
  struct dxmt_msc_capabilities capabilities = {0};
  if (DXMTMSCGetCapabilities(&capabilities) != DXMT_MSC_SUCCESS ||
      !(capabilities.optional_symbols & DXMT_MSC_RUNTIME_BRIDGE_IDENTITY_V1))
    return identity->ret;
  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_get_identity_v1, identity);
  return status ? DXMT_MSC_ERROR_UNAVAILABLE : identity->ret;
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
DXMTMSCPatchSamplerLOD(struct dxmt_msc_patch_lod_params *params) {
  if (!params)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_patch_sampler_lod, params);
  return status ? -1 : params->ret;
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

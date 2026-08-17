#define WINEMETAL_API

#ifdef _WIN32
#undef WINEMETAL_API
#define WINEMETAL_API __declspec(dllexport)
#endif

#include "wineunixlib.h"
#include "metalirconverter_thunks.h"

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
DXMTMSCCompileDXIL(struct dxmt_msc_compile_dxil_params *params) {
  if (!params)
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;

  NTSTATUS status = WINE_UNIX_CALL(unix_dxmt_msc_compile_dxil, params);
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

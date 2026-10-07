#include "windef.h"
#include "winbase.h"
#include "wineunixlib.h"
#include "winemetal_thunks.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  DisableThreadLibraryCalls(instance);
  if (__wine_init_unix_call())
    return FALSE;
  char enabled[2];
  if (GetEnvironmentVariableA("DXMT_TRACE_RUNTIME_IDENTITY", enabled, sizeof(enabled)) == 1 && enabled[0] == '1') {
    uint32_t windows_pid = GetCurrentProcessId();
    WINE_UNIX_CALL(unix_trace_runtime_identity, &windows_pid);
  }
  return TRUE;
}

extern BOOL WINAPI DllMainCRTStartup(HANDLE hDllHandle, DWORD dwReason,
                                       LPVOID lpreserved);

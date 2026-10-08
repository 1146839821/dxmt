#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Failure-injection DLL only: a present, loadable factory that fails operationally.
extern "C" HRESULT WINAPI DxcCreateInstance(REFCLSID, REFIID, void **output) {
  if (output) *output = nullptr;
  return E_NOTIMPL;
}

/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "d3d12.h"
#include "com/com_pointer.hpp"
#include "d3d12_device.hpp"
#include "dxgi_interfaces.h"
#include "log/log.hpp"

namespace dxmt {

Logger Logger::s_instance("d3d12.log");

static bool
is_supported_feature_level(D3D_FEATURE_LEVEL level) {
  switch (level) {
  case D3D_FEATURE_LEVEL_11_0:
  case D3D_FEATURE_LEVEL_11_1:
  case D3D_FEATURE_LEVEL_12_0:
  case D3D_FEATURE_LEVEL_12_1:
    return true;
  default:
    return false;
  }
}

static D3D_FEATURE_LEVEL
get_max_supported_feature_level(WMT::Device device) {
  // FL12_0 and FL12_1 require Tier 2 tiled resources, which DXMT does not
  // expose yet. Keep the hardware-dependent FL11_1 cutoff consistent with
  // the D3D11 device creation path.
  return device.supportsFamily(WMTGPUFamilyApple7) ? D3D_FEATURE_LEVEL_11_1 : D3D_FEATURE_LEVEL_11_0;
}

extern "C" HRESULT WINAPI
D3D12CreateDevice(IUnknown *pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void **ppDevice) {

  Com<IDXGIAdapter> dxgi_adapter = nullptr;
  Com<IDXGIFactory> dxgi_factory = nullptr;
  Com<IMTLDXGIAdapter> dxgi_adapter_mtl = nullptr;

  if (!is_supported_feature_level(MinimumFeatureLevel)) {
    WARN("D3D12CreateDevice: unsupported minimum feature level ", static_cast<unsigned>(MinimumFeatureLevel));
    return E_INVALIDARG;
  }

  HRESULT hr;

  if (!pAdapter) {
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory));

    if (FAILED(hr)) {
      ERR("D3D12CreateDevice: Failed to create a DXGI factory");
      return hr;
    }

    if (FAILED(hr = dxgi_factory->EnumAdapters(0, &dxgi_adapter))) {
      ERR("D3D12CreateDevice: No default adapter available");
      return hr;
    }
  } else {
    dxgi_adapter = com_cast<IDXGIAdapter>(pAdapter);
  }

  if (FAILED(hr = dxgi_adapter->QueryInterface(IID_PPV_ARGS(&dxgi_adapter_mtl)))) {
    ERR("D3D12CreateDevice: Not a DXMT adapter");
    return hr;
  }

  const auto max_feature_level = get_max_supported_feature_level(dxgi_adapter_mtl->GetMTLDevice());
  if (MinimumFeatureLevel > max_feature_level) {
    WARN("D3D12CreateDevice: requested feature level ", static_cast<unsigned>(MinimumFeatureLevel),
         " exceeds maximum supported feature level ", static_cast<unsigned>(max_feature_level));
    return E_INVALIDARG;
  }

  if (!ppDevice)
    return S_FALSE;

  return dxmt::CreateD3D12Device(dxgi_adapter_mtl.ptr(), MinimumFeatureLevel, riid, ppDevice);
}

extern "C" HRESULT WINAPI
D3D12GetInterface(REFCLSID rcslid, REFIID iid, void **debug) {
  return E_NOINTERFACE;
}

BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  DisableThreadLibraryCalls(instance);
  return TRUE;
}

} // namespace dxmt

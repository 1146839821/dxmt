#pragma once

#include "com/com_pointer.hpp"
#include "d3d11_1.h"
#include "Metal/MTLDevice.hpp"
#include "dxgi_interfaces.h"

MIDL_INTERFACE("a46de9a7-0233-4a94-b75c-9c0f8f364cda")
IMTLD3D11Device : public ID3D11Device1 {
  virtual MTL::Device *STDMETHODCALLTYPE GetMTLDevice() = 0;
};
__CRT_UUID_DECL(IMTLD3D11Device, 0xa46de9a7, 0x0233, 0x4a94, 0xb7, 0x5c, 0x9c,
                0x0f, 0x8f, 0x36, 0x4c, 0xda);

namespace dxmt {
Com<IMTLDXGIDevice> CreateD3D11Device(IMTLDXGIAdatper *pAdapter,
                                      D3D_FEATURE_LEVEL FeatureLevel,
                                      UINT Flags);
} // namespace dxmt
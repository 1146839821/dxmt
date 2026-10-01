#include "d3d12_format_support.hpp"
#include <iostream>

int main() {
  for (auto format : dxmt::kD3D12AdditionalTypedUAVFormats) {
    if (!dxmt::IsD3D12TypedUAVFormat(format) ||
        dxmt::SupportsD3D12TypedUAVLoad(format, false, true) ||
        !dxmt::SupportsD3D12TypedUAVLoad(format, true, true) ||
        dxmt::SupportsD3D12TypedUAVLoad(format, true, false))
      return 1;
  }
  for (auto format : {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT})
    if (!dxmt::IsD3D12BaselineTypedUAVFormat(format) || !dxmt::IsD3D12TypedUAVFormat(format) ||
        !dxmt::SupportsD3D12TypedUAVLoad(format, false, true) ||
        dxmt::SupportsD3D12TypedUAVLoad(format, false, false))
      return 1;
  for (auto format : {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_TYPELESS,
                     DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM,
                     DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_NV12})
    if (dxmt::IsD3D12TypedUAVFormat(format) || dxmt::SupportsD3D12TypedUAVLoad(format, true, true))
      return 1;
  std::cout << "typed UAV policy contracts passed\n";
  return 0;
}

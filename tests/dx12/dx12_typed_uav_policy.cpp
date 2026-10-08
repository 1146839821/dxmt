#include "d3d12_format_support.hpp"
#include <iostream>

int main() {
  for (bool shared_backend : {false, true}) for (bool read : {false, true}) for (bool write : {false, true}) {
    const bool ready = dxmt::SupportsD3D12R11G11B10LoadStore(shared_backend, read, write);
    if (ready != (shared_backend && read && write)) {
      std::cerr << "R11 backend availability failed shared=" << shared_backend << " read=" << read << " write=" << write << "\n";
      return 1;
    }
  }
  if (dxmt::SupportsD3D12TypedUAVLoad(DXGI_FORMAT_R11G11B10_FLOAT, false, true) ||
      dxmt::SupportsD3D12TypedUAVLoad(DXGI_FORMAT_R11G11B10_FLOAT, false, false) ||
      !dxmt::SupportsD3D12TypedUAVLoad(DXGI_FORMAT_R11G11B10_FLOAT, true, true) ||
      dxmt::SupportsD3D12TypedUAVLoad(DXGI_FORMAT_R11G11B10_FLOAT, true, false) ||
      dxmt::SupportsD3D12TypedUAVLoad(DXGI_FORMAT_R32G32_FLOAT, false, true)) {
    std::cerr << "R11G11B10 prerequisite/backend load policy failed\n";
    return 1;
  }
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

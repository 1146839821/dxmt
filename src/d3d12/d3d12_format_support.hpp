#pragma once

#include <d3d12.h>
#include <array>

namespace dxmt {

inline constexpr std::array<DXGI_FORMAT, 15> kD3D12AdditionalTypedUAVFormats = {
    DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_SINT,
    DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT,
    DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT,
    DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT,
    DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT,
};

constexpr bool
IsD3D12BaselineTypedUAVFormat(DXGI_FORMAT format) {
  return format == DXGI_FORMAT_R32_FLOAT || format == DXGI_FORMAT_R32_UINT || format == DXGI_FORMAT_R32_SINT;
}

constexpr bool
IsD3D12TypedUAVFormat(DXGI_FORMAT format) {
  if (IsD3D12BaselineTypedUAVFormat(format))
    return true;
  for (auto additional : kD3D12AdditionalTypedUAVFormats)
    if (format == additional)
      return true;
  // The remaining formats are optional, individually queried D3D typed UAV formats.
  switch (format) {
  case DXGI_FORMAT_R16G16B16A16_UNORM:
  case DXGI_FORMAT_R16G16B16A16_SNORM:
  case DXGI_FORMAT_R32G32_FLOAT:
  case DXGI_FORMAT_R32G32_UINT:
  case DXGI_FORMAT_R32G32_SINT:
  case DXGI_FORMAT_R10G10B10A2_UNORM:
  case DXGI_FORMAT_R10G10B10A2_UINT:
  case DXGI_FORMAT_R11G11B10_FLOAT:
  case DXGI_FORMAT_R8G8B8A8_SNORM:
  case DXGI_FORMAT_R16G16_FLOAT:
  case DXGI_FORMAT_R16G16_UNORM:
  case DXGI_FORMAT_R16G16_UINT:
  case DXGI_FORMAT_R16G16_SNORM:
  case DXGI_FORMAT_R16G16_SINT:
  case DXGI_FORMAT_R8G8_UNORM:
  case DXGI_FORMAT_R8G8_UINT:
  case DXGI_FORMAT_R8G8_SNORM:
  case DXGI_FORMAT_R8G8_SINT:
  case DXGI_FORMAT_R16_UNORM:
  case DXGI_FORMAT_R16_SNORM:
  case DXGI_FORMAT_R8_SNORM:
  case DXGI_FORMAT_A8_UNORM:
  case DXGI_FORMAT_B5G6R5_UNORM:
  case DXGI_FORMAT_B5G5R5A1_UNORM:
  case DXGI_FORMAT_B4G4R4A4_UNORM:
    return true;
  default:
    return false;
  }
}

constexpr bool
SupportsD3D12TypedUAVLoad(DXGI_FORMAT format, bool complete_additional_formats, bool native_read_write) {
  return IsD3D12TypedUAVFormat(format) && native_read_write &&
         (IsD3D12BaselineTypedUAVFormat(format) || complete_additional_formats);
}

} // namespace dxmt

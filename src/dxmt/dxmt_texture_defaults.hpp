#pragma once

#include "winemetal.h"
#include <optional>

namespace dxmt {

// All defined channels are zero for OOB access. Only a missing native alpha
// channel contributes one, before applying the format/view's encoded swizzle.
inline std::optional<uint32_t> TextureOutOfBoundsOneMask(WMTPixelFormat format) {
  bool alpha_missing = false;
  switch (ORIGINAL_FORMAT(format)) {
  case WMTPixelFormatR8Unorm: case WMTPixelFormatR8Unorm_sRGB:
  case WMTPixelFormatR8Snorm: case WMTPixelFormatR8Uint: case WMTPixelFormatR8Sint:
  case WMTPixelFormatR16Unorm: case WMTPixelFormatR16Snorm:
  case WMTPixelFormatR16Uint: case WMTPixelFormatR16Sint: case WMTPixelFormatR16Float:
  case WMTPixelFormatR32Uint: case WMTPixelFormatR32Sint: case WMTPixelFormatR32Float:
  case WMTPixelFormatRG8Unorm: case WMTPixelFormatRG8Unorm_sRGB:
  case WMTPixelFormatRG8Snorm: case WMTPixelFormatRG8Uint: case WMTPixelFormatRG8Sint:
  case WMTPixelFormatRG16Unorm: case WMTPixelFormatRG16Snorm:
  case WMTPixelFormatRG16Uint: case WMTPixelFormatRG16Sint: case WMTPixelFormatRG16Float:
  case WMTPixelFormatRG32Uint: case WMTPixelFormatRG32Sint: case WMTPixelFormatRG32Float:
  case WMTPixelFormatB5G6R5Unorm: case WMTPixelFormatRG11B10Float: case WMTPixelFormatRGB9E5Float:
  case WMTPixelFormatBC4_RUnorm: case WMTPixelFormatBC4_RSnorm:
  case WMTPixelFormatBC5_RGUnorm: case WMTPixelFormatBC5_RGSnorm:
  case WMTPixelFormatBC6H_RGBFloat: case WMTPixelFormatBC6H_RGBUfloat:
  case WMTPixelFormatDepth16Unorm: case WMTPixelFormatDepth32Float:
  case WMTPixelFormatStencil8: case WMTPixelFormatDepth24Unorm_Stencil8:
  case WMTPixelFormatDepth32Float_Stencil8: case WMTPixelFormatX32_Stencil8: case WMTPixelFormatX24_Stencil8:
    alpha_missing = true;
    break;
  case WMTPixelFormatA8Unorm:
  case WMTPixelFormatA1BGR5Unorm: case WMTPixelFormatABGR4Unorm: case WMTPixelFormatBGR5A1Unorm:
  case WMTPixelFormatRGBA8Unorm: case WMTPixelFormatRGBA8Unorm_sRGB:
  case WMTPixelFormatRGBA8Snorm: case WMTPixelFormatRGBA8Uint: case WMTPixelFormatRGBA8Sint:
  case WMTPixelFormatBGRA8Unorm: case WMTPixelFormatBGRA8Unorm_sRGB:
  case WMTPixelFormatRGB10A2Unorm: case WMTPixelFormatRGB10A2Uint: case WMTPixelFormatBGR10A2Unorm:
  case WMTPixelFormatRGBA16Unorm: case WMTPixelFormatRGBA16Snorm:
  case WMTPixelFormatRGBA16Uint: case WMTPixelFormatRGBA16Sint: case WMTPixelFormatRGBA16Float:
  case WMTPixelFormatRGBA32Uint: case WMTPixelFormatRGBA32Sint: case WMTPixelFormatRGBA32Float:
  case WMTPixelFormatBC1_RGBA: case WMTPixelFormatBC1_RGBA_sRGB:
  case WMTPixelFormatBC2_RGBA: case WMTPixelFormatBC2_RGBA_sRGB:
  case WMTPixelFormatBC3_RGBA: case WMTPixelFormatBC3_RGBA_sRGB:
  case WMTPixelFormatBC7_RGBAUnorm: case WMTPixelFormatBC7_RGBAUnorm_sRGB:
    break;
  default:
    return std::nullopt; // Do not invent defaults for an unknown native format.
  }
  const unsigned channels[] = {GET_SWIZZLE_RED(format), GET_SWIZZLE_GREEN(format),
      GET_SWIZZLE_BLUE(format), GET_SWIZZLE_ALPHA(format)};
  uint32_t ones = 0;
  for (unsigned component = 0; component < 4; ++component)
    if (channels[component] == 1 || (channels[component] == 5 && alpha_missing))
      ones |= 1u << component;
  return ones;
}

} // namespace dxmt

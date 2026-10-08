#include "air_texture_abi.hpp"
#include "dxmt_texture_defaults.hpp"
#include <cstdio>

int main() {
  struct Case { WMTPixelFormat format; uint32_t ones; };
  const Case cases[] = {
      {WMTPixelFormatR32Float, 8}, {WMTPixelFormatRG32Float, 8}, {WMTPixelFormatRG11B10Float, 8},
      {WMTPixelFormatRGBA8Unorm, 0}, {WMTPixelFormatBGRA8Unorm, 0}, {WMTPixelFormatA8Unorm, 0},
      {WMTPixelFormatBC4_RUnorm, 8}, {WMTPixelFormatBC5_RGSnorm, 8}, {WMTPixelFormatBC6H_RGBFloat, 8},
      {WMTPixelFormatBC1_RGBA, 0}, {WMTPixelFormatBC7_RGBAUnorm, 0}, {WMTPixelFormatR32X8X32, 8},
      {WMTPixelFormat(WMTPixelFormatRGBA8Unorm | WMTPixelFormatAlphaIsOne), 8},
      {WMTPixelFormat(ENCODE_FORMAT_SWIZZLE(WMTPixelFormatR32Float, 5, 5, 5, 5)), 15},
      {WMTPixelFormat(ENCODE_FORMAT_SWIZZLE(WMTPixelFormatRGBA8Unorm, 5, 5, 5, 5)), 0},
      {WMTPixelFormat(ENCODE_FORMAT_SWIZZLE(WMTPixelFormatRGBA8Unorm, 1, 0, 2, 5)), 1},
      {WMTPixelFormat(ENCODE_FORMAT_SWIZZLE(WMTPixelFormatR32Float, 1, 0, 2, 5)), 9},
      {WMTPixelFormat(ENCODE_FORMAT_SWIZZLE(WMTPixelFormatR32Float, 2, 3, 4, 2)), 0},
  };
  unsigned passed = 0;
  for (const auto &test : cases) {
    const auto actual = dxmt::TextureOutOfBoundsOneMask(test.format);
    if (!actual || *actual != test.ones ||
        dxmt::air::PackTextureDefaultComponents(*actual) != (16u | test.ones)) {
      std::fprintf(stderr, "format=%u got=%u expected=%u\n", test.format, actual.value_or(~0u), test.ones);
      return 1;
    }
    ++passed;
  }
  if (dxmt::TextureOutOfBoundsOneMask(WMTPixelFormatInvalid) ||
      dxmt::TextureOutOfBoundsOneMask(WMTPixelFormat(999))) return 1;
  std::printf("AIR texture defaults: passed=%u unknown-rejection=2 stride=32\n", passed);
  return 0;
}

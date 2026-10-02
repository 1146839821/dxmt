#include <metal_stdlib>

using namespace metal;

// Private AIRCONV contract, not an MSC ABI. The caller supplies an unclamped,
// unbiased, point-filtered sampler with the application's address/border modes.
// LOD is already biased and sampler/resource-clamped. Coordinates must be finite.
// Flags: min-linear, mag-linear, mip-linear, maximum, minifying in bits 0..4.
// The caller decides minification after sampler LOD clamping (FL11+ contract).
enum MinMaxFlags : uint {
  MinLinear = 1u, MagLinear = 2u, MipLinear = 4u, Maximum = 8u, Minifying = 16u
};
static uint3 dimensions(texture2d<float> texture, uint mip) {
  return uint3(texture.get_width(mip), texture.get_height(mip), 1);
}
static uint3 dimensions(texture2d_array<float> texture, uint mip) {
  return uint3(texture.get_width(mip), texture.get_height(mip), 1);
}
static uint3 dimensions(texture3d<float> texture, uint mip) {
  return uint3(texture.get_width(mip), texture.get_height(mip), texture.get_depth(mip));
}
static float4 tap(texture2d<float> texture, sampler point, float3 uv, uint array, uint mip) {
  return texture.sample(point, uv.xy, level(float(mip)));
}
static float4 tap(texture2d_array<float> texture, sampler point, float3 uv, uint array, uint mip) {
  return texture.sample(point, uv.xy, array, level(float(mip)));
}
static float4 tap(texture3d<float> texture, sampler point, float3 uv, uint array, uint mip) {
  return texture.sample(point, uv, level(float(mip)));
}

template <typename Texture>
static float4 reduce_level(Texture texture, sampler point, float3 uv, uint array,
                          uint mip, bool linear, bool maximum, int3 offset, uint axes) {
  const float3 size = float3(dimensions(texture, mip));
  const float3 position = uv * size - (linear ? 0.5f : 0.0f);
  const float3 base = floor(position);
  const float3 fraction = position - base;
  float4 result = 0;
  bool have_value = false;
  const uint count = linear ? (1u << axes) : 1u;
  for (uint i = 0; i < count; ++i) {
    const uint3 upper = uint3(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
    bool contributes = true;
    if (linear) {
      // Extrema exclude any texel with zero ordinary-filter weight. Do not
      // sample excluded taps: they must not contribute residency feedback either.
      for (uint axis = 0; axis < axes; ++axis)
        contributes &= upper[axis] ? fraction[axis] > 0.0f : fraction[axis] < 1.0f;
    }
    if (contributes) {
      const float3 center = (base + float3(upper) + float3(offset) + 0.5f) / size;
      const float4 value = tap(texture, point, center, array, mip);
      result = have_value ? (maximum ? fmax(result, value) : fmin(result, value)) : value;
      have_value = true;
    }
  }
  return result;
}

template <typename Texture>
static float4 reduce(Texture texture, sampler point, float3 uv, uint array,
                     float lod, uint flags, int3 offset, uint axes) {
  const bool maximum = (flags & Maximum) != 0;
  const bool linear = (flags & ((flags & Minifying) ? MinLinear : MagLinear)) != 0;
  const bool mip_linear = (flags & MipLinear) != 0;
  const float clamped = clamp(lod, 0.0f, float(texture.get_num_mip_levels() - 1u));
  const uint lower = uint(floor(clamped + (mip_linear ? 0.0f : 0.5f)));
  float4 result = reduce_level(texture, point, uv, array, lower, linear, maximum, offset, axes);
  if (mip_linear && clamped > float(lower)) {
    const uint upper = min(lower + 1u, texture.get_num_mip_levels() - 1u);
    const float4 value = reduce_level(texture, point, uv, array, upper, linear, maximum, offset, axes);
    result = maximum ? fmax(result, value) : fmin(result, value);
  }
  return result;
}

float4 minmax2d(texture2d<float>, sampler, float2, float, uint, int2) asm("dxmt.minmax.sample_level.2d");
float4 minmax2d_array(texture2d_array<float>, sampler, float2, uint, float, uint, int2)
    asm("dxmt.minmax.sample_level.2d_array");
float4 minmax3d(texture3d<float>, sampler, float3, float, uint, int3) asm("dxmt.minmax.sample_level.3d");

float4 minmax2d(texture2d<float> texture, sampler point, float2 uv, float lod, uint flags, int2 offset) {
  return reduce(texture, point, float3(uv, 0), 0, lod, flags, int3(offset, 0), 2);
}
float4 minmax2d_array(texture2d_array<float> texture, sampler point, float2 uv, uint array,
                     float lod, uint flags, int2 offset) {
  return reduce(texture, point, float3(uv, 0), array, lod, flags, int3(offset, 0), 2);
}
float4 minmax3d(texture3d<float> texture, sampler point, float3 uv, float lod, uint flags, int3 offset) {
  return reduce(texture, point, uv, 0, lod, flags, offset, 3);
}

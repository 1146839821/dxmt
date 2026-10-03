#include <metal_stdlib>

using namespace metal;

// Private AIRCONV contract, not an MSC ABI. The caller supplies an unclamped,
// unbiased, point-filtered sampler with the application's address/border modes.
// LOD is already biased and sampler/resource-clamped. Coordinates must be finite.
// Flags: min-linear, mag-linear, mip-linear, maximum, minifying in bits 0..4;
// bit 6 preserves logical 1D dimensionality through the 2D storage remap.
// The caller decides minification after sampler LOD clamping (FL11+ contract).
enum MinMaxFlags : uint {
  MinLinear = 1u, MagLinear = 2u, MipLinear = 4u, Maximum = 8u, Minifying = 16u,
  Logical1D = 64u
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
  if (flags & Logical1D) {
    // The synthetic Y axis is not part of a D3D 1D footprint. Keep it at the
    // sole texel center so border addressing cannot create additional taps.
    axes = 1;
    uv.y = 0.5f;
    offset.y = 0;
  }
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

// Face bases follow the existing cube sampling convention: +X,-X,+Y,-Y,+Z,-Z.
// Use discrete edge remapping, not projection of an off-face texel center:
// the latter would shrink the tangential coordinate and select the wrong row.
static float3 cube_normal(uint face) {
  switch (face) {
  case 0: return float3(1, 0, 0); case 1: return float3(-1, 0, 0);
  case 2: return float3(0, 1, 0); case 3: return float3(0, -1, 0);
  case 4: return float3(0, 0, 1); default: return float3(0, 0, -1);
  }
}
static float3 cube_u(uint face) {
  switch (face) {
  case 0: return float3(0, 0, -1); case 1: return float3(0, 0, 1);
  case 5: return float3(-1, 0, 0); default: return float3(1, 0, 0);
  }
}
static float3 cube_v(uint face) {
  switch (face) {
  case 2: return float3(0, 0, 1); case 3: return float3(0, 0, -1);
  default: return float3(0, -1, 0);
  }
}
static uint cube_face(float3 direction) {
  const float3 a = abs(direction);
  if (a.z >= a.y && a.z >= a.x) return direction.z >= 0 ? 4u : 5u;
  if (a.y >= a.x) return direction.y >= 0 ? 2u : 3u;
  return direction.x >= 0 ? 0u : 1u;
}
static float2 cube_uv(float3 direction, uint face) {
  return 0.5f * (float2(dot(direction, cube_u(face)), dot(direction, cube_v(face))) /
      dot(direction, cube_normal(face)) + 1.0f);
}
static float3 cube_direction(float2 uv, uint face) {
  const float2 p = uv * 2.0f - 1.0f;
  return cube_normal(face) + p.x * cube_u(face) + p.y * cube_v(face);
}
static float4 cube_point(texturecube<float> texture, sampler point, float3 direction, uint array, uint mip) {
  return texture.sample(point, direction, level(float(mip)));
}
static float4 cube_point(texturecube_array<float> texture, sampler point, float3 direction, uint array, uint mip) {
  return texture.sample(point, direction, array, level(float(mip)));
}
static float4 cube_combine(float4 a, float4 b, bool maximum) {
  return maximum ? fmax(a, b) : fmin(a, b);
}
template <typename Texture>
static float4 cube_texel(Texture texture, sampler point, uint face, float2 position,
                         uint array, uint mip, float size) {
  // Every generated sample is strictly inside one face, at a texel center.
  const float2 texel = clamp(floor(position), float2(0), float2(size - 1.0f));
  return cube_point(texture, point, cube_direction((texel + 0.5f) / size, face), array, mip);
}
template <typename Texture>
static float4 cube_tap(Texture texture, sampler point, uint face, float2 position,
                       uint array, uint mip, float size, bool maximum) {
  const bool outside_u = position.x < 0 || position.x >= size;
  const bool outside_v = position.y < 0 || position.y >= size;
  if (!outside_u && !outside_v) return cube_texel(texture, point, face, position, array, mip, size);
  float2 boundary = (position + 0.5f) / size;
  if (outside_u) boundary.x = position.x < 0 ? 0.0f : 1.0f;
  if (outside_v) boundary.y = position.y < 0 ? 0.0f : 1.0f;
  const float3 direction = cube_direction(boundary, face);
  float4 result = 0;
  if (outside_u) {
    const uint adjacent = cube_face(cube_u(face) * (position.x < 0 ? -1.0f : 1.0f));
    result = cube_texel(texture, point, adjacent, cube_uv(direction, adjacent) * size, array, mip, size);
  }
  if (outside_v) {
    const uint adjacent = cube_face(cube_v(face) * (position.y < 0 ? -1.0f : 1.0f));
    const float4 value = cube_texel(texture, point, adjacent, cube_uv(direction, adjacent) * size, array, mip, size);
    result = outside_u ? cube_combine(result, value, maximum) : value;
  }
  // The missing virtual corner distributes positive weight to all three real
  // face corners. Reduction must include all three, never their average.
  if (outside_u && outside_v)
    result = cube_combine(result, cube_texel(texture, point, face, position, array, mip, size), maximum);
  return result;
}
template <typename Texture>
static float4 cube_level(Texture texture, sampler point, uint face, float2 uv,
                         uint array, uint mip, bool linear, bool maximum) {
  const float size = float(texture.get_width(mip));
  if (!linear) return cube_texel(texture, point, face, uv * size, array, mip, size);
  const float2 position = uv * size - (linear ? 0.5f : 0.0f);
  const float2 base = floor(position), fraction = position - base;
  float4 result = cube_tap(texture, point, face, base, array, mip, size, maximum);
  for (uint corner = 1; corner < 4; ++corner) {
    const uint2 upper = uint2(corner & 1u, corner >> 1u);
    if (linear && (!upper.x || fraction.x > 0) && (!upper.y || fraction.y > 0))
      result = cube_combine(result,
          cube_tap(texture, point, face, base + float2(upper), array, mip, size, maximum), maximum);
  }
  return result;
}
template <typename Texture>
static float4 cube_reduce(Texture texture, sampler point, float3 direction, uint array, float lod, uint flags) {
  const uint face = cube_face(direction);
  const float2 uv = cube_uv(direction, face);
  const bool maximum = (flags & Maximum) != 0;
  const bool linear = (flags & ((flags & Minifying) ? MinLinear : MagLinear)) != 0;
  const bool mip_linear = (flags & MipLinear) != 0;
  const float clamped = clamp(lod, 0.0f, float(texture.get_num_mip_levels() - 1u));
  const uint lower = uint(floor(clamped + (mip_linear ? 0.0f : 0.5f)));
  float4 result = cube_level(texture, point, face, uv, array, lower, linear, maximum);
  if (mip_linear && clamped > float(lower))
    result = cube_combine(result, cube_level(texture, point, face, uv, array,
        min(lower + 1u, texture.get_num_mip_levels() - 1u), linear, maximum), maximum);
  return result;
}
float4 minmaxcube(texturecube<float>, sampler, float3, float, uint) asm("dxmt.minmax.sample_level.cube");
float4 minmaxcube_array(texturecube_array<float>, sampler, float3, uint, float, uint)
    asm("dxmt.minmax.sample_level.cube_array");
float4 minmaxcube(texturecube<float> texture, sampler point, float3 direction, float lod, uint flags) {
  return cube_reduce(texture, point, direction, 0, lod, flags);
}
float4 minmaxcube_array(texturecube_array<float> texture, sampler point, float3 direction, uint array,
                        float lod, uint flags) {
  return cube_reduce(texture, point, direction, array, lod, flags);
}

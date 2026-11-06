#ifndef MS_SAMPLES
#define MS_SAMPLES 4
#endif

#ifdef MS_WRITE
cbuffer WriteValue : register(b0) {
  float write_depth;
  uint write_coverage;
};

float4 vertex_main(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * 2.0 - 1.0, 0.5, 1.0);
}

#ifdef MS_COVERAGE
struct WriteOutput {
  float depth : SV_Depth;
  uint coverage : SV_Coverage;
};
WriteOutput pixel_main() {
  WriteOutput value;
  value.depth = write_depth;
#ifdef MS_COVERAGE_SUBSET
  // Wider and narrower than the PSO mask: both inputs must participate in AND.
  value.coverage = (write_coverage & 5) ? 0xffffffff : 0;
#else
  value.coverage = write_coverage;
#endif
  return value;
}
#else
// This variant relies only on PSO SampleMask; keep it as an independent check.
float pixel_main() : SV_Depth { return write_depth; }
#endif
#else
// BC_TEST selects BC1; add BC3_TEST and/or BC_SRGB to match the runner mode.
Texture1D<float4> texture_1d : register(t0);
Texture1DArray<float4> texture_1d_array : register(t1);
Texture2D<float4> texture_2d : register(t2);
Texture2DArray<float4> texture_2d_array : register(t3);
TextureCube<float4> texture_cube : register(t4);
TextureCubeArray<float4> texture_cube_array : register(t5);
#ifdef STENCIL_MS
Texture2DMS<uint4> texture_ms : register(t6);
Texture2DMSArray<uint4> texture_ms_array : register(t7);
#else
Texture2DMS<float4> texture_ms : register(t6);
Texture2DMSArray<float4> texture_ms_array : register(t7);
#endif
Texture3D<float4> texture_3d : register(t8);
SamplerState texture_sampler : register(s0);
#ifdef MS_PIXEL_READ
// Pixel UAV registers follow the color output for the SM5 compiler contract.
RWStructuredBuffer<uint> output : register(u1);
#else
RWStructuredBuffer<uint> output : register(u0);
#endif

uint ToByte(float4 value, uint index) {
#ifdef RGB96
  if (index != 6 && index != 7 &&
      (abs(value.y - (index == 2 ? 1.0 : 0.25)) > 0.00001 ||
       abs(value.z - 0.75) > 0.00001 || value.w != 1.0))
    return 0;
#endif
  return (uint)(value.x * 255.0 + 0.5);
}

#ifdef STENCIL_MS
bool MSMatches(uint4 value, uint expected) {
  if (value.x != 0 || value.z != 0 || value.w != 1)
    return 0;
  return value.y == expected;
}
#else
bool MSMatches(float4 value, uint expected) {
#ifdef DEPTH_MS
  if (abs(value.x - expected / 255.0) > 0.000001 || value.y != 0.0 || value.z != 0.0 ||
      value.w != 1.0)
    return 0;
#endif
  return ToByte(value, 6) == expected;
}
#endif

#ifdef BC_TEST
float4 BCExpected(uint index, uint layer, uint mip, uint block_x) {
  uint red = 4 + (index + layer + 3 * mip + 5 * block_x) % 24;
  // Decode endpoint0. A small tolerance permits hardware endpoint precision.
  float3 rgb = float3((red << 3) | (red >> 2), (17 << 2) | (17 >> 4),
                      (23 << 3) | (23 >> 2)) /
               255.0;
#ifdef BC_SRGB
  // All chosen endpoint channels exceed the sRGB linear-segment threshold.
  rgb = pow((rgb + 0.055) / 1.055, 2.4);
#endif
#ifdef BC3_TEST
  float alpha = (96.0 + 7.0 * layer + mip) / 255.0;
#else
  float alpha = 1.0;
#endif
  return float4(rgb, alpha);
}

bool BCEqual(float4 actual, float4 expected) {
  return all(abs(actual - expected) < 0.006);
}

float3 CubeDirection(uint face, float2 uv) {
  float2 p = 2.0 * uv - 1.0;
  switch (face) {
  case 0:
    return float3(1, -p.y, -p.x);
  case 1:
    return float3(-1, -p.y, p.x);
  case 2:
    return float3(p.x, 1, p.y);
  case 3:
    return float3(p.x, -1, -p.y);
  case 4:
    return float3(p.x, -p.y, 1);
  default:
    return float3(-p.x, -p.y, -1);
  }
}

bool CheckBCVolume() {
  for (uint mip = 0; mip < 4; mip++) {
    uint depth = max(1u, 4u >> mip);
    for (uint z = 0; z < depth; z++) {
      // The upload varies Z like an array layer, while mip depth shrinks.
      float4 expected = BCExpected(8, z, mip, 0);
      float3 uvw = float3(0.25, 0.25, (z + 0.5) / depth);
      if (!BCEqual(texture_3d.Load(int4(0, 0, z, mip)), expected) ||
          !BCEqual(texture_3d.SampleLevel(texture_sampler, uvw, mip), expected))
        return false;
    }
  }
  // Texture3D linear filtering also mixes adjacent depth slices.
  float4 z_midpoint = 0.5 * (BCExpected(8, 0, 0, 0) + BCExpected(8, 1, 0, 0));
  return BCEqual(
      texture_3d.SampleLevel(texture_sampler, float3(0.25, 0.25, 0.25), 0),
      z_midpoint);
}

bool CheckBC(uint index) {
  const uint layers = index == 2 ? 1 : index == 3 ? 2 : index == 4 ? 6 : 12;
  for (uint layer = 0; layer < layers; layer++) {
    for (uint mip = 0; mip < 4; mip++) {
      float4 expected = BCExpected(index, layer, mip, 0);
      float2 uv = mip == 3 ? float2(0.5, 0.5) : float2(0.25, 0.25);
      float4 actual;
      if (index == 2) {
        actual = texture_2d.SampleLevel(texture_sampler, uv, mip);
        if (!BCEqual(texture_2d.Load(int3(0, 0, mip)), expected))
          return false;
      } else if (index == 3) {
        actual = texture_2d_array.SampleLevel(texture_sampler,
                                              float3(uv, layer), mip);
        if (!BCEqual(texture_2d_array.Load(int4(0, 0, layer, mip)), expected))
          return false;
      } else if (index == 4) {
        actual = texture_cube.SampleLevel(texture_sampler,
                                          CubeDirection(layer, uv), mip);
      } else {
        actual = texture_cube_array.SampleLevel(
            texture_sampler, float4(CubeDirection(layer % 6, uv), layer / 6),
            mip);
      }
      if (!BCEqual(actual, expected))
        return false;
    }
    // At the base-mip block boundary, linear filtering must mix two distinct
    // decoded blocks. For sRGB views decoding precedes the average.
    float4 midpoint =
        0.5 * (BCExpected(index, layer, 0, 0) + BCExpected(index, layer, 0, 1));
    float4 filtered;
    if (index == 2)
      filtered = texture_2d.SampleLevel(texture_sampler, float2(0.5, 0.25), 0);
    else if (index == 3)
      filtered = texture_2d_array.SampleLevel(texture_sampler,
                                              float3(0.5, 0.25, layer), 0);
    else if (index == 4)
      filtered = texture_cube.SampleLevel(
          texture_sampler, CubeDirection(layer, float2(0.5, 0.25)), 0);
    else
      filtered = texture_cube_array.SampleLevel(
          texture_sampler,
          float4(CubeDirection(layer % 6, float2(0.5, 0.25)), layer / 6), 0);
    if (!BCEqual(filtered, midpoint))
      return false;
  }
  return true;
}
#endif

void CheckDimensions() {
  output[0] = ToByte(texture_1d.SampleLevel(texture_sampler, 0.5, 0.0), 0);
  output[1] = ToByte(
      texture_1d_array.SampleLevel(texture_sampler, float2(0.5, 1.0), 0.0), 1);
#ifndef BC_TEST
  output[2] =
      ToByte(texture_2d.SampleLevel(texture_sampler, float2(0.5, 0.5), 0.0), 2);
  output[3] = ToByte(
      texture_2d_array.SampleLevel(texture_sampler, float3(0.5, 0.5, 1.0), 0.0),
      3);
  output[4] = ToByte(
      texture_cube.SampleLevel(texture_sampler, float3(0.0, 0.0, 1.0), 0.0), 4);
  output[5] = ToByte(texture_cube_array.SampleLevel(
                         texture_sampler, float4(0.0, 0.0, 1.0, 1.0), 0.0),
                     5);
#else
  for (uint index = 2; index <= 5; index++)
    output[index] = CheckBC(index) ? 11 * (index + 1) : 0;
#endif
  output[6] = 77;
  output[7] = 88;
  uint ms_width, ms_height, ms_samples;
  uint array_width, array_height, array_layers, array_samples;
  texture_ms.GetDimensions(ms_width, ms_height, ms_samples);
  texture_ms_array.GetDimensions(array_width, array_height, array_layers, array_samples);
  // Reject mismatched fixtures before any Load can use an invalid sample index.
  if (ms_width != 1 || ms_height != 1 || ms_samples != MS_SAMPLES) {
    output[6] = 0;
    return;
  }
  if (array_width != 1 || array_height != 1 || array_layers != 2 || array_samples != MS_SAMPLES) {
    output[7] = 0;
    return;
  }
  // Uniform clears cover every index. MS_DISTINCT additionally catches an
  // implementation loading the wrong sample or aliasing the array layers.
  for (uint sample = 0; sample < MS_SAMPLES; sample++) {
#ifdef MS_DISTINCT
    uint sample_delta = 7 * sample;
#else
    uint sample_delta = 0;
#endif
    uint expected_ms = 77 + sample_delta;
#ifdef MS_ZERO
    expected_ms = 0;
#elif defined(MS_SUBSET)
    if (sample & 1)
      expected_ms = 0;
#endif
    if (!MSMatches(texture_ms.Load(int2(0, 0), sample), expected_ms))
      output[6] = 0;
    for (uint layer = 0; layer < 2; layer++) {
#ifdef MS_DISTINCT
      uint expected = 88 + sample_delta + 31 * layer;
#else
      uint expected = 88;
#endif
#ifdef MS_ZERO
      expected = 0;
#elif defined(MS_SUBSET)
      if (sample & 1)
        expected = 0;
#endif
      if (!MSMatches(texture_ms_array.Load(int3(0, 0, layer), sample), expected))
        output[7] = 0;
    }
  }
#ifdef BC_TEST
  output[8] = CheckBCVolume() ? 99 : 0;
#else
  output[8] = ToByte(
      texture_3d.SampleLevel(texture_sampler, float3(0.5, 0.5, 0.5), 0.0), 8);
#endif
#ifdef RGB96
  // Loads also check all components, including default alpha and custom
  // mapping.
  if (ToByte(texture_1d.Load(int2(0, 0)), 0) != 11 ||
      ToByte(texture_1d_array.Load(int3(0, 1, 0)), 1) != 22 ||
      ToByte(texture_2d.Load(int3(0, 0, 0)), 2) != 33 ||
      ToByte(texture_2d_array.Load(int4(0, 0, 1, 0)), 3) != 44 ||
      ToByte(texture_3d.Load(int4(0, 0, 1, 0)), 8) != 99)
    output[0] = 0;
#endif
}

#ifdef MS_PIXEL_READ
float4 pixel_main() : SV_Target {
  CheckDimensions();
  return float4(1, 1, 1, 1);
}
#else
[numthreads(1, 1, 1)] void main() { CheckDimensions(); }
#endif

#endif // MS_WRITE

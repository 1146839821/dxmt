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
RWStructuredBuffer<uint> output : register(u0);

uint ToByte(float4 value, uint index) {
#ifdef RGB96
  if (index != 6 && index != 7 &&
      (abs(value.y - (index == 2 ? 1.0 : 0.25)) > 0.00001 ||
       abs(value.z - 0.75) > 0.00001 || value.w != 1.0))
    return 0;
#endif
  return (uint)(value.x * 255.0 + 0.5);
}

[numthreads(1, 1, 1)] void main() {
  output[0] = ToByte(texture_1d.SampleLevel(texture_sampler, 0.5, 0.0), 0);
  output[1] = ToByte(
      texture_1d_array.SampleLevel(texture_sampler, float2(0.5, 1.0), 0.0), 1);
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
#ifdef STENCIL_MS
  output[6] = texture_ms.Load(int2(0, 0), 0).y;
  output[7] = texture_ms_array.Load(int3(0, 0, 1), 0).y;
  // Check every sample and both array layers, not just sample zero.
  for (uint sample = 0; sample < 4; sample++) {
    if (texture_ms.Load(int2(0, 0), sample).y != 77)
      output[6] = 0;
    for (uint layer = 0; layer < 2; layer++)
      if (texture_ms_array.Load(int3(0, 0, layer), sample).y != 88)
        output[7] = 0;
  }
#else
  output[6] = ToByte(texture_ms.Load(int2(0, 0), 0), 6);
  output[7] = ToByte(texture_ms_array.Load(int3(0, 0, 1), 0), 7);
#endif
  output[8] = ToByte(
      texture_3d.SampleLevel(texture_sampler, float3(0.5, 0.5, 0.5), 0.0), 8);
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

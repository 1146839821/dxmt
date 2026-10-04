#ifdef MINMAX_CUBE_ARRAY
TextureCubeArray<float4> Input : register(t0);
#else
TextureCube<float4> Input : register(t0);
#endif
SamplerState First : register(s0);
SamplerState Second : register(s1);
float4 main(float4 position : SV_Position) : SV_Target {
  float3 direction = float3(1, position.x * 0.125, position.y * 0.125);
#ifdef MINMAX_CUBE_ARRAY
  float4 coordinate = float4(direction, 1);
#else
  float3 coordinate = direction;
#endif
  return 0.5 * (Input.Sample(First, coordinate) + Input.SampleBias(Second, coordinate, 0.5));
}

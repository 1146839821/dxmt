// Interior +Z footprint: every nonzero linear tap belongs to this face.
// Spatial derivatives are nonzero; bias 4 selects the separate mip1 payload.
#ifdef MINMAX_CUBE_ARRAY
TextureCubeArray<float4> Input : register(t0);
#else
TextureCube<float4> Input : register(t0);
#endif
SamplerState First : register(s0);
SamplerState Second : register(s1);

float4 vertex(uint id : SV_VertexID) : SV_Position {
  float2 p = float2((id << 1) & 2, id & 2);
  return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 pixel(float4 position : SV_Position) : SV_Target {
  float2 uv = 0.375 + position.xy * 0.0625;
  float3 direction = float3(2 * uv.x - 1, 1 - 2 * uv.y, 1);
#ifdef MINMAX_CUBE_ARRAY
  float4 coordinate = float4(direction, 1);
#else
  float3 coordinate = direction;
#endif
#ifdef USE_BIAS
  float first = Input.SampleBias(First, coordinate, 4).x;
  float second = Input.SampleBias(Second, coordinate, 4).x;
#else
  float first = Input.Sample(First, coordinate).x;
  float second = Input.Sample(Second, coordinate).x;
#endif
  return float4(first, second, 0, 1);
}

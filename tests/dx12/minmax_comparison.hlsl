Texture2D<float4> Input : register(t0);
SamplerState Reduction : register(s0);
Texture2D<float> Depth : register(t1);
SamplerComparisonState Comparison : register(s1);
#ifdef PIXEL
float4 main(float4 position : SV_Position) : SV_Target {
  float2 uv = position.xy * 0.25;
  float4 value = Input.SampleLevel(Reduction, uv, 0);
  return value + Depth.SampleCmp(Comparison, uv, 0.5);
}
#else
RWStructuredBuffer<float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main() {
  float4 value = Input.SampleLevel(Reduction, float2(0.5, 0.5), 0);
  Output[0] = value + Depth.SampleCmpLevelZero(Comparison, float2(0.5, 0.5), 0.5);
}
#endif

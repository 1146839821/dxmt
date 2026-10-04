Texture2D<float4> Input : register(t0);
Texture2D<float> Depth : register(t1);
SamplerState Reduction : register(s0);
SamplerComparisonState Comparison : register(s1);
RWStructuredBuffer<uint> Output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  Output[0] = uint(Input.SampleLevel(Reduction, float2(0.5, 0.5), 0).r * 255 + 0.5);
  Output[1] = asuint(Depth.SampleCmpLevelZero(Comparison, float2(0.5, 0.5), 0.5));
}

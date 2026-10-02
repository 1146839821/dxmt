Texture2DArray<float4> Input : register(t0);
SamplerState Sampler : register(s0);
#ifdef MINMAX_ARRAY_STRUCTURED
RWStructuredBuffer<uint> Output : register(u0);
#else
RWByteAddressBuffer Output : register(u0);
#endif
[numthreads(1, 1, 1)]
void main() {
#ifdef MINMAX_ARRAY_GRAD
  float4 value = Input.SampleGrad(Sampler, float3(0.5, 0.5, 1),
      float2(0.23, 0), float2(0.23, 0.23));
#else
  float4 value = Input.SampleLevel(Sampler, float3(0.5, 0.5, 1), 0);
#endif
#ifdef MINMAX_ARRAY_STRUCTURED
  Output[0] = uint(round(value.r * 255));
#else
  Output.Store(0, uint(round(value.r * 255)));
#endif
}

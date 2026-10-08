Texture2D<float4> Input : register(t0);
SamplerState Sampler : register(s0);
#ifdef MINMAX_GRAD_STRUCTURED
RWStructuredBuffer<uint> Output : register(u0);
#else
RWByteAddressBuffer Output : register(u0);
#endif
#ifndef MINMAX_GRAD_CLAMP
#define MINMAX_GRAD_CLAMP 0.0
#endif
[numthreads(1, 1, 1)]
void main() {
  float4 value = Input.SampleGrad(Sampler, float2(0.5, 0.5),
      float2(0.23, 0), float2(0.23, 0.23), int2(0, 0), MINMAX_GRAD_CLAMP);
#ifdef MINMAX_GRAD_STRUCTURED
  Output[0] = uint(round(value.r * 255));
#else
  Output.Store(0, uint(round(value.r * 255)));
#endif
}

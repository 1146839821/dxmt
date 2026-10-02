Texture3D<float4> Input : register(t0);
SamplerState Sampler : register(s0);
RWStructuredBuffer<uint> Output : register(u0);
#ifndef MINMAX_VOLUME_Z
#define MINMAX_VOLUME_Z 0.5
#endif
[numthreads(1, 1, 1)]
void main() {
#ifdef MINMAX_VOLUME_GRAD
  float4 value = Input.SampleGrad(Sampler, float3(0.5, 0.5, MINMAX_VOLUME_Z),
      float3(0.1, 0, 0.5), float3(0.1, 0.1, 0.5));
#else
  float4 value = Input.SampleLevel(Sampler, float3(0.5, 0.5, MINMAX_VOLUME_Z), 0);
#endif
  Output[0] = uint(round(value.r * 255));
}

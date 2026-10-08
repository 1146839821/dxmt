#ifdef MINMAX_LINE_ARRAY
Texture1DArray<float4> Input : register(t0);
#define MINMAX_LINE_COORD float2(0.5, 1)
#else
Texture1D<float4> Input : register(t0);
#define MINMAX_LINE_COORD 0.5
#endif
SamplerState Sampler : register(s0);
RWStructuredBuffer<uint> Output : register(u0);
#ifndef MINMAX_LINE_DERIVATIVE
#define MINMAX_LINE_DERIVATIVE 1.0
#endif
#ifndef MINMAX_LINE_DERIVATIVE_Y
#define MINMAX_LINE_DERIVATIVE_Y 0.0
#endif
[numthreads(1, 1, 1)]
void main() {
#ifdef MINMAX_LINE_GRAD
  float4 value = Input.SampleGrad(Sampler, MINMAX_LINE_COORD, MINMAX_LINE_DERIVATIVE, MINMAX_LINE_DERIVATIVE_Y);
#else
  float4 value = Input.SampleLevel(Sampler, MINMAX_LINE_COORD, 0.0);
#endif
  Output[0] = uint(round(value.r * 255));
}

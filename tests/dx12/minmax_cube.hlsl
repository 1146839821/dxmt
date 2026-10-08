#ifndef MINMAX_CUBE_CASE
#define MINMAX_CUBE_CASE 0
#endif
#ifdef MINMAX_CUBE_ARRAY
TextureCubeArray<float4> Input : register(t0);
#else
TextureCube<float4> Input : register(t0);
#endif
SamplerState Sampler : register(s0);
RWStructuredBuffer<uint> Output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  float3 direction = float3(1, 0, 0);
  float3 dx = float3(0.25, 0, 0), dy = float3(0, 0.25, 0);
  float lod = 0;
#if MINMAX_CUBE_CASE == 1
  direction = float3(1, 0, 1);
#elif MINMAX_CUBE_CASE == 2
  direction = float3(1, 1, 1);
#elif MINMAX_CUBE_CASE == 3
  lod = 0.5;
#elif MINMAX_CUBE_CASE == 4
  dx = float3(0, 2, 0); dy = float3(0, 0, 2);
#elif MINMAX_CUBE_CASE == 5
  dx = float3(4096, 0, 0); dy = 0;
#elif MINMAX_CUBE_CASE == 6
  direction = float3(10, 0, 0); dx = float3(0, 20, 0); dy = float3(0, 0, 20);
#elif MINMAX_CUBE_CASE == 7
  direction = float3(-1, 0.5, 0); dx = float3(-2, 1, 0); dy = float3(0, 0, 2);
#elif MINMAX_CUBE_CASE == 8
  direction = float3(-1, 0.5, 0); dx = float3(-4096, 2048, 0); dy = 0;
#elif MINMAX_CUBE_CASE == 9
  direction = float3(1, 0.5, 1); dx = float3(0, 1, 1); dy = 0;
#elif MINMAX_CUBE_CASE == 10
  direction = float3(1, 1, 0.5); dx = float3(0, 1, 1); dy = 0;
#elif MINMAX_CUBE_CASE == 11
  direction = float3(-1, 0.5, 0); dx = float3(-1, 1, 0); dy = 0;
#endif
#ifdef MINMAX_CUBE_ARRAY
  float4 coordinate = float4(direction, 1);
#else
  float3 coordinate = direction;
#endif
#ifdef MINMAX_CUBE_GRAD
  float4 value = Input.SampleGrad(Sampler, coordinate, dx, dy);
#else
  float4 value = Input.SampleLevel(Sampler, coordinate, lod);
#endif
  Output[0] = uint(value.r * 255 + 0.5);
}

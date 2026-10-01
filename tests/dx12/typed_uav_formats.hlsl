// TYPE, SHAPE and CHANNELS are supplied by the fixture build.
#if SHAPE == 0
RWBuffer<TYPE> input : register(u0);
#define SRC(i) (i)
#define DST(i) ((i) + 4)
#elif SHAPE == 1
RWTexture1D<TYPE> input : register(u0);
#define SRC(i) (i)
#define DST(i) ((i) + 4)
#elif SHAPE == 2
RWTexture1DArray<TYPE> input : register(u0);
#define SRC(i) uint2(i, 0)
#define DST(i) uint2((i) + 4, 0)
#elif SHAPE == 3
RWTexture2D<TYPE> input : register(u0);
#define SRC(i) uint2(i, 1)
#define DST(i) uint2((i) + 4, 1)
#elif SHAPE == 4
RWTexture2DArray<TYPE> input : register(u0);
#define SRC(i) uint3(i, 1, 0)
#define DST(i) uint3((i) + 4, 1, 0)
#else
RWTexture3D<TYPE> input : register(u0);
#define SRC(i) uint3(i, 1, 1)
#define DST(i) uint3((i) + 4, 1, 1)
#endif
RWBuffer<uint> output : register(u1);
[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  TYPE value = input[SRC(tid.x)];
  input[DST(tid.x)] = value;
#if CHANNELS == 4
  uint4 bits = asuint(value);
  output[tid.x * 4] = bits.x;
  output[tid.x * 4 + 1] = bits.y;
  output[tid.x * 4 + 2] = bits.z;
  output[tid.x * 4 + 3] = bits.w;
#else
  output[tid.x] = asuint(value);
#endif
}

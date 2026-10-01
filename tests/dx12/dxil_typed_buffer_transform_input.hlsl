// Original unlowered binary fixtures. No origin CBV or source-level bounds guard.
#if KIND == 1
Buffer<uint> input : register(t0);
#else
RWBuffer<uint> input : register(u0);
#endif
RWBuffer<uint> output : register(u1);
[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint index = tid.x + INDEX_BIAS;
#if KIND == 2
  uint value;
  InterlockedAdd(input[index], 13, value);
#else
  uint value = input[index];
#if KIND == 0
  input[index + 4] = value;
#endif
#endif
  output[tid.x] = value;
}

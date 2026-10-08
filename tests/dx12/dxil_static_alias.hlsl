// Original binaries: no origin CBV, no source-level bounds guards.
#if PHASE == 1
RWByteAddressBuffer raw : register(u0);
#else
RWBuffer<uint> a : register(u0);
RWBuffer<uint> b : register(u2);
#endif
RWBuffer<uint> output : register(u1);
[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
#if PHASE == 0
  uint value = a[tid.x];
  b[tid.x + 4] = value;
#elif PHASE == 1
  uint value = raw.Load(tid.x * 4);
  raw.Store(tid.x * 4, value + 13);
#else
  uint value = a[tid.x + 5] + b[tid.x + 4];
#endif
  output[tid.x] = value;
}

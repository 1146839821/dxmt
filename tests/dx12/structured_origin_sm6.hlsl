// Focused lowering contract: arbitrary bindings, existing CBV, branches/loops.
RWBuffer<uint> input : register(u7, space3);
RWBuffer<uint> output : register(u9, space4);
cbuffer App : register(b3, space7) { uint bound; };
[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  if (tid.x & 1) {
    uint old;
    InterlockedAdd(input[tid.x], 1, old);
    output[tid.x] = old;
  } else {
    uint total = 0;
    [loop] for (uint i = 0; i < bound; ++i) total += input[i];
    output[tid.x] = total;
  }
}

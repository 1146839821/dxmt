RWBuffer<uint> input : register(u0);
RWBuffer<uint> output : register(u1);
[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint previous;
  InterlockedAdd(input[tid.x], 13, previous);
  output[tid.x] = previous;
}

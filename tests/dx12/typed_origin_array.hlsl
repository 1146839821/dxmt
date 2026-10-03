Buffer<uint> Inputs[2] : register(t3);
RWBuffer<uint> Outputs[2] : register(u5);

[numthreads(1, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
#ifdef TYPED_ORIGIN_DYNAMIC
  Outputs[1][0] = Inputs[thread_id.x][0] + 17;
#else
  Outputs[1][0] = Inputs[1][0] + 17;
#endif
}

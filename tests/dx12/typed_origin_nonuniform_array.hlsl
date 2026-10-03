Buffer<uint> Inputs[2] : register(t3);
RWBuffer<uint> Outputs[2] : register(u5);

[numthreads(2, 1, 1)]
void main(uint3 lane : SV_GroupThreadID) {
  uint index = NonUniformResourceIndex(lane.x);
  Outputs[index][0] = Inputs[index][0] + Inputs[index][1] + 17;
  Outputs[index][1] = 123;
}

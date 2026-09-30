[shader("raygeneration")]
void RayGen() {
  RWStructuredBuffer<uint> output = ResourceDescriptorHeap[0];
  output[0] = 0xd312d312;
}

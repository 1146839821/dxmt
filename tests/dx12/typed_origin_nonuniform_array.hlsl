Buffer<uint> Inputs[2] : register(t3);
RWBuffer<uint> Outputs[2] : register(u5);

#if EMBEDDED_ROOT
#if STATIC_ROOT
[RootSignature("DescriptorTable(SRV(t3,numDescriptors=2,flags=DATA_VOLATILE),UAV(u5,numDescriptors=2,flags=DATA_VOLATILE)),RootConstants(num32BitConstants=1,b0)")]
#else
[RootSignature("DescriptorTable(SRV(t3,numDescriptors=2,flags=DESCRIPTORS_VOLATILE|DATA_VOLATILE),UAV(u5,numDescriptors=2,flags=DESCRIPTORS_VOLATILE|DATA_VOLATILE)),RootConstants(num32BitConstants=1,b0)")]
#endif
#endif
[numthreads(2, 1, 1)]
void main(uint3 lane : SV_GroupThreadID) {
  uint index = NonUniformResourceIndex(lane.x);
  Outputs[index][0] = Inputs[index][0] + Inputs[index][1] + 17;
  Outputs[index][1] = 123;
}

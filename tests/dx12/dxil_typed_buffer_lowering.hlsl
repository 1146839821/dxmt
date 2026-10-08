// Semantic prototypes only: this is not a pass over an existing DXIL binary.
// KIND: 0 UAV load/store, 1 SRV load, 2 UAV atomic add.
#if RAW_R32
#if KIND == 1
ByteAddressBuffer input : register(t0);
#else
RWByteAddressBuffer input : register(u0);
#endif
#else
#if KIND == 1
Buffer<uint> input : register(t0);
#else
RWBuffer<uint> input : register(u0);
#endif
cbuffer ViewOrigin : register(b0, space1) {
  uint PaddingElements;
  uint LogicalElements;
};
#endif
RWBuffer<uint> output : register(u1);
[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint i = tid.x;
#if RAW_R32
#if KIND == 2
  uint value;
  input.InterlockedAdd(i * 4, 13, value);
#else
  uint value = input.Load(i * 4);
#if KIND == 0
  input.Store((i + 4) * 4, value);
#endif
#endif
#else
  uint value = 0;
  if (i < LogicalElements) {
#if KIND == 2
    InterlockedAdd(input[i + PaddingElements], 13, value);
#else
    value = input[i + PaddingElements];
#if KIND == 0
    if (i + 4 < LogicalElements)
      input[i + 4 + PaddingElements] = value;
#endif
#endif
  }
#endif
  output[i] = value;
}

// Compile both entry variants for cs_5_0 and cs_6_0; run with
// dx12_airconv_compute_residency --counter-pair append.bin consume.bin offset first-element.
#ifdef COUNTER_CONSUME
ConsumeStructuredBuffer<uint> input_data : register(u0);
RWStructuredBuffer<uint> output_data : register(u1);
#else
AppendStructuredBuffer<uint> output_data : register(u0);
#endif

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
#ifdef COUNTER_CONSUME
  output_data[id.x] = input_data.Consume();
#else
  output_data.Append(100 + id.x);
#endif
}

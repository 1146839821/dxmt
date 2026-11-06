ByteAddressBuffer input_buffer : register(t0);
RWByteAddressBuffer output_buffer : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  output_buffer.Store(id.x * 4, input_buffer.Load(id.x * 4));
}

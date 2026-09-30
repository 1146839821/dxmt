ByteAddressBuffer input_buffer : register(t0);
RWByteAddressBuffer output_buffer : register(u0);

[numthreads(1, 1, 1)]
void main() {
  output_buffer.Store(0, input_buffer.Load(0));
}

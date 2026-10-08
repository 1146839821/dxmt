RWByteAddressBuffer output_buffer : register(u0);
ByteAddressBuffer input_buffer : register(t0);
[numthreads(1, 1, 1)]
void main() {
  output_buffer.Store(0, input_buffer.Load(0) + 1);
}

RWByteAddressBuffer sparse_write : register(u1);
[numthreads(1, 1, 1)]
void probe_read() {
  output_buffer.Store(0, input_buffer.Load(0) + 1);
  output_buffer.Store(4, input_buffer.Load(65532) + 1);
}
[numthreads(1, 1, 1)]
void probe_write() {
  sparse_write.Store(0, 0xabcdef01);
  sparse_write.Store(65532, 0xabcdef01);
}

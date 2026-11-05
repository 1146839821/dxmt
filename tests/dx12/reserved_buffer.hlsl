RWByteAddressBuffer output_buffer : register(u0);
#ifdef PROBE_TYPED_READ
Buffer<uint> input_buffer : register(t0);
uint read_input(uint index) { return input_buffer[index]; }
#else
ByteAddressBuffer input_buffer : register(t0);
uint read_input(uint index) { return input_buffer.Load(index * 4); }
#endif
[numthreads(1, 1, 1)]
void main() {
  output_buffer.Store(0, read_input(0) + 1);
}

RWByteAddressBuffer sparse_write : register(u1);
[numthreads(64, 1, 1)]
void probe_read(uint3 id : SV_DispatchThreadID) {
  output_buffer.Store(id.x * 4, read_input(id.x) + 1);
}
[numthreads(1, 1, 1)]
void probe_write() {
  sparse_write.Store(0, 0xabcdef01);
  sparse_write.Store(65532, 0xabcdef01);
}

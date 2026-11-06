RWByteAddressBuffer output_buffer : register(u0);
#if defined(PROBE_TYPED_UAV_READ)
RWBuffer<uint> input_buffer : register(u1);
uint read_input(uint index) { return input_buffer[index]; }
#elif defined(PROBE_TYPED_READ)
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

#if !defined(PROBE_TYPED_UAV_READ)
RWByteAddressBuffer sparse_write : register(u1);
#endif
[numthreads(64, 1, 1)]
void probe_read(uint3 id : SV_DispatchThreadID) {
  output_buffer.Store(id.x * 4, read_input(id.x) + 1);
}
[numthreads(1, 1, 1)]
void probe_write() {
#if defined(PROBE_TYPED_UAV_READ)
  input_buffer[0] = 0xabcdef01;
  input_buffer[16383] = 0xabcdef01;
#else
  sparse_write.Store(0, 0xabcdef01);
  sparse_write.Store(65532, 0xabcdef01);
#endif
}

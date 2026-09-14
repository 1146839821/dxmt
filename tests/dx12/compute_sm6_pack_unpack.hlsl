RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  const int4 signed_values = int4(1, 2, 3, 4);
  const int8_t4_packed packed = pack_s8(signed_values);
  const int4 unpacked = unpack_s8s32(packed);
  output[0] = asuint(unpacked.x + unpacked.y + unpacked.z + unpacked.w);
}

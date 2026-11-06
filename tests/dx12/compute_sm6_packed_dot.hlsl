RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    const uint packed_a = 0x01020380;
    const uint packed_b = 0x01010101;
    const int signed_result = dot4add_i8packed(packed_a, packed_b, 5);
    const uint unsigned_result = dot4add_u8packed(packed_a, packed_b, 5);
    output[0] = asuint(signed_result);
    output[1] = unsigned_result;
}

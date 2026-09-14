RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    const int16_t signed_sum = int16_t(1234) + int16_t(-234);
    const uint16_t unsigned_sum = uint16_t(60000) + uint16_t(1000);
    const int16_t2 vector_result = int16_t2(10, -20) * int16_t2(2, 2);
    const half half_result = half(1.5h) + half(0.5h);
    const bool valid = signed_sum == 1000 && unsigned_sum == 61000 && vector_result.x == 20 &&
                       vector_result.y == -40 && abs((float)half_result - 2.0f) < 0.001f;
    output[0] = valid ? (uint)signed_sum + (uint)unsigned_sum + (uint)((float)half_result * 100.0f) : 0;
}

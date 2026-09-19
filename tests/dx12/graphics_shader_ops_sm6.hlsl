struct VSInput
{
    float2 position : POSITION;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

bool int64_valid()
{
    const uint64_t loaded = 777ull;
    const uint64_t result = loaded + 0x100000000ull + 5ull;
    const uint64_t shifted = (result << 17) >> 17;
    return loaded == 777ull && result > loaded && shifted == result &&
           (result ^ loaded) == 0x100000007ull && (result & 0x100000000ull) != 0;
}

#ifdef DXMT_NATIVE16
bool native16_valid()
{
    const int16_t signed_sum = int16_t(1234) + int16_t(-234);
    const uint16_t unsigned_sum = uint16_t(60000) + uint16_t(1000);
    const int16_t2 vector_result = int16_t2(10, -20) * int16_t2(2, 2);
    const half half_result = half(1.5h) + half(0.5h);
    return signed_sum == 1000 && unsigned_sum == 61000 && vector_result.x == 20 &&
           vector_result.y == -40 && abs((float)half_result - 2.0f) < 0.001f;
}
#endif

VSOutput vs_int64(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = int64_valid() ? float4(0.0, 1.0, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);
    return output;
}

float4 ps_int64(VSOutput input) : SV_Target0
{
    return int64_valid() && input.color.g > 0.5 ? float4(0.0, 1.0, 0.0, 1.0)
                                                : float4(1.0, 0.0, 0.0, 1.0);
}

#ifdef DXMT_NATIVE16
VSOutput vs_native16(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = native16_valid() ? float4(0.0, 1.0, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);
    return output;
}

float4 ps_native16(VSOutput input) : SV_Target0
{
    return native16_valid() && input.color.g > 0.5 ? float4(0.0, 1.0, 0.0, 1.0)
                                                   : float4(1.0, 0.0, 0.0, 1.0);
}
#endif

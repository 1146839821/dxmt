#ifdef NUMERIC_DIFFERENTIAL
StructuredBuffer<uint4> input : register(t0);
RWStructuredBuffer<uint2> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint4 words = input[tid.x];
    uint64_t a = (uint64_t(words.y) << 32) | words.x;
    uint64_t b = (uint64_t(words.w) << 32) | words.z;
    uint64_t divisor = b | 1ull;
    uint shift = words.z & 63;
    int64_t sa = int64_t(a);
    // Keep signed division away from MIN / -1 and zero.
    int64_t sb = int64_t(b & 0x7fffffffffffffffull) | 1ll;
    uint64_t values[12] = {
        a + b, a - b, a * b, a / divisor, a % divisor,
        a << shift, a >> shift, uint64_t(sa >> shift),
        uint64_t(sa / sb), uint64_t(sa % sb),
        uint64_t((a < b ? 1 : 0) | (sa < int64_t(b) ? 2 : 0) | (a == b ? 4 : 0)),
        (a ^ b) & (a | b)
    };
    [unroll] for (uint i = 0; i < 12; ++i)
        output[tid.x * 12 + i] = uint2(uint(values[i]), uint(values[i] >> 32));
}
#else
StructuredBuffer<uint> input : register(t0);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    const uint64_t loaded = (uint64_t)input[0] | ((uint64_t)input[1] << 32);
    const uint64_t result = loaded + 0x100000000ull + 5ull;
    const uint64_t shifted = (result << 17) >> 17;
    const bool valid = loaded == 777 && result > loaded && shifted == result &&
                       (result ^ loaded) == 0x100000007ull &&
                       (result & 0x100000000ull) != 0;

    output[0] = valid ? (uint)result : 0;
    output[1] = valid ? (uint)(result >> 32) : 0;
}

#endif

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

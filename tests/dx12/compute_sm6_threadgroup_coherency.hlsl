// Cross-lane reads occur only after a group synchronization barrier.
StructuredBuffer<uint4> input : register(t0);
RWStructuredBuffer<uint2> output : register(u0);
groupshared uint values[64];

#ifndef DXMT_TEST_CACHE_VARIANT
#define DXMT_TEST_CACHE_VARIANT 0
#endif

[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    values[lane] = input[group.x].x ^ (lane * 0x9e3779b9u) ^ DXMT_TEST_CACHE_VARIANT;
    GroupMemoryBarrierWithGroupSync();
    if (lane < 12)
        output[group.x * 12 + lane] = uint2(values[(lane * 5 + 17) % 64], values[(lane + 32) % 64]);
}

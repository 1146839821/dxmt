RWStructuredBuffer<uint64_t> output : register(u0);

groupshared uint64_t shared_value;

[numthreads(1, 1, 1)]
void main()
{
    shared_value = 1;
    GroupMemoryBarrierWithGroupSync();

    uint64_t original = 0;
    InterlockedAdd(shared_value, 2, original);
    InterlockedCompareExchange(output[0], 3, 7, original);
    output[1] = shared_value;
}

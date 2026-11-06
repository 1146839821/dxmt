globallycoherent RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 group_id : SV_GroupID)
{
    if (group_id.x == 0) {
        output[0] = 0x12345678;
        DeviceMemoryBarrierWithGroupSync();
    } else {
        output[1] = output[0];
    }
}

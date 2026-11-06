RWStructuredBuffer<uint> output : register(u0);

[WaveSize(32)]
[numthreads(32, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    const uint lane = WaveGetLaneIndex();
    if (lane == 0)
        output[0] = WaveGetLaneCount() == 32 ? 0x00C0FFEE : 0;
    if (lane == 31) {
        output[1] = 0x2c000000;
        output[2] = 496;
    }
}

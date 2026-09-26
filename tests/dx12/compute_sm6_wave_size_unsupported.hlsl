RWStructuredBuffer<uint> output : register(u0);

[WaveSize(64)]
[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    output[0] = 1;
}

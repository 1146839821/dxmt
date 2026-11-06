RWStructuredBuffer<uint> output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    const float2 coordinate = float2(tid.xy);
    const float sum = ddx(coordinate.x) + ddy(coordinate.y) +
                      ddx_fine(coordinate.x) + ddy_coarse(coordinate.y);
    output[tid.y * 8 + tid.x] = asuint(sum);
}

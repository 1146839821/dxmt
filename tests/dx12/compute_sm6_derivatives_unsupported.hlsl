RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    const float2 coordinate = float2(tid.xy);
    const float sum = ddx(coordinate.x) + ddy(coordinate.y) +
                      ddx_fine(coordinate.x) + ddy_coarse(coordinate.y);
    if (tid.x == 0 && tid.y == 0)
        output[0] = asuint(sum);
}

float4 vertex(uint id : SV_VertexID) : SV_Position
{
    return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);
}
uint seed(uint sample : SV_SampleIndex) : SV_Target0
{
    return 0x12340000u + sample * 0x101u;
}
uint source(float4 position : SV_Position) : SV_Target0 { return 240; }
Texture2DMS<uint, 4> samples : register(t0);
RWByteAddressBuffer output : register(u0);
[numthreads(4, 1, 1)]
void read_samples(uint3 id : SV_DispatchThreadID)
{
    output.Store(id.x * 4, samples.Load(int2(0, 0), id.x));
}
Texture2DMS<float, 4> depth_samples : register(t0);
[numthreads(4, 1, 1)]
void read_depth(uint3 id : SV_DispatchThreadID)
{
    output.Store(id.x * 4, asuint(depth_samples.Load(int2(0, 0), id.x)));
}

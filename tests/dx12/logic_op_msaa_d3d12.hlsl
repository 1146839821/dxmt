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
struct DepthPoint { float4 position : SV_Position; };
[maxvertexcount(3)]
void depth_geometry(triangle DepthPoint points[3], inout TriangleStream<DepthPoint> stream)
{
    for (uint i = 0; i < 3; ++i) stream.Append(points[i]);
}
struct DepthFactors { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
DepthFactors depth_factors(InputPatch<DepthPoint, 3> points)
{
    DepthFactors result;
    result.edge[0] = result.edge[1] = result.edge[2] = result.inside = 1;
    return result;
}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("depth_factors")][maxtessfactor(1)]
DepthPoint depth_hull(InputPatch<DepthPoint, 3> points, uint id : SV_OutputControlPointID)
{
    return points[id];
}
[domain("tri")]
DepthPoint depth_domain(DepthFactors factors, float3 bary : SV_DomainLocation,
                       const OutputPatch<DepthPoint, 3> points)
{
    DepthPoint result;
    result.position = points[0].position * bary.x + points[1].position * bary.y + points[2].position * bary.z;
    return result;
}

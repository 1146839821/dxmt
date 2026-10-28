#ifdef STAGE_BINDING_DIFFERENTIAL
cbuffer Shared : register(b0) { uint4 values; };
Texture2D<float4> source_texture : register(t0);
SamplerState source_sampler : register(s0);
struct StageVertex { float4 position : SV_Position; float4 color : COLOR; };
float4 read_tag(uint stage)
{
    uint2 texel = uint2(round(source_texture.SampleLevel(source_sampler, float2(0.5, 0.5), 0).rg * 255));
    return float4(values.x + texel.x, values.y + stage, values.z + texel.y, 255);
}
StageVertex vs_main(uint vertex : SV_VertexID)
{
    StageVertex o;
    o.position = float4(vertex == 2 ? 3 : -1, vertex == 1 ? 3 : -1, 0, 1);
    o.color = read_tag(1);
    return o;
}
#ifdef STAGE_TESS_GRID_CONTROL
StageVertex vs_grid_main(uint vertex : SV_VertexID)
{
    const float q = 21845.0 / 65536.0;
    vertex = min(vertex, 6);
    float3 coord = vertex == 0 ? float3(1, 0, 0) :
                   vertex == 1 ? float3(0, 1, 0) :
                   vertex == 2 ? float3(0, 0, 1) :
                   vertex == 3 ? float3(0.5, 0.5, 0) :
                   vertex == 4 ? float3(0, 0.5, 0.5) :
                   vertex == 5 ? float3(0.5, 0, 0.5) :
                                 float3(q, q, 1.0 - 2.0 * q);
    StageVertex o;
    o.position = float4(0, 0.875 - 0.25 * vertex, 0, 1);
    o.color = float4(coord, 1);
    return o;
}
#endif
#ifdef STAGE_TESS_COUNT
float4 ps_main(StageVertex input) : SV_Target0 { return input.color; }
#else
uint4 ps_main(StageVertex input) : SV_Target0 { return uint4(input.color); }
#endif
[maxvertexcount(3)]
void gs_main(triangle StageVertex input[3], inout TriangleStream<StageVertex> stream)
{
    for (uint i = 0; i < 3; ++i) {
        StageVertex o = input[i];
        o.color = read_tag(2);
        o.color.x += input[i].color.x;
        stream.Append(o);
    }
}
#ifdef STAGE_TESS_LINE
struct StageFactors { float edge[2] : SV_TessFactor; };
StageFactors hs_constants(InputPatch<StageVertex, 3> patch, uint primitive : SV_PrimitiveID)
{
    StageFactors factors = {{1, 1}};
    return factors;
}
[domain("isoline")]
[partitioning("integer")]
[outputtopology("line")]
#else
struct StageFactors { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
StageFactors hs_constants(InputPatch<StageVertex, 3> patch, uint primitive : SV_PrimitiveID)
{
#ifdef STAGE_TESS_FACTOR2
    StageFactors factors = {{2, 2, 2}, 2};
#else
    StageFactors factors = {{1, 1, 1}, 1};
#endif
    return factors;
}
[domain("tri")]
[partitioning("integer")]
#ifdef STAGE_TESS_POINT
[outputtopology("point")]
#elif defined(STAGE_TESS_CCW)
[outputtopology("triangle_ccw")]
#else
[outputtopology("triangle_cw")]
#endif
#endif
[outputcontrolpoints(3)]
[patchconstantfunc("hs_constants")]
StageVertex hs_main(InputPatch<StageVertex, 3> patch, uint id : SV_OutputControlPointID)
{
    StageVertex o = patch[id];
    o.color = read_tag(3);
    o.color.x += patch[id].color.x;
    return o;
}
#ifdef STAGE_TESS_LINE
[domain("isoline")]
StageVertex ds_main(StageFactors factors, const OutputPatch<StageVertex, 3> patch, float2 coord : SV_DomainLocation)
#else
[domain("tri")]
StageVertex ds_main(StageFactors factors, const OutputPatch<StageVertex, 3> patch, float3 coord : SV_DomainLocation)
#endif
{
    StageVertex o;
#ifdef STAGE_TESS_POINT
#ifdef STAGE_TESS_GRID
    uint row = coord.x > 0.9 ? 0 : coord.y > 0.9 ? 1 : coord.z > 0.9 ? 2 :
               coord.z < 0.05 ? 3 : coord.x < 0.05 ? 4 : coord.y < 0.05 ? 5 : 6;
    o.position = float4(0, 0.875 - 0.25 * row, 0, 1);
#elif defined(STAGE_TESS_IMAGE)
    o.position = float4(0, coord.x * 1.25 + coord.y * 0.75 - 0.625, 0, 1);
#else
    o.position = float4(0, 0, 0, 1);
#endif
#elif defined(STAGE_TESS_LINE)
#ifdef STAGE_TESS_IMAGE
    o.position = float4(0, 2 - coord.x * 4, 0, 1);
#else
    o.position = float4(coord.x * 2 - 1, 0, 0, 1);
#endif
#else
    o.position = patch[0].position * coord.x + patch[1].position * coord.y + patch[2].position * coord.z;
#endif
#ifdef STAGE_TESS_COORD
    o.color = float4(coord, 1);
#else
    o.color = read_tag(4);
    o.color.x += patch[0].color.x;
#endif
    return o;
}
#ifndef STAGE_BINDING_SM5
struct StagePayload { float4 color; };
#ifdef STAGE_BINDING_AS_MESH
[shader("amplification")]
[numthreads(1, 1, 1)]
void as_main()
{
    StagePayload payload;
    payload.color = read_tag(6);
    DispatchMesh(1, 1, 1, payload);
}
#endif
[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void ms_main(
#ifdef STAGE_BINDING_AS_MESH
    in payload StagePayload payload,
#endif
    out vertices StageVertex vertices[3], out indices uint3 indices[1])
{
    SetMeshOutputCounts(3, 1);
    float4 color = read_tag(5);
#ifdef STAGE_BINDING_AS_MESH
    color = read_tag(7);
    color.x += payload.color.x;
#endif
    for (uint i = 0; i < 3; ++i) {
        vertices[i].position = float4(i == 2 ? 3 : -1, i == 1 ? 3 : -1, 0, 1);
        vertices[i].color = color;
    }
    indices[0] = uint3(0, 1, 2);
}
#endif
#else
struct VSOutput {
  float4 position : SV_Position;
};

[maxvertexcount(3)]
void gs_main(triangle VSOutput input[3], inout TriangleStream<VSOutput> output_stream) {
  [unroll]
  for (uint i = 0; i < 3; i++)
    output_stream.Append(input[i]);
}

struct TessFactors {
  float edge[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

TessFactors hs_constants(InputPatch<VSOutput, 3> patch, uint patch_id : SV_PrimitiveID) {
  TessFactors factors = {{1.0, 1.0, 1.0}, 1.0};
  return factors;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("hs_constants")]
VSOutput hs_main(InputPatch<VSOutput, 3> patch, uint id : SV_OutputControlPointID) {
  return patch[id];
}

[domain("tri")]
VSOutput ds_main(
    TessFactors factors, const OutputPatch<VSOutput, 3> patch, float3 coord : SV_DomainLocation) {
  VSOutput output;
  output.position = patch[0].position * coord.x + patch[1].position * coord.y + patch[2].position * coord.z;
  return output;
}

#endif

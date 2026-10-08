Texture2D<float4> pixel_texture : register(t0);
Texture2D<float4> pre_texture : register(t0, space6);
Texture2D<float4> domain_texture : register(t0, space7);
SamplerState first_sampler : register(s0);
SamplerState second_sampler : register(s1);

float2 reduce_pair(Texture2D<float4> texture, float2 uv)
{
#ifdef USE_GRAD
    return float2(texture.SampleGrad(first_sampler, uv, float2(0.25, 0), float2(0, 0.25)).x,
                  texture.SampleGrad(second_sampler, uv, float2(0.25, 0), float2(0, 0.25)).x);
#else
    return float2(texture.SampleLevel(first_sampler, uv, 0).x,
                  texture.SampleLevel(second_sampler, uv, 0).x);
#endif
}

struct Sampled { float4 position : SV_Position; float2 sampled : TEXCOORD0; };

Sampled vertex(uint id : SV_VertexID)
{
    Sampled result;
    float2 p = float2((id << 1) & 2, id & 2);
    result.position = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    result.sampled = 0;
    return result;
}

[maxvertexcount(3)]
void geometry(triangle Sampled input[3], inout TriangleStream<Sampled> stream)
{
    float2 reduced = reduce_pair(pre_texture, float2(0.5, 0.5));
    for (uint i = 0; i < 3; ++i) {
        Sampled output;
        output.position = input[i].position;
        output.sampled = reduced;
        stream.Append(output);
    }
}

struct Patch {
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
    float contribution : TEXCOORD1;
};
Patch patch_constants(InputPatch<Sampled, 3> input)
{
    Patch result;
    result.edges[0] = result.edges[1] = result.edges[2] = result.inside = 1;
    result.contribution = 4.0 / 255.0;
    return result;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patch_constants")]
[maxtessfactor(1)]
Sampled hull(InputPatch<Sampled, 3> input, uint id : SV_OutputControlPointID)
{
    Sampled result;
    result.position = input[id].position;
    result.sampled = reduce_pair(pre_texture, float2(0.5, 0.5));
    return result;
}

[domain("tri")]
Sampled domain(Patch constants, float3 bary : SV_DomainLocation, const OutputPatch<Sampled, 3> input)
{
    Sampled result;
    result.position = input[0].position * bary.x + input[1].position * bary.y + input[2].position * bary.z;
    // A clamped corner has a distinct oracle from the hull's central footprint.
    result.sampled = (input[0].sampled + reduce_pair(domain_texture, float2(0, 0))) * 0.5 + constants.contribution;
    return result;
}

float4 pixel(Sampled input) : SV_Target0
{
    float2 reduced = reduce_pair(pixel_texture, float2(0.5, 0.5));
    return float4(reduced, input.sampled.x * 0.25 + input.sampled.y * 0.75, 1);
}

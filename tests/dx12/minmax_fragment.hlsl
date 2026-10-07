Texture2D<float4> input_texture : register(t0);
SamplerState first_sampler : register(s0);
SamplerState second_sampler : register(s1);
#ifdef ROOT_UPDATES
cbuffer root_values : register(b0, space8) { uint updated; uint preserved; };
#ifdef ROOT_BUFFERS
cbuffer root_cbv : register(b1, space8) { uint cbv_value; };
ByteAddressBuffer root_srv : register(t1, space8);
RWByteAddressBuffer root_uav : register(u1, space8);
#endif
#endif

float4 vertex(uint id : SV_VertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    float4 result = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
#ifdef VS_SAMPLE
    result.x += input_texture.SampleLevel(first_sampler, float2(0.5, 0.5), 0).x * 0.01;
#endif
    return result;
}

float4 pixel(float4 position : SV_Position) : SV_Target0
{
#if defined(USE_IMPLICIT)
    float2 uv = position.xy * 0.25;
#if defined(USE_DIRECTIONAL_GRAD)
    // A 16:1 footprint: scalar major-axis LOD selects the distinct upper mip,
    // whereas ANISO=16 keeps the symmetric lower-mip footprint.
    float first = input_texture.SampleGrad(first_sampler, float2(0.5, 0.5), float2(2, 0), float2(0, 0.125)).x;
    float second = input_texture.SampleGrad(second_sampler, float2(0.5, 0.5), float2(2, 0), float2(0, 0.125)).x;
#elif defined(USE_VARYING_CLAMP)
    // Adjacent lanes in each quad cross the two-mip view's upper boundary.
    float clamp_lod = (uint(position.x) & 1) ? 0.0 : 3.0;
    float first = input_texture.Sample(first_sampler, uv, int2(0, 0), clamp_lod).x;
    float second = input_texture.Sample(second_sampler, uv, int2(0, 0), clamp_lod).x;
#elif defined(USE_LEVEL)
    float first = input_texture.SampleLevel(first_sampler, uv, 0).x;
    float second = input_texture.SampleLevel(second_sampler, uv, 0).x;
#elif defined(USE_EXPLICIT_GRAD)
    float first = input_texture.SampleGrad(first_sampler, uv, float2(0.25, 0), float2(0, 0.25)).x;
    float second = input_texture.SampleGrad(second_sampler, uv, float2(0.25, 0), float2(0, 0.25)).x;
#elif defined(USE_BIAS)
    float first = input_texture.SampleBias(first_sampler, uv, USE_BIAS).x;
    float second = input_texture.SampleBias(second_sampler, uv, USE_BIAS).x;
#else
    float first = input_texture.Sample(first_sampler, uv).x;
    float second = input_texture.Sample(second_sampler, uv).x;
#endif
#elif defined(USE_GRAD)
    float first = input_texture.SampleGrad(first_sampler, float2(0.5, 0.5), float2(0.25, 0), float2(0, 0.25)).x;
    float second = input_texture.SampleGrad(second_sampler, float2(0.5, 0.5), float2(0.25, 0), float2(0, 0.25)).x;
#else
    float first = input_texture.SampleLevel(first_sampler, float2(0.5, 0.5), 0).x;
    float second = input_texture.SampleLevel(second_sampler, float2(0.5, 0.5), 0).x;
#endif
#ifdef ROOT_UPDATES
    uint blue = updated + preserved;
#ifdef ROOT_BUFFERS
    blue += cbv_value + root_srv.Load(0) + root_uav.Load(0);
#endif
    return float4(first, second, float(blue) / 255.0, 1);
#else
    return float4(first, second, 0, 1);
#endif
}

#ifdef VS_SHARED_REG
Texture2D<float4> vertex_texture : register(t0);
#else
Texture2D<float4> vertex_texture : register(t0, space6);
#endif
float4 vertex_sample(uint id : SV_VertexID, out float2 sampled : TEXCOORD0) : SV_Position
{
#ifdef VS_GRAD
    sampled.x = vertex_texture.SampleGrad(first_sampler, float2(0.5, 0.5), float2(0.25, 0), float2(0, 0.25)).x;
    sampled.y = vertex_texture.SampleGrad(second_sampler, float2(0.5, 0.5), float2(0.25, 0), float2(0, 0.25)).x;
#else
    sampled.x = vertex_texture.SampleLevel(first_sampler, float2(0.5, 0.5), 0).x;
    sampled.y = vertex_texture.SampleLevel(second_sampler, float2(0.5, 0.5), 0).x;
#endif
    return vertex(id);
}

float4 pixel_vertex(float4 position : SV_Position, float2 sampled : TEXCOORD0) : SV_Target0
{
    float4 result = pixel(position);
    result.z = sampled.x * 0.25 + sampled.y * 0.75;
    return result;
}

float4 pixel_vertex_only(float4 position : SV_Position, float2 sampled : TEXCOORD0) : SV_Target0
{
    return float4(sampled, sampled.x * 0.25 + sampled.y * 0.75, 1);
}

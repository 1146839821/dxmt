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
#if defined(USE_LEVEL)
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

Texture2D<float4> input_texture : register(t0);
SamplerState first_sampler : register(s0);
SamplerState second_sampler : register(s1);

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
#ifdef USE_GRAD
    float first = input_texture.SampleGrad(first_sampler, float2(0.5, 0.5), float2(0.25, 0), float2(0, 0.25)).x;
    float second = input_texture.SampleGrad(second_sampler, float2(0.5, 0.5), float2(0.25, 0), float2(0, 0.25)).x;
#else
    float first = input_texture.SampleLevel(first_sampler, float2(0.5, 0.5), 0).x;
    float second = input_texture.SampleLevel(second_sampler, float2(0.5, 0.5), 0).x;
#endif
    return float4(first, second, 0, 1);
}

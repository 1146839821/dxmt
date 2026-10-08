struct VSInput
{
    float2 position : POSITION;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}

uint4 ps_main(VSOutput input) : SV_Target0
{
    return uint4(240, 15, 170, 85);
}

cbuffer LogicSource : register(b0) { uint4 logic_source; };
uint4 ps_root_cbv(VSOutput input) : SV_Target0
{
    return logic_source;
}

Texture2D<float4> reduction_source : register(t0);
SamplerState reduction_sampler : register(s0);
uint4 ps_minmax(VSOutput input) : SV_Target0
{
    uint red = uint(reduction_source.SampleLevel(reduction_sampler, float2(0.5, 0.5), 0).r * 255 + 0.5);
    return uint4(red, 15, 170, 85);
}

uint4 ps_minmax_sample_index(VSOutput input, uint sample : SV_SampleIndex) : SV_Target0
{
    uint red = uint(reduction_source.SampleLevel(reduction_sampler, float2(0.5, 0.5), 0).r * 255 + 0.5);
    return uint4(red ^ sample, 15, 170, 85);
}

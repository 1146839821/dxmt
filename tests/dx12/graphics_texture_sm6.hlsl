Texture2D<float4> input_texture : register(t0);
SamplerState input_sampler : register(s0);

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

float4 ps_main(VSOutput input) : SV_Target0
{
    return input_texture.SampleLevel(input_sampler, float2(0.5, 0.5), 0.0);
}

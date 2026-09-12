StructuredBuffer<float4> root_color : register(t0);

struct VSInput
{
    float2 position : POSITION;
    float4 vertex_color : COLOR;
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
    output.color = root_color[0];
    return output;
}

float4 ps_main(VSOutput input) : SV_Target0
{
    return input.color;
}

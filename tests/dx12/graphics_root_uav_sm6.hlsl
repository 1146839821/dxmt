RWStructuredBuffer<uint> root_output : register(u0);

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
    output.color = input.vertex_color;
    root_output[0] = 0xff00ff00;
    return output;
}

float4 ps_main(VSOutput input) : SV_Target0
{
    return input.color;
}

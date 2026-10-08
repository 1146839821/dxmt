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

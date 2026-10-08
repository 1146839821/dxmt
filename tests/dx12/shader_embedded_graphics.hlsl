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

[RootSignature("RootFlags(0)")]
VSOutput vs_main(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}

[RootSignature("RootFlags(0)")]
float4 ps_main(VSOutput input) : SV_Target0
{
    return input.color;
}

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

[RootSignature("CBV(b0)")]
float4 ps_main(VSOutput input) : SV_Target0
{
    return input.color;
}

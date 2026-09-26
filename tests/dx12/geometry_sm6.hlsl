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

[maxvertexcount(3)]
void gs_main(triangle VSOutput input[3], inout TriangleStream<VSOutput> stream)
{
    for (uint i = 0; i < 3; ++i)
    {
        VSOutput output = input[i];
        output.color = float4(0.0, 1.0, 0.0, 1.0);
        stream.Append(output);
    }
}

float4 ps_main(VSOutput input) : SV_Target0
{
    return input.color;
}

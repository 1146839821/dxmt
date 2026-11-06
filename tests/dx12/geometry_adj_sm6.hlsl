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
void gs_main(triangleadj VSOutput input[6], inout TriangleStream<VSOutput> stream)
{
    VSOutput output = input[0];
    output.color = float4(0.0, 1.0, 0.0, 1.0);
    stream.Append(output);
    output = input[2];
    output.color = float4(0.0, 1.0, 0.0, 1.0);
    stream.Append(output);
    output = input[4];
    output.color = float4(0.0, 1.0, 0.0, 1.0);
    stream.Append(output);
}

float4 ps_main(VSOutput input) : SV_Target0
{
    return input.color;
}

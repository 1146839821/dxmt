#ifdef VERTEX_STRIDE_DIFFERENTIAL
cbuffer DrawConstants : register(b0) { uint row; };
struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation uint4 value : COLOR;
};
VSOutput vs_main(float3 data : POSITION, uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    VSOutput o;
    float2 pixel = float2(vertex + instance * 64 + 0.5, row + 0.5);
    o.position = float4(pixel.x / 128.0 - 1, 1 - pixel.y / 2.0, 0, 1);
    o.value = uint4(uint3(data), 255);
    return o;
}
uint4 ps_main(VSOutput input) : SV_Target0
{
    return input.value;
}
#else
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
    return input.color;
}

#endif

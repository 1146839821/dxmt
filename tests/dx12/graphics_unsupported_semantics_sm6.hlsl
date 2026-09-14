struct VSInput
{
    float2 position : POSITION;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;
    nointerpolation float attribute : ATTRIBUTE0;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.attribute = input.color.x;
    return output;
}

float4 ps_view_id(float4 position : SV_Position, uint view_id : SV_ViewID) : SV_Target0
{
    return float4(view_id ? 1.0 : 0.0, 0.0, 0.0, 1.0);
}

float4 ps_get_attribute(VSOutput input, float3 barycentrics : SV_Barycentrics) : SV_Target0
{
    return float4(GetAttributeAtVertex(input.attribute, 0), barycentrics.x, 0.0, 1.0);
}

float4 ps_shading_rate(float4 position : SV_Position, uint shading_rate : SV_ShadingRate) : SV_Target0
{
    return float4(shading_rate / 255.0, 0.0, 0.0, 1.0);
}

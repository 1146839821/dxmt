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

float4 ps_helper_lane(VSOutput input) : SV_Target0
{
    return IsHelperLane() ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);
}

float4 ps_helper_lane_derivative(VSOutput input) : SV_Target0
{
    float2 gradient = float2(ddx(input.position.x), ddy(input.position.y));
    if (gradient.x < 0.0 || gradient.y < 0.0)
        discard;
    return IsHelperLane() ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);
}

float4 ps_helper_lane_discard(VSOutput input) : SV_Target0
{
    float2 gradient = float2(ddx(input.position.x), ddy(input.position.y));
    if (input.position.x < 0.25 || gradient.x < 0.0)
        discard;
    return IsHelperLane() ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);
}

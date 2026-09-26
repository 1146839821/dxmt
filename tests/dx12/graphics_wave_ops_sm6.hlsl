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

float4 ps_wave_quad(VSOutput input) : SV_Target0
{
    const float value = input.position.x + input.position.y * 100.0;
    const float across_x = QuadReadAcrossX(value);
    const float across_y = QuadReadAcrossY(value);
    const float diagonal = QuadReadAcrossDiagonal(value);
    const float dx = abs(ddx(value));
    const float dy = abs(ddy(value));
    const bool passed = abs(abs(across_x - value) - dx) < 0.1 &&
                        abs(abs(across_y - value) - dy) < 0.1 &&
                        abs(abs(diagonal - value) - (dx + dy)) < 0.1;
    return passed ? float4(0.0, 1.0, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);
}

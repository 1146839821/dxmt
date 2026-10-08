cbuffer RootColor : register(b0) { float4 color; };
struct Input { float2 position : POSITION; float4 vertex_color : COLOR; };
struct Output { float4 position : SV_Position; };
Output vs_main(Input input) {
    Output output;
    output.position = float4(input.position, 0, 1);
    return output;
}
float4 ps_main(Output input) : SV_Target0 { return color; }

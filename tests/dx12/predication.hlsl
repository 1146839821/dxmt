struct VSOutput {
    float4 position : SV_Position;
};

VSOutput vs_main(uint vertex_id : SV_VertexID) {
    VSOutput output;
    if (vertex_id == 0)
        output.position = float4(-1.0, -1.0, 0.0, 1.0);
    else if (vertex_id == 1)
        output.position = float4(3.0, -1.0, 0.0, 1.0);
    else
        output.position = float4(-1.0, 3.0, 0.0, 1.0);
    return output;
}

float4 ps_main(VSOutput input) : SV_Target0 {
    return float4(1.0, 0.0, 0.0, 1.0);
}

RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID) {
    output[0] = 0xc0def00d;
}

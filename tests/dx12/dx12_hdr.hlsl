struct VS_Output {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};

VS_Output vs_main(uint id : SV_VertexID) {
  VS_Output output;
  output.uv = float2((id << 1) & 2, id & 2);
  output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return output;
}

float3 grid_value(float2 uv) {
  float2 coord = floor(uv * float2(10.0, 10.0));
  float value = coord.y * 10.0 + coord.x;
  return float3(value, value, value);
}

float4 ps_main_linear(VS_Output input) : SV_TARGET {
  return float4(grid_value(input.uv), 1.0);
}

float linear_to_pq(float L) {
  float m1 = 2610.0 / 4096.0 / 4.0;
  float m2 = 2523.0 / 4096.0 * 128.0;
  float c1 = 3424.0 / 4096.0;
  float c2 = 2413.0 / 4096.0 * 32.0;
  float c3 = 2392.0 / 4096.0 * 32.0;
  float Lp = pow(L, m1);
  return pow((c1 + c2 * Lp) / (1.0 + c3 * Lp), m2);
}

float4 ps_main_pq(VS_Output input) : SV_TARGET {
  float3 value = grid_value(input.uv) / 100.0;
  return float4(linear_to_pq(value.r), linear_to_pq(value.g), linear_to_pq(value.b), 1.0);
}

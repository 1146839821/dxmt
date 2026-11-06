cbuffer Parameters : register(b0) {
  float4 color;
};

float4 main() : SV_Target0 {
  return color;
}

float4 branch_main() : SV_Target0 {
  if (color.x > 0.5)
    return float4(1, 0, 0, 1);
  return float4(0, 1, 0, 1);
}

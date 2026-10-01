float4 vs_main(uint vertex : SV_VertexID) : SV_Position {
  return float4(float(vertex), 0, 0, 1);
}
float4 ps_main() : SV_Target {
  return float4(1, 0, 0, 1);
}

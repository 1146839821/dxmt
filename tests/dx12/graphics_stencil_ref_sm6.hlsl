struct PSOutput {
  float4 color : SV_Target0;
  uint stencil_ref : SV_StencilRef;
};

PSOutput ps_main(float4 position : SV_Position) {
  PSOutput output;
  output.color = float4(1.0, 0.0, 1.0, 1.0);
  output.stencil_ref = 1;
  return output;
}

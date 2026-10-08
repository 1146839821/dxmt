Buffer<uint> source : register(t0);
struct Control { float4 p : POSITION; };
struct Raster { float4 p : SV_Position; };

Control vertex(uint id : SV_VertexID) {
  Control o;
  o.p = float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, source.Load(0) / 256.0, 1);
  return o;
}
[maxvertexcount(3)]
void geometry(triangle Control input[3], inout TriangleStream<Raster> output) {
  for (uint i = 0; i < 3; ++i) {
    Raster o; o.p = input[i].p; o.p.z = source.Load(0) / 256.0; output.Append(o);
  }
  output.RestartStrip();
}
struct Factors { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
Factors patch(InputPatch<Control, 3> input) {
  Factors o;
  float factor = source.Load(0);
  o.edge[0] = o.edge[1] = o.edge[2] = o.inside = factor;
  return o;
}
[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patch")]
[maxtessfactor(4.0)]
Control hull(InputPatch<Control, 3> input, uint id : SV_OutputControlPointID) {
  Control o = input[id]; o.p.z += source.Load(1) / 256.0; return o;
}
[domain("tri")]
Raster domain(Factors factors, float3 uv : SV_DomainLocation, const OutputPatch<Control, 3> input) {
  Raster o;
  o.p = uv.x * input[0].p + uv.y * input[1].p + uv.z * input[2].p;
  o.p.z = source.Load(0) / 256.0; return o;
}

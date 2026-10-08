// Each stage sees its own t0 descriptor table. Stage-distinct data makes global
// origin-record aliasing observable even though all register identities match.
#ifndef ORDINARY
#ifdef STRUCTURED_BUFFER
StructuredBuffer<uint> source : register(t0);
#else
Buffer<uint> source : register(t0);
#endif
#endif
#ifndef STAGE_WEIGHT
#define STAGE_WEIGHT 1
#endif
cbuffer Guard : register(b3) { uint magic; };
struct Control { float4 p : POSITION; };
struct Raster { float4 p : SV_Position; };
float contribution() {
#ifdef ORDINARY
  return 0;
#else
#ifdef STRUCTURED_BUFFER
  uint count, stride; source.GetDimensions(count, stride);
  uint a = count > 0 ? source[0] : 0;
  uint b = count > 1 ? source[1] : 0;
  uint c = count > 2 ? source[2] : 0;
  return STAGE_WEIGHT * (a + 2 * b + 4 * c) / 4096.0;
#else
  return STAGE_WEIGHT * (source.Load(0) + 2 * source.Load(1) + 4 * source.Load(2)) / 4096.0;
#endif
#endif
}
Control vertex(uint id : SV_VertexID) {
  Control o;
#ifdef ORDINARY
  float depth = 0.75;
#else
  float depth = contribution();
#endif
  o.p = float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, magic == 0xabc123 ? depth : 0.99, 1);
  return o;
}
[maxvertexcount(3)]
void geometry(triangle Control input[3], inout TriangleStream<Raster> output) {
  for (uint i = 0; i < 3; ++i) {
    Raster o; o.p = input[i].p;
    o.p.z += magic == 0xabc123 ? contribution() : 0.5;
    output.Append(o);
  }
  output.RestartStrip();
}
struct Factors { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
Factors patch(InputPatch<Control, 3> input) {
  Factors o;
  o.edge[0] = o.edge[1] = o.edge[2] = o.inside = magic == 0xabc123 ? 1 : 0;
  return o;
}
[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patch")]
[maxtessfactor(4.0)]
Control hull(InputPatch<Control, 3> input, uint id : SV_OutputControlPointID) {
  Control o = input[id];
  o.p.z += magic == 0xabc123 ? contribution() : 0.5;
  return o;
}
[domain("tri")]
Raster domain(Factors factors, float3 uv : SV_DomainLocation, const OutputPatch<Control, 3> input) {
  Raster o;
  o.p = uv.x * input[0].p + uv.y * input[1].p + uv.z * input[2].p;
  o.p.z += magic == 0xabc123 ? contribution() : 0.5;
  return o;
}

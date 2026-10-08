struct VSOutput {
  float4 position : SV_Position;
};

[maxvertexcount(3)]
void gs_main(triangle VSOutput input[3], inout TriangleStream<VSOutput> output_stream) {
  [unroll]
  for (uint i = 0; i < 3; i++)
    output_stream.Append(input[i]);
}

struct TessFactors {
  float edge[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

TessFactors hs_constants(InputPatch<VSOutput, 3> patch, uint patch_id : SV_PrimitiveID) {
  TessFactors factors = {{1.0, 1.0, 1.0}, 1.0};
  return factors;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("hs_constants")]
VSOutput hs_main(InputPatch<VSOutput, 3> patch, uint id : SV_OutputControlPointID) {
  return patch[id];
}

[domain("tri")]
VSOutput ds_main(
    TessFactors factors, const OutputPatch<VSOutput, 3> patch, float3 coord : SV_DomainLocation) {
  VSOutput output;
  output.position = patch[0].position * coord.x + patch[1].position * coord.y + patch[2].position * coord.z;
  return output;
}

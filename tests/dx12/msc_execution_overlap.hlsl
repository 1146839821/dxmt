// Texture mode: compute writes a texture read only by Fragment, while Vertex
// calculates an independent checksum. VERTEX_INPUT mode: compute produces the
// checksum fetched through the vertex buffer. Both reach an exact CPU oracle.
cbuffer Parameters : register(b0) {
  uint seed;
  uint iterations;
};

uint mix_value(uint value) {
  [loop] for (uint i = 0; i < iterations; ++i)
    value = (value ^ (value >> 13)) * 1664525u + 1013904223u;
  return value;
}

#if defined(COMPUTE)
#if defined(VERTEX_INPUT)
RWStructuredBuffer<uint> output_vertices : register(u0);
[numthreads(64, 1, 1)]
void cs_main(uint3 id : SV_DispatchThreadID) {
  output_vertices[id.x] = mix_value(seed + id.x / 6 + 0x12345u);
}
#else
RWTexture2D<uint> output_texture : register(u0);
[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID) {
  output_texture[id.xy] = mix_value(seed ^ (id.y * 128 + id.x));
}
#endif
#else
struct VertexOutput {
  float4 position : SV_Position;
  nointerpolation uint checksum : TEXCOORD0;
};

VertexOutput vs_main(uint id : SV_VertexID
#if defined(VERTEX_INPUT)
                     , uint checksum : TEXCOORD0
#endif
                     ) {
  // Arithmetic corners avoid the separately tracked MSC dynamic-constant-array
  // path. Texture mode has no vertex resource dependency; VERTEX_INPUT uses IA.
  uint local = id % 6;
  uint2 corner = uint2((0x32u >> local) & 1u, (0x2cu >> local) & 1u);
  uint cell = id / 6;
  uint2 grid = uint2(cell % 32, cell / 32) + corner;
  VertexOutput result;
  result.position = float4(float(grid.x) / 16 - 1,
                           1 - float(grid.y) / 16, 0, 1);
#if defined(VERTEX_INPUT)
  result.checksum = checksum;
#else
  result.checksum = mix_value(seed + cell + 0x12345u);
#endif
  return result;
}

#if !defined(VERTEX_INPUT)
Texture2D<uint> input_texture : register(t0);
#endif
uint ps_main(VertexOutput input) : SV_Target {
#if defined(VERTEX_INPUT)
  return input.checksum;
#else
  return input_texture.Load(int3(uint2(input.position.xy), 0)) ^ input.checksum;
#endif
}
#endif

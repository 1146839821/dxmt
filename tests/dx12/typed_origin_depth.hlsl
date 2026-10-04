Buffer<uint> source : register(t0);
cbuffer VertexData : register(b3) { uint vertexMagic; };

float4 position(uint id, float depth) {
  if (id > 2) return float4(-1, -1, 2, 1);
  float4 p = float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, depth, 1);
  if (vertexMagic != 0xabc123) p.x += 10;
  return p;
}

float4 typed_vertex(uint id : SV_VertexID) : SV_Position {
  // Both origin and count matter. The second load must return zero for count1.
  return position(id, float(source.Load(0) + source.Load(1) * 2) / 256.0);
}

float4 ordinary_vertex(uint id : SV_VertexID) : SV_Position {
  return position(id, 0.75);
}

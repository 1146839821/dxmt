Buffer<uint> source : register(t0);
RWBuffer<uint> destination : register(u0);
cbuffer VertexData : register(b3) { uint vertexMagic; };

float4 vs(uint id : SV_VertexID) : SV_Position {
  float4 position = float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);
  if (vertexMagic != 0xabc123) position.x += 10;
  return position;
}

uint ps(float4 position : SV_Position) : SV_Target {
  uint i = uint(position.x);
  uint value = source.Load(i) + destination[i];
  destination[i] = value + 100;
  return value;
}

uint ordinary(float4 position : SV_Position) : SV_Target { return 77; }

float4 typed_vs(uint id : SV_VertexID) : SV_Position {
  float4 position = vs(id);
  position.x += float(source.Load(id));
  return position;
}

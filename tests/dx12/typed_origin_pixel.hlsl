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

struct OriginVertex {
  float4 position : SV_Position;
  nointerpolation uint originValue : TEXCOORD0;
};

#ifdef DXMT_TYPED_ORIGIN_EMBEDDED
#define ORIGIN_ROOT [RootSignature("DescriptorTable(SRV(t0,offset=0,flags=DATA_VOLATILE),UAV(u0,offset=1,flags=DATA_VOLATILE)),RootConstants(num32BitConstants=1,b3,visibility=SHADER_VISIBILITY_VERTEX)")]
#else
#define ORIGIN_ROOT
#endif

ORIGIN_ROOT
OriginVertex typed_vs(uint id : SV_VertexID) {
  OriginVertex vertex;
  vertex.position = vs(id);
  vertex.originValue = source.Load(0) + source.Load(1) * 2;
  return vertex;
}

uint vertex_ps(OriginVertex vertex) : SV_Target {
  return vertex.originValue * 10 + uint(vertex.position.x);
}

ORIGIN_ROOT
uint combined_ps(OriginVertex vertex) : SV_Target {
  uint i = uint(vertex.position.x);
  uint value = vertex.originValue * 10 + source.Load(i) + destination[i];
  destination[i] = value + 100;
  return value;
}

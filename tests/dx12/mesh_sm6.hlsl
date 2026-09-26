struct MeshVertex {
  float4 position : SV_Position;
};

cbuffer MeshConstants : register(b0) {
  uint mesh_value;
};

RWStructuredBuffer<uint> MeshOutput : register(u0);

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(
    in uint group_id : SV_GroupID,
    out vertices MeshVertex vertices[3],
    out indices uint3 indices[1]) {
  MeshOutput[0] = mesh_value + 1;
  SetMeshOutputCounts(3, 1);
  vertices[0].position = float4(-1.0, -1.0, 0.0, 1.0);
  vertices[1].position = float4(3.0, -1.0, 0.0, 1.0);
  vertices[2].position = float4(-1.0, 3.0, 0.0, 1.0);
  indices[0] = uint3(0, 1, 2);
}

float4 ps_main() : SV_Target {
  return float4(1.0, 0.0, 0.0, 1.0);
}

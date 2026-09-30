struct MeshPayload {
  uint value;
};

[numthreads(1, 1, 1)]
void main() {
  MeshPayload payload = {0};
  DispatchMesh(1, 1, 1, payload);
}

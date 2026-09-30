struct Payload {
  uint value;
};

[shader("raygeneration")]
void RayGen() {
  RWStructuredBuffer<uint> output = ResourceDescriptorHeap[0];
  RaytracingAccelerationStructure scene = ResourceDescriptorHeap[1];
  const uint index = DispatchRaysIndex().x;

  RayDesc ray;
  ray.Origin = index == 0 ? float3(0.0, 0.0, -1.0) : float3(2.0, 2.0, -1.0);
  ray.Direction = float3(0.0, 0.0, 1.0);
  ray.TMin = 0.0;
  ray.TMax = 10.0;

  Payload payload = {0};
  TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
  output[index] = payload.value;
}

[shader("miss")]
void Miss(inout Payload payload) {
  payload.value = 0x40;
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes) {
  CallShader(0, payload);
  payload.value |= 0x20;
}

[shader("anyhit")]
void AnyHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes) {
  payload.value |= 0x10;
}

[shader("callable")]
void Callable(inout Payload payload) {
  payload.value |= 0x01;
}

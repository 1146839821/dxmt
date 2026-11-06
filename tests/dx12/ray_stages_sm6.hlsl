struct Payload {
  uint value;
};

struct Attributes {
  float2 barycentrics;
};

[shader("raygeneration")]
void RayGen() {
}

[shader("miss")]
void Miss(inout Payload payload) {
  payload.value += 1;
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, in Attributes attributes) {
  payload.value += (uint)attributes.barycentrics.x;
}

[shader("anyhit")]
void AnyHit(inout Payload payload, in Attributes attributes) {
  payload.value += (uint)attributes.barycentrics.y;
}

[shader("intersection")]
void Intersection() {
}

[shader("callable")]
void Callable(inout Payload payload) {
  payload.value += 1;
}

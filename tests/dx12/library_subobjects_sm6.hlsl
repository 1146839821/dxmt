struct Payload {
  uint value;
};

[shader("raygeneration")]
void RayGen() {
}

[shader("miss")]
void Miss(inout Payload payload) {
  payload.value += 1;
}

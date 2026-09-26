struct [raypayload] Payload {
  uint value : read(caller) : write(caller,miss);
};

[shader("miss")]
void Miss(inout Payload payload) {
  payload.value += 1;
}

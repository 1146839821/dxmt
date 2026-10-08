Buffer<uint> Inputs[2] : register(t3);
RWBuffer<uint> Outputs[2] : register(u5);
cbuffer Selection : register(b0) { uint Index; }

[numthreads(1, 1, 1)]
void main() {
  Outputs[Index][0] = Inputs[Index][0] + Inputs[Index][1] + 17;
  Outputs[Index][1] = 123;
}

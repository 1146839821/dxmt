cbuffer Values : register(b0) { uint value; }
StructuredBuffer<uint> input : register(t0);
RWStructuredBuffer<uint> output : register(u0);
cbuffer Position : register(b1) { uint slot; }
[numthreads(1, 1, 1)]
void main() { output[slot] = value + input[0]; }

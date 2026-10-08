cbuffer C : register(b0) { uint slot; uint value; uint keep; };
RWStructuredBuffer<uint> output : register(u0);
[numthreads(1, 1, 1)]
void main() { output[slot] = value + keep; }

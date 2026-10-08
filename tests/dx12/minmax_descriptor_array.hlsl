Texture2D<float4> Inputs[2] : register(t0);
SamplerState Samplers[2] : register(s0);
RWStructuredBuffer<uint> Output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  Output[0] = uint(round(Inputs[1].SampleLevel(Samplers[1], float2(0.5, 0.5), 0).r * 255));
}

Texture2D<float> textures[5] : register(t0);
SamplerState first : register(s0);
SamplerState second : register(s1);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(5, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x & 1)
    output[id.x] = asuint(textures[id.x].SampleLevel(second, float2(0.5, 0.5), 1.25));
  else
    output[id.x] = asuint(textures[id.x].SampleLevel(first, float2(0.5, 0.5), 1.25));
}

Texture2D<float4> input_texture : register(t0);
SamplerState input_sampler : register(s0);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float4 value = input_texture.SampleLevel(input_sampler, float2(0.5, 0.5), 0.0);
    output[0] = (uint)(value.x * 255.0 + 0.5);
}

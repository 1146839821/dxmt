Texture2D<float4> input_texture : register(t0);
SamplerState input_sampler : register(s0);
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    output[0] = (uint)(input_texture.SampleLevel(input_sampler, float2(0.5, 0.5), 0).x * 255 + 0.5);
}

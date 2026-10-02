Texture2D<float4> input_texture : register(t0);
SamplerState first_sampler : register(s0);
SamplerState second_sampler : register(s1);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    [loop]
    for (uint iteration = 0; iteration < 2; ++iteration) {
        float4 first = input_texture.SampleLevel(first_sampler, float2(0.5, 0.5), 0.0);
        float4 second = input_texture.SampleLevel(second_sampler, float2(0.5, 0.5), 0.0);
        output[iteration * 2] = (uint)(first.x * 255.0 + 0.5);
        output[iteration * 2 + 1] = (uint)(second.x * 255.0 + 0.5);
    }
}

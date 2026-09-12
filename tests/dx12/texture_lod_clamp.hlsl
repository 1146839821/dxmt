Texture2D<float> input0 : register(t0);
Texture2D<float> input1 : register(t1);
Texture2D<float> input2 : register(t2);
Texture2D<float> input3 : register(t3);
Texture2D<float> input_most_detailed : register(t4);
SamplerState input_sampler : register(s0);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    const float2 uv = float2(0.5, 0.5);
    output[0] = asuint(input0.SampleLevel(input_sampler, uv, 0.0));
    output[1] = asuint(input1.SampleLevel(input_sampler, uv, 0.0));
    output[2] = asuint(input2.SampleLevel(input_sampler, uv, 0.0));
    output[3] = asuint(input3.SampleLevel(input_sampler, uv, 0.0));
    output[4] = asuint(input_most_detailed.SampleLevel(input_sampler, uv, 0.0));
}

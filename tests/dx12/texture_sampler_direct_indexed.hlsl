[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    Texture2D<float4> input_texture = ResourceDescriptorHeap[0];
    SamplerState input_sampler = SamplerDescriptorHeap[0];
    RWStructuredBuffer<uint> output = ResourceDescriptorHeap[1];
    float4 value = input_texture.SampleLevel(input_sampler, float2(0.5, 0.5), 0.0);
    output[0] = (uint)(value.x * 255.0 + 0.5);
}

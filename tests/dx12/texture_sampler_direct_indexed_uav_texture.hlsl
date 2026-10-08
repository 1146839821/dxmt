[numthreads(1, 1, 1)]
void main()
{
    Texture2D<float4> input_texture = ResourceDescriptorHeap[0];
    SamplerState input_sampler = SamplerDescriptorHeap[0];
    RWStructuredBuffer<uint> output = ResourceDescriptorHeap[1];
    RWTexture2D<uint> output_texture = ResourceDescriptorHeap[2];
    float4 value = input_texture.SampleLevel(input_sampler, float2(0.5, 0.5), 0.0);
    uint result = (uint)(value.x * 255.0 + 0.5);
    output[0] = result;
    output_texture[uint2(0, 0)] = result;
}

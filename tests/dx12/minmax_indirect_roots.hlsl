Texture2D<float4> input_texture : register(t0);
SamplerState first_sampler : register(s0);
SamplerState second_sampler : register(s1);
cbuffer RootConstants : register(b1) { uint addend; uint unchanged; };
cbuffer RootCBV : register(b2) { uint cbv_addend; };
ByteAddressBuffer root_srv : register(t1);
RWByteAddressBuffer root_uav : register(u1);

[numthreads(1, 1, 1)]
void main()
{
    uint bias = addend + unchanged + cbv_addend + root_srv.Load(0);
    float first = input_texture.SampleLevel(first_sampler, float2(0.5, 0.5), 0).x;
    float second = input_texture.SampleLevel(second_sampler, float2(0.5, 0.5), 0).x;
    root_uav.Store(0, (uint)(first * 255 + 0.5) + bias);
    root_uav.Store(4, (uint)(second * 255 + 0.5) + bias);
}

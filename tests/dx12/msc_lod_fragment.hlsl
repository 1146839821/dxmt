Texture2D<float4> image : register(t0);
SamplerState image_sampler : register(s0);

float4 main(float4 position : SV_Position) : SV_Target {
    return image.SampleLevel(image_sampler, float2(0.5, 0.5), 0.0);
}

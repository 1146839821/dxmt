Texture2D<float4> input_texture : register(t0);
SamplerState input_sampler : register(s0);
RWStructuredBuffer<uint> output : register(u0);

#if defined(TYPED_UAV_UINT) || defined(TYPED_UAV_PACKED_UINT)
#define UAV_VALUE uint4
#elif defined(TYPED_UAV_SINT)
#define UAV_VALUE int4
#else
#define UAV_VALUE float4
#endif

#if defined(TYPED_UAV_UINT) || defined(TYPED_UAV_SINT) || defined(TYPED_UAV_FLOAT) || defined(TYPED_UAV_SNORM) || defined(TYPED_UAV_PACKED_UINT) || defined(TYPED_UAV_ALPHA)
#define TYPED_UAV
RWTexture2D<UAV_VALUE> typed_output : register(u1);
cbuffer Parameters : register(b0) { uint phase; };
#endif

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float4 value = input_texture.SampleLevel(input_sampler, float2(0.5, 0.5), 0.0);
    float intensity = value.x;
#if defined(TYPED_UAV_ALPHA)
    intensity = all(value.rgb == 0) ? value.a * (255.0 / 128.0) : 0;
#endif
    output[0] = (uint)(intensity * 255.0 + 0.5);
#if defined(TYPED_UAV)
    for (uint i = 0; i < 4; ++i) {
        uint2 location = uint2(i % 2, i / 2);
        if (phase == 0) {
#if defined(TYPED_UAV_PACKED_UINT)
            UAV_VALUE initial = uint4(257 + 128 * i, 513 + 64 * i, 769 - 128 * i, i);
#elif defined(TYPED_UAV_UINT)
            UAV_VALUE initial = uint4(3 + 4 * i, 5 + 4 * i, 7 + 4 * i, 1);
#elif defined(TYPED_UAV_SINT)
            UAV_VALUE initial = int4(-3 - 4 * (int)i, 5 + 4 * (int)i, -7 - 4 * (int)i, 1);
#else
            UAV_VALUE initial = float4(0.25, 0.5, 0.75, 1) + i * float4(0.0625, 0.03125, -0.0625, 0);
#if defined(TYPED_UAV_SNORM)
            initial.xz = -initial.xz;
#endif
#if defined(TYPED_UAV_ALPHA)
            initial.a = (i + 1) * 0.25;
#endif
#endif
            typed_output[location] = initial * (UAV_VALUE)intensity;
        } else {
            UAV_VALUE loaded = typed_output[location];
            for (uint component = 0; component < 4; ++component)
                output[1 + 4 * i + component] = asuint(loaded[component]);
#if defined(TYPED_UAV_UINT) || defined(TYPED_UAV_PACKED_UINT)
            typed_output[location] = loaded + uint4(1, 2, 3, 0);
#elif defined(TYPED_UAV_SINT)
            typed_output[location] = loaded + int4(1, -2, 3, 0);
#elif defined(TYPED_UAV_ALPHA)
            typed_output[location] = loaded * float4(1, 1, 1, 0.5);
#else
            typed_output[location] = loaded * float4(0.5, 0.5, 0.5, 1);
#endif
        }
    }
#endif
}

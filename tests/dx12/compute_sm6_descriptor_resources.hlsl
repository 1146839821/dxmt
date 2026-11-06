cbuffer InputConstants : register(b0)
{
    uint value;
};

#ifdef BGRA_BUFFER
Buffer<float4> input : register(t0);
RWBuffer<uint> output : register(u0);
#elif defined(TYPED_BUFFER)
Buffer<uint> input : register(t0);
RWBuffer<uint> output : register(u0);
#else
StructuredBuffer<uint> input : register(t0);
RWStructuredBuffer<uint> output : register(u0);
#endif

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
#ifdef BGRA_BUFFER
    for (uint i = 0; i < 4; i++) {
        uint4 channels = (uint4)round(input[i] * 255.0);
        output[i] = channels.x | (channels.y << 8) | (channels.z << 16) | (channels.w << 24);
    }
#else
    output[0] = value + input[0];
#endif
}

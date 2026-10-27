cbuffer InputConstants : register(b0)
{
    uint value;
};

#ifdef TYPED_BUFFER
Buffer<uint> input : register(t0);
RWBuffer<uint> output : register(u0);
#else
StructuredBuffer<uint> input : register(t0);
RWStructuredBuffer<uint> output : register(u0);
#endif

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    output[0] = value + input[0];
}

cbuffer InputConstants : register(b0, space1)
{
    uint value;
};

StructuredBuffer<uint> input : register(t0, space1);
RWStructuredBuffer<uint> output : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    output[0] = value + input[0];
}

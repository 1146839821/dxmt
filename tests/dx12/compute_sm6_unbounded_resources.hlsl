StructuredBuffer<uint> inputs[] : register(t0);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    output[0] = inputs[1][0];
}

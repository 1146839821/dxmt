[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    RWStructuredBuffer<uint> output = ResourceDescriptorHeap[0];
    output[0] = 4321;
}

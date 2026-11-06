[numthreads(2, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint descriptor_index = NonUniformResourceIndex(tid.x);
    StructuredBuffer<uint> input = ResourceDescriptorHeap[descriptor_index];
    RWStructuredBuffer<uint> output = ResourceDescriptorHeap[2];
    output[tid.x] = input[0];
}

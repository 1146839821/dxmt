struct InputConstants
{
    uint value;
};

[numthreads(1, 1, 1)]
void main()
{
    ConstantBuffer<InputConstants> constants = ResourceDescriptorHeap[0];
    StructuredBuffer<uint> input = ResourceDescriptorHeap[1];
    RWStructuredBuffer<uint> output = ResourceDescriptorHeap[2];
    output[0] = constants.value + input[0];
}

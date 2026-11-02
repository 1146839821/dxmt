#if defined(TYPED_SRV_UINT)
Buffer<uint> input_data : register(t0);
#elif defined(TYPED_SRV_SINT)
Buffer<int> input_data : register(t0);
#else
Buffer<float> input_data : register(t0);
#endif
RWStructuredBuffer<uint> output_data : register(u0);

[numthreads(1, 1, 1)]
void main() {
    output_data[0] = asuint(input_data[0]);
    output_data[1] = asuint(input_data[0xffffffffu]);
    uint count;
    input_data.GetDimensions(count);
    output_data[2] = count;
}

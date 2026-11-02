#if defined(TYPED_SRV_UINT)
Buffer<uint> input_data : register(t0);
#if defined(TYPED_SRV_MULTI)
Buffer<uint> input_other : register(t1);
#endif
#elif defined(TYPED_SRV_SINT)
Buffer<int> input_data : register(t0);
#if defined(TYPED_SRV_MULTI)
Buffer<int> input_other : register(t1);
#endif
#else
Buffer<float> input_data : register(t0);
#if defined(TYPED_SRV_MULTI)
Buffer<float> input_other : register(t1);
#endif
#endif
RWStructuredBuffer<uint> output_data : register(u0);

[numthreads(1, 1, 1)]
void main() {
    output_data[0] = asuint(input_data[0]);
    output_data[1] = asuint(input_data[0xffffffffu]);
    uint count;
    input_data.GetDimensions(count);
    output_data[2] = count;
#if defined(TYPED_SRV_MULTI)
    output_data[3] = asuint(input_other[0]);
    input_other.GetDimensions(count);
    output_data[4] = count;
    output_data[5] = asuint(input_other[0xffffffffu]);
#endif
}

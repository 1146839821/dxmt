RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    const float quiet_nan = asfloat(0x7fc00001);
    const float positive_inf = asfloat(0x7f800000);
    const float negative_inf = asfloat(0xff800000);
    uint result = 0;

    result |= isnan(quiet_nan) ? 1 : 0;
    result |= isinf(positive_inf) ? 2 : 0;
    result |= isinf(negative_inf) ? 4 : 0;
    result |= isnan(quiet_nan + 1.0) ? 8 : 0;
    result |= isinf(positive_inf + positive_inf) ? 16 : 0;
    result |= (quiet_nan == quiet_nan) ? 32 : 0;
    output[0] = result;
}

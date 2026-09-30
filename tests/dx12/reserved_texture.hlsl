Texture2D<uint> input_texture : register(t0);
RWStructuredBuffer<uint> output : register(u0);
RWTexture2D<uint> sparse_output : register(u1);

[numthreads(1, 1, 1)]
void read_phase()
{
    output[0] = input_texture.Load(int3(0, 0, 0));
    output[1] = input_texture.Load(int3(128, 0, 0));
}

[numthreads(1, 1, 1)]
void write_phase()
{
    sparse_output[uint2(0, 0)] = 0xabcdef01;
    output[0] = 0xfeedface;
    output[1] = 0;
}

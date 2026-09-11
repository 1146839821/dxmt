Texture2D<uint> input_texture : register(t0);
RWStructuredBuffer<uint> output : register(u0);
RWTexture2D<uint> sparse_tail : register(u1);

[numthreads(1, 1, 1)]
void read_phase()
{
    output[0] = input_texture.Load(int3(0, 0, 1));
    output[1] = input_texture.Load(int3(31, 31, 1));
}

[numthreads(1, 1, 1)]
void write_phase_a()
{
    sparse_tail[uint2(0, 0)] = 0x11112222;
    sparse_tail[uint2(31, 31)] = 0x11112222;
}

[numthreads(1, 1, 1)]
void write_phase_b()
{
    sparse_tail[uint2(0, 0)] = 0x33334444;
    sparse_tail[uint2(31, 31)] = 0x33334444;
}

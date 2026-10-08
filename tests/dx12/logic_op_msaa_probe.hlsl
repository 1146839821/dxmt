// Compiler probe only: no MSAA GPU correctness claim.
// Match the public MSC framebuffer-fetch ABI without an SDK include path.
Texture2D<uint4> framebuffer : register(t0, space7);

uint4 pixel_frequency(float4 position : SV_Position) : SV_Target0
{
    return framebuffer.Load(int3(0, 0, 0)) ^ uint4(240, 15, 170, 85);
}

uint4 sample_frequency(float4 position : SV_Position, uint sample : SV_SampleIndex) : SV_Target0
{
    // Keep the sample input live so its conversion can be inspected.
    return framebuffer.Load(int3(0, 0, 0)) ^ uint4(sample, 15, 170, 85);
}

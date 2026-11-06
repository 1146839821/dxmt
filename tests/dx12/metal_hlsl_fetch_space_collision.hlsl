// Deliberately ordinary texture binding in the test-only framebuffer-fetch space.
Texture2D<float4> Ordinary : register(t0, space2147420893);
float4 main() : SV_Target0 { return Ordinary.Load(int3(0, 0, 0)); }

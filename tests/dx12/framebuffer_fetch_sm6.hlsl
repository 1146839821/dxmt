Texture2D<float4> framebuffer_float : register(t0, space7);
Texture2D<half4> framebuffer_half : register(t1, space7);
Texture2D<int4> framebuffer_int : register(t2, space7);
Texture2D<uint4> framebuffer_uint : register(t3, space7);

float4 main() : SV_Target0 {
    float4 color = framebuffer_float.Load(int3(0, 0, 0));
    color += float4(framebuffer_half.Load(int3(0, 0, 0)));
    color += float4(framebuffer_int.Load(int3(0, 0, 0)));
    color += float4(framebuffer_uint.Load(int3(0, 0, 0)));
    return color;
}

#define MTL_FRAMEBUFFER_FETCH_SPACE 2147420893
#include "../../include/metal_shader_converter/Metal_HLSL.inc"
float4 main() : SV_Target0 { return MTL_LOAD_FRAMEBUFFER(0, float4) * 0.5 + float4(0.25, 0, 0, 0); }

#define MTL_FUNCTION_CONSTANT_SPACE 2147420894
#include "../../include/metal_shader_converter/Metal_HLSL.inc"
MTL_FUNCTION_CONSTANT(float4, ConstantColor, 0);
float4 main() : SV_Target0 { return ConstantColor; }

#define MTL_FUNCTION_CONSTANT_SPACE 2147420894
#define MTL_FRAMEBUFFER_FETCH_SPACE 2147420893
#include "../../include/metal_shader_converter/Metal_HLSL.inc"
MTL_FUNCTION_CONSTANT(float4, ConstantColor, 0);
#ifdef DXMT_TEST_ORDINARY_RESOURCE
cbuffer Ordinary : register(b0) { float4 OrdinaryColor; }
[RootSignature("RootConstants(num32BitConstants=4, b0), DescriptorTable(CBV(b0, space=2147420894), SRV(t0, space=2147420893))")]
#elif defined(DXMT_TEST_EMBEDDED_ROOT)
[RootSignature("DescriptorTable(CBV(b0, space=2147420894), SRV(t0, space=2147420893))")]
#endif
float4 main() : SV_Target0 {
  float4 result = MTL_LOAD_FRAMEBUFFER(0, float4) * 0.5 + ConstantColor;
#ifdef DXMT_TEST_ORDINARY_RESOURCE
  result += OrdinaryColor;
#endif
  return result;
}

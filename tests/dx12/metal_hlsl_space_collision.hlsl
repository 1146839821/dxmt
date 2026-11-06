// Deliberately ordinary HLSL binding in the test-only function-constant space.
cbuffer Ordinary : register(b1, space2147420894) { float4 Color; }
float4 main() : SV_Target0 { return Color; }

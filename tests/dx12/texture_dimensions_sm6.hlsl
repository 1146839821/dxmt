Texture1D<float4> texture_1d : register(t0);
Texture1DArray<float4> texture_1d_array : register(t1);
Texture2D<float4> texture_2d : register(t2);
Texture2DArray<float4> texture_2d_array : register(t3);
TextureCube<float4> texture_cube : register(t4);
TextureCubeArray<float4> texture_cube_array : register(t5);
Texture2DMS<float4> texture_ms : register(t6);
Texture2DMSArray<float4> texture_ms_array : register(t7);
Texture3D<float4> texture_3d : register(t8);
SamplerState texture_sampler : register(s0);
RWStructuredBuffer<uint> output : register(u0);

uint ToByte(float4 value) {
    return (uint)(value.x * 255.0 + 0.5);
}

[numthreads(1, 1, 1)]
void main() {
    output[0] = ToByte(texture_1d.SampleLevel(texture_sampler, 0.5, 0.0));
    output[1] = ToByte(texture_1d_array.SampleLevel(texture_sampler, float2(0.5, 1.0), 0.0));
    output[2] = ToByte(texture_2d.SampleLevel(texture_sampler, float2(0.5, 0.5), 0.0));
    output[3] = ToByte(texture_2d_array.SampleLevel(texture_sampler, float3(0.5, 0.5, 1.0), 0.0));
    output[4] = ToByte(texture_cube.SampleLevel(texture_sampler, float3(0.0, 0.0, 1.0), 0.0));
    output[5] = ToByte(texture_cube_array.SampleLevel(texture_sampler, float4(0.0, 0.0, 1.0, 1.0), 0.0));
    output[6] = ToByte(texture_ms.Load(int2(0, 0), 0));
    output[7] = ToByte(texture_ms_array.Load(int3(0, 0, 1), 0));
    output[8] = ToByte(texture_3d.SampleLevel(texture_sampler, float3(0.5, 0.5, 0.5), 0.0));
}

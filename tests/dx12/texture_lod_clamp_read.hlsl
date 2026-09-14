Texture2D<float> input_texture : register(t0);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
    output[0] = asuint(input_texture.Load(int3(0, 0, 0)));
}

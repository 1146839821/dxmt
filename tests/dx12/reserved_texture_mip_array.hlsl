Texture2DArray<uint> input_texture : register(t0);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  output[0] = input_texture.Load(int4(10, 10, 0, 0));
  output[1] = input_texture.Load(int4(200, 200, 1, 1));
  output[2] = input_texture.Load(int4(10, 10, 0, 1));
}

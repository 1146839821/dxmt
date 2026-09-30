Texture2D<uint> standard_view : register(t0);
Texture2D<uint> packed_intersecting_view : register(t1);
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  output[0] = standard_view.Load(int3(0, 0, 0));
  output[1] = packed_intersecting_view.Load(int3(0, 0, 0));
  output[2] = packed_intersecting_view.Load(int3(0, 0, 1));
}

RWBuffer<int> a[2] : register(u0);
RWBuffer<int> b[2] : register(u2);
RWStructuredBuffer<uint> output : register(u4);
[numthreads(2,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
 uint i=NonUniformResourceIndex(tid.x & 1);
 uint na,nb; a[i].GetDimensions(na); b[i].GetDimensions(nb);
 int va=a[i][tid.x], vb=b[i][tid.x];
 a[i][tid.x]=va+1; b[i][tid.x]=vb+2;
 output[tid.x]=asuint(va+vb)+na+nb;
}

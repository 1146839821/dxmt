[RootSignature("RootFlags(0)")]
#ifndef DXMT_TEST_THREADS
#define DXMT_TEST_THREADS 1
#endif
[numthreads(DXMT_TEST_THREADS, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
}

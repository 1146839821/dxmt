#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <iostream>

namespace {

bool
CheckHR(const char *operation, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << operation << " returned 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

} // namespace

int
main() {
  ID3D12Device *device = nullptr;
  if (!CheckHR("D3D12CreateDevice",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return 1;

  D3D12_QUERY_HEAP_DESC desc = {};
  desc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
  desc.Count = 1;

  ID3D12QueryHeap *heap = reinterpret_cast<ID3D12QueryHeap *>(static_cast<uintptr_t>(1));
  const HRESULT hr = device->CreateQueryHeap(&desc, IID_PPV_ARGS(&heap));
  device->Release();

  if (hr != E_NOTIMPL || heap != nullptr) {
    std::cerr << "Pipeline Statistics query heap did not fail cleanly: hr=0x" << std::hex
              << static_cast<unsigned long>(hr) << " heap=" << heap << std::dec << "\n";
    if (heap)
      heap->Release();
    return 1;
  }

  std::cout << "D3D12 Pipeline Statistics query is cleanly unsupported\n";
  return 0;
}

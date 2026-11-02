#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

static double ms(LARGE_INTEGER start, LARGE_INTEGER end,
                 LARGE_INTEGER frequency) {
  return double(end.QuadPart - start.QuadPart) * 1000.0 /
         double(frequency.QuadPart);
}

int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  char *end = nullptr;
  const unsigned long count = std::strtoul(argv[2], &end, 10);
  if (!*argv[2] || *end || count < 2 || count > 10000)
    return 2;
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<char> shader((std::istreambuf_iterator<char>(file)), {});
  if (shader.empty())
    return 3;
  for (const auto &[module_name, symbol] :
       {std::pair{"d3d12.dll", "D3D12CreateDevice"},
        std::pair{"winemetal.dll", "DXMTMSCPatchSamplerLOD"}}) {
    auto module = GetModuleHandleA(module_name);
    auto code = module ? reinterpret_cast<const unsigned char *>(
                             GetProcAddress(module, symbol))
                       : nullptr;
    if (!code)
      return 4;
    static const char hex[] = "0123456789abcdef";
    std::cout << "LOADED_CODE " << module_name << " " << symbol << " ";
    for (unsigned i = 0; i < 16; ++i)
      std::cout << hex[code[i] >> 4] << hex[code[i] & 15];
    std::cout << "\n";
  }
  LARGE_INTEGER frequency{}, start{}, finish{};
  if (!QueryPerformanceFrequency(&frequency))
    return 5;
  ID3D12Device *device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device))))
    return 6;
  D3D12_ROOT_SIGNATURE_DESC desc{};
  ID3DBlob *blob = nullptr;
  ID3D12RootSignature *root = nullptr;
  QueryPerformanceCounter(&start);
  HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           &blob, nullptr);
  if (SUCCEEDED(hr))
    hr =
        device->CreateRootSignature(0, blob->GetBufferPointer(),
                                    blob->GetBufferSize(), IID_PPV_ARGS(&root));
  QueryPerformanceCounter(&finish);
  if (blob)
    blob->Release();
  if (FAILED(hr)) {
    device->Release();
    return 7;
  }
  std::cout << "ROOT_MS " << ms(start, finish, frequency) << "\n";
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
  pipeline.pRootSignature = root;
  pipeline.CS = {shader.data(), shader.size()};
  for (unsigned i = 0; i < count; ++i) {
    ID3D12PipelineState *state = nullptr;
    QueryPerformanceCounter(&start);
    hr = device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&state));
    QueryPerformanceCounter(&finish);
    if (FAILED(hr) || !state) {
      std::cerr << "PSO failed at " << i << " hr=" << std::hex
                << static_cast<unsigned long>(hr) << "\n";
      root->Release();
      device->Release();
      return 8;
    }
    state->Release();
    std::cout << "PSO_MS " << i << " " << ms(start, finish, frequency) << "\n";
  }
  root->Release();
  device->Release();
  return 0;
}

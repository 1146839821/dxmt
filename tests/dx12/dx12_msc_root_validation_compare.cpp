#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>
#include "metalirconverter_thunks.h"

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  for (const auto &symbol :
       {std::pair{"d3d12.dll", "D3D12CreateDevice"},
        std::pair{"winemetal.dll", "DXMTMSCCompileDXIL"}}) {
    auto module = GetModuleHandleA(symbol.first);
    auto code = module ? reinterpret_cast<const unsigned char *>(
                             GetProcAddress(module, symbol.second))
                       : nullptr;
    if (!code)
      return 2;
    const char hex[] = "0123456789abcdef";
    std::cout << "LOADED_CODE " << symbol.first << " " << symbol.second << " ";
    for (unsigned i = 0; i < 16; ++i)
      std::cout << hex[code[i] >> 4] << hex[code[i] & 15];
    std::cout << "\n";
  }
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<char> shader((std::istreambuf_iterator<char>(in)), {});
  if (shader.empty())
    return 3;
  dxmt_msc_capabilities capabilities{};
  if (DXMTMSCGetCapabilities(&capabilities) != DXMT_MSC_SUCCESS ||
      !(capabilities.optional_symbols &
        DXMT_MSC_RUNTIME_SYMBOL_VALIDATION_FLAGS)) {
    std::cout << "VALIDATION_SKIP missing MSC setter\n";
    return 77;
  }
  LARGE_INTEGER frequency{};
  if (!QueryPerformanceFrequency(&frequency))
    return 4;
  for (unsigned valid = 0; valid < 2; ++valid) {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.ShaderRegister = 0;
    parameter.Constants.Num32BitValues = 4;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    if (valid) {
      desc.NumParameters = 1;
      desc.pParameters = &parameter;
    }
    ID3DBlob *root = nullptr, *errors = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(
        &desc, D3D_ROOT_SIGNATURE_VERSION_1, &root, &errors);
    if (errors)
      errors->Release();
    if (FAILED(hr) || !root)
      return 5;
    for (unsigned i = 0; i < 6; ++i) {
      dxmt_msc_compile_dxil_params params{};
      char error[1024]{};
      params.dxil = shader.data();
      params.dxil_size = shader.size();
      params.stage = DXMT_MSC_STAGE_FRAGMENT;
      params.root_signature = root->GetBufferPointer();
      params.root_signature_size = root->GetBufferSize();
      params.compatibility_flags =
          DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP;
      params.function_constant_resource_space =
          params.framebuffer_fetch_resource_space =
              DXMT_MSC_RESOURCE_SPACE_DISABLED;
      params.error_message = error;
      params.error_message_capacity = sizeof(error);
      LARGE_INTEGER start{}, end{};
      if (!QueryPerformanceCounter(&start))
        return 6;
      int rc = DXMTMSCCompileDXIL(&params);
      if (!QueryPerformanceCounter(&end))
        return 6;
      std::cout << "VALIDATION_RESULT " << valid << " " << i << " " << rc << " "
                << params.error_code << " " << params.metallib_size << " "
                << (1000.0 * (end.QuadPart - start.QuadPart) /
                    frequency.QuadPart)
                << "\n";
      if (rc != DXMT_MSC_SUCCESS)
        std::cout << "VALIDATION_ERROR " << valid << " " << i << " " << error
                  << "\n";
      if (valid && (rc != DXMT_MSC_SUCCESS || !params.metallib_size))
        return 7;
    }
    root->Release();
  }
  std::cout << "root validation comparison complete\n";
  return 0;
}

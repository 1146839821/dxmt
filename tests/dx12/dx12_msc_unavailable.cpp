#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include "metalirconverter_thunks.h"

int main(int argc, char **argv) {
  if (argc != 3 || (std::strcmp(argv[1], "available") && std::strcmp(argv[1], "incompatible") &&
                    std::strcmp(argv[1], "invalid")))
    return 2;
  auto module = GetModuleHandleA("winemetal.dll");
  auto address = module ? reinterpret_cast<const unsigned char *>(GetProcAddress(module, "DXMTMSCCompileDXIL")) : nullptr;
  if (!address)
    return 2;
  static const char hex[] = "0123456789abcdef";
  std::cout << "LOADED_CODE winemetal.dll DXMTMSCCompileDXIL ";
  for (unsigned i = 0; i < 16; ++i)
    std::cout << hex[address[i] >> 4] << hex[address[i] & 15];
  std::cout << '\n';
  const bool invalid = !std::strcmp(argv[1], "invalid");
  const bool available = invalid || !std::strcmp(argv[1], "available");
  dxmt_msc_capabilities capabilities{};
  const int query = DXMTMSCGetCapabilities(&capabilities);
  if (DXMTMSCIsAvailable() != int(available) || capabilities.core_converter != unsigned(available) ||
      query != (available ? DXMT_MSC_SUCCESS : DXMT_MSC_ERROR_UNAVAILABLE) ||
      (!available && capabilities.optional_symbols != 0))
    return 3;
  std::ifstream file(argv[2], std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (bytes.empty())
    return 2;
  char error[256]{};
  dxmt_msc_compile_dxil_params params{};
  params.dxil = bytes.data();
  params.dxil_size = bytes.size();
  params.stage = DXMT_MSC_STAGE_COMPUTE;
  params.function_constant_resource_space = DXMT_MSC_RESOURCE_SPACE_DISABLED;
  params.framebuffer_fetch_resource_space = DXMT_MSC_RESOURCE_SPACE_DISABLED;
  params.error_message = error;
  params.error_message_capacity = sizeof(error);
  const int result = DXMTMSCCompileDXIL(&params);
  const int expected = invalid ? DXMT_MSC_ERROR_INVALID_DXIL
                       : available ? DXMT_MSC_SUCCESS : DXMT_MSC_ERROR_UNAVAILABLE;
  if (result != expected || params.error_code != unsigned(expected) ||
      (available && !invalid && !params.metallib_size) ||
      (!available && !std::strstr(error, "runtime is unavailable")) ||
      (invalid && !std::strstr(error, "DXIL input is truncated"))) {
    std::cerr << "compile=" << result << " error=" << params.error_code << " message=" << error << '\n';
    return 4;
  }
  std::cout << "MSC thunk " << argv[1] << " PASS\n";
  return 0;
}

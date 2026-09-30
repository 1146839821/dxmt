#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "metalirconverter_thunks.h"

namespace {

using CompileProc = int (*)(dxmt_msc_compile_dxil_params *);

bool
ReadFile(const char *path, std::vector<uint8_t> &data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return false;
  const std::streamsize size = file.tellg();
  if (size <= 0)
    return false;
  data.resize(static_cast<size_t>(size));
  file.seekg(0);
  return file.read(reinterpret_cast<char *>(data.data()), size).good();
}

bool
CheckReflection(const dxmt_msc_shader_reflection &reflection) {
  if (!reflection.needs_function_constants) {
    std::cerr << "MSC did not report required function constants\n";
    return false;
  }
  if (reflection.function_constant_count != 1) {
    std::cerr << "unexpected function constant count: " << reflection.function_constant_count
              << " (needs=" << reflection.needs_function_constants << ")\n";
    return false;
  }

  const auto &constant = reflection.function_constants[0];
  std::cout << "function_constant[" << constant.index << "]=" << constant.name
            << ",type=" << constant.type << "\n";
  if (constant.index != 0 || std::strcmp(constant.name, "MSC_FC_0") != 0 || constant.type != 32) {
    std::cerr << "unexpected function constant metadata\n";
    return false;
  }
  return true;
}

} // namespace

int
main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_msc_function_constants <shader.ps.cso>\n";
    return 2;
  }

  std::vector<uint8_t> shader;
  if (!ReadFile(argv[1], shader)) {
    std::cerr << "failed to read shader\n";
    return 3;
  }

  HMODULE winemetal = LoadLibraryA("winemetal.dll");
  if (!winemetal) {
    std::cerr << "failed to load winemetal.dll\n";
    return 4;
  }
  auto compile = reinterpret_cast<CompileProc>(GetProcAddress(winemetal, "DXMTMSCCompileDXIL"));
  if (!compile) {
    std::cerr << "DXMTMSCCompileDXIL is unavailable\n";
    FreeLibrary(winemetal);
    return 5;
  }

  char error_message[1024] = {};
  dxmt_msc_compile_dxil_params params = {};
  params.dxil = shader.data();
  params.dxil_size = shader.size();
  params.stage = DXMT_MSC_STAGE_FRAGMENT;
  params.compatibility_flags = DXMT_MSC_COMPATIBILITY_FLAG_TEXTURE_MIN_LOD_CLAMP;
  params.function_constant_resource_space = 7;
  params.error_message = error_message;
  params.error_message_capacity = sizeof(error_message);

  const int result = compile(&params);
  if (result != DXMT_MSC_SUCCESS) {
    std::cerr << "DXMTMSCCompileDXIL failed: " << result << " " << error_message << "\n";
    FreeLibrary(winemetal);
    return 6;
  }

  bool passed = CheckReflection(params.reflection);
  FreeLibrary(winemetal);
  if (!passed)
    return 7;

  std::cout << "MSC function constant reflection passed\n";
  return 0;
}

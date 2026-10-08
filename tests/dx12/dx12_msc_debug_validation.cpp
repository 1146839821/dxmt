#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "metalirconverter_thunks.h"

namespace {

using CompileProc = int (*)(dxmt_msc_compile_dxil_params *);

struct CompileOutput {
  std::vector<uint8_t> metallib;
  std::string entry_point;
};

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
Compile(
    CompileProc compile, const std::vector<uint8_t> &shader, uint32_t validation_flags,
    bool ignore_debug_information, CompileOutput &output
) {
  char error_message[1024] = {};
  dxmt_msc_compile_dxil_params params = {};
  params.dxil = shader.data();
  params.dxil_size = shader.size();
  params.stage = DXMT_MSC_STAGE_FRAGMENT;
  params.validation_flags = validation_flags;
  params.ignore_debug_information = ignore_debug_information ? 1u : 0u;
  params.error_message = error_message;
  params.error_message_capacity = sizeof(error_message);

  int result = compile(&params);
  if (result != DXMT_MSC_SUCCESS) {
    std::cerr << "DXMTMSCCompileDXIL sizing pass failed: " << result << " " << error_message << "\n";
    return false;
  }
  if (!params.metallib_size || !params.entry_point_size) {
    std::cerr << "MSC returned empty debug-validation output\n";
    return false;
  }

  output.metallib.resize(params.metallib_size);
  output.entry_point.resize(params.entry_point_size);
  params.metallib = output.metallib.data();
  params.metallib_capacity = output.metallib.size();
  params.entry_point_out = output.entry_point.data();
  params.entry_point_capacity = output.entry_point.size();
  std::memset(error_message, 0, sizeof(error_message));
  params.error_message_size = 0;

  result = compile(&params);
  if (result != DXMT_MSC_SUCCESS) {
    std::cerr << "DXMTMSCCompileDXIL output pass failed: " << result << " " << error_message << "\n";
    return false;
  }
  if (params.metallib_size != output.metallib.size() || params.entry_point_size != output.entry_point.size() ||
      output.entry_point.empty() || output.entry_point.back() != '\0') {
    std::cerr << "MSC returned inconsistent debug-validation output sizes\n";
    return false;
  }
  output.entry_point.resize(params.entry_point_size - 1);
  return true;
}

} // namespace

int
main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_msc_debug_validation <shader.ps.cso>\n";
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

  CompileOutput ignored_debug;
  if (!Compile(
          compile, shader, DXMT_MSC_VALIDATION_FLAG_VALIDATE_DXIL, true, ignored_debug
      )) {
    FreeLibrary(winemetal);
    return 6;
  }

  std::cout << "MSC debug validation passed: ignored=" << ignored_debug.metallib.size()
            << ",entry=" << ignored_debug.entry_point << "\n";
  FreeLibrary(winemetal);
  return 0;
}

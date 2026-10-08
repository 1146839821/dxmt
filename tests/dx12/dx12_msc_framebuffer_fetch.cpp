#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <fstream>
#include <iostream>
#include <string>
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
Compile(CompileProc compile, const std::vector<uint8_t> &shader, std::vector<uint8_t> &metallib, std::string &entry_point) {
  char error_message[1024] = {};
  dxmt_msc_compile_dxil_params params = {};
  params.dxil = shader.data();
  params.dxil_size = shader.size();
  params.stage = DXMT_MSC_STAGE_FRAGMENT;
  params.framebuffer_fetch_resource_space = 7;
  params.error_message = error_message;
  params.error_message_capacity = sizeof(error_message);

  int result = compile(&params);
  if (result != DXMT_MSC_SUCCESS) {
    std::cerr << "DXMTMSCCompileDXIL sizing pass failed: " << result << " " << error_message << "\n";
    return false;
  }
  if (!params.metallib_size || !params.entry_point_size) {
    std::cerr << "MSC returned empty framebuffer-fetch output\n";
    return false;
  }

  metallib.resize(params.metallib_size);
  entry_point.resize(params.entry_point_size);
  params.metallib = metallib.data();
  params.metallib_capacity = metallib.size();
  params.entry_point_out = entry_point.data();
  params.entry_point_capacity = entry_point.size();

  result = compile(&params);
  if (result != DXMT_MSC_SUCCESS) {
    std::cerr << "DXMTMSCCompileDXIL output pass failed: " << result << " " << error_message << "\n";
    return false;
  }
  if (params.metallib_size != metallib.size() || params.entry_point_size != entry_point.size() ||
      entry_point.empty() || entry_point.back() != '\0') {
    std::cerr << "MSC returned inconsistent framebuffer-fetch output sizes\n";
    return false;
  }
  entry_point.resize(params.entry_point_size - 1);
  return true;
}

} // namespace

int
main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_msc_framebuffer_fetch <shader.ps.cso>\n";
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

  std::vector<uint8_t> metallib;
  std::string entry_point;
  const bool passed = Compile(compile, shader, metallib, entry_point);
  if (passed)
    std::cout << "MSC framebuffer fetch conversion passed: types=4,attachments=0-3,size=" << metallib.size()
              << ",entry=" << entry_point << "\n";
  FreeLibrary(winemetal);
  return passed ? 0 : 6;
}

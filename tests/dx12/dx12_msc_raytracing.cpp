#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Metal.hpp"
#include "metalirconverter_thunks.h"

namespace {

using CompileProc = int (*)(dxmt_msc_compile_dxil_params *);

struct CompileOutput {
  std::vector<uint8_t> metallib;
  std::string entry_point;
  dxmt_msc_shader_reflection reflection = {};
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
CompileStage(
    CompileProc compile, const std::vector<uint8_t> &shader, const D3D12_SHADER_BYTECODE &root_signature,
    uint32_t stage, const char *entry_point, CompileOutput &output
) {
  char error_message[1024] = {};
  dxmt_msc_compile_dxil_params params = {};
  params.dxil = shader.data();
  params.dxil_size = shader.size();
  params.stage = stage;
  params.entry_point = entry_point;
  params.entry_point_length = std::strlen(entry_point);
  params.root_signature = root_signature.pShaderBytecode;
  params.root_signature_size = root_signature.BytecodeLength;
  params.validation_flags = DXMT_MSC_VALIDATION_FLAG_VALIDATE_DXIL;
  params.error_message = error_message;
  params.error_message_capacity = sizeof(error_message);

  int result = compile(&params);
  if (result != DXMT_MSC_SUCCESS) {
    std::cerr << entry_point << " sizing pass failed: " << result << " " << error_message << "\n";
    return false;
  }
  if (!params.metallib_size || !params.entry_point_size) {
    std::cerr << entry_point << " returned empty MSC output: metallib=" << params.metallib_size
              << ",entry=" << params.entry_point_size << "\n";
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
    std::cerr << entry_point << " output pass failed: " << result << " " << error_message << "\n";
    return false;
  }
  if (params.metallib_size != output.metallib.size() || params.entry_point_size != output.entry_point.size() ||
      output.entry_point.empty() || output.entry_point.back() != '\0') {
    std::cerr << entry_point << " returned inconsistent MSC output sizes\n";
    return false;
  }
  output.entry_point.resize(params.entry_point_size - 1);
  output.reflection = params.reflection;
  return output.reflection.stage == stage;
}

bool
LoadMetalLibrary(WMT::Device device, const CompileOutput &output, const char *stage_name) {
  WMT::Error error;
  auto library = device.newLibrary(output.metallib.data(), output.metallib.size(), error);
  if (!library) {
    std::cerr << stage_name << " metallib load failed";
    if (error) {
      auto description = error.description();
      std::cerr << ": " << description.getUTF8String();
    }
    std::cerr << "\n";
    return false;
  }

  auto function = library.newFunction(output.entry_point.c_str());
  if (!function) {
    std::cerr << stage_name << " entry point was not present after metallib load: " << output.entry_point << "\n";
    return false;
  }
  return true;
}

} // namespace

int
main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_msc_raytracing <library.lib.cso>\n";
    return 2;
  }

  std::vector<uint8_t> shader;
  if (!ReadFile(argv[1], shader)) {
    std::cerr << "failed to read ray tracing library\n";
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

  const obj_handle_t devices = WMTCopyAllDevices();
  if (!devices || !NSArray_count(devices)) {
    std::cerr << "Metal device enumeration failed\n";
    if (devices)
      NSObject_release(devices);
    FreeLibrary(winemetal);
    return 6;
  }
  WMT::Device device{NSArray_object(devices, 0)};

  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  const D3D12_ROOT_SIGNATURE_DESC empty_root_signature = {};
  if (FAILED(D3D12SerializeRootSignature(
          &empty_root_signature, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error
      ))) {
    std::cerr << "empty root signature serialization failed\n";
    if (root_error)
      root_error->Release();
    NSObject_release(devices);
    FreeLibrary(winemetal);
    return 6;
  }
  if (root_error)
    root_error->Release();
  const D3D12_SHADER_BYTECODE root_signature = {
      root_blob->GetBufferPointer(), root_blob->GetBufferSize()
  };

  CompileOutput raygen;
  CompileOutput miss;
  const bool raygen_compiled = CompileStage(
      compile, shader, root_signature, DXMT_MSC_STAGE_RAY_GENERATION, "RayGen", raygen
  );
  const bool miss_compiled = CompileStage(compile, shader, root_signature, DXMT_MSC_STAGE_MISS, "Miss", miss);
  const bool libraries_loaded = raygen_compiled && miss_compiled && LoadMetalLibrary(device, raygen, "RayGen") &&
                                LoadMetalLibrary(device, miss, "Miss");

  root_blob->Release();
  NSObject_release(devices);
  FreeLibrary(winemetal);
  if (!libraries_loaded)
    return 7;

  std::cout << "MSC ray tracing stages passed: raygen=" << raygen.entry_point << ",miss=" << miss.entry_point
            << "\n";
  return 0;
}

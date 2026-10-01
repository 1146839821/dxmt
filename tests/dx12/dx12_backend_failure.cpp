// Test-linked production compute/converter sources, never production fault hooks.
#include "../../src/d3d12/d3d12_device.hpp"
#include "../../src/d3d12/d3d12_shader_converter.hpp"
#include <d3dcompiler.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include "log/log.hpp"

dxmt::Logger dxmt::Logger::s_instance("dx12_backend_failure");

namespace {
enum class Fault { None, AirInitialize, AirCompile, MSCInvalid, MSCUnsupported, MSCMemory, MSCSecondPass };
Fault fault = Fault::None;
unsigned air_initializations = 0, air_compiles = 0, msc_calls = 0;
unsigned unrelated_host_calls = 0;
}

// Persistence also references non-compute factories/device-child validation.
// Graphics factories must never run. Device identity validation is kept real
// for the explicit empty root signature required by the AIRCONV path.
namespace dxmt {
HRESULT CreateGraphicsPipelineState(MTLD3D12Device *, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *, REFIID, void **) {
  ++unrelated_host_calls; return E_FAIL;
}
HRESULT CreateMeshPipelineState(MTLD3D12Device *, const D3D12PipelineStreamData &, REFIID, void **) {
  ++unrelated_host_calls; return E_FAIL;
}
bool IsSameDevice(MTLD3D12Device *device, ID3D12DeviceChild *child) {
  if (!device || !child) return false;
  IUnknown *child_identity = nullptr, *device_identity = nullptr;
  if (FAILED(child->GetDevice(IID_PPV_ARGS(&child_identity)))) return false;
  const HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&device_identity));
  const bool same = SUCCEEDED(hr) && child_identity == device_identity;
  if (device_identity) device_identity->Release();
  child_identity->Release();
  return same;
}
}

// Wrap imported function pointers, not the DLL implementation. The tested
// production call sites are linked into this executable by Meson.
extern "C" {
extern decltype(&SM50Initialize) __real___imp_SM50Initialize;
extern decltype(&SM50Compile) __real___imp_SM50Compile;
extern decltype(&DXMTMSCCompileDXIL) __real___imp_DXMTMSCCompileDXIL;
}

static int TestAirInitialize(const void *bytes, size_t size, sm50_shader_t *shader,
                            MTL_SHADER_REFLECTION *reflection, sm50_error_t *error) {
  ++air_initializations;
  if (fault == Fault::AirInitialize) { *shader = {}; *error = {}; return 1; }
  return __real___imp_SM50Initialize(bytes, size, shader, reflection, error);
}
static int TestAirCompile(sm50_shader_t shader, SM50_SHADER_COMPILATION_ARGUMENT_DATA *args,
                          const char *name, sm50_bitcode_t *bitcode, sm50_error_t *error) {
  ++air_compiles;
  if (fault == Fault::AirCompile) { *bitcode = {}; *error = {}; return 1; }
  return __real___imp_SM50Compile(shader, args, name, bitcode, error);
}
static int TestMSC(dxmt_msc_compile_dxil_params *params) {
  ++msc_calls;
  switch (fault) {
  case Fault::MSCInvalid: return DXMT_MSC_ERROR_INVALID_DXIL;
  case Fault::MSCUnsupported: return DXMT_MSC_ERROR_UNSUPPORTED_SHADER;
  case Fault::MSCMemory: return DXMT_MSC_ERROR_OUT_OF_MEMORY;
  case Fault::MSCSecondPass:
    if (msc_calls == 2) return DXMT_MSC_ERROR_UNSUPPORTED_FEATURE;
    break;
  default: break;
  }
  return __real___imp_DXMTMSCCompileDXIL(params);
}
extern "C" {
decltype(&SM50Initialize) __wrap___imp_SM50Initialize = TestAirInitialize;
decltype(&SM50Compile) __wrap___imp_SM50Compile = TestAirCompile;
decltype(&DXMTMSCCompileDXIL) __wrap___imp_DXMTMSCCompileDXIL = TestMSC;
}

int main(int argc, char **argv) {
  if (argc != 3) { std::cerr << "usage: probe MODE DXIL.cso\n"; return 2; }
  const std::string mode = argv[1];
  bool dxil = false, wrong_stage = false, empty = false;
  HRESULT expected_hr = S_OK;
  unsigned expected_init = 1, expected_compile = 1, expected_msc = 0;
  if (mode == "air-init-failure") { fault = Fault::AirInitialize; expected_hr = E_FAIL; expected_compile = 0; }
  else if (mode == "air-compile-failure") { fault = Fault::AirCompile; expected_hr = E_FAIL; }
  else if (mode == "air-control") {}
  else if (mode == "air-wrong-stage") { wrong_stage = true; expected_hr = E_INVALIDARG; expected_init = expected_compile = 0; }
  else if (mode == "empty") { empty = true; expected_hr = E_INVALIDARG; expected_init = expected_compile = 0; }
  else {
    dxil = true; expected_init = expected_compile = 0; expected_msc = 1;
    if (mode == "msc-invalid") { fault = Fault::MSCInvalid; expected_hr = E_INVALIDARG; }
    else if (mode == "msc-unsupported") { fault = Fault::MSCUnsupported; expected_hr = E_NOTIMPL; }
    else if (mode == "msc-memory") { fault = Fault::MSCMemory; expected_hr = E_OUTOFMEMORY; }
    else if (mode == "msc-second-pass") { fault = Fault::MSCSecondPass; expected_hr = E_NOTIMPL; expected_msc = 2; }
    else if (mode == "msc-control") { expected_msc = 2; }
    else if (mode == "msc-wrong-stage") { expected_hr = E_INVALIDARG; expected_msc = 0; }
    else return 2;
  }
  SetEnvironmentVariableA("DXMT_SHADER_CACHE", "0");
  std::vector<uint8_t> bytes;
  ID3DBlob *blob = nullptr, *errors = nullptr;
  if (dxil) {
    std::ifstream file(argv[2], std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0) return 2;
    bytes.resize(static_cast<size_t>(file.tellg())); file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), bytes.size())) return 2;
  } else if (!empty) {
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    auto compile = compiler ? reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    const char *source = wrong_stage ? "float4 main() : SV_Position { return 0; }" : "[numthreads(8,8,1)] void main() {}";
    if (!compile) return 2;
    const HRESULT hr = compile(source, std::strlen(source), "backend_failure", nullptr, nullptr,
        "main", wrong_stage ? "vs_5_0" : "cs_5_0", 0, 0, &blob, &errors);
    if (errors) { std::cerr << static_cast<const char *>(errors->GetBufferPointer()); errors->Release(); }
    if (FAILED(hr) || !blob) return 2;
    const auto *begin = static_cast<const uint8_t *>(blob->GetBufferPointer());
    bytes.assign(begin, begin + blob->GetBufferSize()); blob->Release();
  }
  ID3D12Device *device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return 1;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  ID3DBlob *root_blob = nullptr;
  ID3D12RootSignature *root = nullptr;
  HRESULT root_hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, nullptr);
  if (SUCCEEDED(root_hr)) root_hr = device->CreateRootSignature(0, root_blob->GetBufferPointer(),
      root_blob->GetBufferSize(), IID_PPV_ARGS(&root));
  if (root_blob) root_blob->Release();
  if (FAILED(root_hr)) { device->Release(); return 1; }
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root;
  desc.CS = {bytes.empty() ? nullptr : bytes.data(), bytes.size()};
  ID3D12PipelineState *pso = nullptr;
  // This is the exact production factory linked into the probe, not the DLL's
  // uninstrumented public entry. Public PSO regressions run separately.
  const HRESULT hr = dxmt::CreateComputePipelineState(static_cast<dxmt::MTLD3D12Device *>(device),
      &desc, IID_PPV_ARGS(&pso));
  const bool passed = hr == expected_hr && bool(pso) == SUCCEEDED(expected_hr) &&
      air_initializations == expected_init && air_compiles == expected_compile && msc_calls == expected_msc && !unrelated_host_calls;
  std::cout << "backend failure " << mode << ": hr=0x" << std::hex << static_cast<uint32_t>(hr)
      << std::dec << " air_init=" << air_initializations << " air_compile=" << air_compiles
      << " msc=" << msc_calls << " status=" << (passed ? "PASS" : "FAIL") << "\n";
  if (pso) pso->Release();
  root->Release();
  device->Release();
  return passed ? 0 : 1;
}

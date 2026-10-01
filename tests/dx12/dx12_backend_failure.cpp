// Test-linked production compute/graphics/converter sources, never production fault hooks.
#include "../../src/d3d12/d3d12_device.hpp"
#include "../../src/d3d12/d3d12_shader_converter.hpp"
#include <d3dcompiler.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include "log/log.hpp"

dxmt::Logger dxmt::Logger::s_instance("dx12_backend_failure");

namespace {
enum class Fault { None, AirInitialize, AirCompile, MSCInvalid, MSCUnsupported, MSCMemory, MSCSecondPass };
Fault fault = Fault::None;
unsigned air_initializations = 0, air_compiles = 0, msc_calls = 0;
dxmt::D3D12ShaderKind fault_stage = dxmt::D3D12ShaderKind::Unknown;
std::map<sm50_shader_t, dxmt::D3D12ShaderKind> initialized_stages;
std::map<uint32_t, unsigned> msc_stage_calls;
std::vector<std::string> trace;

const char *StageName(dxmt::D3D12ShaderKind stage) {
  switch (stage) {
  case dxmt::D3D12ShaderKind::Vertex: return "vs";
  case dxmt::D3D12ShaderKind::Pixel: return "ps";
  case dxmt::D3D12ShaderKind::Compute: return "cs";
  case dxmt::D3D12ShaderKind::Hull: return "hs";
  case dxmt::D3D12ShaderKind::Domain: return "ds";
  default: return "other";
  }
}
bool SelectedStage(dxmt::D3D12ShaderKind stage) {
  return fault_stage == dxmt::D3D12ShaderKind::Unknown || fault_stage == stage;
}
}

// Test-local host dependency: real canonical device identity checking. Actual
// compute/graphics/mesh factory definitions come from production source files.
namespace dxmt {
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
extern decltype(&SM50CompileTessellationPipelineHull) __real___imp_SM50CompileTessellationPipelineHull;
extern decltype(&SM50CompileTessellationPipelineDomain) __real___imp_SM50CompileTessellationPipelineDomain;
extern decltype(&DXMTMSCCompileDXIL) __real___imp_DXMTMSCCompileDXIL;
}

static int TestAirInitialize(const void *bytes, size_t size, sm50_shader_t *shader,
                            MTL_SHADER_REFLECTION *reflection, sm50_error_t *error) {
  ++air_initializations;
  const auto stage = dxmt::ClassifyD3D12Shader({bytes, size}).shader_kind;
  trace.push_back(std::string("air.init.") + StageName(stage));
  if (fault == Fault::AirInitialize && SelectedStage(stage)) { *shader = {}; *error = {}; return 1; }
  const int result = __real___imp_SM50Initialize(bytes, size, shader, reflection, error);
  if (!result && *shader) initialized_stages[*shader] = stage;
  return result;
}
static int TestAirCompile(sm50_shader_t shader, SM50_SHADER_COMPILATION_ARGUMENT_DATA *args,
                          const char *name, sm50_bitcode_t *bitcode, sm50_error_t *error) {
  ++air_compiles;
  const auto found = initialized_stages.find(shader);
  const auto stage = found == initialized_stages.end() ? dxmt::D3D12ShaderKind::Unknown : found->second;
  trace.push_back(std::string("air.compile.") + StageName(stage));
  if (fault == Fault::AirCompile && SelectedStage(stage)) { *bitcode = {}; *error = {}; return 1; }
  return __real___imp_SM50Compile(shader, args, name, bitcode, error);
}
static int TestAirTessellation(sm50_shader_t first, sm50_shader_t second,
    SM50_SHADER_COMPILATION_ARGUMENT_DATA *args, const char *name, sm50_bitcode_t *bitcode,
    sm50_error_t *error, dxmt::D3D12ShaderKind stage) {
  ++air_compiles;
  const auto identify = [](sm50_shader_t handle) {
    const auto found = initialized_stages.find(handle);
    return StageName(found == initialized_stages.end() ? dxmt::D3D12ShaderKind::Unknown : found->second);
  };
  trace.push_back(std::string("air.tess.") + StageName(stage) + "." + identify(first) + "+" + identify(second));
  if (fault == Fault::AirCompile && SelectedStage(stage)) { *bitcode = {}; *error = {}; return 1; }
  if (stage == dxmt::D3D12ShaderKind::Hull)
    return __real___imp_SM50CompileTessellationPipelineHull(first, second, args, name, bitcode, error);
  return __real___imp_SM50CompileTessellationPipelineDomain(first, second, args, name, bitcode, error);
}
static int TestAirHull(sm50_shader_t vs, sm50_shader_t hs, SM50_SHADER_COMPILATION_ARGUMENT_DATA *args,
    const char *name, sm50_bitcode_t *bitcode, sm50_error_t *error) {
  return TestAirTessellation(vs, hs, args, name, bitcode, error, dxmt::D3D12ShaderKind::Hull);
}
static int TestAirDomain(sm50_shader_t hs, sm50_shader_t ds, SM50_SHADER_COMPILATION_ARGUMENT_DATA *args,
    const char *name, sm50_bitcode_t *bitcode, sm50_error_t *error) {
  return TestAirTessellation(hs, ds, args, name, bitcode, error, dxmt::D3D12ShaderKind::Domain);
}
static int TestMSC(dxmt_msc_compile_dxil_params *params) {
  ++msc_calls;
  const unsigned stage_call = ++msc_stage_calls[params->stage];
  const auto stage = params->stage == DXMT_MSC_STAGE_VERTEX ? dxmt::D3D12ShaderKind::Vertex :
      params->stage == DXMT_MSC_STAGE_FRAGMENT ? dxmt::D3D12ShaderKind::Pixel :
      params->stage == DXMT_MSC_STAGE_HULL ? dxmt::D3D12ShaderKind::Hull :
      params->stage == DXMT_MSC_STAGE_DOMAIN ? dxmt::D3D12ShaderKind::Domain :
      params->stage == DXMT_MSC_STAGE_COMPUTE ? dxmt::D3D12ShaderKind::Compute : dxmt::D3D12ShaderKind::Unknown;
  trace.push_back(std::string("msc.") + StageName(stage) + (params->metallib ? ".materialize" : ".query"));
  switch (SelectedStage(stage) ? fault : Fault::None) {
  case Fault::MSCInvalid: return DXMT_MSC_ERROR_INVALID_DXIL;
  case Fault::MSCUnsupported: return DXMT_MSC_ERROR_UNSUPPORTED_SHADER;
  case Fault::MSCMemory: return DXMT_MSC_ERROR_OUT_OF_MEMORY;
  case Fault::MSCSecondPass:
    if (stage_call == 2) return DXMT_MSC_ERROR_UNSUPPORTED_FEATURE;
    break;
  default: break;
  }
  return __real___imp_DXMTMSCCompileDXIL(params);
}
extern "C" {
decltype(&SM50Initialize) __wrap___imp_SM50Initialize = TestAirInitialize;
decltype(&SM50Compile) __wrap___imp_SM50Compile = TestAirCompile;
decltype(&SM50CompileTessellationPipelineHull) __wrap___imp_SM50CompileTessellationPipelineHull = TestAirHull;
decltype(&SM50CompileTessellationPipelineDomain) __wrap___imp_SM50CompileTessellationPipelineDomain = TestAirDomain;
decltype(&DXMTMSCCompileDXIL) __wrap___imp_DXMTMSCCompileDXIL = TestMSC;
}

static bool LoadShader(const char *path, std::vector<uint8_t> &bytes) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() <= 0) return false;
  bytes.resize(static_cast<size_t>(file.tellg())); file.seekg(0);
  return bool(file.read(reinterpret_cast<char *>(bytes.data()), bytes.size()));
}

static bool CompileLegacy(const char *source, const char *target, std::vector<uint8_t> &bytes, const char *entry = "main") {
  HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
  auto compile = compiler ? reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
  if (!compile) return false;
  ID3DBlob *blob = nullptr, *errors = nullptr;
  const HRESULT hr = compile(source, std::strlen(source), "backend_failure", nullptr, nullptr,
      entry, target, 0, 0, &blob, &errors);
  if (errors) { std::cerr << static_cast<const char *>(errors->GetBufferPointer()); errors->Release(); }
  if (FAILED(hr) || !blob) { if (blob) blob->Release(); return false; }
  const auto *begin = static_cast<const uint8_t *>(blob->GetBufferPointer());
  bytes.assign(begin, begin + blob->GetBufferSize()); blob->Release();
  return true;
}

static HRESULT CreateProbeRoot(ID3D12Device *device, ID3D12RootSignature **root) {
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  ID3DBlob *blob = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr);
  if (SUCCEEDED(hr)) hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(root));
  if (blob) blob->Release();
  return hr;
}

static void PrintResult(const std::string &mode, HRESULT hr, bool passed) {
  std::cout << "backend failure " << mode << ": hr=0x" << std::hex << static_cast<uint32_t>(hr)
      << std::dec << " air_init=" << air_initializations << " air_compile=" << air_compiles
      << " msc=" << msc_calls << " trace=";
  for (const auto &event : trace) std::cout << event << ";";
  std::cout << " status=" << (passed ? "PASS" : "FAIL") << "\n";
}

static int RunGraphics(const std::string &mode, const char *vs_path, const char *ps_path,
    const char *hs_path = nullptr, const char *ds_path = nullptr, const char *stages_path = nullptr) {
  using dxmt::D3D12ShaderKind;
  const bool tessellation = hs_path && ds_path && stages_path;
  bool vertex_dxil = false, pixel_dxil = false, wrong_vs = false, wrong_ps = false, reject = false;
  bool wrong_hs = false, wrong_ds = false;
  std::string mixed_stage;
  HRESULT expected_hr = S_OK;
  const std::string prefix = tessellation ? "tess-" : "graphics-";
  const std::string air_prefix = prefix + "air-", msc_prefix = prefix + "msc-";
  std::string operation;
  if (mode == "graphics-mixed-air-vs") { pixel_dxil = true; reject = true; expected_hr = E_NOTIMPL; }
  else if (mode == "graphics-mixed-msc-vs") { vertex_dxil = true; reject = true; expected_hr = E_NOTIMPL; }
  else {
    if (mode.rfind(air_prefix, 0) == 0) operation = mode.substr(air_prefix.size());
    else if (mode.rfind(msc_prefix, 0) == 0) {
      operation = mode.substr(msc_prefix.size()); vertex_dxil = pixel_dxil = true;
    } else return 2;
    if (tessellation && (operation == "mixed-hs" || operation == "mixed-ds")) {
      mixed_stage = operation.substr(6); reject = true; expected_hr = E_NOTIMPL;
    } else if (tessellation && (operation == "wrong-hs" || operation == "wrong-ds")) {
      wrong_hs = operation == "wrong-hs"; wrong_ds = !wrong_hs;
      reject = true; expected_hr = E_INVALIDARG;
    } else if (operation == "wrong-vs" || operation == "wrong-ps") {
      wrong_vs = operation == "wrong-vs"; wrong_ps = !wrong_vs;
      reject = true; expected_hr = E_INVALIDARG;
    } else if (operation != "control") {
      if (operation.rfind("vs-", 0) == 0) fault_stage = D3D12ShaderKind::Vertex;
      else if (operation.rfind("ps-", 0) == 0) fault_stage = D3D12ShaderKind::Pixel;
      else if (tessellation && operation.rfind("hs-", 0) == 0) fault_stage = D3D12ShaderKind::Hull;
      else if (tessellation && operation.rfind("ds-", 0) == 0) fault_stage = D3D12ShaderKind::Domain;
      else return 2;
      const auto failure = operation.substr(3);
      if (!vertex_dxil) {
        if (failure == "init") fault = Fault::AirInitialize;
        else if (failure == "compile") fault = Fault::AirCompile;
        else return 2;
        expected_hr = E_FAIL;
      } else {
        if (failure == "invalid") { fault = Fault::MSCInvalid; expected_hr = E_INVALIDARG; }
        else if (failure == "unsupported") { fault = Fault::MSCUnsupported; expected_hr = E_NOTIMPL; }
        else if (failure == "memory") { fault = Fault::MSCMemory; expected_hr = E_OUTOFMEMORY; }
        else if (failure == "second-pass") { fault = Fault::MSCSecondPass; expected_hr = E_NOTIMPL; }
        else return 2;
      }
    }
  }
  std::vector<std::string> expected_trace;
  if (!reject) {
    if (tessellation) {
      expected_trace = vertex_dxil ? std::vector<std::string>{
          "msc.vs.query", "msc.vs.materialize", "msc.ps.query", "msc.ps.materialize",
          "msc.hs.query", "msc.hs.materialize", "msc.ds.query", "msc.ds.materialize"} :
          std::vector<std::string>{"air.init.vs", "air.init.hs", "air.init.ds", "air.init.ps",
          "air.compile.ps", "air.tess.ds.hs+ds", "air.tess.hs.vs+hs", "air.tess.hs.vs+hs", "air.tess.hs.vs+hs"};
      if (fault != Fault::None) {
        const bool hull = fault_stage == D3D12ShaderKind::Hull;
        if (vertex_dxil) expected_trace.resize((hull ? 4 : 6) + (fault == Fault::MSCSecondPass ? 2 : 1));
        else if (fault == Fault::AirInitialize) expected_trace.resize(hull ? 2 : 3);
        else expected_trace.resize(hull ? 7 : 6);
      }
    } else {
      expected_trace = vertex_dxil ? std::vector<std::string>{"msc.vs.query", "msc.vs.materialize", "msc.ps.query", "msc.ps.materialize"} :
          std::vector<std::string>{"air.init.vs", "air.compile.vs", "air.init.ps", "air.compile.ps"};
      if (fault != Fault::None) {
        const unsigned prior = fault_stage == D3D12ShaderKind::Vertex ? 0 : 2;
        expected_trace.resize(prior + (fault == Fault::AirCompile || fault == Fault::MSCSecondPass ? 2 : 1));
      }
    }
  }
  std::vector<uint8_t> vs, ps;
  if (!(vertex_dxil ? LoadShader(vs_path, vs) : CompileLegacy(
          "float4 main(uint vertex : SV_VertexID) : SV_Position { return float4(float(vertex),0,0,1); }", "vs_5_0", vs)) ||
      !(pixel_dxil ? LoadShader(ps_path, ps) : CompileLegacy(
          "float4 main() : SV_Target { return float4(1,0,0,1); }", "ps_5_0", ps))) return 2;
  std::vector<uint8_t> hs, ds;
  if (tessellation) {
    std::vector<uint8_t> source;
    if (!LoadShader(stages_path, source)) return 2;
    source.push_back(0);
    const bool hull_dxil = mixed_stage == "hs" ? !vertex_dxil : vertex_dxil;
    const bool domain_dxil = mixed_stage == "ds" ? !vertex_dxil : vertex_dxil;
    if (!(hull_dxil ? LoadShader(hs_path, hs) : CompileLegacy(reinterpret_cast<const char *>(source.data()), "hs_5_0", hs, "hs_main")) ||
        !(domain_dxil ? LoadShader(ds_path, ds) : CompileLegacy(reinterpret_cast<const char *>(source.data()), "ds_5_0", ds, "ds_main"))) return 2;
  }
  ID3D12Device *device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return 1;
  ID3D12RootSignature *root = nullptr;
  if (FAILED(CreateProbeRoot(device, &root))) { device->Release(); return 1; }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root;
  desc.VS = wrong_vs ? D3D12_SHADER_BYTECODE{ps.data(), ps.size()} : D3D12_SHADER_BYTECODE{vs.data(), vs.size()};
  desc.PS = wrong_ps ? D3D12_SHADER_BYTECODE{vs.data(), vs.size()} : D3D12_SHADER_BYTECODE{ps.data(), ps.size()};
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  if (tessellation) {
    desc.HS = wrong_hs ? D3D12_SHADER_BYTECODE{ds.data(), ds.size()} : D3D12_SHADER_BYTECODE{hs.data(), hs.size()};
    desc.DS = wrong_ds ? D3D12_SHADER_BYTECODE{hs.data(), hs.size()} : D3D12_SHADER_BYTECODE{ds.data(), ds.size()};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
  }
  desc.NumRenderTargets = 1; desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1; desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  ID3D12PipelineState *pso = nullptr;
  const HRESULT hr = dxmt::CreateGraphicsPipelineState(static_cast<dxmt::MTLD3D12Device *>(device), &desc, IID_PPV_ARGS(&pso));
  const bool passed = hr == expected_hr && bool(pso) == SUCCEEDED(expected_hr) && trace == expected_trace;
  PrintResult(mode, hr, passed);
  if (pso) pso->Release();
  root->Release(); device->Release();
  return passed ? 0 : 1;
}

int main(int argc, char **argv) {
  SetEnvironmentVariableA("DXMT_SHADER_CACHE", "0");
  if (argc == 7) return RunGraphics(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
  if (argc == 4) return RunGraphics(argv[1], argv[2], argv[3]);
  if (argc != 3) { std::cerr << "usage: probe COMPUTE_MODE DXIL.cso | GRAPHICS_MODE VS.cso PS.cso\n"; return 2; }
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
  std::vector<uint8_t> bytes;
  if (dxil) {
    if (!LoadShader(argv[2], bytes)) return 2;
  } else if (!empty) {
    const char *source = wrong_stage ? "float4 main() : SV_Position { return 0; }" : "[numthreads(8,8,1)] void main() {}";
    if (!CompileLegacy(source, wrong_stage ? "vs_5_0" : "cs_5_0", bytes)) return 2;
  }
  ID3D12Device *device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return 1;
  ID3D12RootSignature *root = nullptr;
  const HRESULT root_hr = CreateProbeRoot(device, &root);
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
      air_initializations == expected_init && air_compiles == expected_compile && msc_calls == expected_msc;
  PrintResult(mode, hr, passed);
  if (pso) pso->Release();
  root->Release();
  device->Release();
  return passed ? 0 : 1;
}

// Test-linked production compute/graphics/converter sources, never production fault hooks.
#include "../../src/d3d12/d3d12_device.hpp"
#include "../../src/d3d12/d3d12_shader_converter.hpp"
#include "../../src/d3d12/d3d12_pipeline_persistence.hpp"
#include "../../src/d3d12/d3d12_raytracing_pipeline.hpp"
#include <d3dcompiler.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include "log/log.hpp"

dxmt::Logger dxmt::Logger::s_instance("dx12_backend_failure");

namespace {
enum class Fault { None, AirInitialize, AirCompile, AirObjectCompile, MSCInvalid, MSCUnsupported, MSCMemory, MSCSecondPass };
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
  case dxmt::D3D12ShaderKind::Geometry: return "gs";
  case dxmt::D3D12ShaderKind::Mesh: return "ms";
  case dxmt::D3D12ShaderKind::Amplification: return "as";
  case dxmt::D3D12ShaderKind::RayGeneration: return "raygen";
  case dxmt::D3D12ShaderKind::Miss: return "miss";
  case dxmt::D3D12ShaderKind::ClosestHit: return "closesthit";
  case dxmt::D3D12ShaderKind::AnyHit: return "anyhit";
  case dxmt::D3D12ShaderKind::Intersection: return "intersection";
  case dxmt::D3D12ShaderKind::Callable: return "callable";
  default: return "other";
  }
}
bool SelectedStage(dxmt::D3D12ShaderKind stage) {
  return fault_stage == dxmt::D3D12ShaderKind::Unknown || fault_stage == stage;
}
const char *HandleStageName(sm50_shader_t handle) {
  const auto found = initialized_stages.find(handle);
  return StageName(found == initialized_stages.end() ? dxmt::D3D12ShaderKind::Unknown : found->second);
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
extern decltype(&SM50CompileGeometryPipelineVertex) __real___imp_SM50CompileGeometryPipelineVertex;
extern decltype(&SM50CompileGeometryPipelineGeometry) __real___imp_SM50CompileGeometryPipelineGeometry;
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
  trace.push_back(std::string("air.tess.") + StageName(stage) + "." + HandleStageName(first) + "+" + HandleStageName(second));
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
static int TestAirGeometry(sm50_shader_t vs, sm50_shader_t gs, SM50_SHADER_COMPILATION_ARGUMENT_DATA *args,
    const char *name, sm50_bitcode_t *bitcode, sm50_error_t *error, bool object) {
  ++air_compiles;
  trace.push_back(std::string(object ? "air.geom.object." : "air.geom.mesh.") + HandleStageName(vs) + "+" + HandleStageName(gs));
  if (fault == (object ? Fault::AirObjectCompile : Fault::AirCompile) && SelectedStage(dxmt::D3D12ShaderKind::Geometry)) {
    *bitcode = {}; *error = {}; return 1;
  }
  if (object) return __real___imp_SM50CompileGeometryPipelineVertex(vs, gs, args, name, bitcode, error);
  return __real___imp_SM50CompileGeometryPipelineGeometry(vs, gs, args, name, bitcode, error);
}
static int TestAirGeometryVertex(sm50_shader_t vs, sm50_shader_t gs, SM50_SHADER_COMPILATION_ARGUMENT_DATA *args,
    const char *name, sm50_bitcode_t *bitcode, sm50_error_t *error) {
  return TestAirGeometry(vs, gs, args, name, bitcode, error, true);
}
static int TestAirGeometryMesh(sm50_shader_t vs, sm50_shader_t gs, SM50_SHADER_COMPILATION_ARGUMENT_DATA *args,
    const char *name, sm50_bitcode_t *bitcode, sm50_error_t *error) {
  return TestAirGeometry(vs, gs, args, name, bitcode, error, false);
}
static int TestMSC(dxmt_msc_compile_dxil_params *params) {
  ++msc_calls;
  const unsigned stage_call = ++msc_stage_calls[params->stage];
  const auto stage = params->stage == DXMT_MSC_STAGE_VERTEX ? dxmt::D3D12ShaderKind::Vertex :
      params->stage == DXMT_MSC_STAGE_FRAGMENT ? dxmt::D3D12ShaderKind::Pixel :
      params->stage == DXMT_MSC_STAGE_HULL ? dxmt::D3D12ShaderKind::Hull :
      params->stage == DXMT_MSC_STAGE_DOMAIN ? dxmt::D3D12ShaderKind::Domain :
      params->stage == DXMT_MSC_STAGE_GEOMETRY ? dxmt::D3D12ShaderKind::Geometry :
      params->stage == DXMT_MSC_STAGE_MESH ? dxmt::D3D12ShaderKind::Mesh :
      params->stage == DXMT_MSC_STAGE_AMPLIFICATION ? dxmt::D3D12ShaderKind::Amplification :
      params->stage == DXMT_MSC_STAGE_COMPUTE ? dxmt::D3D12ShaderKind::Compute : dxmt::D3D12ShaderKind::Unknown;
  const auto ray_stage = params->stage == DXMT_MSC_STAGE_RAY_GENERATION ? dxmt::D3D12ShaderKind::RayGeneration :
      params->stage == DXMT_MSC_STAGE_MISS ? dxmt::D3D12ShaderKind::Miss :
      params->stage == DXMT_MSC_STAGE_CLOSEST_HIT ? dxmt::D3D12ShaderKind::ClosestHit :
      params->stage == DXMT_MSC_STAGE_ANY_HIT ? dxmt::D3D12ShaderKind::AnyHit :
      params->stage == DXMT_MSC_STAGE_INTERSECTION ? dxmt::D3D12ShaderKind::Intersection :
      params->stage == DXMT_MSC_STAGE_CALLABLE ? dxmt::D3D12ShaderKind::Callable : dxmt::D3D12ShaderKind::Unknown;
  const auto shader_stage = ray_stage == dxmt::D3D12ShaderKind::Unknown ? stage : ray_stage;
  trace.push_back(std::string("msc.") + StageName(shader_stage) +
      (ray_stage == dxmt::D3D12ShaderKind::Unknown ? "" : "." + std::string(params->entry_point ? params->entry_point : "", params->entry_point ? params->entry_point_length : 0)) +
      (params->metallib ? ".materialize" : ".query"));
  switch (SelectedStage(shader_stage) ? fault : Fault::None) {
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
decltype(&SM50CompileGeometryPipelineVertex) __wrap___imp_SM50CompileGeometryPipelineVertex = TestAirGeometryVertex;
decltype(&SM50CompileGeometryPipelineGeometry) __wrap___imp_SM50CompileGeometryPipelineGeometry = TestAirGeometryMesh;
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
    const char *hs_path = nullptr, const char *ds_path = nullptr, const char *stages_path = nullptr,
    const char *gs_path = nullptr) {
  using dxmt::D3D12ShaderKind;
  const bool tessellation = hs_path && ds_path && stages_path;
  const bool geometry = gs_path && stages_path;
  bool vertex_dxil = false, pixel_dxil = false, wrong_vs = false, wrong_ps = false, reject = false;
  bool wrong_hs = false, wrong_ds = false;
  bool wrong_gs = false;
  std::string mixed_stage;
  HRESULT expected_hr = S_OK;
  const std::string prefix = geometry ? "geom-" : tessellation ? "tess-" : "graphics-";
  const std::string air_prefix = prefix + "air-", msc_prefix = prefix + "msc-";
  std::string operation;
  if (mode == "graphics-mixed-air-vs") { pixel_dxil = true; reject = true; expected_hr = E_NOTIMPL; }
  else if (mode == "graphics-mixed-msc-vs") { vertex_dxil = true; reject = true; expected_hr = E_NOTIMPL; }
  else {
    if (mode.rfind(air_prefix, 0) == 0) operation = mode.substr(air_prefix.size());
    else if (mode.rfind(msc_prefix, 0) == 0) {
      operation = mode.substr(msc_prefix.size()); vertex_dxil = pixel_dxil = true;
    } else return 2;
    if (geometry && operation == "mixed-gs") {
      mixed_stage = "gs"; reject = true; expected_hr = E_NOTIMPL;
    } else if (geometry && operation == "wrong-gs") {
      wrong_gs = true; reject = true; expected_hr = E_INVALIDARG;
    } else if (tessellation && (operation == "mixed-hs" || operation == "mixed-ds")) {
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
      else if (geometry && operation.rfind("gs-", 0) == 0) fault_stage = D3D12ShaderKind::Geometry;
      else return 2;
      const auto failure = operation.substr(3);
      if (!vertex_dxil) {
        if (failure == "init") fault = Fault::AirInitialize;
        else if (failure == "compile") fault = Fault::AirCompile;
        else if (geometry && failure == "object-compile") fault = Fault::AirObjectCompile;
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
    if (geometry) {
      expected_trace = vertex_dxil ? std::vector<std::string>{"msc.vs.query", "msc.vs.materialize",
          "msc.ps.query", "msc.ps.materialize", "msc.gs.query", "msc.gs.materialize"} :
          std::vector<std::string>{"air.init.vs", "air.init.gs", "air.init.ps", "air.compile.ps"};
      if (!vertex_dxil) {
        for (unsigned strip = 0; strip < 2; ++strip) {
          expected_trace.push_back("air.geom.mesh.vs+gs");
          for (unsigned index = 0; index < 3; ++index) expected_trace.push_back("air.geom.object.vs+gs");
        }
      }
      if (fault != Fault::None) {
        if (vertex_dxil) expected_trace.resize(fault == Fault::MSCSecondPass ? 6 : 5);
        else expected_trace.resize(fault == Fault::AirInitialize ? 2 : fault == Fault::AirObjectCompile ? 6 : 5);
      }
    } else if (tessellation) {
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
  std::vector<uint8_t> hs, ds, gs;
  if (geometry) {
    std::vector<uint8_t> source;
    if (!LoadShader(stages_path, source)) return 2;
    source.push_back(0);
    const bool geometry_dxil = mixed_stage == "gs" ? !vertex_dxil : vertex_dxil;
    if (!(geometry_dxil ? LoadShader(gs_path, gs) : CompileLegacy(
        reinterpret_cast<const char *>(source.data()), "gs_5_0", gs, "gs_main"))) return 2;
  }
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
  if (geometry) desc.GS = wrong_gs ? D3D12_SHADER_BYTECODE{vs.data(), vs.size()} : D3D12_SHADER_BYTECODE{gs.data(), gs.size()};
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

static int RunMesh(const std::string &mode, const char *ms_path, const char *as_path, const char *ps_path) {
  using dxmt::D3D12ShaderKind;
  if (mode.rfind("mesh-", 0) != 0) return 2;
  const auto operation = mode.substr(5);
  HRESULT expected_hr = S_OK;
  bool reject = false;
  if (operation == "empty-ms" || operation == "wrong-ms" || operation == "wrong-as" || operation == "wrong-ps") {
    reject = true; expected_hr = E_INVALIDARG;
  } else if (operation == "dxbc-ms" || operation == "dxbc-as" || operation == "dxbc-ps") {
    reject = true; expected_hr = E_NOTIMPL;
  } else if (operation != "control-no-as" && operation != "control-as") {
    if (operation.rfind("ms-", 0) == 0) fault_stage = D3D12ShaderKind::Mesh;
    else if (operation.rfind("as-", 0) == 0) fault_stage = D3D12ShaderKind::Amplification;
    else if (operation.rfind("ps-", 0) == 0) fault_stage = D3D12ShaderKind::Pixel;
    else return 2;
    const auto failure = operation.substr(3);
    if (failure == "invalid") { fault = Fault::MSCInvalid; expected_hr = E_INVALIDARG; }
    else if (failure == "unsupported") { fault = Fault::MSCUnsupported; expected_hr = E_NOTIMPL; }
    else if (failure == "memory") { fault = Fault::MSCMemory; expected_hr = E_OUTOFMEMORY; }
    else if (failure == "second-pass") { fault = Fault::MSCSecondPass; expected_hr = E_NOTIMPL; }
    else return 2;
  }
  std::vector<std::string> expected_trace;
  if (!reject) {
    expected_trace = {"msc.ms.query", "msc.ms.materialize"};
    if (operation != "control-no-as") {
      expected_trace.push_back("msc.as.query"); expected_trace.push_back("msc.as.materialize");
    }
    expected_trace.push_back("msc.ps.query"); expected_trace.push_back("msc.ps.materialize");
    if (fault != Fault::None) {
      const unsigned prior = fault_stage == D3D12ShaderKind::Mesh ? 0 : fault_stage == D3D12ShaderKind::Amplification ? 2 : 4;
      expected_trace.resize(prior + (fault == Fault::MSCSecondPass ? 2 : 1));
    }
  }
  dxmt::D3D12PipelineStreamData data;
  data.type = dxmt::D3D12PipelineType::Graphics;
  if (!LoadShader(ms_path, data.mesh_shader) || !LoadShader(as_path, data.amplification_shader) ||
      !LoadShader(ps_path, data.pixel_shader)) return 2;
  if (operation == "control-no-as") data.amplification_shader.clear();
  else if (operation == "empty-ms") data.mesh_shader.clear();
  else if (operation == "wrong-ms") data.mesh_shader = data.pixel_shader;
  else if (operation == "wrong-as") data.amplification_shader = data.mesh_shader;
  else if (operation == "wrong-ps") data.pixel_shader = data.mesh_shader;
  else if (operation.rfind("dxbc-", 0) == 0) {
    std::vector<uint8_t> legacy;
    const bool pixel = operation == "dxbc-ps";
    if (!CompileLegacy(pixel ? "float4 main() : SV_Target { return 0; }" :
        "float4 main() : SV_Position { return 0; }", pixel ? "ps_5_0" : "vs_5_0", legacy)) return 2;
    if (operation == "dxbc-ms") data.mesh_shader = legacy;
    else if (operation == "dxbc-as") data.amplification_shader = legacy;
    else data.pixel_shader = legacy;
  }
  ID3D12Device *device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return 1;
  ID3D12RootSignature *root = nullptr;
  if (FAILED(CreateProbeRoot(device, &root))) { device->Release(); return 1; }
  data.root_signature = root;
  data.num_render_targets = 1; data.render_target_formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  data.sample_desc.Count = 1;
  data.rasterizer_state.FillMode = D3D12_FILL_MODE_SOLID;
  data.rasterizer_state.CullMode = D3D12_CULL_MODE_NONE;
  data.rasterizer_state.DepthClipEnable = TRUE;
  data.blend_state.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  ID3D12PipelineState *pso = nullptr;
  const HRESULT hr = dxmt::CreateMeshPipelineState(static_cast<dxmt::MTLD3D12Device *>(device), data, IID_PPV_ARGS(&pso));
  const bool passed = hr == expected_hr && bool(pso) == SUCCEEDED(expected_hr) && trace == expected_trace;
  PrintResult(mode, hr, passed);
  if (pso) pso->Release();
  data.root_signature = nullptr;
  root->Release(); device->Release();
  return passed ? 0 : 1;
}

static int RunLibrary(const std::string &mode, const char *path, const char *ps_path = nullptr,
    const char *hs_path = nullptr, const char *ds_path = nullptr, const char *stages_path = nullptr,
    const char *gs_path = nullptr) {
  const bool graphics = ps_path != nullptr;
  const bool tessellation = hs_path && ds_path && stages_path;
  const bool geometry = gs_path && stages_path;
  const std::string family = geometry ? "library-geom-" : tessellation ? "library-tess-" : graphics ? "library-graphics-" : "library-";
  const bool dxil = mode.rfind(family + "msc-", 0) == 0;
  const std::string prefix = family + (dxil ? "msc-" : "air-");
  if (mode.rfind(prefix, 0) != 0) return 2;
  std::string operation = mode.substr(prefix.size());
  if (geometry && operation.rfind("gs-", 0) == 0) {
    fault_stage = dxmt::D3D12ShaderKind::Geometry; operation = operation.substr(3);
  }
  if (tessellation && (operation.rfind("hs-", 0) == 0 || operation.rfind("ds-", 0) == 0)) {
    fault_stage = operation.rfind("hs-", 0) == 0 ? dxmt::D3D12ShaderKind::Hull : dxmt::D3D12ShaderKind::Domain;
    operation = operation.substr(3);
  }
  if (graphics && (operation.rfind("vs-", 0) == 0 || operation.rfind("ps-", 0) == 0)) {
    fault_stage = operation.rfind("vs-", 0) == 0 ? dxmt::D3D12ShaderKind::Vertex : dxmt::D3D12ShaderKind::Pixel;
    operation = operation.substr(3);
  }
  Fault selected = Fault::None;
  HRESULT expected = S_OK;
  if (operation == "init") { selected = Fault::AirInitialize; expected = E_FAIL; }
  else if (operation == "compile") { selected = Fault::AirCompile; expected = E_FAIL; }
  else if (geometry && operation == "object-compile") { selected = Fault::AirObjectCompile; expected = E_FAIL; }
  else if (operation == "invalid") { selected = Fault::MSCInvalid; expected = E_INVALIDARG; }
  else if (operation == "unsupported") { selected = Fault::MSCUnsupported; expected = E_NOTIMPL; }
  else if (operation == "memory") { selected = Fault::MSCMemory; expected = E_OUTOFMEMORY; }
  else if (operation == "second-pass") { selected = Fault::MSCSecondPass; expected = E_NOTIMPL; }
  else if (operation != "retained" && operation != "reload" && operation != "missing" && operation != "mismatch") return 2;
  std::vector<uint8_t> bytes, ps;
  if (dxil ? !LoadShader(path, bytes) : !CompileLegacy(graphics ?
      "float4 main(uint vertex : SV_VertexID) : SV_Position { return float4(float(vertex),0,0,1); }" :
      "[numthreads(8,8,1)] void main() {}", graphics ? "vs_5_0" : "cs_5_0", bytes)) return 2;
  if (graphics && (dxil ? !LoadShader(ps_path, ps) : !CompileLegacy(
      "float4 main() : SV_Target { return float4(1,0,0,1); }", "ps_5_0", ps))) return 2;
  std::vector<uint8_t> hs, ds, gs;
  if (geometry) {
    std::vector<uint8_t> source;
    if (!LoadShader(stages_path, source)) return 2;
    source.push_back(0);
    if (!(dxil ? LoadShader(gs_path, gs) : CompileLegacy(reinterpret_cast<const char *>(source.data()), "gs_5_0", gs, "gs_main"))) return 2;
  }
  if (tessellation) {
    std::vector<uint8_t> source;
    if (!LoadShader(stages_path, source)) return 2;
    source.push_back(0);
    if (!(dxil ? LoadShader(hs_path, hs) : CompileLegacy(reinterpret_cast<const char *>(source.data()), "hs_5_0", hs, "hs_main")) ||
        !(dxil ? LoadShader(ds_path, ds) : CompileLegacy(reinterpret_cast<const char *>(source.data()), "ds_5_0", ds, "ds_main"))) return 2;
  }
  dxmt::Com<ID3D12Device> device;
  ID3D12Device *raw_device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)))) return 1;
  device = dxmt::Com<ID3D12Device>::transfer(raw_device);
  ID3D12RootSignature *raw_root = nullptr;
  if (FAILED(CreateProbeRoot(device.ptr(), &raw_root))) return 1;
  auto root = dxmt::Com<ID3D12RootSignature>::transfer(raw_root);
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root.ptr(); desc.CS = {bytes.data(), bytes.size()};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_desc = {};
  if (graphics) {
    graphics_desc.pRootSignature = root.ptr();
    graphics_desc.VS = {bytes.data(), bytes.size()}; graphics_desc.PS = {ps.data(), ps.size()};
    graphics_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphics_desc.NumRenderTargets = 1; graphics_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    graphics_desc.SampleDesc.Count = 1; graphics_desc.SampleMask = UINT_MAX;
    graphics_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    graphics_desc.RasterizerState.DepthClipEnable = TRUE;
    graphics_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (geometry) graphics_desc.GS = {gs.data(), gs.size()};
    if (tessellation) {
      graphics_desc.HS = {hs.data(), hs.size()}; graphics_desc.DS = {ds.data(), ds.size()};
      graphics_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    }
  }
  // Seed in the real DLL: its converter cache is separate from the wrapped,
  // source-linked cold rebuild. The seed is a real PSO with real cached metadata.
  ID3D12PipelineState *raw_seed = nullptr;
  HRESULT hr = graphics ? device->CreateGraphicsPipelineState(&graphics_desc, IID_PPV_ARGS(&raw_seed)) :
      device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&raw_seed));
  auto seed = dxmt::Com<ID3D12PipelineState>::transfer(raw_seed);
  bool passed = hr == S_OK && seed && trace.empty();
  auto *impl = static_cast<dxmt::MTLD3D12Device *>(device.ptr());
  ID3D12PipelineLibrary *raw_library = nullptr;
  hr = dxmt::CreateD3D12PipelineLibrary(impl, nullptr, 0, IID_PPV_ARGS(&raw_library));
  auto library = dxmt::Com<ID3D12PipelineLibrary>::transfer(raw_library);
  const WCHAR *name = graphics ? L"graphics" : L"compute";
  if (!passed || FAILED(hr) || !library || FAILED(library->StorePipeline(name, seed.ptr()))) {
    PrintResult(mode, hr, false); return 1;
  }
  if (operation != "retained") {
    std::vector<uint8_t> serialized(library->GetSerializedSize());
    hr = library->Serialize(serialized.data(), serialized.size());
    if (FAILED(hr)) { PrintResult(mode, hr, false); return 1; }
    library = nullptr; seed = nullptr;
    raw_library = nullptr;
    hr = dxmt::CreateD3D12PipelineLibrary(impl, serialized.data(), serialized.size(), IID_PPV_ARGS(&raw_library));
    library = dxmt::Com<ID3D12PipelineLibrary>::transfer(raw_library);
    if (FAILED(hr) || !library) { PrintResult(mode, hr, false); return 1; }
  }
  passed = passed && trace.empty();
  std::vector<std::string> full = geometry ? (dxil ?
      std::vector<std::string>{"msc.vs.query", "msc.vs.materialize", "msc.ps.query", "msc.ps.materialize",
          "msc.gs.query", "msc.gs.materialize"} :
      std::vector<std::string>{"air.init.vs", "air.init.gs", "air.init.ps", "air.compile.ps"}) : tessellation ? (dxil ?
      std::vector<std::string>{"msc.vs.query", "msc.vs.materialize", "msc.ps.query", "msc.ps.materialize",
          "msc.hs.query", "msc.hs.materialize", "msc.ds.query", "msc.ds.materialize"} :
      std::vector<std::string>{"air.init.vs", "air.init.hs", "air.init.ds", "air.init.ps", "air.compile.ps",
          "air.tess.ds.hs+ds", "air.tess.hs.vs+hs", "air.tess.hs.vs+hs", "air.tess.hs.vs+hs"}) : graphics ? (dxil ?
      std::vector<std::string>{"msc.vs.query", "msc.vs.materialize", "msc.ps.query", "msc.ps.materialize"} :
      std::vector<std::string>{"air.init.vs", "air.compile.vs", "air.init.ps", "air.compile.ps"}) :
      (dxil ? std::vector<std::string>{"msc.cs.query", "msc.cs.materialize"} :
      std::vector<std::string>{"air.init.cs", "air.compile.cs"});
  if (geometry && !dxil) {
    for (unsigned strip = 0; strip < 2; ++strip) {
      full.push_back("air.geom.mesh.vs+gs");
      for (unsigned index = 0; index < 3; ++index) full.push_back("air.geom.object.vs+gs");
    }
  }
  auto wanted = full;
  fault = selected;
  if (operation == "retained") { fault = dxil ? Fault::MSCUnsupported : Fault::AirInitialize; wanted.clear(); }
  if (operation == "missing" || operation == "mismatch") { expected = E_INVALIDARG; wanted.clear(); }
  if (operation == "mismatch") { desc.NodeMask = 1; graphics_desc.NodeMask = 1; }
  if (selected != Fault::None) {
    if (geometry) {
      if (dxil) wanted.resize(selected == Fault::MSCSecondPass ? 6 : 5);
      else wanted.resize(selected == Fault::AirInitialize ? 2 : selected == Fault::AirObjectCompile ? 6 : 5);
    } else if (tessellation) {
      const bool hull = fault_stage == dxmt::D3D12ShaderKind::Hull;
      if (dxil) wanted.resize((hull ? 4 : 6) + (selected == Fault::MSCSecondPass ? 2 : 1));
      else wanted.resize(selected == Fault::AirInitialize ? (hull ? 2 : 3) : (hull ? 7 : 6));
    } else {
      const unsigned prior = graphics && fault_stage == dxmt::D3D12ShaderKind::Pixel ? 2 : 0;
      wanted.resize(prior + (selected == Fault::AirCompile || selected == Fault::MSCSecondPass ? 2 : 1));
    }
  }
  auto load = [&](const WCHAR *entry, ID3D12PipelineState **output) {
    return graphics ? library->LoadGraphicsPipeline(entry, &graphics_desc, IID_PPV_ARGS(output)) :
        library->LoadComputePipeline(entry, &desc, IID_PPV_ARGS(output));
  };
  ID3D12PipelineState *raw_pso = nullptr;
  hr = load(operation == "missing" ? L"absent" : name, &raw_pso);
  auto pso = dxmt::Com<ID3D12PipelineState>::transfer(raw_pso);
  passed = passed && hr == expected && bool(pso) == SUCCEEDED(expected) && trace == wanted;
  if (operation == "retained") passed = passed && pso.ptr() == seed.ptr();
  PrintResult(mode + ".load", hr, passed);
  if (selected != Fault::None) {
    fault = Fault::None;
    const size_t begin = trace.size();
    raw_pso = nullptr;
    hr = load(name, &raw_pso);
    pso = dxmt::Com<ID3D12PipelineState>::transfer(raw_pso);
    // Earlier successful MSC stages survive in the conversion cache even
    // though the failed library rebuild has not retained a PSO yet.
    auto retry = full;
    if (dxil && geometry) retry.erase(retry.begin(), retry.begin() + 4);
    else if (dxil && tessellation) retry.erase(retry.begin(), retry.begin() +
        (fault_stage == dxmt::D3D12ShaderKind::Hull ? 4 : 6));
    else if (graphics && dxil && fault_stage == dxmt::D3D12ShaderKind::Pixel) retry.erase(retry.begin(), retry.begin() + 2);
    passed = passed && hr == S_OK && pso && std::vector<std::string>(trace.begin() + begin, trace.end()) == retry;
    PrintResult(mode + ".retry", hr, passed);
  }
  if (pso) {
    const size_t begin = trace.size();
    fault = dxil ? Fault::MSCUnsupported : Fault::AirInitialize;
    ID3D12PipelineState *hit = nullptr;
    hr = load(name, &hit);
    passed = passed && hr == S_OK && hit == pso.ptr() && trace.size() == begin;
    if (hit) hit->Release();
  }
  PrintResult(mode, hr, passed);
  return passed ? 0 : 1;
}

static int RunShaderLibrary(const std::string &mode, const char *library_path, const char *ordinary_path,
    const char *qualifiers_path) {
  struct RayExport { const char *name; const char *entry; uint32_t stage; };
  const RayExport exports[] = {
    {"raygen", "RayGen", DXMT_MSC_STAGE_RAY_GENERATION}, {"miss", "Miss", DXMT_MSC_STAGE_MISS},
    {"closesthit", "ClosestHit", DXMT_MSC_STAGE_CLOSEST_HIT}, {"anyhit", "AnyHit", DXMT_MSC_STAGE_ANY_HIT},
    {"intersection", "Intersection", DXMT_MSC_STAGE_INTERSECTION}, {"callable", "Callable", DXMT_MSC_STAGE_CALLABLE},
  };
  const RayExport *target = &exports[0];
  std::string operation;
  for (const auto &item : exports) {
    const std::string prefix = std::string("shaderlib-") + item.name + "-";
    if (mode.rfind(prefix, 0) == 0) { target = &item; operation = mode.substr(prefix.size()); break; }
  }
  const bool reject = mode == "shaderlib-legacy" || mode == "shaderlib-ordinary" ||
      mode == "shaderlib-empty-entry" || mode == "shaderlib-qualifiers";
  if (mode == "shaderlib-qualifiers") target = &exports[1];
  HRESULT expected = S_OK;
  if (operation == "invalid") { fault = Fault::MSCInvalid; expected = E_INVALIDARG; }
  else if (operation == "unsupported") { fault = Fault::MSCUnsupported; expected = E_NOTIMPL; }
  else if (operation == "memory") { fault = Fault::MSCMemory; expected = E_OUTOFMEMORY; }
  else if (operation == "second-pass") { fault = Fault::MSCSecondPass; expected = E_NOTIMPL; }
  else if (!reject && operation != "control") return 2;
  std::vector<uint8_t> bytes;
  if (mode == "shaderlib-legacy") {
    if (!CompileLegacy("[numthreads(8,8,1)] void main() {}", "cs_5_0", bytes)) return 2;
  } else if (!LoadShader(mode == "shaderlib-ordinary" ? ordinary_path :
      mode == "shaderlib-qualifiers" ? qualifiers_path : library_path, bytes)) return 2;
  ID3D12Device *raw_device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)))) return 1;
  auto device = dxmt::Com<ID3D12Device>::transfer(raw_device);
  ID3DBlob *raw_root = nullptr;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &raw_root, nullptr))) return 1;
  auto root = dxmt::Com<ID3DBlob>::transfer(raw_root);
  const D3D12_SHADER_BYTECODE shader = {bytes.data(), bytes.size()};
  const auto classification = dxmt::ClassifyD3D12Shader(shader);
  auto *impl = static_cast<dxmt::MTLD3D12Device *>(device.ptr());
  auto convert = [&](dxmt::D3D12ConvertedShader &output) {
    return dxmt::ConvertD3D12LibraryShader(classification, shader, target->stage,
        mode == "shaderlib-empty-entry" ? "" : target->entry, output,
        root->GetBufferPointer(), root->GetBufferSize(), root->GetBufferPointer(), root->GetBufferSize(),
        &impl->GetMSCCapabilities());
  };
  const std::string prefix = std::string("msc.") + target->name + "." + target->entry;
  const std::vector<std::string> full = {prefix + ".query", prefix + ".materialize"};
  auto wanted = full;
  if (reject) { wanted.clear(); expected = mode == "shaderlib-qualifiers" ? E_NOTIMPL : E_INVALIDARG; }
  else if (fault != Fault::None && fault != Fault::MSCSecondPass) wanted.resize(1);
  auto usable = [&](const dxmt::D3D12ConvertedShader &output) {
    if (output.backend != dxmt::D3D12ShaderBackend::MetalShaderConverter || output.metallib.empty() ||
        output.entry_point.empty() || output.reflection.stage != target->stage) return false;
    WMT::Error error;
    auto library = impl->GetMTLDevice().newLibrary(output.metallib.data(), output.metallib.size(), error);
    return library && bool(library.newFunction(output.entry_point.c_str()));
  };
  dxmt::D3D12ConvertedShader output;
  HRESULT hr = convert(output);
  bool passed = hr == expected && trace == wanted && (SUCCEEDED(hr) ? usable(output) :
      output.backend == dxmt::D3D12ShaderBackend::None && output.entry_point.empty());
  PrintResult(mode + ".convert", hr, passed);
  if (!reject && fault != Fault::None) {
    fault = Fault::None;
    const size_t begin = trace.size();
    dxmt::D3D12ConvertedShader retry;
    hr = convert(retry);
    passed = passed && hr == S_OK && usable(retry) && std::vector<std::string>(trace.begin() + begin, trace.end()) == full;
    output = std::move(retry);
    PrintResult(mode + ".retry", hr, passed);
  }
  if (!reject) {
    fault = Fault::MSCUnsupported;
    const size_t begin = trace.size();
    dxmt::D3D12ConvertedShader hit;
    hr = convert(hit);
    passed = passed && hr == S_OK && trace.size() == begin && usable(hit) &&
        hit.metallib == output.metallib && hit.entry_point == output.entry_point;
  }
  PrintResult(mode, hr, passed);
  return passed ? 0 : 1;
}

static int RunStateObject(const std::string &mode, const char *library_path, const char *ordinary_path,
    const char *qualifiers_path) {
  struct Export { const char *name; const char *entry; const WCHAR *wide; dxmt::D3D12ShaderKind kind; };
  const Export exports[] = {
    {"raygen", "RayGen", L"RayGen", dxmt::D3D12ShaderKind::RayGeneration},
    {"miss", "Miss", L"Miss", dxmt::D3D12ShaderKind::Miss},
    {"closesthit", "ClosestHit", L"ClosestHit", dxmt::D3D12ShaderKind::ClosestHit},
    {"anyhit", "AnyHit", L"AnyHit", dxmt::D3D12ShaderKind::AnyHit},
    {"intersection", "Intersection", L"Intersection", dxmt::D3D12ShaderKind::Intersection},
    {"callable", "Callable", L"Callable", dxmt::D3D12ShaderKind::Callable},
  };
  const bool hinted = mode.rfind("state-hint-", 0) == 0;
  const Export *target = &exports[0];
  std::string operation;
  for (const auto &item : exports) {
    const std::string prefix = std::string(hinted ? "state-hint-" : "state-") + item.name + "-";
    if (mode.rfind(prefix, 0) == 0) { target = &item; operation = mode.substr(prefix.size()); break; }
  }
  const bool reject = mode == "state-legacy" || mode == "state-ordinary" ||
      mode == "state-qualifiers" || mode == "state-missing-export";
  if (mode == "state-qualifiers") target = &exports[1];
  if (hinted && target->kind != dxmt::D3D12ShaderKind::AnyHit &&
      target->kind != dxmt::D3D12ShaderKind::ClosestHit) return 2;
  fault_stage = target->kind;
  if (operation == "invalid") fault = Fault::MSCInvalid;
  else if (operation == "unsupported") fault = Fault::MSCUnsupported;
  else if (operation == "memory") fault = Fault::MSCMemory;
  else if (operation == "second-pass") fault = Fault::MSCSecondPass;
  else if (!reject && operation != "control") return 2;
  const bool injected = fault != Fault::None;
  std::vector<uint8_t> bytes;
  if (mode == "state-legacy") {
    if (!CompileLegacy("[numthreads(8,8,1)] void main() {}", "cs_5_0", bytes)) return 2;
  } else if (!LoadShader(mode == "state-ordinary" ? ordinary_path :
      mode == "state-qualifiers" ? qualifiers_path : library_path, bytes)) return 2;
  ID3D12Device *raw_device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)))) return 1;
  auto device = dxmt::Com<ID3D12Device>::transfer(raw_device);
  ID3DBlob *raw_root = nullptr;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &raw_root, nullptr))) return 1;
  auto root_blob = dxmt::Com<ID3DBlob>::transfer(raw_root);
  ID3D12RootSignature *raw_signature = nullptr;
  if (FAILED(device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
      IID_PPV_ARGS(&raw_signature)))) return 1;
  auto root = dxmt::Com<ID3D12RootSignature>::transfer(raw_signature);
  D3D12_EXPORT_DESC export_desc = {
    L"PublicExport", mode == "state-missing-export" ? L"MissingExport" : target->wide, D3D12_EXPORT_FLAG_NONE};
  const D3D12_DXIL_LIBRARY_DESC library = {{bytes.data(), bytes.size()}, 1, &export_desc};
  const D3D12_GLOBAL_ROOT_SIGNATURE global = {root.ptr()};
  const D3D12_RAYTRACING_SHADER_CONFIG config = {4, 16};
  const D3D12_RAYTRACING_PIPELINE_CONFIG pipeline = {1};
  const D3D12_HIT_GROUP_DESC hit_group = {L"HitGroup", D3D12_HIT_GROUP_TYPE_TRIANGLES,
    target->kind == dxmt::D3D12ShaderKind::AnyHit ? L"PublicExport" : nullptr,
    target->kind == dxmt::D3D12ShaderKind::ClosestHit ? L"PublicExport" : nullptr, nullptr};
  const D3D12_STATE_SUBOBJECT subobjects[] = {
    {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library},
    {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global},
    {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &config},
    {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline},
    {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group},
  };
  const D3D12_STATE_OBJECT_DESC desc = {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
    hinted ? 5u : 4u, subobjects};
  auto create = [&](ID3D12StateObject **output) {
    return dxmt::CreateD3D12RaytracingStateObject(static_cast<dxmt::MTLD3D12Device *>(device.ptr()),
        &desc, __uuidof(ID3D12StateObject), reinterpret_cast<void **>(output));
  };
  auto usable = [&](ID3D12StateObject *object) {
    if (!object) return false;
    ID3D12StateObjectProperties *raw = nullptr;
    if (FAILED(object->QueryInterface(IID_PPV_ARGS(&raw)))) return false;
    auto properties = dxmt::Com<ID3D12StateObjectProperties>::transfer(raw);
    return properties->GetShaderIdentifier(L"PublicExport") && !properties->GetShaderIdentifier(target->wide) &&
        (!hinted || properties->GetShaderIdentifier(L"HitGroup"));
  };
  auto expected_trace = [&](bool fail, bool cached) {
    std::vector<std::string> result;
    if (reject && mode != "state-missing-export") return result;
    for (const auto &item : exports) {
      if (hinted && &item != target) continue;
      const bool selected = &item == target && !reject;
      if (selected && cached) continue;
      const std::string prefix = std::string("msc.") + item.name + "." +
          (mode == "state-missing-export" ? "MissingExport" : target->entry);
      result.push_back(prefix + ".query");
      if (selected && (!fail || operation == "second-pass")) result.push_back(prefix + ".materialize");
    }
    return result;
  };
  ID3D12StateObject *raw = reinterpret_cast<ID3D12StateObject *>(uintptr_t(1));
  HRESULT hr = create(&raw);
  if (raw == reinterpret_cast<ID3D12StateObject *>(uintptr_t(1))) {
    PrintResult(mode, hr, false);
    return 1;
  }
  auto object = dxmt::Com<ID3D12StateObject>::transfer(raw);
  bool passed = hr == (injected || reject ? E_NOTIMPL : S_OK) &&
      trace == expected_trace(injected, false) && (FAILED(hr) ? !object : usable(object.ptr()));
  PrintResult(mode + ".create", hr, passed);
  if (injected) {
    fault = Fault::None;
    const size_t begin = trace.size();
    raw = nullptr;
    hr = create(&raw);
    object = dxmt::Com<ID3D12StateObject>::transfer(raw);
    passed = passed && hr == S_OK && usable(object.ptr()) &&
        std::vector<std::string>(trace.begin() + begin, trace.end()) == expected_trace(false, false);
    PrintResult(mode + ".retry", hr, passed);
  }
  if (!reject) {
    fault = Fault::MSCUnsupported;
    const size_t begin = trace.size();
    raw = nullptr;
    hr = create(&raw);
    auto hit = dxmt::Com<ID3D12StateObject>::transfer(raw);
    passed = passed && hr == S_OK && usable(hit.ptr()) &&
        std::vector<std::string>(trace.begin() + begin, trace.end()) == expected_trace(false, true);
  }
  PrintResult(mode, hr, passed);
  return passed ? 0 : 1;
}

int main(int argc, char **argv) {
  SetEnvironmentVariableA("DXMT_SHADER_CACHE", "0");
  if (argc == 5 && std::string(argv[1]).rfind("state-", 0) == 0)
    return RunStateObject(argv[1], argv[2], argv[3], argv[4]);
  if (argc == 5) return std::string(argv[1]).rfind("shaderlib-", 0) == 0 ?
      RunShaderLibrary(argv[1], argv[2], argv[3], argv[4]) : RunMesh(argv[1], argv[2], argv[3], argv[4]);
  if (argc == 6) return std::string(argv[1]).rfind("library-geom-", 0) == 0 ?
      RunLibrary(argv[1], argv[2], argv[3], nullptr, nullptr, argv[5], argv[4]) :
      RunGraphics(argv[1], argv[2], argv[3], nullptr, nullptr, argv[5], argv[4]);
  if (argc == 7) return std::string(argv[1]).rfind("library-tess-", 0) == 0 ?
      RunLibrary(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]) :
      RunGraphics(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
  if (argc == 4) return std::string(argv[1]).rfind("library-graphics-", 0) == 0 ?
      RunLibrary(argv[1], argv[2], argv[3]) : RunGraphics(argv[1], argv[2], argv[3]);
  if (argc != 3) { std::cerr << "usage: probe COMPUTE_MODE DXIL.cso | GRAPHICS_MODE VS.cso PS.cso\n"; return 2; }
  const std::string mode = argv[1];
  if (mode.rfind("library-", 0) == 0) return RunLibrary(mode, argv[2]);
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

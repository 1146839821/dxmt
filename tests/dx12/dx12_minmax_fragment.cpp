#include "d3d12_device.hpp"
#include "d3d12_minmax.hpp"
#include "d3d12_shader_converter.hpp"
#include "log/log.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

dxmt::Logger dxmt::Logger::s_instance("dx12_minmax_fragment");
template <typename T> struct ReleaseCOM { void operator()(T *p) const { if (p) p->Release(); } };
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;

static bool Load(const wchar_t *path, std::vector<uint8_t> &bytes) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {}; DWORD read = 0;
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 32 * 1024 * 1024;
  if (ok) { bytes.resize(size.QuadPart); ok = ReadFile(file, bytes.data(), bytes.size(), &read, nullptr) && read == bytes.size(); }
  CloseHandle(file); return ok;
}

int wmain(int argc, wchar_t **argv) {
  if (argc != 4) return 1;
  std::vector<uint8_t> ps, vs;
  if (!Load(argv[1], ps) || !Load(argv[2], vs)) return 1;
  const D3D12_SHADER_BYTECODE pixel = {ps.data(), ps.size()}, vertex = {vs.data(), vs.size()};
  dxmt::D3D12MinMaxShader prepared;
  std::string error;
  using Stage = dxmt::D3D12MinMaxShaderStage;
  auto hr = dxmt::PrepareD3D12MinMaxShader(pixel, argv[3], prepared, error, Stage::Pixel);
  if (FAILED(hr) || prepared.stage != Stage::Pixel || prepared.bindings.size() != 2) {
    std::printf("pixel preparation failed %08lx %s\n", (unsigned long)hr, error.c_str()); return 1;
  }
  const auto saved = prepared;
  const auto unchanged = [&] {
    return prepared.stage == saved.stage && prepared.bytecode == saved.bytecode && prepared.bindings.size() == 2 &&
        !std::memcmp(prepared.bindings.data(), saved.bindings.data(), 2 * sizeof(saved.bindings[0]));
  };
  if (dxmt::PrepareD3D12MinMaxShader(pixel, argv[3], prepared, error) != E_NOTIMPL || !unchanged() ||
      dxmt::PrepareD3D12MinMaxShader(vertex, argv[3], prepared, error, Stage::Pixel) != E_NOTIMPL || !unchanged() ||
      dxmt::PrepareD3D12MinMaxShader(pixel, argv[3], prepared, error, static_cast<Stage>(99)) != E_INVALIDARG || !unchanged())
    return 1;
  ID3D12Device *raw_device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)))) return 1;
  OwnedCOM<ID3D12Device> device(raw_device);
  auto *native = static_cast<dxmt::MTLD3D12Device *>(device.get());
  auto metal = native->GetMTLDevice();
  for (unsigned mode = 0; mode < 4; ++mode) {
    const bool static_sampler = mode & 2;
    const auto visibility = mode & 1 ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE1 ranges[] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 2},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 1}};
    D3D12_ROOT_PARAMETER1 parameters[2] = {};
    for (unsigned i = 0; i < 2; ++i) {
      parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[i].DescriptorTable = {1, ranges + i}; parameters[i].ShaderVisibility = visibility;
    }
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    for (unsigned i = 0; i < 2; ++i) {
      samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      samplers[i].MaxLOD = D3D12_FLOAT32_MAX; samplers[i].ShaderRegister = i; samplers[i].ShaderVisibility = visibility;
    }
    D3D12_ROOT_SIGNATURE_DESC1 application = {static_sampler ? 1u : 2u, parameters,
        static_sampler ? 2u : 0u, samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    dxmt::D3D12MinMaxRoot root;
    if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, 2, root, error))) return 1;
    std::vector<dxmt::D3D12MinMaxPairLocation> locations;
    if (FAILED(dxmt::ResolveD3D12MinMaxBindings(root, prepared.bindings, locations, error, Stage::Pixel)) ||
        locations.size() != 2 || locations[0].texture.parameter_index != 0 || locations[0].texture.table_offset != 2 ||
        (static_sampler ? locations[1].sampler.static_sampler_index != 1 : locations[1].sampler.table_offset != 2)) return 1;
    const auto saved_locations = locations;
    if ((mode & 1) && (dxmt::ResolveD3D12MinMaxBindings(root, prepared.bindings, locations, error) != E_NOTIMPL ||
        locations.size() != saved_locations.size() || locations[0].texture.table_offset != 2)) return 1;
    dxmt::D3D12ConvertedShader converted_ps, converted_vs;
    if (dxmt::ConvertD3D12MinMaxComputeShader(prepared, root, converted_ps, &native->GetMSCCapabilities()) != E_INVALIDARG)
      return 1;
    hr = dxmt::ConvertD3D12MinMaxShader(prepared, root, converted_ps, &native->GetMSCCapabilities());
    if (FAILED(hr) || converted_ps.metallib.empty() || converted_ps.entry_point.empty()) {
      std::printf("pixel conversion failed %08lx\n", (unsigned long)hr); return 1;
    }
    auto mislabeled = prepared; mislabeled.stage = Stage::Compute;
    const auto saved_metallib = converted_ps.metallib;
    const auto saved_entry = converted_ps.entry_point;
    if (SUCCEEDED(dxmt::ConvertD3D12MinMaxShader(mislabeled, root, converted_ps, &native->GetMSCCapabilities())) ||
        converted_ps.metallib != saved_metallib || converted_ps.entry_point != saved_entry) return 1;
    hr = dxmt::ConvertD3D12Shader(vertex, DXMT_MSC_STAGE_VERTEX, converted_vs,
        root.layout.bytecode.data(), root.layout.bytecode.size(), nullptr, 0, &native->GetMSCCapabilities());
    if (FAILED(hr)) return 1;
    WMT::Error metal_error;
    auto ps_lib = metal.newLibrary(converted_ps.metallib.data(), converted_ps.metallib.size(), metal_error);
    auto vs_lib = metal.newLibrary(converted_vs.metallib.data(), converted_vs.metallib.size(), metal_error);
    if (!ps_lib || !vs_lib) return 1;
    auto ps_function = ps_lib.newFunction(converted_ps.entry_point.c_str());
    auto vs_function = vs_lib.newFunction(converted_vs.entry_point.c_str());
    if (!ps_function || !vs_function) return 1;
    WMTRenderPipelineInfo info; WMT::InitializeRenderPipelineInfo(info);
    info.vertex_function = vs_function.handle; info.fragment_function = ps_function.handle;
    info.colors[0].pixel_format = WMTPixelFormatRGBA8Unorm;
    auto pso = metal.newRenderPipelineState(info, metal_error);
    if (!pso) {
      const auto message = metal_error ? metal_error.description().getUTF8String() : "unknown";
      std::printf("native render PSO failed: %s\n", message.c_str()); return 1;
    }
    const auto unchanged_locations = [&] {
      return locations.size() == saved_locations.size() &&
          !std::memcmp(locations.data(), saved_locations.data(), locations.size() * sizeof(locations[0]));
    };
    if (dxmt::ResolveD3D12MinMaxBindings(root, prepared.bindings, locations, error,
        static_cast<Stage>(99)) != E_INVALIDARG || !unchanged_locations()) return 1;
    application.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
    dxmt::D3D12MinMaxRoot denied;
    if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, 2, denied, error)) ||
        dxmt::ResolveD3D12MinMaxBindings(denied, prepared.bindings, locations, error, Stage::Pixel) != E_NOTIMPL ||
        !unchanged_locations()) return 1;
    application.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    if (static_sampler) samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    else parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    dxmt::D3D12MinMaxRoot wrong_visibility;
    if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, 2, wrong_visibility, error)) ||
        dxmt::ResolveD3D12MinMaxBindings(wrong_visibility, prepared.bindings, locations, error, Stage::Pixel) != E_NOTIMPL ||
        !unchanged_locations()) return 1;
    std::printf("MINMAX_FRAGMENT mode=%u validated pixel artifact/native render PSO PASS (no draw)\n", mode);
  }
  std::puts("MinMax pixel compiler integration PASS; graphics runtime/readback remains open");
  return 0;
}

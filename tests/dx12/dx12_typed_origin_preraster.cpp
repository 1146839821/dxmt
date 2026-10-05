#include "d3d12_device.hpp"
#include "d3d12_typed_origin.hpp"
#include "d3d12_shader_converter.hpp"
#include "log/log.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

dxmt::Logger dxmt::Logger::s_instance("dx12_typed_origin_preraster");
template <typename T> struct ReleaseCOM { void operator()(T *p) const { if (p) p->Release(); } };
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;

int wmain(int argc, wchar_t **argv) {
  if (argc != 4) return 1;
  D3D12_SHADER_VISIBILITY stage;
  if (!wcscmp(argv[3], L"vertex")) stage = D3D12_SHADER_VISIBILITY_VERTEX;
  else if (!wcscmp(argv[3], L"geometry")) stage = D3D12_SHADER_VISIBILITY_GEOMETRY;
  else if (!wcscmp(argv[3], L"hull")) stage = D3D12_SHADER_VISIBILITY_HULL;
  else if (!wcscmp(argv[3], L"domain")) stage = D3D12_SHADER_VISIBILITY_DOMAIN;
  else return 1;
  HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 1;
  LARGE_INTEGER length = {}; DWORD read = 0;
  bool loaded = GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart <= 32 * 1024 * 1024;
  std::vector<uint8_t> input;
  if (loaded) { input.resize(length.QuadPart); loaded = ReadFile(file, input.data(), input.size(), &read, nullptr) && read == input.size(); }
  CloseHandle(file);
  if (!loaded) return 1;
  const D3D12_SHADER_BYTECODE bytecode = {input.data(), input.size()};
  const auto check = [](HRESULT hr, const char *what, const std::string &error = {}) {
    if (FAILED(hr)) std::printf("%s failed %08lx %s\n", what, (unsigned long)hr, error.c_str());
    return SUCCEEDED(hr);
  };
  ID3D12Device *raw = nullptr;
  if (!check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw)), "device")) return 1;
  OwnedCOM<ID3D12Device> device(raw);
  auto *native = static_cast<dxmt::MTLD3D12Device *>(device.get());
  D3D12_DESCRIPTOR_RANGE1 range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
      D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0};
  D3D12_ROOT_PARAMETER1 parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {1, &range}; parameter.ShaderVisibility = stage;
  D3D12_ROOT_SIGNATURE_DESC1 desc = {1, &parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  dxmt::D3D12TypedOriginRoot root;
  std::string diagnostics;
  if (!check(dxmt::PrepareD3D12TypedOriginRoot(desc, root, diagnostics), "root", diagnostics)) return 1;
  for (bool shared : {false, true}) {
    dxmt::D3D12TypedOriginShader prepared;
    if (!check(dxmt::PrepareD3D12TypedOriginShader(bytecode, argv[2], prepared, diagnostics,
        stage, shared ? 3 : 0, shared ? 8 : 0), "prepare", diagnostics)) return 1;
    std::printf("PREPARED stage=%u offset=%u count=%u bindings=%zu\n", unsigned(prepared.visibility),
        prepared.record_offset, prepared.record_count, prepared.bindings.size());
    if (prepared.bindings.size() != 1 || prepared.bindings[0].resource_class ||
        prepared.bindings[0].register_space || prepared.bindings[0].shader_register ||
        prepared.visibility != stage || prepared.record_offset != (shared ? 3u : 0u) ||
        prepared.record_count != (shared ? 8u : 1u)) {
      std::puts("prepared binding/layout mismatch"); return 1;
    }
    const auto saved = prepared;
    const auto unchanged = [&] { return prepared.bytecode == saved.bytecode &&
        prepared.visibility == saved.visibility && prepared.record_offset == saved.record_offset &&
        prepared.record_count == saved.record_count && prepared.bindings.size() == saved.bindings.size() &&
        !std::memcmp(prepared.bindings.data(), saved.bindings.data(), sizeof(prepared.bindings[0])); };
    if (dxmt::PrepareD3D12TypedOriginShader(bytecode, argv[2], prepared, diagnostics, stage, 8, 8) != E_INVALIDARG || !unchanged()) {
      std::puts("invalid interval rejection/publication failed"); return 1;
    }
    auto wrong = stage == D3D12_SHADER_VISIBILITY_VERTEX ? D3D12_SHADER_VISIBILITY_GEOMETRY : D3D12_SHADER_VISIBILITY_VERTEX;
    if (SUCCEEDED(dxmt::PrepareD3D12TypedOriginShader(bytecode, argv[2], prepared, diagnostics, wrong)) || !unchanged()) {
      std::puts("wrong-stage rejection/publication failed"); return 1;
    }
    std::vector<dxmt::D3D12TypedOriginBindingLocation> locations;
    if (!check(dxmt::ResolveD3D12TypedOriginBindings(root, prepared.bindings, locations, diagnostics, stage), "resolve", diagnostics) ||
        locations.size() != 1 || locations[0].parameter_index || locations[0].table_offset || locations[0].flags != range.Flags) {
      std::puts("resolved location mismatch"); return 1;
    }
    dxmt::D3D12ConvertedShader converted;
    dxmt_msc_input_layout layout = {};
    const bool vertex = stage == D3D12_SHADER_VISIBILITY_VERTEX;
    if (!check(dxmt::ConvertD3D12TypedOriginShader(prepared, root, converted, &native->GetMSCCapabilities(),
        vertex ? &layout : nullptr, vertex ? DXMT_MSC_COMPILE_FLAG_GEOMETRY_EMULATION : 0), "MSC")) return 1;
    if (converted.metallib.empty() || converted.entry_point.empty() || (vertex && converted.stage_in_metallib.empty())) {
      std::printf("MSC artifact incomplete bytes=%zu entry=%s stage_in=%zu\n", converted.metallib.size(),
          converted.entry_point.c_str(), converted.stage_in_metallib.size()); return 1;
    }
    std::printf("PRERASTER_PREPARE_MSC stage=%u offset=%u count=%u bytes=%zu PASS (not PSO/draw acceptance)\n",
        unsigned(stage), prepared.record_offset, prepared.record_count, converted.metallib.size());
  }
  return 0;
}

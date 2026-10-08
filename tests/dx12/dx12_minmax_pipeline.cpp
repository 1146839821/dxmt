#include "d3d12_device.hpp"
#include "d3d12_minmax_pipeline.hpp"
#include <cstdio>
#include <memory>
#include <cwchar>

template <typename T> struct ReleaseCOM {
  void operator()(T *value) const { if (value) value->Release(); }
};
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;

int wmain(int argc, wchar_t **argv) {
  if (argc != 4 || (std::wcscmp(argv[3], L"1") && std::wcscmp(argv[3], L"2"))) return 1;
  const size_t expected_pairs = argv[3][0] - L'0';
  HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 1;
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 32 * 1024 * 1024) {
    CloseHandle(file); return 1;
  }
  std::vector<uint8_t> bytes(size.QuadPart);
  DWORD read = 0;
  bool loaded = ReadFile(file, bytes.data(), bytes.size(), &read, nullptr) && read == bytes.size();
  CloseHandle(file);
  if (!loaded) return 1;
  ID3D12Device *raw_device = nullptr;
  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device));
  OwnedCOM<ID3D12Device> device(raw_device);
  if (FAILED(hr)) return 1;
  for (uint32_t pass = 0; pass < 4; ++pass) {
    bool legacy = pass & 1, use_static = pass & 2;
    D3D12_DESCRIPTOR_RANGE1 ranges[3] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, 5},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 3}};
    D3D12_ROOT_PARAMETER1 parameters[2] = {};
    for (auto &parameter : parameters) parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable = {2, ranges};
    parameters[1].DescriptorTable = {1, ranges + 2};
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    for (uint32_t i = 0; i < 2; ++i) {
      samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
      samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
      samplers[i].ShaderRegister = i;
    }
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    root_desc.Desc_1_1 = {use_static ? 1u : 2u, parameters, use_static ? 2u : 0u,
        use_static ? samplers : nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_DESCRIPTOR_RANGE old_ranges[3] = {};
    for (uint32_t i = 0; i < 3; ++i)
      old_ranges[i] = {ranges[i].RangeType, ranges[i].NumDescriptors, ranges[i].BaseShaderRegister,
          ranges[i].RegisterSpace, ranges[i].OffsetInDescriptorsFromTableStart};
    D3D12_ROOT_PARAMETER old_parameters[2] = {};
    for (auto &parameter : old_parameters) parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    old_parameters[0].DescriptorTable = {2, old_ranges};
    old_parameters[1].DescriptorTable = {1, old_ranges + 2};
    if (legacy) {
      root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
      root_desc.Desc_1_0 = {use_static ? 1u : 2u, old_parameters, use_static ? 2u : 0u,
          use_static ? samplers : nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    }
    ID3DBlob *raw_blob = nullptr;
    hr = D3D12SerializeVersionedRootSignature(&root_desc, &raw_blob, nullptr);
    OwnedCOM<ID3DBlob> blob(raw_blob);
    if (FAILED(hr)) return 1;
    ID3D12RootSignature *raw_root = nullptr;
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&raw_root));
    OwnedCOM<ID3D12RootSignature> root(raw_root);
    if (FAILED(hr)) return 1;
    auto caller_bytes = bytes;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root.get(); desc.CS = {caller_bytes.data(), caller_bytes.size()};
    ID3D12PipelineState *raw_pso = nullptr;
    hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&raw_pso));
    OwnedCOM<ID3D12PipelineState> pso(raw_pso);
    if (FAILED(hr)) { std::printf("application PSO failed %08lx\n", (unsigned long)hr); return 1; }
    caller_bytes.assign(caller_bytes.size(), 0); root.reset();
    auto *implementation = static_cast<dxmt::MTLD3D12ComputePipelineState *>(pso.get());
    const auto original = implementation->pso.handle;
    const dxmt::D3D12MinMaxComputeVariant *variant = nullptr;
    if (implementation->GetMinMaxVariant(nullptr, nullptr) != E_POINTER ||
        implementation->GetMinMaxVariant(nullptr, &variant) != E_INVALIDARG || variant ||
        implementation->GetMinMaxVariant(L"relative", &variant) != E_INVALIDARG || variant) return 1;
    hr = implementation->GetMinMaxVariant(argv[2], &variant);
    if (FAILED(hr) || !variant) { std::printf("variant failed %08lx\n", (unsigned long)hr); return 1; }
    if (!variant->pso || variant->pso.handle == original || implementation->pso.handle != original ||
        variant->bindings.size() != expected_pairs ||
        variant->locations.size() != variant->bindings.size() || variant->root.pair_count != variant->bindings.size() ||
        variant->root.layout.application_parameter_count != (use_static ? 1u : 2u) ||
        variant->threadgroup_size.width != implementation->threadgroup_size.width ||
        variant->threadgroup_size.height != implementation->threadgroup_size.height ||
        variant->threadgroup_size.depth != implementation->threadgroup_size.depth) return 1;
    for (size_t i = 0; i < variant->bindings.size(); ++i) {
      const auto &binding = variant->bindings[i]; const auto &location = variant->locations[i];
      if (binding.texture_space || binding.texture_register || binding.sampler_space || binding.sampler_register != i ||
          location.texture.parameter_index || location.texture.table_offset != 5 ||
          location.texture.flags != (legacy ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
              D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE : D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC)) return 1;
      if (use_static ? (location.sampler.static_sampler_index != i || location.sampler.parameter_index != UINT32_MAX) :
          (location.sampler.parameter_index != 1 || location.sampler.table_offset != 3 + i ||
           location.sampler.flags != (legacy ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE :
               D3D12_DESCRIPTOR_RANGE_FLAG_NONE))) return 1;
    }
    const dxmt::D3D12MinMaxComputeVariant *again = nullptr;
    if (FAILED(implementation->GetMinMaxVariant(argv[2], &again)) || again != variant ||
        implementation->GetMinMaxVariant(L"Z:/different", &again) != E_INVALIDARG || again) return 1;
    std::printf("MINMAX_PSO_CREATED pass=%u pairs=%zu\n", pass, variant->bindings.size());
  }
  std::puts("MinMax actual PSO PASS (not dispatch/readback acceptance)");
  return 0;
}

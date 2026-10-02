#include "d3d12_device.hpp"
#include "d3d12_typed_origin_pipeline.hpp"
#include <cstdio>
#include <memory>

template <typename T> struct ReleaseCOM {
  void operator()(T *value) const { if (value) value->Release(); }
};
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;

int wmain(int argc, wchar_t **argv) {
  if (argc != 3) return 1;
  HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 1;
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 32 * 1024 * 1024) {
    CloseHandle(file); return 1;
  }
  std::vector<uint8_t> bytes(size.QuadPart);
  DWORD read = 0;
  const bool loaded = ReadFile(file, bytes.data(), bytes.size(), &read, nullptr) && read == bytes.size();
  CloseHandle(file);
  if (!loaded) return 1;
  ID3D12Device *device_raw = nullptr;
  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_raw));
  OwnedCOM<ID3D12Device> device(device_raw);
  if (FAILED(hr)) return 1;
  for (uint32_t pass = 0; pass < 3; ++pass) {
    D3D12_DESCRIPTOR_RANGE1 range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, 5};
    D3D12_ROOT_PARAMETER1 parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable = {1, &range};
    D3D12_DESCRIPTOR_RANGE1 appended[2] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 10, 7, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, 0}, range};
    appended[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    if (pass) parameter.DescriptorTable = {2, appended};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    root_desc.Desc_1_1 = {1, &parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_DESCRIPTOR_RANGE legacy_ranges[2] = {};
    for (uint32_t i = 0; i < 2; ++i)
      legacy_ranges[i] = {appended[i].RangeType, appended[i].NumDescriptors, appended[i].BaseShaderRegister,
          appended[i].RegisterSpace, appended[i].OffsetInDescriptorsFromTableStart};
    D3D12_ROOT_PARAMETER legacy_parameter = {};
    legacy_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    legacy_parameter.DescriptorTable = {2, legacy_ranges};
    if (pass) {
      root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
      root_desc.Desc_1_0 = {1, &legacy_parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    }
    ID3DBlob *blob_raw = nullptr;
    hr = D3D12SerializeVersionedRootSignature(&root_desc, &blob_raw, nullptr);
    OwnedCOM<ID3DBlob> blob(blob_raw);
    if (FAILED(hr)) return 1;
    ID3D12RootSignature *root_raw = nullptr;
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_raw));
    OwnedCOM<ID3D12RootSignature> root(root_raw);
    if (FAILED(hr)) return 1;
    auto caller_bytes = bytes;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root.get();
    desc.CS = {caller_bytes.data(), caller_bytes.size()};
    ID3D12PipelineState *pso_raw = nullptr;
    hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso_raw));
    OwnedCOM<ID3D12PipelineState> pso(pso_raw);
    if (FAILED(hr)) { std::printf("application PSO failed 0x%08lx\n", (unsigned long)hr); return 1; }
    caller_bytes.assign(caller_bytes.size(), 0);
    root.reset();
    auto *implementation = static_cast<dxmt::MTLD3D12ComputePipelineState *>(pso.get());
    const auto original = implementation->pso.handle;
    const dxmt::D3D12TypedOriginComputeVariant *variant = nullptr;
    if (implementation->GetTypedOriginVariant(L"relative", &variant) != E_INVALIDARG || variant) return 1;
    hr = implementation->GetTypedOriginVariant(argv[2], &variant);
    if (FAILED(hr) || !variant) { std::printf("variant failed 0x%08lx\n", (unsigned long)hr); return 1; }
    if (!variant->pso || variant->pso.handle == original || variant->bindings.size() != 2 ||
        variant->locations.size() != 2 || implementation->pso.handle != original ||
        variant->root.application_parameter_count != 1 || variant->root.hidden_parameter_index != 1 ||
        variant->threadgroup_size.width != implementation->threadgroup_size.width ||
        variant->threadgroup_size.height != implementation->threadgroup_size.height ||
        variant->threadgroup_size.depth != implementation->threadgroup_size.depth) return 1;
    for (uint32_t i = 0; i < 2; ++i) {
      if (variant->bindings[i].resource_class != 1 || variant->bindings[i].register_space ||
          variant->bindings[i].shader_register != i || variant->locations[i].parameter_index ||
          variant->locations[i].table_offset != 5 + i ||
          variant->locations[i].flags != (pass ?
              D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE :
              D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC)) return 1;
    }
    const dxmt::D3D12TypedOriginComputeVariant *again = nullptr;
    if (FAILED(implementation->GetTypedOriginVariant(argv[2], &again)) || again != variant) return 1;
    if (implementation->GetTypedOriginVariant(L"Z:/different", &again) != E_INVALIDARG || again) return 1;
    std::printf("ORIGIN_PSO_CREATED pass=%u records=%zu offsets=5,6 tg=%llu,%llu,%llu\n", pass,
        variant->bindings.size(), (unsigned long long)variant->threadgroup_size.width,
        (unsigned long long)variant->threadgroup_size.height, (unsigned long long)variant->threadgroup_size.depth);
  }
  std::puts("typed-origin actual PSO PASS (not dispatch/readback acceptance)");
  return 0;
}

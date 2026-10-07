#include "d3d12_minmax.hpp"
#include "d3d12_device.hpp"
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <memory>

template <typename T> struct ReleaseCOM {
  void operator()(T *value) const { if (value) value->Release(); }
};
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;

static bool Run(ID3D12Device *device, bool legacy, bool static_samplers, bool unbounded) {
  D3D12_DESCRIPTOR_RANGE1 texture[] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 5, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, 2},
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, unbounded ? UINT32_MAX : 2u, 7, 3,
          D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND}};
  D3D12_DESCRIPTOR_RANGE1 sampler = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 5, 4,
      D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 3};
  D3D12_ROOT_PARAMETER1 parameters[3] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable = {2, texture};
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants = {3, 7, static_samplers ? 59u : 58u};
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[2].DescriptorTable = {1, &sampler};
  D3D12_STATIC_SAMPLER_DESC statics[2] = {};
  for (unsigned i = 0; i < 2; ++i) {
    statics[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    statics[i].AddressU = statics[i].AddressV = statics[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    statics[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    statics[i].MaxLOD = FLT_MAX;
    statics[i].ShaderRegister = 5 + i;
    statics[i].RegisterSpace = 4;
  }
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
  desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  desc.Desc_1_1 = {static_samplers ? 2u : 3u, parameters, static_samplers ? 2u : 0u, statics,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  D3D12_DESCRIPTOR_RANGE texture0[2], sampler0 = {sampler.RangeType, 2, 5, 4, 3};
  D3D12_ROOT_PARAMETER parameters0[3] = {};
  for (unsigned i = 0; i < 2; ++i)
    texture0[i] = {texture[i].RangeType, texture[i].NumDescriptors, texture[i].BaseShaderRegister,
        texture[i].RegisterSpace, texture[i].OffsetInDescriptorsFromTableStart};
  parameters0[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters0[0].DescriptorTable = {2, texture0};
  parameters0[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters0[1].Constants = parameters[1].Constants;
  parameters0[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters0[2].DescriptorTable = {1, &sampler0};
  if (legacy) {
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
    desc.Desc_1_0 = {static_samplers ? 2u : 3u, parameters0, static_samplers ? 2u : 0u, statics,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  }
  ID3DBlob *blob_raw = nullptr;
  if (FAILED(D3D12SerializeVersionedRootSignature(&desc, &blob_raw, nullptr))) return false;
  OwnedCOM<ID3DBlob> blob(blob_raw);
  ID3D12VersionedRootSignatureDeserializer *decoded_raw = nullptr;
  if (FAILED(D3D12CreateVersionedRootSignatureDeserializer(blob->GetBufferPointer(), blob->GetBufferSize(),
      IID_PPV_ARGS(&decoded_raw)))) return false;
  OwnedCOM<ID3D12VersionedRootSignatureDeserializer> decoded(decoded_raw);
  const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *converted = nullptr;
  if (FAILED(decoded->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &converted))) return false;
  auto application = converted->Desc_1_1;
  for (const auto direct_flag : {D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED,
      D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED}) {
    auto direct = application;
    direct.Flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(direct.Flags | direct_flag);
    dxmt::D3D12MinMaxRoot rejected;
    rejected.pair_count = 7;
    rejected.layout.bytecode = {1, 2, 3};
    std::string diagnostics;
    if (dxmt::PrepareD3D12MinMaxRoot(direct, 2, rejected, diagnostics) != E_NOTIMPL ||
        rejected.pair_count != 7 || rejected.layout.bytecode != std::vector<uint8_t>({1, 2, 3}) ||
        diagnostics.empty()) return false;
  }
  ID3D12RootSignature *application_root_raw = nullptr;
  if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
      IID_PPV_ARGS(&application_root_raw)))) return false;
  OwnedCOM<ID3D12RootSignature> application_root(application_root_raw);
  auto *implementation = static_cast<dxmt::MTLD3D12RootSignature *>(application_root.get());
  if (FAILED(implementation->InitializeMSCLayout())) return false;
  const auto application_size = implementation->MSCArgumentBufferSize;
  const auto upload_qwords = implementation->UploadQwords;
  const dxmt::D3D12MinMaxRoot *borrowed = nullptr;
  std::string error;
  HRESULT hr = implementation->GetMinMaxCompilerRoot(2, &borrowed);
  if (FAILED(hr)) { std::fprintf(stderr, "root hr=%08lx %s\n", (unsigned long)hr, error.c_str()); return false; }
  if (!borrowed) return false;
  const dxmt::D3D12MinMaxRoot *again = nullptr, *one = nullptr;
  if (FAILED(implementation->GetMinMaxCompilerRoot(1, &one)) || !one || one == borrowed ||
      FAILED(implementation->GetMinMaxCompilerRoot(2, &again)) || again != borrowed) return false;
  const void *original = nullptr;
  if (implementation->GetBlob(&original) != blob->GetBufferSize() ||
      std::memcmp(original, blob->GetBufferPointer(), blob->GetBufferSize()) ||
      implementation->MSCArgumentBufferSize != application_size || implementation->UploadQwords != upload_qwords)
    return false;
  dxmt::D3D12MinMaxRoot root = *borrowed;
  const auto &layout = root.layout;
  if (layout.application_cost != 60 || layout.application_parameter_count != application.NumParameters ||
      layout.hidden_parameter_index != application.NumParameters ||
      layout.layouts.size() != application.NumParameters + 3 + (static_samplers ? 1 : 0)) return false;
  const auto &cb = layout.layouts[layout.hidden_parameter_index];
  if (cb.resource_type != DXMT_MSC_RESOURCE_CBV || cb.shader_register || cb.register_space != 2 || cb.size_bytes != 8)
    return false;
  for (unsigned i = 1; i < 3; ++i)
    if (layout.layouts[layout.hidden_parameter_index + i].resource_type != DXMT_MSC_RESOURCE_TABLE ||
        layout.layouts[layout.hidden_parameter_index + i].size_bytes != 8) return false;
  if (static_samplers && layout.layouts.back().parameter_index != UINT32_MAX) return false;
  ID3D12VersionedRootSignatureDeserializer *compiler_raw = nullptr;
  if (FAILED(D3D12CreateVersionedRootSignatureDeserializer(layout.bytecode.data(), layout.bytecode.size(),
      IID_PPV_ARGS(&compiler_raw)))) return false;
  OwnedCOM<ID3D12VersionedRootSignatureDeserializer> compiler(compiler_raw);
  const auto *compiler_desc = compiler->GetUnconvertedRootSignatureDesc();
  if (!compiler_desc || compiler_desc->Version != D3D_ROOT_SIGNATURE_VERSION_1_1) return false;
  const auto &augmented = compiler_desc->Desc_1_1;
  const auto &hidden_cb = augmented.pParameters[layout.hidden_parameter_index];
  if (hidden_cb.ParameterType != D3D12_ROOT_PARAMETER_TYPE_CBV || hidden_cb.Descriptor.ShaderRegister ||
      hidden_cb.Descriptor.RegisterSpace != 2 || hidden_cb.Descriptor.Flags != D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE ||
      hidden_cb.ShaderVisibility != D3D12_SHADER_VISIBILITY_ALL) return false;
  for (unsigned i = 1; i < 3; ++i) {
    const auto &table = augmented.pParameters[layout.hidden_parameter_index + i];
    if (table.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE || table.DescriptorTable.NumDescriptorRanges != 1 ||
        table.ShaderVisibility != D3D12_SHADER_VISIBILITY_ALL) return false;
    const auto &range = table.DescriptorTable.pDescriptorRanges[0];
    const auto expected_flags = i == 1 ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE : D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    if (range.RangeType != (i == 1 ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER) ||
        range.NumDescriptors != (i == 1 ? 2u : 4u) || range.BaseShaderRegister || range.RegisterSpace != 2 ||
        range.OffsetInDescriptorsFromTableStart || range.Flags != expected_flags) return false;
  }
  std::vector<dxmt_msc_minmax_binding> bindings = {{3, 7, 4, 5}, {3, 8, 4, 6}};
  std::vector<dxmt::D3D12MinMaxPairLocation> locations;
  if (FAILED(dxmt::ResolveD3D12MinMaxBindings(root, bindings, locations, error)) || locations.size() != 2) return false;
  for (unsigned i = 0; i < 2; ++i) {
    const auto &p = locations[i];
    const auto flags = legacy ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE : D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    if (p.texture.parameter_index != 0 || p.texture.table_offset != 4 + i || p.texture.flags != flags ||
        p.texture.static_sampler_index != UINT32_MAX) return false;
    if (static_samplers) {
      if (p.sampler.parameter_index != UINT32_MAX || p.sampler.static_sampler_index != i) return false;
    } else if (p.sampler.parameter_index != 2 || p.sampler.table_offset != 3 + i ||
        !(p.sampler.flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE)) return false;
  }
  const auto saved_locations = locations;
  bindings[1].sampler_register = 99;
  if (SUCCEEDED(dxmt::ResolveD3D12MinMaxBindings(root, bindings, locations, error)) ||
      locations.size() != saved_locations.size() ||
      std::memcmp(locations.data(), saved_locations.data(), locations.size() * sizeof(locations[0]))) return false;
  bindings[1].sampler_register = 6;
  auto reject_locations = [&](D3D12_ROOT_SIGNATURE_DESC1 changed) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned = {};
    versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned.Desc_1_1 = changed;
    ID3DBlob *raw = nullptr;
    if (FAILED(D3D12SerializeVersionedRootSignature(&versioned, &raw, nullptr))) return false;
    OwnedCOM<ID3DBlob> bytes(raw);
    auto altered = root;
    const auto *data = static_cast<const uint8_t *>(bytes->GetBufferPointer());
    altered.layout.bytecode.assign(data, data + bytes->GetBufferSize());
    return dxmt::ResolveD3D12MinMaxBindings(altered, bindings, locations, error) == E_NOTIMPL &&
        locations.size() == saved_locations.size() &&
        !std::memcmp(locations.data(), saved_locations.data(), locations.size() * sizeof(locations[0]));
  };
  std::vector<D3D12_ROOT_PARAMETER1> changed_parameters(augmented.pParameters, augmented.pParameters + augmented.NumParameters);
  auto changed = augmented;
  changed.pParameters = changed_parameters.data();
  changed_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  if (!reject_locations(changed)) return false;
  changed_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_DESCRIPTOR_RANGE1 changed_ranges[2] = {texture[0], texture[1]};
  changed_parameters[0].DescriptorTable = {2, changed_ranges};
  changed_ranges[0].BaseShaderRegister = 7;
  if (!reject_locations(changed)) return false;
  changed_ranges[0] = texture[0];
  changed_ranges[0].RegisterSpace = 8;
  changed_ranges[0].NumDescriptors = UINT32_MAX;
  if (!reject_locations(changed)) return false;
  if (static_samplers) {
    changed_ranges[0] = texture[0];
    changed_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    changed_parameters[1].DescriptorTable = {1, &sampler};
    if (!reject_locations(changed)) return false;
  }
  const auto saved_root = root;
  auto unchanged = [&] { return root.layout.bytecode == saved_root.layout.bytecode &&
      root.pair_count == saved_root.pair_count && root.layout.argument_buffer_size == saved_root.layout.argument_buffer_size &&
      root.layout.application_cost == saved_root.layout.application_cost &&
      root.layout.application_parameter_count == saved_root.layout.application_parameter_count &&
      root.layout.hidden_parameter_index == saved_root.layout.hidden_parameter_index &&
      root.layout.layouts.size() == saved_root.layout.layouts.size() &&
      !std::memcmp(root.layout.layouts.data(), saved_root.layout.layouts.data(), root.layout.layouts.size() * sizeof(cb)); };
  if (dxmt::PrepareD3D12MinMaxRoot(application, 0, root, error) != E_INVALIDARG || !unchanged()) return false;
  if (dxmt::PrepareD3D12MinMaxRoot(application, 65, root, error) != E_INVALIDARG || !unchanged()) return false;
  auto invalid = application;
  D3D12_ROOT_PARAMETER1 collision = {};
  collision.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  collision.Constants = {9, 2, 1};
  invalid.NumParameters = 1;
  invalid.pParameters = &collision;
  if (dxmt::PrepareD3D12MinMaxRoot(invalid, 2, root, error) != E_NOTIMPL || !unchanged()) return false;
  for (auto type : {D3D12_ROOT_PARAMETER_TYPE_CBV, D3D12_ROOT_PARAMETER_TYPE_SRV, D3D12_ROOT_PARAMETER_TYPE_UAV}) {
    collision.ParameterType = type;
    collision.Descriptor = {9, 2, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    if (dxmt::PrepareD3D12MinMaxRoot(invalid, 2, root, error) != E_NOTIMPL || !unchanged()) return false;
  }
  collision.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  D3D12_DESCRIPTOR_RANGE1 private_range = sampler;
  private_range.RegisterSpace = 2;
  collision.DescriptorTable = {1, &private_range};
  if (dxmt::PrepareD3D12MinMaxRoot(invalid, 2, root, error) != E_NOTIMPL || !unchanged()) return false;
  invalid.NumParameters = 0;
  auto private_static = statics[0];
  private_static.RegisterSpace = 2;
  invalid.NumStaticSamplers = 1;
  invalid.pStaticSamplers = &private_static;
  if (dxmt::PrepareD3D12MinMaxRoot(invalid, 2, root, error) != E_NOTIMPL || !unchanged()) return false;
  invalid = application;
  invalid.NumParameters = 1;
  invalid.pParameters = &collision;
  collision.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  collision.Constants = {9, 9, 61};
  if (dxmt::PrepareD3D12MinMaxRoot(invalid, 2, root, error) != E_NOTIMPL || !unchanged()) return false;
  std::printf("MINMAX_ROOT RS%s static=%u unbounded=%u reflected=%zu cost=64 locations=verified\n",
      legacy ? "1.0" : "1.1", static_samplers, unbounded, layout.layouts.size());
  return true;
}
int main() {
  ID3D12Device *raw = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw)))) return 1;
  OwnedCOM<ID3D12Device> device(raw);
  bool ok = Run(device.get(), false, false, false) && Run(device.get(), true, false, false) &&
      Run(device.get(), false, true, false) && Run(device.get(), true, true, false) && Run(device.get(), false, false, true);
  return ok ? 0 : 1;
}

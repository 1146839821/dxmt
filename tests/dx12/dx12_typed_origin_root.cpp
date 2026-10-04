#include "d3d12_device.hpp"
#include "d3d12_typed_origin.hpp"
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <memory>

template <typename T> struct ReleaseCOM {
  void operator()(T *value) const { if (value) value->Release(); }
};
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;

static bool Run(ID3D12Device *device, bool legacy, uint32_t constants, bool collision, bool unbounded) {
  D3D12_DESCRIPTOR_RANGE1 range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, unbounded ? UINT32_MAX : 2u, 7, 3,
      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
  D3D12_ROOT_PARAMETER1 parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable = {1, &range};
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants = {collision ? 0u : 3u, collision ? 1u : 7u, constants};
  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  sampler.MaxLOD = FLT_MAX;
  sampler.RegisterSpace = 9;
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
  desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  desc.Desc_1_1 = {2, parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  D3D12_DESCRIPTOR_RANGE range0 = {range.RangeType, range.NumDescriptors, range.BaseShaderRegister,
      range.RegisterSpace, range.OffsetInDescriptorsFromTableStart};
  D3D12_ROOT_PARAMETER parameters0[2] = {};
  parameters0[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters0[0].DescriptorTable = {1, &range0};
  parameters0[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters0[1].Constants = parameters[1].Constants;
  if (legacy) {
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
    desc.Desc_1_0 = {2, parameters0, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  }
  ID3DBlob *blob_raw = nullptr;
  HRESULT hr = D3D12SerializeVersionedRootSignature(&desc, &blob_raw, nullptr);
  OwnedCOM<ID3DBlob> blob(blob_raw);
  if (FAILED(hr)) return false;
  ID3D12RootSignature *root_raw = nullptr;
  hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_raw));
  OwnedCOM<ID3D12RootSignature> root(root_raw);
  if (FAILED(hr)) return false;
  auto *implementation = static_cast<dxmt::MTLD3D12RootSignature *>(root.get());
  if (FAILED(implementation->InitializeMSCLayout())) return false;
  const uint64_t application_size = implementation->MSCArgumentBufferSize;
  const uint32_t upload_qwords = implementation->UploadQwords;
  const uint32_t parameter_slots = implementation->ParameterSlots;
  std::vector<uint32_t> staging_offsets(implementation->SlotQwordOffsets,
      implementation->SlotQwordOffsets + parameter_slots);
  std::vector<dxmt_msc_root_parameter_layout> application_layout(
      implementation->MSCParameterLayouts, implementation->MSCParameterLayouts + implementation->MSCParameterCount);
  const dxmt::D3D12TypedOriginRoot *compiler = nullptr;
  hr = implementation->GetTypedOriginCompilerRoot(&compiler);
  if (collision || constants > 61) return hr == E_NOTIMPL && !compiler;
  if (FAILED(hr) || !compiler || compiler->application_parameter_count != 2 ||
      compiler->application_cost != constants + 1 || compiler->hidden_parameter_index != 2 ||
      compiler->layouts.size() != 4 || compiler->argument_buffer_size != application_size + 8) return false;
  const dxmt::D3D12TypedOriginRoot *again = nullptr;
  if (FAILED(implementation->GetTypedOriginCompilerRoot(&again)) || again != compiler) return false;
  const auto &hidden = compiler->layouts[2];
  if (hidden.resource_type != DXMT_MSC_RESOURCE_CBV || hidden.shader_register || hidden.register_space != 1 ||
      hidden.size_bytes != 8 || compiler->layouts[3].parameter_index != UINT32_MAX ||
      compiler->layouts[3].top_level_offset == application_layout.back().top_level_offset) return false;
  const void *app_blob = nullptr;
  if (implementation->GetBlob(&app_blob) != blob->GetBufferSize() ||
      std::memcmp(app_blob, blob->GetBufferPointer(), blob->GetBufferSize()) ||
      implementation->MSCArgumentBufferSize != application_size ||
      implementation->UploadQwords != upload_qwords || implementation->ParameterSlots != parameter_slots ||
      std::memcmp(implementation->SlotQwordOffsets, staging_offsets.data(), parameter_slots * sizeof(uint32_t)) ||
      implementation->MSCParameterCount != application_layout.size() ||
      std::memcmp(implementation->MSCParameterLayouts, application_layout.data(),
          application_layout.size() * sizeof(application_layout[0]))) return false;
  ID3D12VersionedRootSignatureDeserializer *decoded_raw = nullptr;
  hr = D3D12CreateVersionedRootSignatureDeserializer(compiler->bytecode.data(), compiler->bytecode.size(),
      IID_PPV_ARGS(&decoded_raw));
  OwnedCOM<ID3D12VersionedRootSignatureDeserializer> decoded(decoded_raw);
  if (FAILED(hr)) return false;
  const auto *output = decoded->GetUnconvertedRootSignatureDesc();
  const auto &compiler_desc = output->Desc_1_1;
  const auto &copied = compiler_desc.pParameters[0].DescriptorTable.pDescriptorRanges[0];
  const uint32_t expected_flags = legacy ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE : D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
  if (output->Version != D3D_ROOT_SIGNATURE_VERSION_1_1 || compiler_desc.NumParameters != 3 ||
      compiler_desc.Flags != D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT ||
      copied.Flags != expected_flags || copied.NumDescriptors != range.NumDescriptors ||
      copied.BaseShaderRegister != 7 || copied.RegisterSpace != 3 ||
      copied.OffsetInDescriptorsFromTableStart != range.OffsetInDescriptorsFromTableStart ||
      compiler_desc.pParameters[2].Descriptor.Flags != D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE ||
      compiler_desc.NumStaticSamplers != 1 || compiler_desc.pStaticSamplers[0].RegisterSpace != 9) return false;
  std::printf("ROOT_PREPARED RS%s constants=%u unbounded=%u bytes=%zu hidden_offset=%llu sampler_offset=%llu\n",
      legacy ? "1.0" : "1.1", constants, unbounded, compiler->bytecode.size(),
      static_cast<unsigned long long>(hidden.top_level_offset),
      static_cast<unsigned long long>(compiler->layouts.back().top_level_offset));
  return true;
}

static bool RunStageBindings() {
  using namespace dxmt;
  D3D12_DESCRIPTOR_RANGE1 range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 7, 3,
      D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 2};
  D3D12_ROOT_PARAMETER1 parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {1, &range};
  D3D12_ROOT_SIGNATURE_DESC1 desc = {1, &parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  const std::vector<dxmt_msc_typed_origin_binding> bindings = {{1, 3, 8}};
  for (auto visibility : {D3D12_SHADER_VISIBILITY_ALL, D3D12_SHADER_VISIBILITY_PIXEL,
                         D3D12_SHADER_VISIBILITY_VERTEX}) {
    for (auto flags : {D3D12_ROOT_SIGNATURE_FLAG_NONE,
                       D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS,
                       D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS}) {
      parameter.ShaderVisibility = visibility;
      desc.Flags = flags;
      D3D12TypedOriginRoot root;
      std::string diagnostics;
      if (FAILED(PrepareD3D12TypedOriginRoot(desc, root, diagnostics))) return false;
      for (auto stage : {D3D12_SHADER_VISIBILITY_PIXEL, D3D12_SHADER_VISIBILITY_VERTEX,
                         D3D12_SHADER_VISIBILITY_ALL}) {
        std::vector<D3D12TypedOriginBindingLocation> locations = {{99, 17, D3D12_DESCRIPTOR_RANGE_FLAG_NONE}};
        const auto original = locations;
        const auto hr = ResolveD3D12TypedOriginBindings(root, bindings, locations, diagnostics, stage);
        const bool denied = (stage == D3D12_SHADER_VISIBILITY_PIXEL &&
            (flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS)) ||
            (stage == D3D12_SHADER_VISIBILITY_VERTEX &&
            (flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS));
        const bool supported = !denied && (visibility == D3D12_SHADER_VISIBILITY_ALL || visibility == stage);
        if (supported) {
          if (hr != S_OK || locations.size() != 1 || locations[0].parameter_index != 0 ||
              locations[0].table_offset != 3 || locations[0].flags != range.Flags) return false;
        } else if (hr != E_NOTIMPL || locations.size() != 1 ||
            std::memcmp(locations.data(), original.data(), sizeof(original[0]))) return false;
        const auto saved = locations;
        if (ResolveD3D12TypedOriginBindings(root, bindings, locations, diagnostics,
            D3D12_SHADER_VISIBILITY_GEOMETRY) != E_INVALIDARG || locations.size() != saved.size() ||
            std::memcmp(locations.data(), saved.data(), sizeof(saved[0]))) return false;
      }
    }
  }
  std::puts("typed-origin compute/vertex/pixel visibility/deny/flags/transactional resolution PASS");
  return true;
}

static bool RunDisjointStageBindings() {
  using namespace dxmt;
  D3D12_DESCRIPTOR_RANGE1 ranges[2] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 7, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, 2},
      {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 7, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 5}};
  D3D12_ROOT_PARAMETER1 parameters[2] = {};
  for (unsigned i = 0; i < 2; ++i) {
    parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[i].DescriptorTable = {1, &ranges[i]};
  }
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC1 desc = {2, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  const std::vector<dxmt_msc_typed_origin_binding> bindings = {{1, 3, 8}};
  D3D12TypedOriginRoot root;
  std::string diagnostics;
  if (FAILED(PrepareD3D12TypedOriginRoot(desc, root, diagnostics))) return false;
  for (unsigned i = 0; i < 2; ++i) {
    std::vector<D3D12TypedOriginBindingLocation> locations;
    if (ResolveD3D12TypedOriginBindings(root, bindings, locations, diagnostics,
        parameters[i].ShaderVisibility) != S_OK || locations.size() != 1 ||
        locations[0].parameter_index != i || locations[0].table_offset != ranges[i].OffsetInDescriptorsFromTableStart + 1 ||
        locations[0].flags != ranges[i].Flags) return false;
    const auto original = locations;
    // A later missing resource must not publish the earlier resolved location.
    const std::vector<dxmt_msc_typed_origin_binding> missing = {{1, 3, 8}, {1, 3, 99}};
    if (ResolveD3D12TypedOriginBindings(root, missing, locations, diagnostics,
        parameters[i].ShaderVisibility) != E_NOTIMPL || locations.size() != original.size() ||
        std::memcmp(locations.data(), original.data(), sizeof(original[0]))) return false;
  }
  // Overlapping ALL and stage-specific declarations are ambiguous to that stage.
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  if (FAILED(PrepareD3D12TypedOriginRoot(desc, root, diagnostics))) return false;
  std::vector<D3D12TypedOriginBindingLocation> locations = {{99, 17, D3D12_DESCRIPTOR_RANGE_FLAG_NONE}};
  const auto original = locations;
  if (ResolveD3D12TypedOriginBindings(root, bindings, locations, diagnostics,
      D3D12_SHADER_VISIBILITY_VERTEX) != E_NOTIMPL || locations.size() != original.size() ||
      std::memcmp(locations.data(), original.data(), sizeof(original[0]))) return false;
  std::puts("typed-origin disjoint VS/PS register identity and ambiguity PASS");
  return true;
}

int main() {
  ID3D12Device *raw = nullptr;
  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw));
  OwnedCOM<ID3D12Device> device(raw);
  if (FAILED(hr)) return 1;
  bool ok = Run(device.get(), false, 3, false, false) && Run(device.get(), true, 3, false, false) &&
      Run(device.get(), false, 3, false, true) && Run(device.get(), false, 61, false, false) &&
      Run(device.get(), false, 62, false, false) &&
      Run(device.get(), false, 3, true, false) && RunStageBindings() && RunDisjointStageBindings();
  std::printf("typed-origin production root %s (not PSO/GPU acceptance)\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

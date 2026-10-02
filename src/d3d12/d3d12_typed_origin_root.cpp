#include "d3d12_typed_origin.hpp"
#include <memory>
#include <new>

namespace dxmt {
namespace {
struct ReleaseBlob {
  void operator()(ID3DBlob *blob) const { if (blob) blob->Release(); }
};
using OwnedBlob = std::unique_ptr<ID3DBlob, ReleaseBlob>;
constexpr uint32_t kHiddenRegister = 0;
constexpr uint32_t kHiddenSpace = 1;
static_assert(uint32_t(D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) == DXMT_MSC_RESOURCE_TABLE);
static_assert(uint32_t(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) == DXMT_MSC_RESOURCE_CONSTANT);
static_assert(uint32_t(D3D12_ROOT_PARAMETER_TYPE_CBV) == DXMT_MSC_RESOURCE_CBV);
static_assert(uint32_t(D3D12_ROOT_PARAMETER_TYPE_SRV) == DXMT_MSC_RESOURCE_SRV);
static_assert(uint32_t(D3D12_ROOT_PARAMETER_TYPE_UAV) == DXMT_MSC_RESOURCE_UAV);

bool HiddenCBVCollision(uint32_t shader_register, uint32_t register_space) {
  return shader_register == kHiddenRegister && register_space == kHiddenSpace;
}

HRESULT PrepareRootInternal(
    const D3D12_ROOT_SIGNATURE_DESC1 &application, D3D12TypedOriginRoot &prepared,
    std::string &diagnostics) {
  diagnostics.clear();
  const auto fail = [&](HRESULT status, const char *reason) {
    diagnostics = reason;
    return status;
  };
  if (application.NumParameters > D3D12_MAX_ROOT_COST ||
      (application.NumParameters && !application.pParameters) ||
      (application.NumStaticSamplers && !application.pStaticSamplers))
    return fail(E_INVALIDARG, "invalid application root counts or pointers");
  if (application.Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE)
    return fail(E_NOTIMPL, "local compiler roots are unsupported");
  uint32_t cost = 0;
  for (uint32_t i = 0; i < application.NumParameters; ++i) {
    const auto &parameter = application.pParameters[i];
    uint32_t added = 0;
    bool collision = false;
    switch (parameter.ParameterType) {
    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
      added = parameter.Constants.Num32BitValues;
      if (!added) return fail(E_INVALIDARG, "empty root constants");
      collision = HiddenCBVCollision(parameter.Constants.ShaderRegister, parameter.Constants.RegisterSpace);
      break;
    case D3D12_ROOT_PARAMETER_TYPE_CBV:
      collision = HiddenCBVCollision(parameter.Descriptor.ShaderRegister, parameter.Descriptor.RegisterSpace);
      added = 2;
      break;
    case D3D12_ROOT_PARAMETER_TYPE_SRV:
    case D3D12_ROOT_PARAMETER_TYPE_UAV:
      added = 2;
      break;
    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
      const auto &table = parameter.DescriptorTable;
      if (!table.NumDescriptorRanges || !table.pDescriptorRanges)
        return fail(E_INVALIDARG, "empty descriptor table");
      bool samplers = false, resources = false;
      for (uint32_t j = 0; j < table.NumDescriptorRanges; ++j) {
        const auto &range = table.pDescriptorRanges[j];
        if (!range.NumDescriptors || (range.NumDescriptors != UINT32_MAX &&
            uint64_t(range.BaseShaderRegister) + range.NumDescriptors > uint64_t(UINT32_MAX) + 1))
          return fail(E_INVALIDARG, "empty or overflowing descriptor range");
        switch (range.RangeType) {
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
          collision |= HiddenCBVCollision(range.BaseShaderRegister, range.RegisterSpace);
          resources = true;
          break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: resources = true; break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER: samplers = true; break;
        default: return fail(E_INVALIDARG, "unknown descriptor range type");
        }
      }
      if (samplers && resources) return fail(E_INVALIDARG, "mixed sampler/resource table");
      added = 1;
      break;
    }
    default: return fail(E_INVALIDARG, "unknown root parameter type");
    }
    if (collision) { diagnostics = "application root overlaps private CBV b0/space1"; return E_NOTIMPL; }
    if (added > D3D12_MAX_ROOT_COST - cost) return fail(E_INVALIDARG, "application root exceeds DWORD budget");
    cost += added;
  }
  if (cost > D3D12_MAX_ROOT_COST - 2) {
    diagnostics = "application root leaves no two-DWORD private CBV budget"; return E_NOTIMPL;
  }

  std::vector<D3D12_ROOT_PARAMETER1> parameters;
  if (application.NumParameters)
    parameters.assign(application.pParameters, application.pParameters + application.NumParameters);
  D3D12_ROOT_PARAMETER1 hidden = {};
  hidden.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  hidden.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  hidden.Descriptor = {kHiddenRegister, kHiddenSpace, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
  parameters.push_back(hidden);
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC descriptor = {};
  descriptor.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  descriptor.Desc_1_1 = application;
  descriptor.Desc_1_1.NumParameters = parameters.size();
  descriptor.Desc_1_1.pParameters = parameters.data();
  ID3DBlob *blob_raw = nullptr, *errors_raw = nullptr;
  HRESULT hr = D3D12SerializeVersionedRootSignature(&descriptor, &blob_raw, &errors_raw);
  OwnedBlob blob(blob_raw), errors(errors_raw);
  if (errors && errors->GetBufferSize())
    diagnostics.append(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
  if (FAILED(hr)) return hr;
  if (!blob || !blob->GetBufferSize()) return fail(E_FAIL, "empty serialized compiler root");
  D3D12TypedOriginRoot candidate;
  const auto *bytes = static_cast<const uint8_t *>(blob->GetBufferPointer());
  candidate.bytecode.assign(bytes, bytes + blob->GetBufferSize());
  candidate.application_parameter_count = application.NumParameters;
  candidate.application_cost = cost;
  candidate.hidden_parameter_index = application.NumParameters;

  char error_message[1024] = {};
  dxmt_msc_get_root_layout_params query = {};
  query.root_signature = candidate.bytecode.data();
  query.root_signature_size = candidate.bytecode.size();
  query.error_message = error_message;
  query.error_message_capacity = sizeof(error_message);
  int result = DXMTMSCGetRootSignatureLayout(&query);
  if (result != DXMT_MSC_SUCCESS) {
    diagnostics += error_message;
    return result == DXMT_MSC_ERROR_OUT_OF_MEMORY ? E_OUTOFMEMORY : E_FAIL;
  }
  const size_t expected_count = parameters.size() + (application.NumStaticSamplers ? 1 : 0);
  if (query.layout_count != expected_count) return fail(E_FAIL, "unexpected MSC root location count");
  candidate.layouts.resize(expected_count);
  query.layouts = candidate.layouts.data();
  query.layout_capacity = candidate.layouts.size();
  result = DXMTMSCGetRootSignatureLayout(&query);
  if (result != DXMT_MSC_SUCCESS || query.layout_count != expected_count) {
    diagnostics += error_message;
    if (diagnostics.empty()) diagnostics = "MSC reflection failed or location count changed";
    return result == DXMT_MSC_ERROR_OUT_OF_MEMORY ? E_OUTOFMEMORY : E_FAIL;
  }
  candidate.argument_buffer_size = query.argument_buffer_size;
  for (size_t i = 0; i < expected_count; ++i) {
    const auto &layout = candidate.layouts[i];
    if (layout.top_level_offset > query.argument_buffer_size ||
        layout.size_bytes > query.argument_buffer_size - layout.top_level_offset)
      return fail(E_FAIL, "MSC root location exceeds argument buffer");
    const bool static_sampler = i == parameters.size();
    if (static_sampler) {
      if (layout.parameter_index != UINT32_MAX || layout.size_bytes != sizeof(uint64_t) ||
          (layout.resource_type != DXMT_MSC_RESOURCE_TABLE && layout.resource_type != DXMT_MSC_RESOURCE_SAMPLER))
        return fail(E_FAIL, "unexpected MSC static sampler location");
    } else {
      const auto &parameter = parameters[i];
      const uint64_t expected_size = parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS
          ? uint64_t(parameter.Constants.Num32BitValues) * sizeof(uint32_t) : sizeof(uint64_t);
      // D3D12 parameter types and the shared MSC location types use the same
      // values for tables, constants and root CBV/SRV/UAV entries.
      if (layout.parameter_index != i || layout.resource_type != uint32_t(parameter.ParameterType) ||
          layout.size_bytes != expected_size) return fail(E_FAIL, "MSC root location index/type/size mismatch");
    }
    for (size_t j = 0; j < i; ++j) {
      const auto &previous = candidate.layouts[j];
      if (layout.top_level_offset < previous.top_level_offset + previous.size_bytes &&
          previous.top_level_offset < layout.top_level_offset + layout.size_bytes)
        return fail(E_FAIL, "overlapping MSC root locations");
    }
  }
  const auto &hidden_layout = candidate.layouts[candidate.hidden_parameter_index];
  if (!HiddenCBVCollision(hidden_layout.shader_register, hidden_layout.register_space))
    return fail(E_FAIL, "MSC hidden CBV register/space mismatch");
  prepared = std::move(candidate);
  return S_OK;
}
} // namespace

HRESULT ResolveD3D12TypedOriginBindings(
    const D3D12TypedOriginRoot &root, const std::vector<dxmt_msc_typed_origin_binding> &bindings,
    std::vector<D3D12TypedOriginBindingLocation> &locations, std::string &diagnostics) {
  diagnostics.clear();
  struct ReleaseDeserializer {
    void operator()(ID3D12VersionedRootSignatureDeserializer *value) const { if (value) value->Release(); }
  };
  try {
    if (root.bytecode.empty() || bindings.empty() || bindings.size() > 64) return E_INVALIDARG;
    ID3D12VersionedRootSignatureDeserializer *raw = nullptr;
    HRESULT hr = D3D12CreateVersionedRootSignatureDeserializer(
        root.bytecode.data(), root.bytecode.size(), IID_PPV_ARGS(&raw));
    std::unique_ptr<ID3D12VersionedRootSignatureDeserializer, ReleaseDeserializer> decoded(raw);
    if (FAILED(hr)) return hr;
    const auto *versioned = decoded->GetUnconvertedRootSignatureDesc();
    if (!versioned || versioned->Version != D3D_ROOT_SIGNATURE_VERSION_1_1) return E_INVALIDARG;
    const auto &desc = versioned->Desc_1_1;
    if (desc.NumParameters != root.application_parameter_count + 1) return E_INVALIDARG;
    std::vector<D3D12TypedOriginBindingLocation> candidate;
    candidate.reserve(bindings.size());
    for (const auto &binding : bindings) {
      if (binding.resource_class > 1) return E_INVALIDARG;
      const auto type = binding.resource_class ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      bool found = false;
      D3D12TypedOriginBindingLocation location;
      for (uint32_t i = 0; i < root.application_parameter_count; ++i) {
        const auto &parameter = desc.pParameters[i];
        if (parameter.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
            parameter.ShaderVisibility != D3D12_SHADER_VISIBILITY_ALL) continue;
        uint64_t next = 0;
        for (uint32_t j = 0; j < parameter.DescriptorTable.NumDescriptorRanges; ++j) {
          const auto &range = parameter.DescriptorTable.pDescriptorRanges[j];
          const uint64_t offset = range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
              ? next : range.OffsetInDescriptorsFromTableStart;
          if (range.RangeType == type && range.RegisterSpace == binding.register_space &&
              binding.shader_register >= range.BaseShaderRegister &&
              (range.NumDescriptors == UINT32_MAX ||
               uint64_t(binding.shader_register) - range.BaseShaderRegister < range.NumDescriptors)) {
            const uint64_t slot = offset + uint64_t(binding.shader_register) - range.BaseShaderRegister;
            if (found || slot >= UINT32_MAX) {
              diagnostics = "ambiguous or overflowing typed-origin descriptor table location";
              return E_NOTIMPL;
            }
            location = {i, static_cast<uint32_t>(slot), range.Flags};
            found = true;
          }
          next = range.NumDescriptors == UINT32_MAX ? uint64_t(UINT32_MAX) + 1 : offset + range.NumDescriptors;
        }
      }
      if (!found) {
        diagnostics = "typed-origin resource has no compute-visible application descriptor table location";
        return E_NOTIMPL;
      }
      candidate.push_back(location);
    }
    locations = std::move(candidate);
    return S_OK;
  } catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
}

HRESULT PrepareD3D12TypedOriginRoot(
    const D3D12_ROOT_SIGNATURE_DESC1 &application, D3D12TypedOriginRoot &prepared,
    std::string &diagnostics) {
  try {
    return PrepareRootInternal(application, prepared, diagnostics);
  } catch (const std::bad_alloc &) {
    return E_OUTOFMEMORY;
  }
}
} // namespace dxmt

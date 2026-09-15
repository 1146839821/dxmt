/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "d3d12_raytracing_pipeline.hpp"

#include "com/com_object.hpp"
#include "com/com_pointer.hpp"
#include "d3d12_pageable.hpp"
#include "d3d12_shader_converter.hpp"
#include "sha1/sha1_util.hpp"

#include <algorithm>
#include <array>
#include <cwchar>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dxmt {

namespace {

struct StateObjectShaderRecord {
  std::wstring export_name;
  std::array<uint8_t, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES> identifier = {};
  uint64_t stack_size = 0;
  WMT::Reference<WMT::Library> library;
  WMT::Reference<WMT::Function> function;
  std::string source_name;
  uint32_t stage = UINT32_MAX;
  bool is_hit_group = false;
};

struct LibraryExport {
  std::wstring public_name;
  std::string source_name;
};

bool
IsValidName(const WCHAR *name) {
  return name && name[0];
}

std::wstring
MakeWideName(const WCHAR *name) {
  return IsValidName(name) ? std::wstring(name) : std::wstring();
}

bool
MakeNarrowName(const WCHAR *name, std::string &result) {
  if (!IsValidName(name))
    return false;
  const size_t length = std::wcslen(name);
  result.clear();
  result.reserve(length);
  for (size_t i = 0; i < length; i++) {
    if (name[i] > 0x7f)
      return false;
    result.push_back(static_cast<char>(name[i]));
  }
  return !result.empty();
}

bool
ContainsName(const std::vector<StateObjectShaderRecord> &records, const std::wstring &name) {
  return std::any_of(records.begin(), records.end(), [&](const StateObjectShaderRecord &record) {
    return record.export_name == name;
  });
}

bool
ContainsName(const std::vector<LibraryExport> &exports, const std::wstring &name) {
  return std::any_of(exports.begin(), exports.end(), [&](const LibraryExport &entry) {
    return entry.public_name == name;
  });
}

void
HashWideString(Sha1HashState &hash, const std::wstring &value) {
  const uint64_t size = value.size();
  hash.update(size);
  if (size)
    hash.update(value.data(), size * sizeof(value[0]));
}

std::array<uint8_t, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES>
MakeShaderIdentifier(
    const std::wstring &export_name, std::string_view source_name, uint32_t stage,
    const std::vector<uint8_t> &metallib
) {
  Sha1HashState first_hash;
  constexpr char first_tag[] = "dxmt-d3d12-raytracing-shader-identifier";
  first_hash.update(first_tag, sizeof(first_tag) - 1);
  HashWideString(first_hash, export_name);
  first_hash.update(source_name.data(), source_name.size());
  first_hash.update(stage);
  const uint64_t metallib_size = metallib.size();
  first_hash.update(metallib_size);
  if (!metallib.empty())
    first_hash.update(metallib.data(), metallib.size());
  const Sha1Digest first = first_hash.final();

  Sha1HashState second_hash;
  constexpr char second_tag[] = "dxmt-d3d12-raytracing-shader-identifier-tail";
  second_hash.update(second_tag, sizeof(second_tag) - 1);
  second_hash.update(first);
  second_hash.update(stage);
  const Sha1Digest second = second_hash.final();

  std::array<uint8_t, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES> identifier = {};
  std::memcpy(identifier.data(), first.data, sizeof(first.data));
  std::memcpy(identifier.data() + sizeof(first.data), second.data,
              identifier.size() - sizeof(first.data));
  return identifier;
}

uint32_t
StageForRaytracingShader(unsigned index) {
  constexpr uint32_t stages[] = {
      DXMT_MSC_STAGE_RAY_GENERATION,
      DXMT_MSC_STAGE_MISS,
      DXMT_MSC_STAGE_CLOSEST_HIT,
      DXMT_MSC_STAGE_ANY_HIT,
      DXMT_MSC_STAGE_INTERSECTION,
      DXMT_MSC_STAGE_CALLABLE,
  };
  return stages[index];
}

class MTLD3D12RaytracingStateObjectImpl final
    : public MTLD3D12Pageable<ID3D12StateObject>, public ID3D12StateObjectProperties {
  std::vector<StateObjectShaderRecord> shader_records_;
  uint64_t pipeline_stack_size_ = 0;
  UINT max_payload_size_ = 0;
  UINT max_attribute_size_ = 0;
  UINT max_trace_recursion_depth_ = 0;
  bool allow_state_object_additions_ = false;

public:
  explicit MTLD3D12RaytracingStateObjectImpl(MTLD3D12Device *device)
      : MTLD3D12Pageable<ID3D12StateObject>(device) {}

  ULONG STDMETHODCALLTYPE AddRef() override {
    return MTLD3D12Pageable<ID3D12StateObject>::AddRef();
  }

  ULONG STDMETHODCALLTYPE Release() override {
    return MTLD3D12Pageable<ID3D12StateObject>::Release();
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;
    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) ||
        riid == __uuidof(ID3D12DeviceChild) || riid == __uuidof(ID3D12Pageable) ||
        riid == __uuidof(ID3D12StateObject)) {
      *ppvObject = ref(static_cast<ID3D12StateObject *>(this));
      return S_OK;
    }
    if (riid == __uuidof(ID3D12StateObjectProperties)) {
      *ppvObject = ref(static_cast<ID3D12StateObjectProperties *>(this));
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  HRESULT
  Initialize(
      const D3D12_STATE_OBJECT_DESC *desc, const MTLD3D12RaytracingStateObjectImpl *parent = nullptr
  ) {
    if (!desc || !desc->pSubobjects || !desc->NumSubobjects)
      return E_INVALIDARG;
    if (desc->Type != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
      return E_NOTIMPL;

    const bool is_addition = parent != nullptr;
    if (is_addition) {
      if (!parent->allow_state_object_additions_)
        return E_INVALIDARG;
      shader_records_ = parent->shader_records_;
      pipeline_stack_size_ = parent->pipeline_stack_size_;
      max_payload_size_ = parent->max_payload_size_;
      max_attribute_size_ = parent->max_attribute_size_;
      max_trace_recursion_depth_ = parent->max_trace_recursion_depth_;
      allow_state_object_additions_ = parent->allow_state_object_additions_;
    }

    const D3D12_DXIL_LIBRARY_DESC *library_desc = nullptr;
    const D3D12_GLOBAL_ROOT_SIGNATURE *global_root_signature = nullptr;
    const D3D12_RAYTRACING_SHADER_CONFIG *shader_config = nullptr;
    const D3D12_RAYTRACING_PIPELINE_CONFIG *pipeline_config = nullptr;
    std::vector<const D3D12_HIT_GROUP_DESC *> hit_groups;

    for (UINT i = 0; i < desc->NumSubobjects; i++) {
      const D3D12_STATE_SUBOBJECT &subobject = desc->pSubobjects[i];
      if (!subobject.pDesc)
        return E_INVALIDARG;
      switch (subobject.Type) {
      case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG: {
        const auto *config = static_cast<const D3D12_STATE_OBJECT_CONFIG *>(subobject.pDesc);
        constexpr D3D12_STATE_OBJECT_FLAGS supported_flags =
            D3D12_STATE_OBJECT_FLAG_NONE | D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS;
        if (config->Flags & ~supported_flags)
          return E_NOTIMPL;
        if (is_addition && (config->Flags & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS) !=
                               (allow_state_object_additions_ ? D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS
                                                               : D3D12_STATE_OBJECT_FLAG_NONE))
          return E_INVALIDARG;
        allow_state_object_additions_ =
            (config->Flags & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS) != 0;
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
        if (global_root_signature)
          return E_INVALIDARG;
        global_root_signature = static_cast<const D3D12_GLOBAL_ROOT_SIGNATURE *>(subobject.pDesc);
        if (!global_root_signature->pGlobalRootSignature)
          return E_INVALIDARG;
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
        if (library_desc)
          return E_INVALIDARG;
        library_desc = static_cast<const D3D12_DXIL_LIBRARY_DESC *>(subobject.pDesc);
        if (!library_desc->DXILLibrary.pShaderBytecode || !library_desc->DXILLibrary.BytecodeLength ||
            !library_desc->NumExports || !library_desc->pExports)
          return E_INVALIDARG;
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
        if (shader_config)
          return E_INVALIDARG;
        shader_config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG *>(subobject.pDesc);
        if (shader_config->MaxAttributeSizeInBytes > 16)
          return E_NOTIMPL;
        if (is_addition &&
            (shader_config->MaxPayloadSizeInBytes != max_payload_size_ ||
             shader_config->MaxAttributeSizeInBytes != max_attribute_size_))
          return E_INVALIDARG;
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
        if (pipeline_config)
          return E_INVALIDARG;
        pipeline_config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG *>(subobject.pDesc);
        if (!pipeline_config->MaxTraceRecursionDepth ||
            pipeline_config->MaxTraceRecursionDepth > D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH)
          return E_INVALIDARG;
        if (is_addition && pipeline_config->MaxTraceRecursionDepth != max_trace_recursion_depth_)
          return E_INVALIDARG;
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP: {
        const auto *hit_group = static_cast<const D3D12_HIT_GROUP_DESC *>(subobject.pDesc);
        if (!IsValidName(hit_group->HitGroupExport) || hit_group->Type != D3D12_HIT_GROUP_TYPE_TRIANGLES ||
            hit_group->IntersectionShaderImport)
          return E_NOTIMPL;
        if (!IsValidName(hit_group->AnyHitShaderImport) && !IsValidName(hit_group->ClosestHitShaderImport))
          return E_INVALIDARG;
        hit_groups.push_back(hit_group);
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK: {
        const auto *node_mask = static_cast<const D3D12_NODE_MASK *>(subobject.pDesc);
        if (node_mask->NodeMask != 1)
          return E_INVALIDARG;
        break;
      }
      default:
        return E_NOTIMPL;
      }
    }

    if ((!is_addition && (!library_desc || !shader_config || !pipeline_config)) ||
        (is_addition && !library_desc && hit_groups.empty()))
      return E_INVALIDARG;

    if (shader_config) {
      max_payload_size_ = shader_config->MaxPayloadSizeInBytes;
      max_attribute_size_ = shader_config->MaxAttributeSizeInBytes;
    } else if (!is_addition) {
      return E_INVALIDARG;
    }
    if (pipeline_config)
      max_trace_recursion_depth_ = pipeline_config->MaxTraceRecursionDepth;
    else if (!is_addition) {
      return E_INVALIDARG;
    }

    D3D12_SHADER_BYTECODE shader = {};
    D3D12ShaderClassification classification;
    if (library_desc) {
      shader = library_desc->DXILLibrary;
      classification = ClassifyD3D12Shader(shader);
      if (FAILED(classification.validation_hr))
        return classification.validation_hr;
      if (classification.backend != D3D12ShaderBackend::MetalShaderConverter ||
          !classification.is_library_shader)
        return E_NOTIMPL;
    }

    const void *root_signature = nullptr;
    size_t root_signature_size = 0;
    if (global_root_signature) {
      if (!IsSameDevice(device_, global_root_signature->pGlobalRootSignature))
        return E_INVALIDARG;
      auto *root = static_cast<MTLD3D12RootSignature *>(global_root_signature->pGlobalRootSignature);
      HRESULT hr = root->InitializeMSCLayout();
      if (FAILED(hr))
        return hr;
      root_signature_size = root->GetBlob(&root_signature);
    }

    /* MSC requires an explicit local-root descriptor when compiling a ray
     * tracing library.  A state object without a local-root subobject has an
     * empty local root signature, so serialize that descriptor for the
     * compiler even when the state object has no global root signature. */
    const D3D12_ROOT_SIGNATURE_DESC empty_root_signature = {};
    ID3DBlob *serialized_local_root = nullptr;
    ID3DBlob *serialized_local_root_error = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(
        &empty_root_signature, D3D_ROOT_SIGNATURE_VERSION_1, &serialized_local_root, &serialized_local_root_error
    );
    Com<ID3DBlob> local_root_blob = Com<ID3DBlob>::transfer(serialized_local_root);
    Com<ID3DBlob> local_root_error = Com<ID3DBlob>::transfer(serialized_local_root_error);
    if (FAILED(hr) || !local_root_blob)
      return FAILED(hr) ? hr : E_FAIL;
    const void *local_root_signature = local_root_blob->GetBufferPointer();
    const size_t local_root_signature_size = local_root_blob->GetBufferSize();

    std::vector<LibraryExport> exports;
    if (library_desc)
      exports.reserve(library_desc->NumExports);
    std::map<std::string, uint32_t> stage_hints;
    if (library_desc) {
      for (UINT i = 0; i < library_desc->NumExports; i++) {
        const D3D12_EXPORT_DESC &export_desc = library_desc->pExports[i];
        if (!IsValidName(export_desc.Name) || export_desc.Flags != D3D12_EXPORT_FLAG_NONE)
          return E_INVALIDARG;
        const WCHAR *source_name_wide = export_desc.ExportToRename ? export_desc.ExportToRename : export_desc.Name;
        if (!IsValidName(source_name_wide))
          return E_INVALIDARG;

        LibraryExport export_entry;
        export_entry.public_name = MakeWideName(export_desc.Name);
        if (!MakeNarrowName(source_name_wide, export_entry.source_name))
          return E_NOTIMPL;
        if (ContainsName(exports, export_entry.public_name) || ContainsName(shader_records_, export_entry.public_name))
          return E_INVALIDARG;
        exports.push_back(std::move(export_entry));
      }
    }

    auto find_export_record = [&](const std::wstring &public_name) -> const StateObjectShaderRecord * {
      for (const auto &record : shader_records_)
        if (record.export_name == public_name && !record.is_hit_group)
          return &record;
      return nullptr;
    };
    auto find_export_source = [&](const std::wstring &public_name) -> const std::string * {
      for (const auto &entry : exports)
        if (entry.public_name == public_name)
          return &entry.source_name;
      if (const auto *record = find_export_record(public_name))
        return &record->source_name;
      return nullptr;
    };
    auto add_stage_hint = [&](const WCHAR *public_name, uint32_t stage) -> HRESULT {
      if (!IsValidName(public_name))
        return S_OK;
      const std::string *source_name = find_export_source(MakeWideName(public_name));
      if (!source_name)
        return E_INVALIDARG;
      auto [it, inserted] = stage_hints.emplace(*source_name, stage);
      if (!inserted && it->second != stage)
        return E_INVALIDARG;
      return S_OK;
    };

    for (const auto *hit_group : hit_groups) {
      HRESULT hr = add_stage_hint(hit_group->AnyHitShaderImport, DXMT_MSC_STAGE_ANY_HIT);
      if (FAILED(hr))
        return hr;
      hr = add_stage_hint(hit_group->ClosestHitShaderImport, DXMT_MSC_STAGE_CLOSEST_HIT);
      if (FAILED(hr))
        return hr;
    }

    for (const auto &export_entry : exports) {
      StateObjectShaderRecord record;
      record.export_name = export_entry.public_name;
      record.source_name = export_entry.source_name;

      std::vector<uint32_t> candidate_stages;
      auto hint = stage_hints.find(export_entry.source_name);
      if (hint != stage_hints.end()) {
        candidate_stages.push_back(hint->second);
      } else {
        for (unsigned stage_index = 0; stage_index < 6; stage_index++)
          candidate_stages.push_back(StageForRaytracingShader(stage_index));
      }

      D3D12ConvertedShader converted;
      uint32_t selected_stage = 0;
      bool found = false;
      for (uint32_t stage : candidate_stages) {
        D3D12ConvertedShader candidate;
        const std::string source_name = export_entry.source_name;
        HRESULT hr = ConvertD3D12LibraryShader(
            classification, shader, stage, source_name.c_str(), candidate, root_signature, root_signature_size,
            local_root_signature, local_root_signature_size, &device_->GetMSCCapabilities()
        );
        if (FAILED(hr))
          continue;
        WMT::Error error;
        auto library = device_->GetMTLDevice().newLibrary(candidate.metallib.data(), candidate.metallib.size(), error);
        if (!library)
          continue;
        auto function = library.newFunction(candidate.entry_point.c_str());
        if (!function)
          continue;
        if (found)
          return E_NOTIMPL;
        found = true;
        selected_stage = stage;
        converted = std::move(candidate);
        record.library = std::move(library);
        record.function = std::move(function);
      }
      if (!found)
        return E_NOTIMPL;

      record.stage = selected_stage;
      record.identifier = MakeShaderIdentifier(
          record.export_name, export_entry.source_name, selected_stage, converted.metallib
      );
      shader_records_.push_back(std::move(record));
    }

    for (const auto *hit_group : hit_groups) {
      const std::wstring name = MakeWideName(hit_group->HitGroupExport);
      if (ContainsName(shader_records_, name))
        return E_INVALIDARG;
      if (hit_group->AnyHitShaderImport &&
          !find_export_source(MakeWideName(hit_group->AnyHitShaderImport)))
        return E_INVALIDARG;
      if (hit_group->ClosestHitShaderImport &&
          !find_export_source(MakeWideName(hit_group->ClosestHitShaderImport)))
        return E_INVALIDARG;

      std::string any_hit;
      std::string closest_hit;
      if (hit_group->AnyHitShaderImport) {
        const auto *source_name = find_export_source(MakeWideName(hit_group->AnyHitShaderImport));
        any_hit = source_name ? *source_name : std::string();
      }
      if (hit_group->ClosestHitShaderImport) {
        const auto *source_name = find_export_source(MakeWideName(hit_group->ClosestHitShaderImport));
        closest_hit = source_name ? *source_name : std::string();
      }
      StateObjectShaderRecord record;
      record.export_name = name;
      record.source_name = any_hit + closest_hit;
      record.is_hit_group = true;
      record.identifier = MakeShaderIdentifier(record.export_name, any_hit + closest_hit, UINT32_MAX, {});
      shader_records_.push_back(std::move(record));
    }

    return S_OK;
  }

  void *STDMETHODCALLTYPE GetShaderIdentifier(const WCHAR *export_name) override {
    if (!IsValidName(export_name))
      return nullptr;
    const std::wstring name(export_name);
    for (auto &record : shader_records_)
      if (record.export_name == name)
        return record.identifier.data();
    return nullptr;
  }

  UINT64 STDMETHODCALLTYPE GetShaderStackSize(const WCHAR *export_name) override {
    if (!IsValidName(export_name))
      return 0;
    const std::wstring name(export_name);
    for (const auto &record : shader_records_)
      if (record.export_name == name)
        return record.stack_size;
    return 0;
  }

  UINT64 STDMETHODCALLTYPE GetPipelineStackSize() override {
    return pipeline_stack_size_;
  }

  void STDMETHODCALLTYPE SetPipelineStackSize(UINT64 pipeline_stack_size_in_bytes) override {
    pipeline_stack_size_ = pipeline_stack_size_in_bytes;
  }
};

} // namespace

HRESULT
CreateD3D12RaytracingStateObject(
    MTLD3D12Device *device, const D3D12_STATE_OBJECT_DESC *desc, REFIID riid, void **state_object
) {
  InitReturnPtr(state_object);
  if (!device || !desc || !state_object)
    return E_INVALIDARG;

  auto object = Com(new MTLD3D12RaytracingStateObjectImpl(device));
  HRESULT hr = object->Initialize(desc);
  if (FAILED(hr))
    return hr;
  return object->QueryInterface(riid, state_object);
}

HRESULT
AddD3D12RaytracingStateObject(
    MTLD3D12Device *device, const D3D12_STATE_OBJECT_DESC *addition, ID3D12StateObject *state_object_to_grow_from,
    REFIID riid, void **state_object
) {
  InitReturnPtr(state_object);
  if (!device || !addition || !state_object_to_grow_from || !state_object)
    return E_INVALIDARG;
  if (!IsSameDevice(device, state_object_to_grow_from))
    return E_INVALIDARG;

  auto *parent = dynamic_cast<MTLD3D12RaytracingStateObjectImpl *>(state_object_to_grow_from);
  if (!parent)
    return E_INVALIDARG;

  auto object = Com(new MTLD3D12RaytracingStateObjectImpl(device));
  HRESULT hr = object->Initialize(addition, parent);
  if (FAILED(hr))
    return hr;
  return object->QueryInterface(riid, state_object);
}

} // namespace dxmt

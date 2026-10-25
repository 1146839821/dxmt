#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include "d3d12_descriptor_heap.hpp"
#include "d3d12_device.hpp"

#include <iostream>
#include <chrono>
#include <string>
#include <set>
#include <thread>

namespace {

template <typename T>
void Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

bool CheckHR(const char *name, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

D3D12_HEAP_PROPERTIES DefaultHeap() {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC BufferDescription(UINT64 width, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = width;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
  return desc;
}

D3D12_RESOURCE_DESC RenderTargetDescription() {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 1;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  return desc;
}

dxmt::ShaderVisibleDescriptorCPUStorage ReadDescriptorFields(dxmt::MTLD3D12DescriptorHeap *heap, UINT index) {
  auto read = heap->ReadDescriptor(index);
  return read.get();
}

bool IsDescriptorType(
    dxmt::MTLD3D12DescriptorHeap *heap, dxmt::ShaderVisibleDescriptorType expected, const char *name
) {
  if (ReadDescriptorFields(heap, 0).type == expected)
    return true;
  std::cerr << name << " descriptor type changed unexpectedly\n";
  return false;
}

bool IsDescriptorTypeAt(
    dxmt::MTLD3D12DescriptorHeap *heap, UINT index, dxmt::ShaderVisibleDescriptorType expected, const char *name
) {
  if (ReadDescriptorFields(heap, index).type == expected)
    return true;
  std::cerr << name << " descriptor type changed unexpectedly\n";
  return false;
}

bool CheckRootSignatureBudget(ID3D12Device *device) {
  D3D12_DESCRIPTOR_RANGE1 ranges[65] = {};
  for (UINT i = 0; i < std::size(ranges); ++i) {
    ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[i].NumDescriptors = 1;
    ranges[i].BaseShaderRegister = i;
  }
  D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
  for (UINT i = 0; i < std::size(samplers); ++i) {
    auto &sampler = samplers[i];
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = i;
  }
  const auto constants = [](UINT count, UINT values) {
    std::vector<D3D12_ROOT_PARAMETER1> result(count);
    for (UINT i = 0; i < count; ++i) {
      result[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      result[i].Constants = {i, 0, values};
    }
    return result;
  };
  const auto descriptors = [](D3D12_ROOT_PARAMETER_TYPE type, UINT count) {
    std::vector<D3D12_ROOT_PARAMETER1> result(count);
    for (UINT i = 0; i < count; ++i) {
      result[i].ParameterType = type;
      result[i].Descriptor.ShaderRegister = i;
    }
    return result;
  };
  const auto tables = [&](UINT count) {
    std::vector<D3D12_ROOT_PARAMETER1> result(count);
    for (UINT i = 0; i < count; ++i) {
      result[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      result[i].DescriptorTable = {1, &ranges[i]};
    }
    return result;
  };
  bool passed = true;
  UINT cases = 0;
  for (const auto version :
       {D3D_ROOT_SIGNATURE_VERSION_1_0, D3D_ROOT_SIGNATURE_VERSION_1_1}) {
    const auto check = [&](const char *name,
                           const std::vector<D3D12_ROOT_PARAMETER1> &parameters,
                           HRESULT expected, UINT qwords = 0,
                           bool local = false, UINT sampler_count = 0) {
      D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
      desc.Version = version;
      const auto flags = local ? D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE
                               : D3D12_ROOT_SIGNATURE_FLAG_NONE;
      std::vector<D3D12_ROOT_PARAMETER> legacy(parameters.size());
      std::vector<D3D12_DESCRIPTOR_RANGE> legacy_ranges(parameters.size());
      if (version == D3D_ROOT_SIGNATURE_VERSION_1_0) {
        for (UINT i = 0; i < parameters.size(); ++i) {
          const auto &source = parameters[i];
          auto &target = legacy[i];
          target.ParameterType = source.ParameterType;
          target.ShaderVisibility = source.ShaderVisibility;
          if (source.ParameterType ==
              D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            const auto &range = *source.DescriptorTable.pDescriptorRanges;
            legacy_ranges[i] = {range.RangeType, range.NumDescriptors,
                                range.BaseShaderRegister, range.RegisterSpace,
                                range.OffsetInDescriptorsFromTableStart};
            target.DescriptorTable = {1, &legacy_ranges[i]};
          } else if (source.ParameterType ==
                     D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
            target.Constants = source.Constants;
          } else {
            target.Descriptor = {source.Descriptor.ShaderRegister,
                                 source.Descriptor.RegisterSpace};
          }
        }
        desc.Desc_1_0 = {static_cast<UINT>(legacy.size()), legacy.data(),
                         sampler_count, samplers, flags};
      } else {
        desc.Desc_1_1 = {static_cast<UINT>(parameters.size()),
                         parameters.data(), sampler_count, samplers, flags};
      }
      ID3DBlob *blob = nullptr, *errors = nullptr;
      const HRESULT serialized =
          D3D12SerializeVersionedRootSignature(&desc, &blob, &errors);
      Release(errors);
      if (!CheckHR("Serialize root budget fixture", serialized)) {
        Release(blob);
        passed = false;
        return;
      }
      auto *root = reinterpret_cast<ID3D12RootSignature *>(uintptr_t(1));
      const HRESULT hr = device->CreateRootSignature(
          0, blob->GetBufferPointer(), blob->GetBufferSize(),
          IID_PPV_ARGS(&root));
      bool good =
          hr == expected && (SUCCEEDED(hr) ? root != nullptr : root == nullptr);
      if (SUCCEEDED(hr) && root) {
        auto *metadata = static_cast<dxmt::MTLD3D12RootSignature *>(root);
        if (SUCCEEDED(expected))
          good &= metadata->UploadQwords == qwords &&
                  metadata->ParameterSlots == parameters.size() &&
                  metadata->NumStaticSamplers == sampler_count;
        root->Release();
      }
      Release(blob);
      ++cases;
      std::cout << "root-budget version=" << version << " case=" << name
                << " hr=0x" << std::hex << static_cast<UINT>(hr) << std::dec
                << " result=" << (good ? "PASS" : "FAIL") << "\n";
      passed &= good;
    };
    check("empty", {}, S_OK);
    check("constants63", constants(1, 63), S_OK, 32);
    check("constants64", constants(1, 64), S_OK, 32);
    check("constants65", constants(1, 65), E_INVALIDARG);
    check("constants129", constants(1, 129), E_INVALIDARG);
    check("constants-max", constants(1, UINT_MAX), E_INVALIDARG);
    for (const auto type :
         {D3D12_ROOT_PARAMETER_TYPE_CBV, D3D12_ROOT_PARAMETER_TYPE_SRV,
          D3D12_ROOT_PARAMETER_TYPE_UAV}) {
      check(type == D3D12_ROOT_PARAMETER_TYPE_CBV   ? "cbv32"
            : type == D3D12_ROOT_PARAMETER_TYPE_SRV ? "srv32"
                                                    : "uav32",
            descriptors(type, 32), S_OK, 32);
      check(type == D3D12_ROOT_PARAMETER_TYPE_CBV   ? "cbv33"
            : type == D3D12_ROOT_PARAMETER_TYPE_SRV ? "srv33"
                                                    : "uav33",
            descriptors(type, 33), E_INVALIDARG);
    }
    check("tables64", tables(64), S_OK, 64);
    check("tables65", tables(65), E_INVALIDARG);
    check("padded-constants64", constants(64, 1), S_OK, 64);
    for (UINT count : {61u, 62u}) {
      auto mixed = constants(1, count);
      auto descriptor = descriptors(D3D12_ROOT_PARAMETER_TYPE_CBV, 1)[0];
      descriptor.Descriptor.ShaderRegister = 1;
      mixed.push_back(descriptor);
      mixed.push_back(tables(1)[0]);
      check(count == 61 ? "mixed64" : "mixed65", mixed,
            count == 61 ? S_OK : E_INVALIDARG, 33);
    }
    check("constants64-static", constants(1, 64), S_OK, 32, false, 2);
    check("cbv32-static", descriptors(D3D12_ROOT_PARAMETER_TYPE_CBV, 32), S_OK,
          32, false, 2);
    // Local arguments live in shader records, not the global 64-DWORD budget.
    // Creation controls only: do not bind these as a global signature or claim
    // DXR execution.
    check("local-constants65", constants(1, 65), S_OK, 33, true);
    check("local-cbv33", descriptors(D3D12_ROOT_PARAMETER_TYPE_CBV, 33), S_OK,
          33, true);
  }
  std::cout << "root-budget cases=" << cases
            << " result=" << (passed ? "PASS" : "FAIL") << "\n";
  return passed;
}

bool CheckRootSignatureMetadata(ID3D12Device *device) {
  D3D12_DESCRIPTOR_RANGE1 ranges[3] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 2;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 3;
  ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  ranges[2].NumDescriptors = 1;
  ranges[2].OffsetInDescriptorsFromTableStart = 4;

  D3D12_ROOT_PARAMETER1 parameters[4] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[1].Descriptor.ShaderRegister = 1;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[2].Descriptor.ShaderRegister = 2;
  parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
  parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[3].DescriptorTable.NumDescriptorRanges = 3;
  parameters[3].DescriptorTable.pDescriptorRanges = ranges;
  parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_STATIC_SAMPLER_DESC static_sampler = {};
  static_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  static_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  static_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  static_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  static_sampler.MaxAnisotropy = 1;
  static_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  static_sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
  static_sampler.MinLOD = 0.0f;
  static_sampler.MaxLOD = D3D12_FLOAT32_MAX;
  static_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_desc = {};
  versioned_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  versioned_desc.Desc_1_1.NumParameters = 4;
  versioned_desc.Desc_1_1.pParameters = parameters;
  versioned_desc.Desc_1_1.NumStaticSamplers = 1;
  versioned_desc.Desc_1_1.pStaticSamplers = &static_sampler;
  versioned_desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

  ID3DBlob *blob = nullptr;
  ID3DBlob *errors = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3D12VersionedRootSignatureDeserializer *deserializer = nullptr;
  bool passed = SUCCEEDED(D3D12SerializeVersionedRootSignature(&versioned_desc, &blob, &errors));
  if (passed)
    passed = SUCCEEDED(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)
    ));
  if (passed)
    passed = SUCCEEDED(D3D12CreateVersionedRootSignatureDeserializer(
        blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&deserializer)
    ));

  const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *canonical = nullptr;
  if (passed)
    passed = SUCCEEDED(deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &canonical)) &&
             canonical && canonical->Desc_1_1.NumParameters == 4;

  if (passed) {
    auto *metadata = static_cast<dxmt::MTLD3D12RootSignature *>(root_signature);
    const dxmt::MTLD3D12RootSignature::RootResourceBindingMetadata expected[] = {
        {0, D3D12_ROOT_PARAMETER_TYPE_CBV, metadata->SlotQwordOffsets[0], D3D12_SHADER_VISIBILITY_VERTEX},
        {1, D3D12_ROOT_PARAMETER_TYPE_SRV, metadata->SlotQwordOffsets[1], D3D12_SHADER_VISIBILITY_PIXEL},
        {2, D3D12_ROOT_PARAMETER_TYPE_UAV, metadata->SlotQwordOffsets[2], D3D12_SHADER_VISIBILITY_GEOMETRY},
    };
    passed = metadata->RootResourceBindingCount == std::size(expected) && metadata->RootResourceBindings &&
             metadata->RootDescriptorTableCount == 1 && metadata->RootDescriptorTables &&
             metadata->RootDescriptorRangeCount == 3 && metadata->RootDescriptorRanges &&
             metadata->ResourceHeapDirectlyIndexed ==
                 ((canonical->Desc_1_1.Flags & D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED) != 0) &&
             metadata->NumStaticSamplers == canonical->Desc_1_1.NumStaticSamplers;
    if (!passed)
      std::cerr << "root metadata counts/flags mismatch: resources=" << metadata->RootResourceBindingCount
                << " tables=" << metadata->RootDescriptorTableCount << " ranges=" << metadata->RootDescriptorRangeCount
                << " direct=" << metadata->ResourceHeapDirectlyIndexed
                << " samplers=" << metadata->NumStaticSamplers << "\n";
    for (size_t i = 0; passed && i < std::size(expected); ++i) {
      const auto &actual = metadata->RootResourceBindings[i];
      passed = actual.parameter_index == expected[i].parameter_index && actual.type == expected[i].type &&
               actual.source_qword == expected[i].source_qword && actual.visibility == expected[i].visibility &&
               canonical->Desc_1_1.pParameters[actual.parameter_index].ParameterType == actual.type &&
               canonical->Desc_1_1.pParameters[actual.parameter_index].ShaderVisibility == actual.visibility &&
               actual.source_qword == metadata->SlotQwordOffsets[actual.parameter_index];
      if (!passed)
        std::cerr << "root resource metadata mismatch at parameter " << i << "\n";
    }
    if (passed) {
      const auto &table = metadata->RootDescriptorTables[0];
      const auto &canonical_table = canonical->Desc_1_1.pParameters[3];
      passed = table.parameter_index == 3 && table.source_qword == metadata->SlotQwordOffsets[3] &&
               table.first_range == 0 && table.range_count == 3 && table.visibility == D3D12_SHADER_VISIBILITY_ALL &&
               canonical_table.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE &&
               canonical_table.ShaderVisibility == table.visibility &&
               canonical_table.DescriptorTable.NumDescriptorRanges == table.range_count &&
               metadata->RootDescriptorRanges[0].type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
               metadata->RootDescriptorRanges[0].num_descriptors == 2 &&
               metadata->RootDescriptorRanges[0].offset == 0 &&
               metadata->RootDescriptorRanges[1].type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV &&
               metadata->RootDescriptorRanges[1].num_descriptors == 3 &&
               metadata->RootDescriptorRanges[1].offset == 2 &&
               metadata->RootDescriptorRanges[2].type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV &&
               metadata->RootDescriptorRanges[2].num_descriptors == 1 &&
               metadata->RootDescriptorRanges[2].offset == 4;
      if (!passed)
        std::cerr << "root descriptor table/range metadata mismatch\n";
      uint64_t append_offset = 0;
      for (UINT i = 0; passed && i < table.range_count; ++i) {
        const auto &canonical_range = canonical_table.DescriptorTable.pDescriptorRanges[i];
        const uint64_t effective_offset = canonical_range.OffsetInDescriptorsFromTableStart ==
                                                  D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                                              ? append_offset
                                              : canonical_range.OffsetInDescriptorsFromTableStart;
        const auto &actual_range = metadata->RootDescriptorRanges[table.first_range + i];
        passed = actual_range.type == canonical_range.RangeType &&
                 actual_range.num_descriptors == canonical_range.NumDescriptors &&
                 actual_range.offset == effective_offset;
        append_offset = effective_offset + canonical_range.NumDescriptors;
      }
    }
  }

  Release(deserializer);
  Release(root_signature);
  Release(errors);
  Release(blob);

  D3D12_ROOT_PARAMETER root_parameter = {};
  root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_parameter.Descriptor.ShaderRegister = 0;
  root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  D3D12_ROOT_SIGNATURE_DESC legacy_desc = {};
  legacy_desc.NumParameters = 1;
  legacy_desc.pParameters = &root_parameter;
  blob = nullptr;
  errors = nullptr;
  root_signature = nullptr;
  if (passed)
    passed = SUCCEEDED(D3D12SerializeRootSignature(
        &legacy_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &errors
    ));
  if (passed)
    passed = SUCCEEDED(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)
    ));
  if (passed) {
    auto *metadata = static_cast<dxmt::MTLD3D12RootSignature *>(root_signature);
    passed = metadata->RootResourceBindingCount == 1 && metadata->RootResourceBindings &&
             metadata->RootResourceBindings[0].parameter_index == 0 &&
             metadata->RootResourceBindings[0].type == D3D12_ROOT_PARAMETER_TYPE_CBV &&
             metadata->RootResourceBindings[0].visibility == D3D12_SHADER_VISIBILITY_VERTEX &&
             metadata->RootDescriptorTableCount == 0 && !metadata->ResourceHeapDirectlyIndexed;
  }
  Release(root_signature);
  Release(errors);
  Release(blob);
  if (passed) {
    D3D12_ROOT_SIGNATURE_DESC empty_desc = {};
    passed = SUCCEEDED(D3D12SerializeRootSignature(
        &empty_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &errors
    ));
  }
  if (passed)
    passed = SUCCEEDED(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)
    ));
  if (passed) {
    auto *metadata = static_cast<dxmt::MTLD3D12RootSignature *>(root_signature);
    passed = metadata->ParameterSlots == 0 && metadata->RootResourceBindingCount == 0 &&
             metadata->RootDescriptorTableCount == 0 && metadata->RootDescriptorRangeCount == 0 &&
             !metadata->ResourceHeapDirectlyIndexed && metadata->NumStaticSamplers == 0;
  }
  Release(root_signature);
  Release(errors);
  Release(blob);
  if (!passed)
    std::cerr << "root signature cached residency metadata disagrees with the serialized description\n";
  return passed;
}

bool CheckViewPermissions(bool cost = false) {
  struct Objects {
    ID3D12Device *device = nullptr;
    ID3D12DescriptorHeap *shader = nullptr, *rtv = nullptr, *dsv = nullptr;
    ID3D12Resource *color = nullptr, *plain = nullptr, *depth = nullptr,
        *plain_depth = nullptr, *readable_depth = nullptr, *denied_depth = nullptr,
        *buffer = nullptr, *plain_buffer = nullptr;
    ~Objects() {
      for (auto heap : {shader, rtv, dsv}) if (heap) heap->Release();
      for (auto resource : {color, plain, depth, plain_depth, readable_depth,
                            denied_depth, buffer, plain_buffer})
        if (resource)
          resource->Release();
      if (device)
        device->Release();
    }
  } o;
  if (!CheckHR("View permission device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&o.device))))
    return false;
  const auto properties = DefaultHeap();
  auto desc = RenderTargetDescription();
  const auto create = [&](DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                          ID3D12Resource **out) {
    desc.Format = format;
    desc.Flags = flags;
    return CheckHR(
        "View permission resource",
        o.device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
                                          &desc, D3D12_RESOURCE_STATE_COMMON,
                                          nullptr, IID_PPV_ARGS(out)));
  };
  if (!create(DXGI_FORMAT_R8G8B8A8_UNORM,
              D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
              &o.color) ||
      !create(DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, &o.plain) ||
      !create(DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
              &o.depth) ||
      !create(DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
              &o.plain_depth) ||
      !create(DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
              &o.readable_depth) ||
      !create(DXGI_FORMAT_R32_TYPELESS,
              D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
                  D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
              &o.denied_depth))
    return false;
  desc = BufferDescription(256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  if (!CheckHR(
          "View permission UAV buffer",
          o.device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
                                            &desc, D3D12_RESOURCE_STATE_COMMON,
                                            nullptr, IID_PPV_ARGS(&o.buffer))))
    return false;
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  if (!CheckHR("View permission plain buffer",
               o.device->CreateCommittedResource(
                   &properties, D3D12_HEAP_FLAG_NONE, &desc,
                   D3D12_RESOURCE_STATE_COMMON, nullptr,
                   IID_PPV_ARGS(&o.plain_buffer))))
    return false;
  for (auto pair :
       {std::pair{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &o.shader},
        std::pair{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, &o.rtv},
        std::pair{D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &o.dsv}}) {
    D3D12_DESCRIPTOR_HEAP_DESC heap = {};
    heap.Type = pair.first;
    heap.NumDescriptors = 1;
    if (!CheckHR("View permission heap", o.device->CreateDescriptorHeap(
                                             &heap, IID_PPV_ARGS(pair.second))))
      return false;
  }
  auto *shader = static_cast<dxmt::MTLD3D12DescriptorHeap *>(o.shader);
  auto *rtv = static_cast<dxmt::MTLD3D12RenderTargetDescriptorHeap *>(o.rtv);
  auto *dsv = static_cast<dxmt::MTLD3D12RenderTargetDescriptorHeap *>(o.dsv);
  const auto sh = o.shader->GetCPUDescriptorHandleForHeapStart();
  const auto rh = o.rtv->GetCPUDescriptorHandleForHeapStart();
  const auto dh = o.dsv->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 64;
  uav.Buffer.StructureByteStride = 4;
  if (cost) {
    for (UINT round = 0; round < 25; round++) {
      const auto start = std::chrono::steady_clock::now();
      for (UINT repeat = 0; repeat < 2000; repeat++) {
        o.device->CreateShaderResourceView(o.readable_depth, &srv, sh);
        o.device->CreateUnorderedAccessView(o.color, nullptr, nullptr, sh);
        o.device->CreateUnorderedAccessView(o.buffer, nullptr, &uav, sh);
        o.device->CreateRenderTargetView(o.color, nullptr, rh);
        o.device->CreateDepthStencilView(o.depth, nullptr, dh);
      }
      const double us = std::chrono::duration<double, std::micro>(
                            std::chrono::steady_clock::now() - start)
                            .count();
      const auto descriptor = ReadDescriptorFields(shader, 0);
      if (descriptor.type != dxmt::ShaderVisibleDescriptorType::UAVBuffer ||
          descriptor.UAVBuffer.buffer !=
              static_cast<dxmt::MTLD3D12Resource *>(o.buffer)->buffer.ptr() ||
          rtv->GetRenderTarget(0).Texture !=
              static_cast<dxmt::MTLD3D12Resource *>(o.color)->texture.ptr() ||
          dsv->GetRenderTarget(0).Texture !=
              static_cast<dxmt::MTLD3D12Resource *>(o.depth)->texture.ptr())
        return false;
      std::cout << "view_cost round=" << round
                << " writes=10000 write_us=" << us << "\n";
    }
    return true;
  }
  bool good = true;
  for (const bool seeded : {false, true}) {
    for (UINT kind = 0; kind < 5; kind++) {
      shader->ClearDescriptor(0);
      rtv->AddRenderTarget(0, nullptr);
      dsv->AddRenderTarget(0, nullptr);
      const auto write = [&](bool allowed) {
        switch (kind) {
        case 0:
          o.device->CreateShaderResourceView(
              allowed ? o.readable_depth : o.denied_depth, &srv, sh);
          break;
        case 1:
          o.device->CreateUnorderedAccessView(allowed ? o.color : o.plain,
                                              nullptr, nullptr, sh);
          break;
        case 2:
          o.device->CreateUnorderedAccessView(
              allowed ? o.buffer : o.plain_buffer, nullptr, &uav, sh);
          break;
        case 3:
          o.device->CreateRenderTargetView(allowed ? o.color : o.plain, nullptr,
                                           rh);
          break;
        case 4:
          o.device->CreateDepthStencilView(allowed ? o.depth : o.plain_depth,
                                           nullptr, dh);
          break;
        }
      };
      if (seeded)
        write(true);
      const auto before = ReadDescriptorFields(shader, 0);
      const auto before_rtv = rtv->GetRenderTarget(0);
      const auto before_dsv = dsv->GetRenderTarget(0);
      bool pass =
          !seeded ||
          (kind == 0
               ? before.type == dxmt::ShaderVisibleDescriptorType::SRVTexture
           : kind == 1
               ? before.type == dxmt::ShaderVisibleDescriptorType::UAVTexture
           : kind == 2
               ? before.type == dxmt::ShaderVisibleDescriptorType::UAVBuffer
           : kind == 3 ? before_rtv.Texture != nullptr
                       : before_dsv.Texture != nullptr);
      write(false);
      const auto after = ReadDescriptorFields(shader, 0);
      pass &= before.type == after.type;
      if (seeded && kind == 0)
        pass &= before.SRVTexture.texture == after.SRVTexture.texture;
      if (seeded && kind == 1)
        pass &= before.UAVTexture.texture == after.UAVTexture.texture;
      if (seeded && kind == 2)
        pass &= before.UAVBuffer.buffer == after.UAVBuffer.buffer;
      pass &= before_rtv.Texture == rtv->GetRenderTarget(0).Texture &&
              before_dsv.Texture == dsv->GetRenderTarget(0).Texture;
      std::cout << "view_permission kind=" << kind << " seeded=" << seeded
                << " pass=" << pass << "\n";
      good &= pass;
    }
  }
  o.device->CreateShaderResourceView(o.readable_depth, &srv, sh);
  o.device->CreateShaderResourceView(nullptr, &srv, sh);
  bool nulls = ReadDescriptorFields(shader, 0).type ==
               dxmt::ShaderVisibleDescriptorType::Null;
  o.device->CreateUnorderedAccessView(o.buffer, nullptr, &uav, sh);
  o.device->CreateUnorderedAccessView(nullptr, nullptr, &uav, sh);
  nulls &= ReadDescriptorFields(shader, 0).type ==
           dxmt::ShaderVisibleDescriptorType::Null;
  o.device->CreateRenderTargetView(o.color, nullptr, rh);
  o.device->CreateRenderTargetView(nullptr, nullptr, rh);
  o.device->CreateDepthStencilView(o.depth, nullptr, dh);
  o.device->CreateDepthStencilView(nullptr, nullptr, dh);
  nulls &= !rtv->GetRenderTarget(0).Texture && !dsv->GetRenderTarget(0).Texture;
  std::cout << "view_permission null_descriptors pass=" << nulls << "\n";
  good &= nulls;
  return good;
}

bool CheckUAVCounterContract() {
  struct Objects {
    ID3D12Device *device = nullptr;
    ID3D12DescriptorHeap *heap = nullptr;
    ID3D12Resource *buffer = nullptr, *seed = nullptr, *counter = nullptr;
    ~Objects() {
      Release(heap);
      Release(buffer);
      Release(seed);
      Release(counter);
      Release(device);
    }
  } o;
  if (!CheckHR("Counter contract device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&o.device))))
    return false;
  const auto properties = DefaultHeap();
  auto desc =
      BufferDescription(8196, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  for (auto out : {&o.buffer, &o.seed, &o.counter})
    if (!CheckHR("Counter contract buffer",
                 o.device->CreateCommittedResource(
                     &properties, D3D12_HEAP_FLAG_NONE, &desc,
                     D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(out))))
      return false;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 1;
  if (!CheckHR("Counter contract heap", o.device->CreateDescriptorHeap(
                                            &heap_desc, IID_PPV_ARGS(&o.heap))))
    return false;
  auto *heap = static_cast<dxmt::MTLD3D12DescriptorHeap *>(o.heap);
  const auto handle = o.heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_UNORDERED_ACCESS_VIEW_DESC structured = {};
  structured.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  structured.Buffer.NumElements = 4;
  structured.Buffer.StructureByteStride = 4;
  auto raw = structured;
  raw.Format = DXGI_FORMAT_R32_TYPELESS;
  raw.Buffer.StructureByteStride = 0;
  raw.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  auto typed = structured;
  typed.Format = DXGI_FORMAT_R32_UINT;
  typed.Buffer.StructureByteStride = 0;
  const auto write = [&](ID3D12Resource *resource, ID3D12Resource *counter,
                         const D3D12_UNORDERED_ACCESS_VIEW_DESC *view) {
    o.device->CreateUnorderedAccessView(resource, counter, view, handle);
  };
  bool good = true;
  // Distinct buffer identity makes even offset-only invalid writes observable.
  // Invalid input is checked for safe slot preservation, not native slot
  // semantics.
  for (const bool seeded : {false, true}) {
    for (UINT kind = 0; kind < 15; kind++) {
      heap->ClearDescriptor(0);
      if (seeded)
        write(o.seed, nullptr, &structured);
      const auto before = ReadDescriptorFields(heap, 0);
      auto view = structured;
      ID3D12Resource *resource = o.buffer, *counter = o.counter;
      const D3D12_UNORDERED_ACCESS_VIEW_DESC *view_ptr = &view;
      const char *name = nullptr;
      switch (kind) {
      case 0:
        name = "offset4";
        view.Buffer.CounterOffsetInBytes = 4;
        break;
      case 1:
        name = "offset4092";
        view.Buffer.CounterOffsetInBytes = 4092;
        break;
      case 2:
        name = "offset4100";
        view.Buffer.CounterOffsetInBytes = 4100;
        break;
      case 3:
        name = "offset-past-last-word";
        view.Buffer.CounterOffsetInBytes = 8196;
        break;
      case 4:
        name = "offset-overflow";
        view.Buffer.CounterOffsetInBytes = UINT64_MAX;
        break;
      case 5:
        name = "raw-counter";
        view = raw;
        break;
      case 6:
        name = "typed-counter";
        view = typed;
        break;
      case 7:
        name = "zero-stride-counter";
        view.Buffer.StructureByteStride = 0;
        break;
      case 8:
        name = "null-resource-counter";
        resource = nullptr;
        break;
      case 9:
        name = "null-desc-counter";
        view_ptr = nullptr;
        break;
      case 10:
        name = "structured-offset-without-counter";
        counter = nullptr;
        view.Buffer.CounterOffsetInBytes = 4096;
        break;
      case 11:
        name = "raw-offset-without-counter";
        counter = nullptr;
        view = raw;
        view.Buffer.CounterOffsetInBytes = 4096;
        break;
      case 12:
        name = "typed-offset-without-counter";
        counter = nullptr;
        view = typed;
        view.Buffer.CounterOffsetInBytes = 4096;
        break;
      case 13:
        name = "aligned-out-of-range";
        view.Buffer.CounterOffsetInBytes = 12288;
        break;
      case 14:
        name = "null-resource-offset-without-counter";
        resource = nullptr;
        counter = nullptr;
        view.Buffer.CounterOffsetInBytes = 4096;
        break;
      }
      write(resource, counter, view_ptr);
      const auto after = ReadDescriptorFields(heap, 0);
      const bool pass =
          before.type == after.type &&
          (!seeded ||
           (after.type == dxmt::ShaderVisibleDescriptorType::UAVBuffer &&
            after.UAVBuffer.buffer == before.UAVBuffer.buffer &&
            after.UAVBuffer.counter == before.UAVBuffer.counter));
      std::cout << "counter_contract case=" << name << " seeded=" << seeded
                << " pass=" << pass << "\n";
      good &= pass;
    }
  }
  for (const UINT64 offset : {UINT64(0), UINT64(4096), UINT64(8192)}) {
    heap->ClearDescriptor(0);
    auto view = structured;
    view.Buffer.CounterOffsetInBytes = offset;
    write(o.buffer, o.counter, &view);
    const auto stored = ReadDescriptorFields(heap, 0);
    const bool pass =
        stored.type == dxmt::ShaderVisibleDescriptorType::UAVBuffer &&
        stored.UAVBuffer.buffer ==
            static_cast<dxmt::MTLD3D12Resource *>(o.buffer)->buffer.ptr() &&
        stored.UAVBuffer.counter ==
            static_cast<dxmt::MTLD3D12Resource *>(o.counter)->buffer.ptr();
    std::cout << "counter_contract valid_offset=" << offset << " pass=" << pass
              << "\n";
    good &= pass;
  }
  for (const auto &view : {structured, raw, typed}) {
    heap->ClearDescriptor(0);
    write(o.buffer, nullptr, &view);
    const auto stored = ReadDescriptorFields(heap, 0);
    const auto expected =
        view.Format == DXGI_FORMAT_R32_UINT
            ? dxmt::ShaderVisibleDescriptorType::UAVTexelBuffer
            : dxmt::ShaderVisibleDescriptorType::UAVBuffer;
    const bool pass = stored.type == expected;
    std::cout << "counter_contract valid_counterless_format=" << view.Format
              << " pass=" << pass << "\n";
    good &= pass;
  }
  write(nullptr, nullptr, &structured);
  const bool null_pass = ReadDescriptorFields(heap, 0).type ==
                         dxmt::ShaderVisibleDescriptorType::Null;
  std::cout << "counter_contract valid_null pass=" << null_pass << "\n";
  return good && null_pass;
}

bool CheckTextureViewDimensions() {
  struct Objects {
    ID3D12Device *device = nullptr;
    ID3D12DescriptorHeap *heaps[3] = {};
    ID3D12Resource *resources[8] = {};
    ~Objects() {
      for (auto &heap : heaps) Release(heap);
      for (auto &resource : resources) Release(resource);
      Release(device);
    }
  } o;
  if (!CheckHR("View dimensions device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&o.device))))
    return false;
  const auto properties = DefaultHeap();
  for (UINT i = 0; i < 8; i++) {
    // Native depth/stencil 1D resources are an existing unsupported path.
    if (i == 3)
      continue;
    const bool depth = (i >= 3 && i <= 5) || i == 7;
    const bool multisample = i == 2 || i == 5;
    auto desc = RenderTargetDescription();
    desc.Dimension = (i == 0 || i == 3) ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
                                        : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.DepthOrArraySize = 2;
    desc.SampleDesc.Count = multisample ? 2 : 1;
    desc.Format = depth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Flags = depth         ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                 : multisample ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
                               : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    std::cout << "view_dimensions create_resource=" << i << std::endl;
    if (!CheckHR("View dimensions resource",
                 o.device->CreateCommittedResource(
                     &properties, D3D12_HEAP_FLAG_NONE, &desc,
                     D3D12_RESOURCE_STATE_COMMON, nullptr,
                     IID_PPV_ARGS(&o.resources[i]))))
      return false;
  }
  for (UINT i = 0; i < 3; i++) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = i == 0   ? D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                : i == 1 ? D3D12_DESCRIPTOR_HEAP_TYPE_RTV
                         : D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    desc.NumDescriptors = 1;
    if (!CheckHR("View dimensions heap", o.device->CreateDescriptorHeap(
                                             &desc, IID_PPV_ARGS(&o.heaps[i]))))
      return false;
  }
  auto *shader = static_cast<dxmt::MTLD3D12DescriptorHeap *>(o.heaps[0]);
  auto *rtv =
      static_cast<dxmt::MTLD3D12RenderTargetDescriptorHeap *>(o.heaps[1]);
  auto *dsv =
      static_cast<dxmt::MTLD3D12RenderTargetDescriptorHeap *>(o.heaps[2]);
  // shape: 0=logical1D array,1=logical2D array,2=logical2D MS array.
  const auto write = [&](UINT kind, UINT resource, UINT shape) {
    auto *texture = o.resources[resource];
    if (kind == 0) {
      D3D12_SHADER_RESOURCE_VIEW_DESC v = {};
      v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      v.ViewDimension = shape == 0   ? D3D12_SRV_DIMENSION_TEXTURE1DARRAY
                        : shape == 1 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY
                                     : D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
      if (shape == 0) {
        v.Texture1DArray.MipLevels = 1;
        v.Texture1DArray.ArraySize = 2;
      } else if (shape == 1) {
        v.Texture2DArray.MipLevels = 1;
        v.Texture2DArray.ArraySize = 2;
      } else
        v.Texture2DMSArray.ArraySize = 2;
      o.device->CreateShaderResourceView(
          texture, &v, o.heaps[0]->GetCPUDescriptorHandleForHeapStart());
    } else if (kind == 1) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC v = {};
      v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      v.ViewDimension = shape == 0 ? D3D12_UAV_DIMENSION_TEXTURE1DARRAY
                                   : D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
      if (shape == 0)
        v.Texture1DArray.ArraySize = 2;
      else
        v.Texture2DArray.ArraySize = 2;
      o.device->CreateUnorderedAccessView(
          texture, nullptr, &v,
          o.heaps[0]->GetCPUDescriptorHandleForHeapStart());
    } else if (kind == 2) {
      D3D12_RENDER_TARGET_VIEW_DESC v = {};
      v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      v.ViewDimension = shape == 0   ? D3D12_RTV_DIMENSION_TEXTURE1DARRAY
                        : shape == 1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY
                                     : D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
      if (shape == 0)
        v.Texture1DArray.ArraySize = 2;
      else if (shape == 1)
        v.Texture2DArray.ArraySize = 2;
      else
        v.Texture2DMSArray.ArraySize = 2;
      o.device->CreateRenderTargetView(
          texture, &v, o.heaps[1]->GetCPUDescriptorHandleForHeapStart());
    } else {
      D3D12_DEPTH_STENCIL_VIEW_DESC v = {};
      v.Format = DXGI_FORMAT_D32_FLOAT;
      v.ViewDimension = shape == 0   ? D3D12_DSV_DIMENSION_TEXTURE1DARRAY
                        : shape == 1 ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY
                                     : D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
      if (shape == 0)
        v.Texture1DArray.ArraySize = 2;
      else if (shape == 1)
        v.Texture2DArray.ArraySize = 2;
      else
        v.Texture2DMSArray.ArraySize = 2;
      o.device->CreateDepthStencilView(
          texture, &v, o.heaps[2]->GetCPUDescriptorHandleForHeapStart());
    }
  };
  const auto bound_texture = [&](UINT kind) -> dxmt::Texture * {
    if (kind == 2)
      return rtv->GetRenderTarget(0).Texture;
    if (kind == 3)
      return dsv->GetRenderTarget(0).Texture;
    const auto stored = ReadDescriptorFields(shader, 0);
    if (kind == 0 &&
        stored.type == dxmt::ShaderVisibleDescriptorType::SRVTexture)
      return stored.SRVTexture.texture;
    if (kind == 1 &&
        stored.type == dxmt::ShaderVisibleDescriptorType::UAVTexture)
      return stored.UAVTexture.texture;
    return nullptr;
  };
  bool good = true;
  for (UINT kind = 0; kind < 4; kind++) {
    for (const bool seeded : {false, true}) {
      // Cross logical dimensions in both directions. RTV/DSV additionally cover
      // both MSAA mismatches without materializing invalid native views.
      for (UINT mismatch = kind == 3 ? 1 : 0; mismatch < (kind >= 2 ? 4u : 2u);
           mismatch++) {
        shader->ClearDescriptor(0);
        rtv->AddRenderTarget(0, nullptr);
        dsv->AddRenderTarget(0, nullptr);
        if (seeded)
          write(kind, kind == 3 ? 7 : 6, 1);
        const auto before = bound_texture(kind);
        const UINT resource = (kind == 3 ? 3 : 0) + (mismatch == 0   ? 0
                                                     : mismatch == 3 ? 2
                                                                     : 1);
        const UINT shape = mismatch == 0 || mismatch == 3 ? 1
                           : mismatch == 1                ? 0
                                                          : 2;
        write(kind, resource, shape);
        const bool pass =
            bound_texture(kind) == before &&
            (!seeded || before == static_cast<dxmt::MTLD3D12Resource *>(
                                      o.resources[kind == 3 ? 7 : 6])
                                      ->texture.ptr());
        std::cout << "view_dimensions kind=" << kind << " mismatch=" << mismatch
                  << " seeded=" << seeded << " pass=" << pass << "\n";
        good &= pass;
      }
    }
    for (UINT shape = kind == 3 ? 1 : 0; shape < (kind == 1 ? 2u : 3u);
         shape++) {
      write(kind, (kind == 3 ? 3 : 0) + shape, shape);
      const bool pass =
          bound_texture(kind) == static_cast<dxmt::MTLD3D12Resource *>(
                                     o.resources[(kind == 3 ? 3 : 0) + shape])
                                     ->texture.ptr();
      std::cout << "view_dimensions kind=" << kind << " valid_shape=" << shape
                << " pass=" << pass << "\n";
      good &= pass;
    }
  }
  return good;
}

bool CheckTypedViewFormats(UINT mode = 0) {
  struct Objects {
    ID3D12Device *device = nullptr;
    ID3D12DescriptorHeap *heaps[3] = {};
    ID3D12Resource *resources[23] = {};
    ~Objects() {
      for (auto &heap : heaps)
        Release(heap);
      for (auto &resource : resources)
        Release(resource);
      Release(device);
    }
  } o;
  if (!CheckHR("Typed formats device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&o.device))))
    return false;
  const auto properties = DefaultHeap();
  // Texture storage: RGBA8,R32,RGB96,D32S8,D24S8, plus a buffer and seed
  // texture.
  const DXGI_FORMAT formats[] = {
      DXGI_FORMAT_R8G8B8A8_TYPELESS,  DXGI_FORMAT_R32_TYPELESS,
      DXGI_FORMAT_R32G32B32_TYPELESS, DXGI_FORMAT_R32G8X24_TYPELESS,
      DXGI_FORMAT_R24G8_TYPELESS,     DXGI_FORMAT_UNKNOWN,
      DXGI_FORMAT_R8G8B8A8_UNORM,     DXGI_FORMAT_R32_TYPELESS,
      DXGI_FORMAT_D32_FLOAT,          DXGI_FORMAT_R32_FLOAT,
      DXGI_FORMAT_R8G8B8A8_SNORM,     DXGI_FORMAT_R32_UINT,
      DXGI_FORMAT_B8G8R8A8_TYPELESS,  DXGI_FORMAT_BC1_TYPELESS,
      DXGI_FORMAT_BC4_TYPELESS,       DXGI_FORMAT_BC4_UNORM,
      DXGI_FORMAT_R16_TYPELESS,       DXGI_FORMAT_D16_UNORM,
      DXGI_FORMAT_R16_TYPELESS,       DXGI_FORMAT_R24G8_TYPELESS,
      DXGI_FORMAT_R24G8_TYPELESS,     DXGI_FORMAT_R32G8X24_TYPELESS,
      DXGI_FORMAT_R32G8X24_TYPELESS};
  for (UINT i = 0; i < (mode == 3   ? 23u
                        : mode == 2 ? 19u
                        : mode == 1 ? 16u
                                    : 9u);
       i++) {
    auto desc =
        i == 5
            ? BufferDescription(256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
            : RenderTargetDescription();
    desc.Format = formats[i];
    if (i == 0 || i == 1 || i == 6 || (i >= 9 && i < 12) || i == 18)
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (i == 3 || i == 4 || i == 7 || i == 8 || i == 16 || i == 17)
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (i >= 12 && i < 16) {
      desc.Width = desc.Height = 4;
      desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    }
    if (mode == 2 && i == 12)
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (i >= 19) {
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
      desc.SampleDesc.Count = 4;
      desc.DepthOrArraySize = i % 2 ? 1 : 2;
    }
    if (!CheckHR("Typed formats resource",
                 o.device->CreateCommittedResource(
                     &properties, D3D12_HEAP_FLAG_NONE, &desc,
                     D3D12_RESOURCE_STATE_COMMON, nullptr,
                     IID_PPV_ARGS(&o.resources[i]))))
      return false;
  }
  for (UINT i = 0; i < 3; i++) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = i == 0   ? D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                : i == 1 ? D3D12_DESCRIPTOR_HEAP_TYPE_RTV
                         : D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    desc.NumDescriptors = 1;
    if (!CheckHR("Typed formats heap", o.device->CreateDescriptorHeap(
                                           &desc, IID_PPV_ARGS(&o.heaps[i]))))
      return false;
  }
  auto *shader = static_cast<dxmt::MTLD3D12DescriptorHeap *>(o.heaps[0]);
  auto *rtv =
      static_cast<dxmt::MTLD3D12RenderTargetDescriptorHeap *>(o.heaps[1]);
  auto *dsv =
      static_cast<dxmt::MTLD3D12RenderTargetDescriptorHeap *>(o.heaps[2]);
  const auto write = [&](UINT kind, UINT resource, DXGI_FORMAT format,
                         bool raw = false, UINT plane = 0,
                         bool defaults = false) {
    auto *object = o.resources[resource];
    if (kind == 0) {
      D3D12_SHADER_RESOURCE_VIEW_DESC v = {};
      v.Format = format;
      v.ViewDimension = resource == 5 ? D3D12_SRV_DIMENSION_BUFFER
                                      : D3D12_SRV_DIMENSION_TEXTURE2D;
      v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      if (resource == 5) {
        v.Buffer.NumElements = 4;
        v.Buffer.StructureByteStride = format == DXGI_FORMAT_UNKNOWN ? 4 : 0;
        v.Buffer.Flags =
            raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
      } else {
        v.Texture2D.MipLevels = 1;
        v.Texture2D.PlaneSlice = plane;
      }
      if (resource >= 19) {
        v = {};
        v.Format = format;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (resource % 2) {
          v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        } else {
          v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
          v.Texture2DMSArray.ArraySize = 2;
        }
      }
      o.device->CreateShaderResourceView(
          object, defaults ? nullptr : &v,
          o.heaps[0]->GetCPUDescriptorHandleForHeapStart());
    } else if (kind == 1) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC v = {};
      v.Format = format;
      v.ViewDimension = resource == 5 ? D3D12_UAV_DIMENSION_BUFFER
                                      : D3D12_UAV_DIMENSION_TEXTURE2D;
      if (resource == 5) {
        v.Buffer.NumElements = 4;
        v.Buffer.StructureByteStride = format == DXGI_FORMAT_UNKNOWN ? 4 : 0;
        v.Buffer.Flags =
            raw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
      }
      o.device->CreateUnorderedAccessView(
          object, nullptr, defaults ? nullptr : &v,
          o.heaps[0]->GetCPUDescriptorHandleForHeapStart());
    } else if (kind == 2) {
      D3D12_RENDER_TARGET_VIEW_DESC v = {};
      v.Format = format;
      v.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
      o.device->CreateRenderTargetView(
          object, defaults ? nullptr : &v,
          o.heaps[1]->GetCPUDescriptorHandleForHeapStart());
    } else {
      D3D12_DEPTH_STENCIL_VIEW_DESC v = {};
      v.Format = format;
      v.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
      o.device->CreateDepthStencilView(
          object, defaults ? nullptr : &v,
          o.heaps[2]->GetCPUDescriptorHandleForHeapStart());
    }
  };
  const auto identity = [&](UINT kind) -> const void * {
    if (kind == 2)
      return rtv->GetRenderTarget(0).Texture;
    if (kind == 3)
      return dsv->GetRenderTarget(0).Texture;
    const auto v = ReadDescriptorFields(shader, 0);
    switch (v.type) {
    case dxmt::ShaderVisibleDescriptorType::SRVTexture:
      return v.SRVTexture.texture;
    case dxmt::ShaderVisibleDescriptorType::UAVTexture:
      return v.UAVTexture.texture;
    case dxmt::ShaderVisibleDescriptorType::SRVBuffer:
      return v.SRVBuffer.buffer;
    case dxmt::ShaderVisibleDescriptorType::SRVTexelBuffer:
      return v.SRVTexelBuffer.buffer;
    case dxmt::ShaderVisibleDescriptorType::UAVBuffer:
      return v.UAVBuffer.buffer;
    case dxmt::ShaderVisibleDescriptorType::UAVTexelBuffer:
      return v.UAVTexelBuffer.buffer;
    default:
      return nullptr;
    }
  };
  if (mode == 3) {
    const DXGI_FORMAT planes[] = {DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
                                  DXGI_FORMAT_X24_TYPELESS_G8_UINT,
                                  DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
                                  DXGI_FORMAT_X32_TYPELESS_G8X24_UINT};
    bool good = true;
    for (UINT resource = 19; resource < 23; resource++) {
      for (UINT plane = 0; plane < 2; plane++) {
        shader->ClearDescriptor(0);
        write(0, resource, planes[(resource < 21 ? 0 : 2) + plane]);
        const bool pass = identity(0) == static_cast<dxmt::MTLD3D12Resource *>(
                                             o.resources[resource])
                                             ->texture.ptr();
        std::cout << "view_plane implicit_resource=" << resource
                  << " plane=" << plane << " pass=" << pass << "\n";
        good &= pass;
      }
    }
    for (UINT i = 0; i < 4; i++) {
      const UINT resource = i < 2 ? 4 : 3;
      for (UINT plane : {0u, 1u, 2u, ~0u}) {
        const bool valid = plane == i % 2;
        for (bool seeded : {false, true}) {
          shader->ClearDescriptor(0);
          if (seeded)
            write(0, 6, DXGI_FORMAT_R8G8B8A8_UNORM);
          const void *before = identity(0);
          write(0, resource, planes[i], false, plane);
          const bool pass =
              valid ? identity(0) == static_cast<dxmt::MTLD3D12Resource *>(
                                         o.resources[resource])
                                         ->texture.ptr()
                    : identity(0) == before && (!seeded || before);
          std::cout << "view_plane explicit=" << i << " plane=" << plane
                    << " seeded=" << seeded << " pass=" << pass << "\n";
          good &= pass;
        }
      }
    }
    return good;
  }
  if (mode == 2) {
    struct Role {
      UINT kind, resource;
      DXGI_FORMAT format;
      UINT plane = 0;
      bool defaults = false;
    };
    const Role invalid[] = {{0, 7, DXGI_FORMAT_D32_FLOAT},
                            {0, 8, DXGI_FORMAT_D32_FLOAT},
                            {0, 3, DXGI_FORMAT_D32_FLOAT_S8X24_UINT},
                            {0, 4, DXGI_FORMAT_D24_UNORM_S8_UINT},
                            {0, 16, DXGI_FORMAT_D16_UNORM},
                            {0, 17, DXGI_FORMAT_D16_UNORM},
                            {0, 8, DXGI_FORMAT_UNKNOWN, 0, true},
                            {0, 17, DXGI_FORMAT_UNKNOWN, 0, true},
                            {3, 7, DXGI_FORMAT_R32_FLOAT},
                            {3, 16, DXGI_FORMAT_R16_UNORM},
                            {3, 3, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS},
                            {3, 3, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT},
                            {3, 4, DXGI_FORMAT_R24_UNORM_X8_TYPELESS},
                            {3, 4, DXGI_FORMAT_X24_TYPELESS_G8_UINT},
                            {2, 1, DXGI_FORMAT_D32_FLOAT},
                            {2, 18, DXGI_FORMAT_D16_UNORM},
                            {1, 1, DXGI_FORMAT_D32_FLOAT},
                            {1, 18, DXGI_FORMAT_D16_UNORM},
                            {1, 0, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
                            {1, 6, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
                            {1, 12, DXGI_FORMAT_B8G8R8A8_UNORM},
                            {1, 12, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}};
    bool good = true;
    for (UINT i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
      const auto &c = invalid[i];
      for (bool seeded : {false, true}) {
        shader->ClearDescriptor(0);
        rtv->AddRenderTarget(0, nullptr);
        dsv->AddRenderTarget(0, nullptr);
        if (seeded)
          write(c.kind, c.kind == 3 ? 8 : (c.resource == 6 ? 0 : 6),
                c.kind == 3 ? DXGI_FORMAT_D32_FLOAT
                            : DXGI_FORMAT_R8G8B8A8_UNORM);
        const void *before = identity(c.kind);
        const auto type_before = ReadDescriptorFields(shader, 0).type;
        write(c.kind, c.resource, c.format, false, c.plane, c.defaults);
        const bool pass = identity(c.kind) == before && (!seeded || before) &&
                          (c.kind >= 2 ||
                           ReadDescriptorFields(shader, 0).type == type_before);
        std::cout << "view_role invalid=" << i << " kind=" << c.kind
                  << " seeded=" << seeded << " pass=" << pass << "\n";
        good &= pass;
      }
    }
    const Role valid[] = {{0, 7, DXGI_FORMAT_R32_FLOAT},
                          {0, 16, DXGI_FORMAT_R16_UNORM},
                          {0, 17, DXGI_FORMAT_R16_UNORM},
                          {0, 3, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS},
                          {0, 3, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, 1},
                          {0, 4, DXGI_FORMAT_R24_UNORM_X8_TYPELESS},
                          {0, 4, DXGI_FORMAT_X24_TYPELESS_G8_UINT, 1},
                          {3, 7, DXGI_FORMAT_D32_FLOAT},
                          {3, 3, DXGI_FORMAT_D32_FLOAT_S8X24_UINT},
                          {3, 4, DXGI_FORMAT_D24_UNORM_S8_UINT},
                          {3, 16, DXGI_FORMAT_D16_UNORM},
                          {3, 17, DXGI_FORMAT_UNKNOWN, 0, true},
                          {0, 0, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
                          {2, 0, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
                          {1, 0, DXGI_FORMAT_R8G8B8A8_UINT},
                          {1, 18, DXGI_FORMAT_R16_UNORM},
                          {2, 18, DXGI_FORMAT_R16_UNORM},
                          {0, 11, DXGI_FORMAT_R32_UINT},
                          {2, 2, DXGI_FORMAT_R32G32B32_FLOAT},
                          {0, 12, DXGI_FORMAT_B8G8R8A8_UNORM},
                          {0, 12, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},
                          {2, 12, DXGI_FORMAT_B8G8R8A8_UNORM},
                          {2, 12, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}};
    for (UINT i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
      const auto &c = valid[i];
      shader->ClearDescriptor(0);
      rtv->AddRenderTarget(0, nullptr);
      dsv->AddRenderTarget(0, nullptr);
      write(c.kind, c.resource, c.format, false, c.plane, c.defaults);
      const bool pass =
          identity(c.kind) ==
          static_cast<dxmt::MTLD3D12Resource *>(o.resources[c.resource])
              ->texture.ptr();
      std::cout << "view_role valid=" << i << " kind=" << c.kind
                << " pass=" << pass << "\n";
      good &= pass;
    }
    return good;
  }
  if (mode == 1) {
    struct Cast {
      UINT resource;
      DXGI_FORMAT format;
      bool depth;
    };
    // Cross-family pairs include equal-width color formats and packed depth
    // formats that alias the same native Metal storage on this host.
    const Cast invalid[] = {{0, DXGI_FORMAT_R32_UINT, false},
                            {1, DXGI_FORMAT_R8G8B8A8_UINT, false},
                            {6, DXGI_FORMAT_R16G16_UINT, false},
                            {7, DXGI_FORMAT_D16_UNORM, true},
                            {3, DXGI_FORMAT_D24_UNORM_S8_UINT, true},
                            {4, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, true},
                            {9, DXGI_FORMAT_R32_UINT, false},
                            {11, DXGI_FORMAT_R32_FLOAT, false},
                            {6, DXGI_FORMAT_R8G8B8A8_SNORM, false},
                            {10, DXGI_FORMAT_R8G8B8A8_UNORM, false}};
    bool good = true;
    for (UINT i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
      const auto &c = invalid[i];
      for (UINT kind = c.depth ? 3u : 0u; kind < (c.depth ? 4u : 3u); kind++)
        for (bool seeded : {false, true}) {
          shader->ClearDescriptor(0);
          rtv->AddRenderTarget(0, nullptr);
          dsv->AddRenderTarget(0, nullptr);
          // A distinct seed resource makes a rejected write observable even
          // when the view format changes but its descriptor kind does not.
          if (seeded)
            write(kind, c.depth ? 8 : (c.resource == 6 ? 0 : 6),
                  c.depth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM);
          const void *before = identity(kind);
          const auto before_shader = ReadDescriptorFields(shader, 0);
          write(kind, c.resource, c.format);
          const bool pass =
              identity(kind) == before && (!seeded || before) &&
              (kind >= 2 ||
               ReadDescriptorFields(shader, 0).type == before_shader.type);
          std::cout << "view_cast invalid=" << i << " kind=" << kind
                    << " seeded=" << seeded << " pass=" << pass << "\n";
          good &= pass;
        }
    }
    const Cast valid[] = {{0, DXGI_FORMAT_R8G8B8A8_UNORM, false},
                          {0, DXGI_FORMAT_R8G8B8A8_SNORM, false},
                          {0, DXGI_FORMAT_R8G8B8A8_UINT, false},
                          {0, DXGI_FORMAT_R8G8B8A8_SINT, false},
                          {1, DXGI_FORMAT_R32_FLOAT, false},
                          {1, DXGI_FORMAT_R32_UINT, false},
                          {1, DXGI_FORMAT_R32_SINT, false},
                          {6, DXGI_FORMAT_R8G8B8A8_UINT, false},
                          {6, DXGI_FORMAT_R8G8B8A8_SINT, false},
                          {10, DXGI_FORMAT_R8G8B8A8_SINT, false},
                          {11, DXGI_FORMAT_R32_SINT, false},
                          {9, DXGI_FORMAT_R32_FLOAT, false},
                          {7, DXGI_FORMAT_D32_FLOAT, true},
                          {3, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, true},
                          {4, DXGI_FORMAT_D24_UNORM_S8_UINT, true}};
    for (UINT i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
      const auto &c = valid[i];
      for (UINT kind = c.depth ? 3u : 0u; kind < (c.depth ? 4u : 3u); kind++) {
        shader->ClearDescriptor(0);
        rtv->AddRenderTarget(0, nullptr);
        dsv->AddRenderTarget(0, nullptr);
        write(kind, c.resource, c.format);
        const bool pass =
            identity(kind) ==
            static_cast<dxmt::MTLD3D12Resource *>(o.resources[c.resource])
                ->texture.ptr();
        std::cout << "view_cast valid=" << i << " kind=" << kind
                  << " pass=" << pass << "\n";
        good &= pass;
      }
    }
    for (UINT kind : {0u, 2u}) {
      write(kind, 6, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
      const bool pass =
          identity(kind) ==
          static_cast<dxmt::MTLD3D12Resource *>(o.resources[6])->texture.ptr();
      std::cout << "view_cast srgb kind=" << kind << " pass=" << pass << "\n";
      good &= pass;
    }
    const Cast sampled[] = {{12, DXGI_FORMAT_B8G8R8A8_UNORM, false},
                            {12, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, false},
                            {13, DXGI_FORMAT_BC1_UNORM, false},
                            {13, DXGI_FORMAT_BC1_UNORM_SRGB, false},
                            {14, DXGI_FORMAT_BC4_UNORM, false},
                            {14, DXGI_FORMAT_BC4_SNORM, false}};
    for (const auto &c : sampled) {
      shader->ClearDescriptor(0);
      write(0, c.resource, c.format);
      const bool pass = identity(0) == static_cast<dxmt::MTLD3D12Resource *>(
                                           o.resources[c.resource])
                                           ->texture.ptr();
      std::cout << "view_cast sampled_format=" << c.format << " pass=" << pass
                << "\n";
      good &= pass;
    }
    for (bool seeded : {false, true}) {
      shader->ClearDescriptor(0);
      if (seeded)
        write(0, 6, DXGI_FORMAT_R8G8B8A8_UNORM);
      const void *before = identity(0);
      write(0, 15, DXGI_FORMAT_BC4_SNORM);
      const bool pass = identity(0) == before && (!seeded || before);
      std::cout << "view_cast typed_bc4 seeded=" << seeded << " pass=" << pass
                << "\n";
      good &= pass;
    }
    return good;
  }
  struct Case {
    UINT kind, resource;
    DXGI_FORMAT typed;
  };
  const Case cases[] = {{0, 0, DXGI_FORMAT_R8G8B8A8_UNORM},
                        {0, 1, DXGI_FORMAT_R32_FLOAT},
                        {0, 2, DXGI_FORMAT_R32G32B32_FLOAT},
                        {1, 0, DXGI_FORMAT_R8G8B8A8_UNORM},
                        {1, 1, DXGI_FORMAT_R32_UINT},
                        {2, 0, DXGI_FORMAT_R8G8B8A8_UNORM},
                        {2, 1, DXGI_FORMAT_R32_FLOAT},
                        {2, 2, DXGI_FORMAT_R32G32B32_FLOAT},
                        {3, 7, DXGI_FORMAT_D32_FLOAT},
                        {3, 3, DXGI_FORMAT_D32_FLOAT_S8X24_UINT},
                        {3, 4, DXGI_FORMAT_D24_UNORM_S8_UINT},
                        {0, 5, DXGI_FORMAT_R32_UINT},
                        {1, 5, DXGI_FORMAT_R32_UINT}};
  bool good = true;
  for (UINT i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const auto &c = cases[i];
    for (const bool defaults : {false, true})
      for (const bool seeded : {false, true}) {
        shader->ClearDescriptor(0);
        rtv->AddRenderTarget(0, nullptr);
        dsv->AddRenderTarget(0, nullptr);
        if (seeded)
          write(c.kind, c.kind == 3 ? 8 : 6,
                c.kind == 3 ? DXGI_FORMAT_D32_FLOAT
                            : DXGI_FORMAT_R8G8B8A8_UNORM);
        const auto before = identity(c.kind);
        const auto before_shader = ReadDescriptorFields(shader, 0);
        write(c.kind, c.resource,
              c.resource == 5 ? DXGI_FORMAT_R32_TYPELESS : formats[c.resource],
              false, 0, defaults);
        const bool pass =
            identity(c.kind) == before && (!seeded || before) &&
            (c.kind >= 2 ||
             ReadDescriptorFields(shader, 0).type == before_shader.type);
        std::cout << "typed_view case=" << i << " defaults=" << defaults
                  << " seeded=" << seeded << " pass=" << pass << "\n";
        good &= pass;
      }
    write(c.kind, c.resource, c.typed);
    const void *expected =
        c.resource == 5
            ? static_cast<const void *>(
                  static_cast<dxmt::MTLD3D12Resource *>(o.resources[5])
                      ->buffer.ptr())
            : static_cast<dxmt::MTLD3D12Resource *>(o.resources[c.resource])
                  ->texture.ptr();
    const bool pass = identity(c.kind) == expected;
    std::cout << "typed_view valid=" << i << " pass=" << pass << "\n";
    good &= pass;
  }
  for (UINT kind : {0u, 1u})
    for (const bool raw : {false, true}) {
      write(kind, 5, raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN, raw);
      const auto v = ReadDescriptorFields(shader, 0);
      const bool pass =
          v.type == (kind == 0 ? dxmt::ShaderVisibleDescriptorType::SRVBuffer
                               : dxmt::ShaderVisibleDescriptorType::UAVBuffer);
      std::cout << "typed_view raw=" << raw << " kind=" << kind
                << " pass=" << pass << "\n";
      good &= pass;
    }
  const DXGI_FORMAT planes[] = {
      DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,
      DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_X24_TYPELESS_G8_UINT};
  for (UINT i = 0; i < 4; i++) {
    write(0, i < 2 ? 3 : 4, planes[i], false, i % 2);
    const bool pass = ReadDescriptorFields(shader, 0).type ==
                          dxmt::ShaderVisibleDescriptorType::SRVTexture &&
                      identity(0) == static_cast<dxmt::MTLD3D12Resource *>(
                                         o.resources[i < 2 ? 3 : 4])
                                         ->texture.ptr();
    std::cout << "typed_view depth_plane=" << i << " pass=" << pass << "\n";
    good &= pass;
  }
  return good;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--root-signature-budget") {
    ID3D12Device *device = nullptr;
    if (!CheckHR("Root budget device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
      return 1;
    const bool passed = CheckRootSignatureBudget(device);
    device->Release();
    return passed ? 0 : 1;
  }

  if (argc == 2 && std::string(argv[1]) == "--texture-view-planes")
    return CheckTypedViewFormats(3) ? 0 : 1;
  if (argc == 2 && std::string(argv[1]) == "--texture-view-roles")
    return CheckTypedViewFormats(2) ? 0 : 1;
  if (argc == 2 && std::string(argv[1]) == "--texture-view-casts")
    return CheckTypedViewFormats(1) ? 0 : 1;
  if (argc == 2 && std::string(argv[1]) == "--typed-view-formats")
    return CheckTypedViewFormats() ? 0 : 1;
  if (argc == 2 && std::string(argv[1]) == "--texture-view-dimensions")
    return CheckTextureViewDimensions() ? 0 : 1;
  if (argc == 2 && std::string(argv[1]) == "--uav-counter-contract")
    return CheckUAVCounterContract() ? 0 : 1;
  if (argc == 2 && (std::string(argv[1]) == "--view-permissions" ||
                    std::string(argv[1]) == "--view-permission-cost"))
    return CheckViewPermissions(std::string(argv[1]) ==
                                "--view-permission-cost")
               ? 0
               : 1;
  ID3D12Device *device_a = nullptr;
  ID3D12Device *device_b = nullptr;
  ID3D12DescriptorHeap *shader_heap_a = nullptr;
  ID3D12DescriptorHeap *shader_heap_b = nullptr;
  ID3D12DescriptorHeap *rtv_heap_a = nullptr;
  ID3D12DescriptorHeap *rtv_heap_b = nullptr;
  ID3D12Resource *buffer_a = nullptr;
  ID3D12Resource *buffer_b = nullptr;
  ID3D12Resource *counter_a = nullptr;
  ID3D12Resource *counter_b = nullptr;
  ID3D12Resource *texture_a = nullptr;
  ID3D12Resource *texture_b = nullptr;
  bool passed = true;

  auto cleanup = [&] {
    Release(texture_b);
    Release(texture_a);
    Release(counter_b);
    Release(counter_a);
    Release(buffer_b);
    Release(buffer_a);
    Release(rtv_heap_b);
    Release(rtv_heap_a);
    Release(shader_heap_b);
    Release(shader_heap_a);
    Release(device_b);
    Release(device_a);
    return passed;
  };
  auto abort = [&] {
    passed = false;
    return cleanup() ? 0 : 1;
  };

  if (!CheckHR("D3D12CreateDevice A", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_a))) ||
      !CheckHR("D3D12CreateDevice B", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_b))))
    return abort();

  passed &= CheckRootSignatureMetadata(device_a);
  passed &= CheckRootSignatureBudget(device_a);
  if (!passed)
    return abort();

  D3D12_DESCRIPTOR_HEAP_DESC shader_desc = {};
  shader_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  shader_desc.NumDescriptors = 2;
  shader_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (!CheckHR("CreateShaderHeap A", device_a->CreateDescriptorHeap(&shader_desc, IID_PPV_ARGS(&shader_heap_a))) ||
      !CheckHR("CreateShaderHeap B", device_b->CreateDescriptorHeap(&shader_desc, IID_PPV_ARGS(&shader_heap_b))))
    return abort();

  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_desc.NumDescriptors = 1;
  if (!CheckHR("CreateRTVHeap A", device_a->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap_a))) ||
      !CheckHR("CreateRTVHeap B", device_b->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap_b))))
    return abort();

  const auto default_heap = DefaultHeap();
  const auto buffer_desc = BufferDescription(256);
  const auto uav_buffer_desc = BufferDescription(256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  const auto texture_desc = RenderTargetDescription();
  if (!CheckHR("CreateBuffer A", device_a->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer_a))) ||
      !CheckHR("CreateBuffer B", device_b->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &uav_buffer_desc,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&buffer_b))) ||
      !CheckHR("CreateCounter A", device_a->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&counter_a))) ||
      !CheckHR("CreateCounter B", device_b->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&counter_b))) ||
      !CheckHR("CreateTexture A", device_a->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&texture_a))) ||
      !CheckHR("CreateTexture B", device_b->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&texture_b))))
    return abort();

  auto *shader_impl_a = static_cast<dxmt::MTLD3D12DescriptorHeap *>(shader_heap_a);
  auto *shader_impl_b = static_cast<dxmt::MTLD3D12DescriptorHeap *>(shader_heap_b);
  auto *rtv_impl_a = static_cast<dxmt::MTLD3D12RenderTargetDescriptorHeap *>(rtv_heap_a);
  auto *rtv_impl_b = static_cast<dxmt::MTLD3D12RenderTargetDescriptorHeap *>(rtv_heap_b);
  D3D12_CPU_DESCRIPTOR_HANDLE shader_cpu_a = {};
  D3D12_CPU_DESCRIPTOR_HANDLE shader_cpu_b = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_a = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_b = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = DXGI_FORMAT_UNKNOWN;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Buffer.NumElements = 64;
  srv_desc.Buffer.StructureByteStride = 4;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 64;
  uav_desc.Buffer.StructureByteStride = 4;
  D3D12_RENDER_TARGET_VIEW_DESC rtv_view = {};
  rtv_view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rtv_view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  rtv_view.Texture2D.MipSlice = 0;
  rtv_view.Texture2D.PlaneSlice = 0;
  shader_heap_a->GetCPUDescriptorHandleForHeapStart(&shader_cpu_a);
  shader_heap_b->GetCPUDescriptorHandleForHeapStart(&shader_cpu_b);
  rtv_heap_a->GetCPUDescriptorHandleForHeapStart(&rtv_cpu_a);
  rtv_heap_b->GetCPUDescriptorHandleForHeapStart(&rtv_cpu_b);

  const auto initial_generation_a = shader_impl_a->GetMutationGeneration();
  const auto initial_generation_b = shader_impl_b->GetMutationGeneration();
  device_a->CreateShaderResourceView(buffer_a, &srv_desc, shader_cpu_a);
  passed &= shader_impl_a->GetMutationGeneration() == initial_generation_a + 1;
  passed &= IsDescriptorType(shader_impl_a, dxmt::ShaderVisibleDescriptorType::SRVBuffer, "own SRV");
  passed &= IsDescriptorType(shader_impl_b, dxmt::ShaderVisibleDescriptorType::Null, "initial B");

  device_a->CreateShaderResourceView(buffer_a, &srv_desc, shader_cpu_b);
  passed &= shader_impl_b->GetMutationGeneration() == initial_generation_b;
  passed &= IsDescriptorType(shader_impl_b, dxmt::ShaderVisibleDescriptorType::Null, "foreign heap SRV");

  device_b->CreateShaderResourceView(buffer_a, &srv_desc, shader_cpu_b);
  passed &= IsDescriptorType(shader_impl_b, dxmt::ShaderVisibleDescriptorType::Null, "foreign resource SRV");

  device_b->CreateUnorderedAccessView(buffer_b, counter_a, &uav_desc, shader_cpu_b);
  passed &= IsDescriptorType(shader_impl_b, dxmt::ShaderVisibleDescriptorType::Null, "foreign UAV counter");

  device_b->CreateUnorderedAccessView(texture_b, counter_b, nullptr, shader_cpu_b);
  passed &= shader_impl_b->GetMutationGeneration() == initial_generation_b;
  passed &= IsDescriptorType(shader_impl_b, dxmt::ShaderVisibleDescriptorType::Null, "texture UAV counter");

  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
  cbv_desc.BufferLocation = buffer_a->GetGPUVirtualAddress();
  cbv_desc.SizeInBytes = 256;
  auto generation = shader_impl_a->GetMutationGeneration();
  device_a->CreateConstantBufferView(&cbv_desc, shader_cpu_a);
  passed &= shader_impl_a->GetMutationGeneration() == generation + 1;
  const auto &valid_cbv = ReadDescriptorFields(shader_impl_a, 0);
  passed &= valid_cbv.type == dxmt::ShaderVisibleDescriptorType::ConstantBuffer;
  passed &= valid_cbv.ConstantBuffer.address == cbv_desc.BufferLocation;
  passed &= valid_cbv.ConstantBuffer.size == cbv_desc.SizeInBytes;
  if (!passed)
    std::cerr << "valid CBV descriptor was not recorded\n";

  // A null CBV must replace the previous descriptor and clear all payloads.
  generation = shader_impl_a->GetMutationGeneration();
  device_a->CreateConstantBufferView(nullptr, shader_cpu_a);
  passed &= shader_impl_a->GetMutationGeneration() == generation + 1;
  const auto &null_cbv = ReadDescriptorFields(shader_impl_a, 0);
  passed &= null_cbv.type == dxmt::ShaderVisibleDescriptorType::Null;
  passed &= null_cbv.ConstantBuffer.address == 0 && null_cbv.ConstantBuffer.size == 0;
  if (!passed)
    std::cerr << "null CBV did not clear the previous descriptor payload\n";

  // Copying a null CBV must preserve the canonical null state.
  const UINT shader_stride = device_a->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE copied_cbv = shader_cpu_a;
  copied_cbv.ptr += shader_stride;
  const auto &fresh_null_cbv = ReadDescriptorFields(shader_impl_a, 1);
  passed &= fresh_null_cbv.type == dxmt::ShaderVisibleDescriptorType::Null;
  passed &= fresh_null_cbv.ConstantBuffer.address == 0 && fresh_null_cbv.ConstantBuffer.size == 0;
  generation = shader_impl_a->GetMutationGeneration();
  device_a->CreateConstantBufferView(nullptr, copied_cbv);
  passed &= shader_impl_a->GetMutationGeneration() == generation + 1;
  passed &= IsDescriptorTypeAt(shader_impl_a, 1, dxmt::ShaderVisibleDescriptorType::Null, "fresh null CBV");
  generation = shader_impl_a->GetMutationGeneration();
  device_a->CopyDescriptorsSimple(1, copied_cbv, shader_cpu_a, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  passed &= shader_impl_a->GetMutationGeneration() == generation + 1;
  passed &= IsDescriptorTypeAt(shader_impl_a, 1, dxmt::ShaderVisibleDescriptorType::Null, "copied null CBV");
  const auto &copied_null_cbv = ReadDescriptorFields(shader_impl_a, 1);
  passed &= copied_null_cbv.ConstantBuffer.address == 0 && copied_null_cbv.ConstantBuffer.size == 0;

  // Reusing the slot with a valid CBV must work after the null overwrite.
  generation = shader_impl_a->GetMutationGeneration();
  device_a->CreateConstantBufferView(&cbv_desc, shader_cpu_a);
  passed &= shader_impl_a->GetMutationGeneration() == generation + 1;
  passed &= IsDescriptorType(shader_impl_a, dxmt::ShaderVisibleDescriptorType::ConstantBuffer, "restored CBV");

  // Residency enumeration can inspect an unused slot while the application
  // replaces it. Type, payload, and resource lifetime must form one read.
  D3D12_DESCRIPTOR_HEAP_DESC source_desc = shader_desc;
  source_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ID3D12DescriptorHeap *source_heap = nullptr;
  if (!CheckHR("Create descriptor race source", device_a->CreateDescriptorHeap(&source_desc, IID_PPV_ARGS(&source_heap))))
    return abort();
  const auto source_cpu = source_heap->GetCPUDescriptorHandleForHeapStart();
  auto source_cbv = source_cpu;
  source_cbv.ptr += shader_stride;
  device_a->CreateShaderResourceView(texture_a, nullptr, source_cpu);
  device_a->CreateConstantBufferView(&cbv_desc, source_cbv);
  // Null range-size arrays mean one descriptor in each range, independently
  // for source and destination. They are optional, not invalid arguments.
  const UINT one_descriptor = 1;
  for (unsigned omitted = 1; omitted < 4; ++omitted) {
    device_a->CreateConstantBufferView(nullptr, shader_cpu_a);
    generation = shader_impl_a->GetMutationGeneration();
    device_a->CopyDescriptors(
        1, &shader_cpu_a, omitted & 1 ? nullptr : &one_descriptor,
        1, &source_cpu, omitted & 2 ? nullptr : &one_descriptor,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );
    passed &= shader_impl_a->GetMutationGeneration() == generation + 1;
    const auto descriptor = ReadDescriptorFields(shader_impl_a, 0);
    const bool copied = descriptor.type == dxmt::ShaderVisibleDescriptorType::SRVTexture &&
                        descriptor.SRVTexture.texture != nullptr;
    if (!copied)
      std::cerr << "FAIL: CopyDescriptors ignored optional range sizes (omitted=" << omitted << ")\n";
    passed &= copied;
  }
  for (bool copy : {false, true}) {
    device_a->CopyDescriptorsSimple(1, shader_cpu_a, source_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    HANDLE start = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread writer([&] {
      WaitForSingleObject(start, INFINITE);
      SetEvent(entered);
      if (copy)
        device_a->CopyDescriptorsSimple(1, shader_cpu_a, source_cbv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      else
        device_a->CreateConstantBufferView(&cbv_desc, shader_cpu_a);
      SetEvent(done);
    });
    {
      auto descriptor_read = shader_impl_a->ReadDescriptor(0);
      const auto &descriptor = descriptor_read.get();
      const auto type = descriptor.type;
      const auto texture = descriptor.SRVTexture.texture;
      const auto view = descriptor.SRVTexture.view;
      SetEvent(start);
      WaitForSingleObject(entered, INFINITE);
      // Allow the writer to finish if the read does not protect its payload.
      WaitForSingleObject(done, 50);
      const bool stable = type == dxmt::ShaderVisibleDescriptorType::SRVTexture &&
                          descriptor.type == type && descriptor.SRVTexture.texture == texture &&
                          descriptor.SRVTexture.view.index == view.index;
      if (!stable)
        std::cerr << "FAIL: descriptor changed from SRV to CBV during residency read (copy=" << copy << ")\n";
      passed &= stable;
    }
    writer.join();
    CloseHandle(done);
    CloseHandle(entered);
    CloseHandle(start);
    passed &= IsDescriptorType(shader_impl_a, dxmt::ShaderVisibleDescriptorType::ConstantBuffer, "concurrent CBV update");
  }

  device_a->CopyDescriptorsSimple(1, shader_cpu_a, source_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  generation = shader_impl_a->GetMutationGeneration();
  HANDLE batch_start = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE batch_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE batch_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  std::thread batch_writer([&] {
    WaitForSingleObject(batch_start, INFINITE);
    SetEvent(batch_entered);
    device_a->CreateConstantBufferView(nullptr, shader_cpu_a);
    SetEvent(batch_done);
  });
  {
    auto descriptor_batch = shader_impl_a->ReadDescriptorBatch();
    const auto first_resource = descriptor_batch.first_resource();
    const auto next_resource = descriptor_batch.next_resource(first_resource);
    const auto &descriptor = descriptor_batch.get(0);
    const auto type = descriptor.type;
    const auto texture = descriptor.SRVTexture.texture;
    const auto view = descriptor.SRVTexture.view;
    SetEvent(batch_start);
    WaitForSingleObject(batch_entered, INFINITE);
    WaitForSingleObject(batch_done, 50);
    const bool stable =
        type == dxmt::ShaderVisibleDescriptorType::SRVTexture &&
        descriptor.type == type && descriptor.SRVTexture.texture == texture &&
        descriptor.SRVTexture.view.index == view.index &&
        first_resource == descriptor_batch.first_resource() &&
        next_resource == descriptor_batch.next_resource(first_resource);
    if (!stable)
      std::cerr << "FAIL: batched descriptor read did not protect type, "
                   "payload, and owner\n";
    passed &= stable;
  }
  batch_writer.join();
  CloseHandle(batch_done);
  CloseHandle(batch_entered);
  CloseHandle(batch_start);
  passed &= shader_impl_a->GetMutationGeneration() == generation + 1;
  passed &=
      IsDescriptorType(shader_impl_a, dxmt::ShaderVisibleDescriptorType::Null,
                       "batched writer update");
  {
    ID3D12DescriptorHeap *index_heap = nullptr;
    ID3D12Resource *index_uav_buffer = nullptr, *index_uav_texture = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC hd = shader_desc;
    hd.NumDescriptors = 128;
    auto td = texture_desc;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    bool ok = SUCCEEDED(device_a->CreateDescriptorHeap(
                  &hd, IID_PPV_ARGS(&index_heap))) &&
              SUCCEEDED(device_a->CreateCommittedResource(
                  &default_heap, D3D12_HEAP_FLAG_NONE, &uav_buffer_desc,
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                  IID_PPV_ARGS(&index_uav_buffer))) &&
              SUCCEEDED(device_a->CreateCommittedResource(
                  &default_heap, D3D12_HEAP_FLAG_NONE, &td,
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                  IID_PPV_ARGS(&index_uav_texture)));
    if (!ok) {
      Release(index_uav_texture);
      Release(index_uav_buffer);
      Release(index_heap);
      return abort();
    }
    const auto stride = device_a->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto handle = [&](UINT slot) {
      auto h = index_heap->GetCPUDescriptorHandleForHeapStart();
      h.ptr += size_t(slot) * stride;
      return h;
    };
    std::set<UINT> expected;
    auto verify = [&]() {
      auto batch = static_cast<dxmt::MTLD3D12DescriptorHeap *>(index_heap)
                       ->ReadDescriptorBatch();
      std::set<UINT> actual;
      for (UINT i = batch.first_resource(); i != UINT_MAX;
           i = batch.next_resource(i)) {
        if (i >= index_heap->GetDesc().NumDescriptors ||
            (!actual.empty() && i <= *actual.rbegin()) ||
            !actual.insert(i).second ||
            actual.size() > index_heap->GetDesc().NumDescriptors)
          return false;
      }
      std::set<UINT> candidates;
      for (UINT i = batch.first_residency_slot(); i != UINT_MAX;
           i = batch.next_residency_slot(i)) {
        if (i >= index_heap->GetDesc().NumDescriptors ||
            (!candidates.empty() && i <= *candidates.rbegin()) ||
            !candidates.insert(i).second ||
            candidates.size() > index_heap->GetDesc().NumDescriptors)
          return false;
      }
      for (auto i : expected)
        if (!candidates.count(i))
          return false;
      if (candidates.size() > 4 * expected.size())
        return false;
      if (actual != expected) {
        std::cerr << "resource index set mismatch expected=" << expected.size()
                  << " actual=" << actual.size() << "\n";
        return false;
      }
      return true;
    };
    ok &= verify();
    device_a->CreateConstantBufferView(&cbv_desc, handle(0));
    expected.insert(0);
    ok &= verify();
    device_a->CreateShaderResourceView(buffer_a, &srv_desc, handle(3));
    expected.insert(3);
    ok &= verify();
    auto typed_srv = srv_desc;
    typed_srv.Format = DXGI_FORMAT_R32_UINT;
    typed_srv.Buffer.StructureByteStride = 0;
    device_a->CreateShaderResourceView(buffer_a, &typed_srv, handle(7));
    expected.insert(7);
    ok &= verify();
    device_a->CreateUnorderedAccessView(index_uav_buffer, counter_a, &uav_desc,
                                        handle(31));
    expected.insert(31);
    ok &= verify();
    auto typed_uav = uav_desc;
    typed_uav.Format = DXGI_FORMAT_R32_UINT;
    typed_uav.Buffer.StructureByteStride = 0;
    device_a->CreateUnorderedAccessView(index_uav_buffer, nullptr, &typed_uav,
                                        handle(64));
    expected.insert(64);
    ok &= verify();
    device_a->CreateShaderResourceView(texture_a, nullptr, handle(127));
    expected.insert(127);
    ok &= verify();
    device_a->CreateUnorderedAccessView(index_uav_texture, nullptr, nullptr,
                                        handle(65));
    expected.insert(65);
    ok &= verify();
    for (auto slot : {65u, 7u, 0u, 127u, 3u, 31u, 64u}) {
      device_a->CreateConstantBufferView(nullptr, handle(slot));
      expected.erase(slot);
      ok &= verify();
    }
    // Repeated clear, reinsertion, live-to-live replacement, and copied nulls.
    UINT state = 41;
    for (UINT iteration = 0; iteration < 256; iteration++) {
      state = state * 1664525u + 1013904223u;
      UINT slot = (state >> 16) % 128;
      if (iteration % 3 == 0) {
        device_a->CreateConstantBufferView(nullptr, handle(slot));
        expected.erase(slot);
      } else if (iteration % 3 == 1) {
        device_a->CreateConstantBufferView(&cbv_desc, handle(slot));
        expected.insert(slot);
      } else {
        device_a->CopyDescriptorsSimple(1, handle(slot), source_cpu,
                                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        expected.insert(slot);
      }
      if (iteration % 37 == 0)
        ok &= verify();
    }
    for (UINT i = 0; i < 128; i++) {
      device_a->CreateConstantBufferView(&cbv_desc, handle(i));
      expected.insert(i);
    }
    ok &= verify();
    for (UINT i = 0; i < 128; i++) {
      device_a->CreateConstantBufferView(nullptr, handle(i));
      expected.erase(i);
      if (i % 17 == 0)
        ok &= verify();
    }
    ok &= verify();
    // Non-overlapping copies within a legal CPU-only source heap update the
    // resource index too.
    Release(index_heap);
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ok &= SUCCEEDED(
        device_a->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&index_heap)));
    if (ok) {
      device_a->CopyDescriptorsSimple(2, handle(0), source_cpu,
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      expected = {0, 1};
      ok &= verify();
      device_a->CopyDescriptorsSimple(2, handle(64), handle(0),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      expected.insert(64);
      expected.insert(65);
      ok &= verify();
      device_a->CreateConstantBufferView(nullptr, handle(1));
      expected.erase(1);
      device_a->CopyDescriptorsSimple(1, handle(64), handle(1),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      expected.erase(64);
      ok &= verify();
    }
    Release(index_heap);
    hd.NumDescriptors = 262147;
    ok &= SUCCEEDED(
        device_a->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&index_heap)));
    expected.clear();
    if (ok) {
      for (UINT i : {0u, 63u, 64u, 65u, 4095u, 4096u, 4097u, 262143u, 262144u,
                     262145u, 262146u}) {
        device_a->CreateConstantBufferView(&cbv_desc, handle(i));
        expected.insert(i);
        ok &= verify();
      }
      for (UINT i : {4096u, 262144u, 0u, 262146u, 63u, 65u, 4097u, 262145u, 64u,
                     4095u, 262143u}) {
        device_a->CreateConstantBufferView(nullptr, handle(i));
        expected.erase(i);
        ok &= verify();
      }
    }
    Release(index_uav_texture);
    Release(index_uav_buffer);
    Release(index_heap);
    passed &= ok;
    if (!ok)
      std::cerr << "resource index enumeration or candidate bound failed\n";
    if (ok)
      std::cout << "resource index seven live view types, clear/reinsert, 256 "
                   "mixed mutations, sparse/dense transitions, and CPU "
                   "same-heap copies passed\n";
  }
  Release(source_heap);

  device_a->CreateRenderTargetView(texture_a, &rtv_view, rtv_cpu_a);
  passed &= rtv_impl_a->GetRenderTarget(0).Texture != nullptr;
  if (!passed)
    std::cerr << "own RTV descriptor was not created\n";
  device_a->CreateRenderTargetView(texture_a, &rtv_view, rtv_cpu_b);
  passed &= rtv_impl_b->GetRenderTarget(0).Texture == nullptr;
  if (!passed)
    std::cerr << "foreign RTV heap was modified\n";

  if (!passed)
    return abort();
  std::cout << "D3D12 descriptor and view ownership validation passed\n";
  return cleanup() ? 0 : 1;
}

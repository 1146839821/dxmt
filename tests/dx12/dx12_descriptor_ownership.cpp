#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include "d3d12_descriptor_heap.hpp"
#include "d3d12_device.hpp"

#include <iostream>
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

} // namespace

int main() {
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
    device_a->CreateConstantBufferView(&cbv_desc, shader_cpu_a);
    SetEvent(batch_done);
  });
  {
    auto descriptor_batch = shader_impl_a->ReadDescriptorBatch();
    const auto &descriptor = descriptor_batch.get(0);
    const auto type = descriptor.type;
    const auto texture = descriptor.SRVTexture.texture;
    const auto view = descriptor.SRVTexture.view;
    SetEvent(batch_start);
    WaitForSingleObject(batch_entered, INFINITE);
    WaitForSingleObject(batch_done, 50);
    const bool stable = type == dxmt::ShaderVisibleDescriptorType::SRVTexture &&
                        descriptor.type == type && descriptor.SRVTexture.texture == texture &&
                        descriptor.SRVTexture.view.index == view.index;
    if (!stable)
      std::cerr << "FAIL: batched descriptor read did not protect type, payload, and owner\n";
    passed &= stable;
  }
  batch_writer.join();
  CloseHandle(batch_done);
  CloseHandle(batch_entered);
  CloseHandle(batch_start);
  passed &= shader_impl_a->GetMutationGeneration() == generation + 1;
  passed &= IsDescriptorType(shader_impl_a, dxmt::ShaderVisibleDescriptorType::ConstantBuffer, "batched writer update");
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

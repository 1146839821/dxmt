#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

template <typename T>
struct Owned {
  T *ptr = nullptr;

  ~Owned() {
    if (ptr)
      ptr->Release();
  }

  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;

  T *operator->() const { return ptr; }
};

bool
CheckHR(const char *name, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

D3D12_HEAP_PROPERTIES
HeapProperties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = type;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC
BufferDesc(UINT64 width, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
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

bool
CreateBuffer(
    ID3D12Device *device, D3D12_HEAP_TYPE heap_type, UINT64 size, D3D12_RESOURCE_STATES state,
    D3D12_RESOURCE_FLAGS flags, Owned<ID3D12Resource> &resource
) {
  const auto properties = HeapProperties(heap_type);
  const auto desc = BufferDesc(size, flags);
  return CheckHR(
      "CreateCommittedResource",
      device->CreateCommittedResource(
          &properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource.ptr)
      )
  );
}

bool
ReadFile(const char *path, std::vector<uint8_t> &data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return false;
  const std::streamsize size = file.tellg();
  if (size <= 0)
    return false;
  data.resize(static_cast<size_t>(size));
  file.seekg(0);
  return file.read(reinterpret_cast<char *>(data.data()), size).good();
}

bool
WaitForQueue(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12CommandList *list) {
  Owned<ID3D12Fence> fence;
  ID3D12CommandList *lists[] = {list};
  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.ptr))) ||
      !CheckHR("Signal", queue->Signal(fence.ptr, 1)))
    return false;

  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    return false;
  const bool signaled = CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)) &&
                        WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0;
  CloseHandle(event);
  return signaled;
}

bool
ResetCommandList(ID3D12CommandAllocator *allocator, ID3D12GraphicsCommandList4 *list) {
  HRESULT hr = E_FAIL;
  for (unsigned attempt = 0; attempt < 100 && FAILED(hr); attempt++) {
    hr = allocator->Reset();
    if (FAILED(hr))
      Sleep(1);
  }
  return CheckHR("ResetCommandAllocator", hr) && CheckHR("ResetCommandList", list->Reset(allocator, nullptr));
}

D3D12_CPU_DESCRIPTOR_HANDLE
DescriptorAt(ID3D12Device *device, ID3D12DescriptorHeap *heap, UINT index) {
  D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += SIZE_T(index) * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  return handle;
}

} // namespace

int
main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_raytracing_trace <library.lib.cso>\n";
    return 2;
  }

  std::vector<uint8_t> shader;
  if (!ReadFile(argv[1], shader)) {
    std::cerr << "failed to read ray tracing library\n";
    return 3;
  }

  Owned<ID3D12Device> device;
  Owned<ID3D12Device5> device5;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> base_list;
  Owned<ID3D12GraphicsCommandList4> list;
  Owned<ID3D12Resource> vertices;
  Owned<ID3D12Resource> blas;
  Owned<ID3D12Resource> blas_scratch;
  Owned<ID3D12Resource> instances;
  Owned<ID3D12Resource> tlas;
  Owned<ID3D12Resource> tlas_scratch;
  Owned<ID3D12Resource> output;
  Owned<ID3D12Resource> readback;
  Owned<ID3D12Resource> shader_binding_table;
  Owned<ID3D12DescriptorHeap> descriptor_heap;
  Owned<ID3D12RootSignature> root_signature;
  Owned<ID3D12StateObject> state_object;
  Owned<ID3D12StateObjectProperties> state_properties;
  Owned<ID3DBlob> root_blob;
  Owned<ID3DBlob> root_error;

  if (!CheckHR(
          "D3D12CreateDevice",
          D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr))
      ) ||
      !CheckHR("QueryInterface(ID3D12Device5)", device->QueryInterface(IID_PPV_ARGS(&device5.ptr))))
    return 1;

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr))
      ) ||
      !CheckHR(
          "CreateCommandList",
          device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, nullptr, IID_PPV_ARGS(&base_list.ptr)
          )
      ) ||
      !CheckHR("QueryInterface(ID3D12GraphicsCommandList4)", base_list->QueryInterface(IID_PPV_ARGS(&list.ptr))))
    return 1;

  const float initial_vertices[] = {
      -1.0f, -1.0f, 0.0f,
      1.0f,  -1.0f, 0.0f,
      0.0f,  1.0f,  0.0f,
  };
  if (!CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_UPLOAD, sizeof(initial_vertices), D3D12_RESOURCE_STATE_GENERIC_READ,
          D3D12_RESOURCE_FLAG_NONE, vertices
      ))
    return 1;
  void *mapped = nullptr;
  if (!CheckHR("MapVertices", vertices->Map(0, nullptr, &mapped)))
    return 1;
  std::memcpy(mapped, initial_vertices, sizeof(initial_vertices));
  vertices->Unmap(0, nullptr);

  D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
  geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geometry.Triangles.VertexBuffer.StartAddress = vertices->GetGPUVirtualAddress();
  geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
  geometry.Triangles.VertexCount = 3;
  geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geometry.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs = {};
  blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  blas_inputs.NumDescs = 1;
  blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blas_inputs.pGeometryDescs = &geometry;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_info = {};
  device5->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &blas_info);
  if (!blas_info.ResultDataMaxSizeInBytes || !blas_info.ScratchDataSizeInBytes ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, blas_info.ResultDataMaxSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, blas
      ) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, blas_info.ScratchDataSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, blas_scratch
      )) {
    std::cerr << "BLAS setup failed\n";
    return 1;
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build = {};
  blas_build.DestAccelerationStructureData = blas->GetGPUVirtualAddress();
  blas_build.Inputs = blas_inputs;
  blas_build.ScratchAccelerationStructureData = blas_scratch->GetGPUVirtualAddress();
  list->BuildRaytracingAccelerationStructure(&blas_build, 0, nullptr);
  if (!CheckHR("Close(BLAS)", list->Close()) || !WaitForQueue(device.ptr, queue.ptr, list.ptr) ||
      !ResetCommandList(allocator.ptr, list.ptr))
    return 1;

  D3D12_RAYTRACING_INSTANCE_DESC instance = {};
  instance.Transform[0][0] = 1.0f;
  instance.Transform[1][1] = 1.0f;
  instance.Transform[2][2] = 1.0f;
  instance.InstanceMask = 0xff;
  instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
  instance.AccelerationStructure = blas->GetGPUVirtualAddress();
  if (!CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_UPLOAD, sizeof(instance), D3D12_RESOURCE_STATE_GENERIC_READ,
          D3D12_RESOURCE_FLAG_NONE, instances
      ) ||
      !CheckHR("MapInstances", instances->Map(0, nullptr, &mapped)))
    return 1;
  std::memcpy(mapped, &instance, sizeof(instance));
  instances->Unmap(0, nullptr);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs = {};
  tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  tlas_inputs.NumDescs = 1;
  tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlas_inputs.InstanceDescs = instances->GetGPUVirtualAddress();
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info = {};
  device5->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_info);
  if (!tlas_info.ResultDataMaxSizeInBytes || !tlas_info.ScratchDataSizeInBytes ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, tlas_info.ResultDataMaxSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, tlas
      ) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, tlas_info.ScratchDataSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, tlas_scratch
      )) {
    std::cerr << "TLAS setup failed\n";
    return 1;
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build = {};
  tlas_build.DestAccelerationStructureData = tlas->GetGPUVirtualAddress();
  tlas_build.Inputs = tlas_inputs;
  tlas_build.ScratchAccelerationStructureData = tlas_scratch->GetGPUVirtualAddress();
  list->BuildRaytracingAccelerationStructure(&tlas_build, 0, nullptr);
  if (!CheckHR("Close(TLAS)", list->Close()) || !WaitForQueue(device.ptr, queue.ptr, list.ptr) ||
      !ResetCommandList(allocator.ptr, list.ptr))
    return 1;

  D3D12_ROOT_SIGNATURE_DESC1 root_desc = {};
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_desc = {};
  versioned_root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  versioned_root_desc.Desc_1_1 = root_desc;
  if (!CheckHR(
          "D3D12SerializeVersionedRootSignature",
          D3D12SerializeVersionedRootSignature(&versioned_root_desc, &root_blob.ptr, &root_error.ptr)
      ) ||
      !CheckHR(
          "CreateRootSignature",
          device->CreateRootSignature(
              0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature.ptr)
          )
      ))
    return 1;

  D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
  descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_heap_desc.NumDescriptors = 2;
  descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (!CheckHR(
          "CreateDescriptorHeap",
          device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap.ptr))
      ) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, sizeof(uint32_t) * 2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, output
      ) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_READBACK, sizeof(uint32_t) * 2, D3D12_RESOURCE_STATE_COPY_DEST,
          D3D12_RESOURCE_FLAG_NONE, readback
      ))
    return 1;

  D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
  output_uav.Format = DXGI_FORMAT_UNKNOWN;
  output_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  output_uav.Buffer.NumElements = 2;
  output_uav.Buffer.StructureByteStride = sizeof(uint32_t);
  device->CreateUnorderedAccessView(output.ptr, nullptr, &output_uav, DescriptorAt(device.ptr, descriptor_heap.ptr, 0));

  D3D12_SHADER_RESOURCE_VIEW_DESC acceleration_structure_srv = {};
  acceleration_structure_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  acceleration_structure_srv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
  acceleration_structure_srv.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress();
  device->CreateShaderResourceView(nullptr, &acceleration_structure_srv, DescriptorAt(device.ptr, descriptor_heap.ptr, 1));

  const D3D12_EXPORT_DESC exports[] = {
      {L"RayGen", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"Miss", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"ClosestHit", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"AnyHit", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"Callable", nullptr, D3D12_EXPORT_FLAG_NONE},
  };
  const D3D12_DXIL_LIBRARY_DESC library = {
      {shader.data(), shader.size()}, static_cast<UINT>(sizeof(exports) / sizeof(exports[0])),
      const_cast<D3D12_EXPORT_DESC *>(exports)
  };
  const D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature = {root_signature.ptr};
  const D3D12_HIT_GROUP_DESC hit_group = {
      L"HitGroup", D3D12_HIT_GROUP_TYPE_TRIANGLES, L"AnyHit", L"ClosestHit", nullptr
  };
  const D3D12_RAYTRACING_SHADER_CONFIG shader_config = {4, 8};
  const D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = {1};
  const D3D12_STATE_SUBOBJECT subobjects[] = {
      {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library},
      {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_root_signature},
      {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader_config},
      {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline_config},
      {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group},
  };
  const D3D12_STATE_OBJECT_DESC state_desc = {
      D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, static_cast<UINT>(sizeof(subobjects) / sizeof(subobjects[0])),
      subobjects
  };
  if (!CheckHR("CreateStateObject", device5->CreateStateObject(&state_desc, IID_PPV_ARGS(&state_object.ptr))) ||
      !CheckHR(
          "QueryInterface(ID3D12StateObjectProperties)",
          state_object->QueryInterface(IID_PPV_ARGS(&state_properties.ptr))
      ))
    return 1;

  const wchar_t *shader_names[] = {L"RayGen", L"Miss", L"HitGroup", L"Callable"};
  if (!state_properties->GetShaderIdentifier(shader_names[0]) ||
      !state_properties->GetShaderIdentifier(shader_names[1]) ||
      !state_properties->GetShaderIdentifier(shader_names[2]) ||
      !state_properties->GetShaderIdentifier(shader_names[3])) {
    std::cerr << "ray tracing shader identifiers were not created\n";
    return 1;
  }
  constexpr UINT shader_record_stride = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
  if (!CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_UPLOAD, shader_record_stride * 4, D3D12_RESOURCE_STATE_GENERIC_READ,
          D3D12_RESOURCE_FLAG_NONE, shader_binding_table
      ) ||
      !CheckHR("MapShaderBindingTable", shader_binding_table->Map(0, nullptr, &mapped)))
    return 1;
  std::memset(mapped, 0, shader_record_stride * 4);
  std::memcpy(static_cast<uint8_t *>(mapped) + shader_record_stride * 0, state_properties->GetShaderIdentifier(L"RayGen"),
              D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  std::memcpy(static_cast<uint8_t *>(mapped) + shader_record_stride * 1, state_properties->GetShaderIdentifier(L"Miss"),
              D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  std::memcpy(
      static_cast<uint8_t *>(mapped) + shader_record_stride * 2, state_properties->GetShaderIdentifier(L"HitGroup"),
      D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES
  );
  std::memcpy(
      static_cast<uint8_t *>(mapped) + shader_record_stride * 3, state_properties->GetShaderIdentifier(L"Callable"),
      D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES
  );
  shader_binding_table->Unmap(0, nullptr);

  ID3D12DescriptorHeap *heaps[] = {descriptor_heap.ptr};
  list->SetDescriptorHeaps(1, heaps);
  list->SetPipelineState1(state_object.ptr);

  D3D12_DISPATCH_RAYS_DESC dispatch = {};
  dispatch.RayGenerationShaderRecord.StartAddress = shader_binding_table->GetGPUVirtualAddress();
  dispatch.RayGenerationShaderRecord.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  dispatch.MissShaderTable.StartAddress = shader_binding_table->GetGPUVirtualAddress() + shader_record_stride;
  dispatch.MissShaderTable.SizeInBytes = shader_record_stride;
  dispatch.MissShaderTable.StrideInBytes = shader_record_stride;
  dispatch.HitGroupTable.StartAddress = shader_binding_table->GetGPUVirtualAddress() + shader_record_stride * 2;
  dispatch.HitGroupTable.SizeInBytes = shader_record_stride;
  dispatch.HitGroupTable.StrideInBytes = shader_record_stride;
  dispatch.CallableShaderTable.StartAddress = shader_binding_table->GetGPUVirtualAddress() + shader_record_stride * 3;
  dispatch.CallableShaderTable.SizeInBytes = shader_record_stride;
  dispatch.CallableShaderTable.StrideInBytes = shader_record_stride;
  dispatch.Width = 2;
  dispatch.Height = 1;
  dispatch.Depth = 1;
  list->DispatchRays(&dispatch);

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = output.ptr;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
  list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, sizeof(uint32_t) * 2);

  if (!CheckHR("Close", list->Close()) || !WaitForQueue(device.ptr, queue.ptr, list.ptr))
    return 1;

  if (!CheckHR("MapReadback", readback->Map(0, nullptr, &mapped)))
    return 1;
  const auto *values = static_cast<const uint32_t *>(mapped);
  const uint32_t hit_value = values[0];
  const uint32_t miss_value = values[1];
  readback->Unmap(0, nullptr);
  if (hit_value != 0x31 || miss_value != 0x40) {
    std::cerr << "DispatchRays trace mismatch: hit=0x" << std::hex << hit_value << " miss=0x" << miss_value
              << " expected hit=0x31 miss=0x40\n";
    return 1;
  }

  std::cout << "D3D12 TraceRay SBT passed: hit=0x" << std::hex << hit_value << " miss=0x" << miss_value << std::dec
            << " callable=1\n";
  return 0;
}

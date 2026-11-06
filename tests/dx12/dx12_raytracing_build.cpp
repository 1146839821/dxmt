#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

template <typename T> struct Owned {
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
BufferDesc(UINT64 width) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = width;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return desc;
}

bool
CreateBuffer(
    ID3D12Device *device, D3D12_HEAP_TYPE heap_type, UINT64 width, D3D12_RESOURCE_STATES state,
    Owned<ID3D12Resource> &resource
) {
  const auto properties = HeapProperties(heap_type);
  const auto desc = BufferDesc(width);
  return CheckHR(
      "CreateCommittedResource",
      device->CreateCommittedResource(
          &properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource.ptr)
      )
  );
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
ResetAllocator(ID3D12CommandAllocator *allocator, const char *name) {
  HRESULT hr = E_FAIL;
  for (unsigned attempt = 0; attempt < 100 && FAILED(hr); attempt++) {
    hr = allocator->Reset();
    if (FAILED(hr))
      Sleep(1);
  }
  return CheckHR(name, hr);
}

} // namespace

int
main() {
  Owned<ID3D12Device> device;
  Owned<ID3D12Device5> device5;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> base_list;
  Owned<ID3D12GraphicsCommandList4> list;
  Owned<ID3D12Resource> vertices;
  Owned<ID3D12Resource> result;
  Owned<ID3D12Resource> scratch;
  Owned<ID3D12Resource> clone;
  Owned<ID3D12Resource> compact;
  Owned<ID3D12Resource> readback;
  Owned<ID3D12Resource> instances;
  Owned<ID3D12Resource> top_level_result;
  Owned<ID3D12Resource> top_level_scratch;
  Owned<ID3D12Resource> top_level_clone;
  Owned<ID3D12Resource> top_level_compact;

  if (!CheckHR(
          "D3D12CreateDevice",
          D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr))
      ) ||
      !CheckHR("QueryInterface(ID3D12Device5)", device->QueryInterface(IID_PPV_ARGS(&device5.ptr))))
    return 1;

  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  if (!CheckHR(
          "CheckFeatureSupport(OPTIONS5)",
          device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))
      ) ||
      options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
    std::cerr << "ray tracing capability was advertised before execution support was complete\n";
    return 1;
  }

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
          device.ptr, D3D12_HEAP_TYPE_UPLOAD, sizeof(initial_vertices), D3D12_RESOURCE_STATE_GENERIC_READ, vertices
      ))
    return 1;

  void *mapped_vertices = nullptr;
  if (!CheckHR("MapVertices", vertices->Map(0, nullptr, &mapped_vertices)))
    return 1;
  std::memcpy(mapped_vertices, initial_vertices, sizeof(initial_vertices));
  vertices->Unmap(0, nullptr);

  D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
  geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  geometry.Triangles.VertexBuffer.StartAddress = vertices->GetGPUVirtualAddress();
  geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
  geometry.Triangles.VertexCount = 3;
  geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geometry.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION |
                 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  inputs.NumDescs = 1;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  inputs.pGeometryDescs = &geometry;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
  device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
  if (!prebuild.ResultDataMaxSizeInBytes || !prebuild.ScratchDataSizeInBytes ||
      prebuild.UpdateScratchDataSizeInBytes > prebuild.ScratchDataSizeInBytes) {
    std::cerr << "BLAS prebuild query returned invalid sizes\n";
    return 1;
  }

  if (!CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, prebuild.ResultDataMaxSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, result
      ) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, prebuild.ScratchDataSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, scratch
      ) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, prebuild.ResultDataMaxSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, clone
      ) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, prebuild.ResultDataMaxSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, compact
      ) ||
      !CreateBuffer(device.ptr, D3D12_HEAP_TYPE_READBACK, 256, D3D12_RESOURCE_STATE_COPY_DEST, readback))
    return 1;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
  build.DestAccelerationStructureData = result->GetGPUVirtualAddress();
  build.Inputs = inputs;
  build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild = {};
  postbuild.DestBuffer = readback->GetGPUVirtualAddress();
  postbuild.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
  list->BuildRaytracingAccelerationStructure(&build, 1, &postbuild);

  D3D12_GPU_VIRTUAL_ADDRESS source_addresses[] = {result->GetGPUVirtualAddress()};
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC emit = postbuild;
  emit.DestBuffer += sizeof(uint64_t);
  list->EmitRaytracingAccelerationStructurePostbuildInfo(&emit, 1, source_addresses);
  list->CopyRaytracingAccelerationStructure(
      clone->GetGPUVirtualAddress(), result->GetGPUVirtualAddress(),
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE
  );
  list->CopyRaytracingAccelerationStructure(
      compact->GetGPUVirtualAddress(), result->GetGPUVirtualAddress(),
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT
  );

  if (!CheckHR("Close(build)", list->Close()) || !WaitForQueue(device.ptr, queue.ptr, list.ptr)) {
    std::cerr << "BLAS command execution failed\n";
    return 1;
  }

  uint64_t *mapped_readback = nullptr;
  if (!CheckHR("MapReadback(build)", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_readback))))
    return 1;
  const uint64_t compacted_size = mapped_readback[0];
  const uint64_t emitted_size = mapped_readback[1];
  readback->Unmap(0, nullptr);
  if (!compacted_size || compacted_size > prebuild.ResultDataMaxSizeInBytes || emitted_size != compacted_size) {
    std::cerr << "invalid compacted size output: " << compacted_size << "," << emitted_size << "\n";
    return 1;
  }

  const float updated_vertices[] = {
      -0.5f, -1.0f, 0.0f,
      0.5f,  -1.0f, 0.0f,
      0.0f,  0.5f,  0.0f,
  };
  if (!CheckHR("MapVertices(update)", vertices->Map(0, nullptr, &mapped_vertices)))
    return 1;
  std::memcpy(mapped_vertices, updated_vertices, sizeof(updated_vertices));
  vertices->Unmap(0, nullptr);

  if (!ResetAllocator(allocator.ptr, "ResetAllocator(update)") ||
      !CheckHR("ResetCommandList(update)", list->Reset(allocator.ptr, nullptr)))
    return 1;

  build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
  build.SourceAccelerationStructureData = result->GetGPUVirtualAddress();
  postbuild.DestBuffer = readback->GetGPUVirtualAddress() + 2 * sizeof(uint64_t);
  list->BuildRaytracingAccelerationStructure(&build, 1, &postbuild);
  if (!CheckHR("Close(update)", list->Close()) || !WaitForQueue(device.ptr, queue.ptr, list.ptr)) {
    std::cerr << "BLAS update execution failed\n";
    return 1;
  }

  if (!CheckHR("MapReadback(update)", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_readback))))
    return 1;
  const uint64_t updated_compacted_size = mapped_readback[2];
  readback->Unmap(0, nullptr);
  if (!updated_compacted_size || updated_compacted_size > prebuild.ResultDataMaxSizeInBytes) {
    std::cerr << "invalid updated compacted size output: " << updated_compacted_size << "\n";
    return 1;
  }

  D3D12_RAYTRACING_INSTANCE_DESC instance = {};
  instance.Transform[0][0] = 1.0f;
  instance.Transform[1][1] = 1.0f;
  instance.Transform[2][2] = 1.0f;
  instance.InstanceID = 17;
  instance.InstanceMask = 0xff;
  instance.AccelerationStructure = result->GetGPUVirtualAddress();
  if (!CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_UPLOAD, sizeof(instance), D3D12_RESOURCE_STATE_GENERIC_READ, instances
      ))
    return 1;
  if (!CheckHR("MapInstances", instances->Map(0, nullptr, &mapped_vertices)))
    return 1;
  std::memcpy(mapped_vertices, &instance, sizeof(instance));
  instances->Unmap(0, nullptr);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top_level_inputs = {};
  top_level_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  top_level_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                           D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION |
                           D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  top_level_inputs.NumDescs = 1;
  top_level_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  top_level_inputs.InstanceDescs = instances->GetGPUVirtualAddress();

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_level_prebuild = {};
  device5->GetRaytracingAccelerationStructurePrebuildInfo(&top_level_inputs, &top_level_prebuild);
  if (!top_level_prebuild.ResultDataMaxSizeInBytes || !top_level_prebuild.ScratchDataSizeInBytes ||
      top_level_prebuild.UpdateScratchDataSizeInBytes > top_level_prebuild.ScratchDataSizeInBytes) {
    std::cerr << "TLAS prebuild query returned invalid sizes\n";
    return 1;
  }
  if (!CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, top_level_prebuild.ResultDataMaxSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, top_level_result
      ) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, top_level_prebuild.ScratchDataSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, top_level_scratch
      ) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, top_level_prebuild.ResultDataMaxSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, top_level_clone
      ) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, top_level_prebuild.ResultDataMaxSizeInBytes,
          D3D12_RESOURCE_STATE_COMMON, top_level_compact
      ))
    return 1;

  if (!ResetAllocator(allocator.ptr, "ResetAllocator(top-level)") ||
      !CheckHR("ResetCommandList(top-level)", list->Reset(allocator.ptr, nullptr)))
    return 1;
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_level_build = {};
  top_level_build.DestAccelerationStructureData = top_level_result->GetGPUVirtualAddress();
  top_level_build.Inputs = top_level_inputs;
  top_level_build.ScratchAccelerationStructureData = top_level_scratch->GetGPUVirtualAddress();
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC top_level_postbuild = {};
  top_level_postbuild.DestBuffer = readback->GetGPUVirtualAddress() + 3 * sizeof(uint64_t);
  top_level_postbuild.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
  list->BuildRaytracingAccelerationStructure(&top_level_build, 1, &top_level_postbuild);
  D3D12_GPU_VIRTUAL_ADDRESS top_level_source[] = {top_level_result->GetGPUVirtualAddress()};
  top_level_postbuild.DestBuffer += sizeof(uint64_t);
  list->EmitRaytracingAccelerationStructurePostbuildInfo(&top_level_postbuild, 1, top_level_source);
  list->CopyRaytracingAccelerationStructure(
      top_level_clone->GetGPUVirtualAddress(), top_level_result->GetGPUVirtualAddress(),
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE
  );
  list->CopyRaytracingAccelerationStructure(
      top_level_compact->GetGPUVirtualAddress(), top_level_result->GetGPUVirtualAddress(),
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT
  );
  if (!CheckHR("Close(top-level)", list->Close()) || !WaitForQueue(device.ptr, queue.ptr, list.ptr)) {
    std::cerr << "TLAS command execution failed\n";
    return 1;
  }
  if (!CheckHR("MapReadback(top-level)", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_readback))))
    return 1;
  const uint64_t top_level_compacted_size = mapped_readback[3];
  const uint64_t top_level_emitted_size = mapped_readback[4];
  readback->Unmap(0, nullptr);
  if (!top_level_compacted_size || top_level_compacted_size > top_level_prebuild.ResultDataMaxSizeInBytes ||
      top_level_emitted_size != top_level_compacted_size) {
    std::cerr << "invalid TLAS compacted size output: " << top_level_compacted_size << ","
              << top_level_emitted_size << "\n";
    return 1;
  }

  if (!ResetAllocator(allocator.ptr, "ResetAllocator(unsupported)") ||
      !CheckHR("ResetCommandList(unsupported)", list->Reset(allocator.ptr, nullptr)))
    return 1;
  geometry.Triangles.VertexFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
  list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
  if (list->Close() != E_FAIL) {
    std::cerr << "unsupported BLAS command was accepted\n";
    return 1;
  }

  std::cout << "D3D12 raytracing command build passed: result=" << prebuild.ResultDataMaxSizeInBytes
            << ",compacted=" << compacted_size << ",updated=" << updated_compacted_size
            << ",tlas=" << top_level_prebuild.ResultDataMaxSizeInBytes << "\n";
  return 0;
}

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

} // namespace

int
main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_raytracing_dispatch <library.lib.cso>\n";
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
  descriptor_heap_desc.NumDescriptors = 1;
  descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (!CheckHR("CreateDescriptorHeap", device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap.ptr))) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_DEFAULT, sizeof(uint32_t), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, output
      ) ||
      !CreateBuffer(device.ptr, D3D12_HEAP_TYPE_READBACK, sizeof(uint32_t), D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_FLAG_NONE, readback) ||
      !CreateBuffer(
          device.ptr, D3D12_HEAP_TYPE_UPLOAD, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT,
          D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, shader_binding_table
      ))
    return 1;

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 1;
  uav.Buffer.StructureByteStride = sizeof(uint32_t);
  device->CreateUnorderedAccessView(
      output.ptr, nullptr, &uav, descriptor_heap->GetCPUDescriptorHandleForHeapStart()
  );

  D3D12_EXPORT_DESC export_desc = {L"RayGen", nullptr, D3D12_EXPORT_FLAG_NONE};
  D3D12_DXIL_LIBRARY_DESC library_desc = {{shader.data(), shader.size()}, 1, &export_desc};
  D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature = {root_signature.ptr};
  D3D12_RAYTRACING_SHADER_CONFIG shader_config = {4, 16};
  D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = {1};
  D3D12_STATE_SUBOBJECT subobjects[] = {
      {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library_desc},
      {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_root_signature},
      {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader_config},
      {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline_config},
  };
  D3D12_STATE_OBJECT_DESC state_desc = {
      D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, static_cast<UINT>(sizeof(subobjects) / sizeof(subobjects[0])), subobjects,
  };
  if (!CheckHR("CreateStateObject", device5->CreateStateObject(&state_desc, IID_PPV_ARGS(&state_object.ptr))) ||
      !CheckHR(
          "QueryInterface(ID3D12StateObjectProperties)",
          state_object->QueryInterface(IID_PPV_ARGS(&state_properties.ptr))
      ))
    return 1;

  const void *shader_identifier = state_properties->GetShaderIdentifier(L"RayGen");
  if (!shader_identifier) {
    std::cerr << "RayGen shader identifier was not created\n";
    return 1;
  }
  void *mapped_sbt = nullptr;
  if (!CheckHR("MapShaderBindingTable", shader_binding_table->Map(0, nullptr, &mapped_sbt)))
    return 1;
  std::memset(mapped_sbt, 0, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
  std::memcpy(mapped_sbt, shader_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  shader_binding_table->Unmap(0, nullptr);

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

  ID3D12DescriptorHeap *heaps[] = {descriptor_heap.ptr};
  list->SetDescriptorHeaps(1, heaps);
  list->SetPipelineState1(state_object.ptr);

  D3D12_DISPATCH_RAYS_DESC dispatch = {};
  dispatch.RayGenerationShaderRecord.StartAddress = shader_binding_table->GetGPUVirtualAddress();
  dispatch.RayGenerationShaderRecord.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  dispatch.Width = 1;
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
  list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, sizeof(uint32_t));

  if (!CheckHR("Close", list->Close()) || !WaitForQueue(device.ptr, queue.ptr, list.ptr))
    return 1;

  void *mapped_readback = nullptr;
  if (!CheckHR("MapReadback", readback->Map(0, nullptr, &mapped_readback)))
    return 1;
  const uint32_t value = *static_cast<const uint32_t *>(mapped_readback);
  readback->Unmap(0, nullptr);
  if (value != 0xd312d312) {
    std::cerr << "DispatchRays readback mismatch: 0x" << std::hex << value << " expected 0xd312d312\n";
    return 1;
  }

  std::cout << "D3D12 DispatchRays passed: value=0x" << std::hex << value << std::dec << "\n";
  return 0;
}

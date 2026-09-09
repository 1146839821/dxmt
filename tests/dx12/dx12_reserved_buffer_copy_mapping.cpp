#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr UINT TileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT Expected = 0x76543210;

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

void Check(const char *operation, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << operation << ": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    throw std::runtime_error(operation);
  }
}

D3D12_HEAP_PROPERTIES Properties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = type;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = bytes;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
  return desc;
}

void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  list->ResourceBarrier(1, &barrier);
}

std::vector<char> ReadShader(const char *path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() <= 0)
    throw std::runtime_error("cannot open shader");
  const auto size = file.tellg();
  std::vector<char> data(static_cast<size_t>(size));
  file.seekg(0);
  file.read(data.data(), data.size());
  if (!file)
    throw std::runtime_error("cannot read shader");
  return data;
}

void Wait(ID3D12Fence *fence, UINT64 value) {
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    throw std::runtime_error("CreateEvent");
  const HRESULT hr = fence->SetEventOnCompletion(value, event);
  const DWORD status = SUCCEEDED(hr) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event);
  if (status != WAIT_OBJECT_0) {
    std::cerr << "GPU completion failed or timed out at " << value << "\n";
    ExitProcess(1);
  }
}

void CreateBuffer(ID3D12Device *device, Owned<ID3D12Resource> &resource, UINT64 bytes, D3D12_HEAP_TYPE type,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
  auto properties = Properties(type);
  auto desc = BufferDesc(bytes, flags);
  Check("buffer", device->CreateCommittedResource(
                       &properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                       IID_PPV_ARGS(&resource.ptr)));
}

int Run(const char *shader_path) {
  const auto shader = ReadShader(shader_path);

  Owned<ID3D12Device> device;
  Check("device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)));
  Owned<ID3D12CommandQueue> direct;
  Owned<ID3D12CommandQueue> compute;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Check("direct queue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&direct.ptr)));
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  Check("compute queue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&compute.ptr)));

  Owned<ID3D12Fence> mapping_done;
  Owned<ID3D12Fence> compute_done;
  Check("mapping fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mapping_done.ptr)));
  Check("compute fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&compute_done.ptr)));

  Owned<ID3D12Heap> heap;
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = 2ull * TileBytes;
  heap_desc.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  Check("tile heap", device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap.ptr)));

  Owned<ID3D12Resource> source;
  Owned<ID3D12Resource> destination;
  const auto sparse_desc = BufferDesc(TileBytes);
  Check("source", device->CreateReservedResource(
                         &sparse_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&source.ptr)));
  Check("destination", device->CreateReservedResource(
                              &sparse_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&destination.ptr)));
  if (!source->GetGPUVirtualAddress() || !destination->GetGPUVirtualAddress()) {
    std::cout << "SKIP: reserved buffer has no shader-visible backing\n";
    return 77;
  }

  Owned<ID3D12Resource> upload;
  Owned<ID3D12Resource> output;
  Owned<ID3D12Resource> readback;
  CreateBuffer(device.ptr, upload, TileBytes, D3D12_HEAP_TYPE_UPLOAD,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
  CreateBuffer(device.ptr, output, sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
  CreateBuffer(device.ptr, readback, sizeof(UINT), D3D12_HEAP_TYPE_READBACK,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  UINT *upload_data = nullptr;
  Check("upload map", upload->Map(0, nullptr, reinterpret_cast<void **>(&upload_data)));
  for (UINT i = 0; i < TileBytes / sizeof(UINT); i++)
    upload_data[i] = Expected;
  upload->Unmap(0, nullptr);

  // The destination descriptor exists before CopyTileMappings changes its
  // physical backing. The descriptor must continue to resolve the same sparse
  // virtual resource after the mapping copy.
  Owned<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc = {};
  descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_desc.NumDescriptors = 2;
  descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check("descriptor heap", device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&descriptors.ptr)));
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
  const UINT stride = device->GetDescriptorHandleIncrementSize(descriptor_desc.Type);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = TileBytes / sizeof(UINT);
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device->CreateShaderResourceView(destination.ptr, &srv, cpu);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 1;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  cpu.ptr += stride;
  device->CreateUnorderedAccessView(output.ptr, nullptr, &uav, cpu);

  D3D12_DESCRIPTOR_RANGE ranges[2] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
      {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1},
  };
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  Owned<ID3DBlob> root_blob;
  Owned<ID3DBlob> root_errors;
  Check("serialize root signature", D3D12SerializeRootSignature(
                                         &root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &root_blob.ptr, &root_errors.ptr));
  Owned<ID3D12RootSignature> root;
  Check("root signature", device->CreateRootSignature(
                              0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                              IID_PPV_ARGS(&root.ptr)));
  Owned<ID3D12PipelineState> pso;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.ptr;
  pipeline_desc.CS = {shader.data(), shader.size()};
  Check("compute PSO", device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pso.ptr)));

  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  D3D12_TILE_REGION_SIZE region = {};
  region.NumTiles = 1;
  UINT heap_tile = 1;
  direct->UpdateTileMappings(source.ptr, 1, &coordinate, &region, heap.ptr, 1,
                              nullptr, &heap_tile, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);

  Owned<ID3D12CommandAllocator> init_allocator;
  Owned<ID3D12GraphicsCommandList> init_list;
  Check("init allocator", device->CreateCommandAllocator(
                                  D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&init_allocator.ptr)));
  Check("init list", device->CreateCommandList(
                            0, D3D12_COMMAND_LIST_TYPE_DIRECT, init_allocator.ptr, nullptr,
                            IID_PPV_ARGS(&init_list.ptr)));
  Transition(init_list.ptr, source.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
  init_list->CopyTiles(source.ptr, &coordinate, &region, upload.ptr, 0,
                       D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  Transition(init_list.ptr, source.ptr, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
  Check("init close", init_list->Close());
  ID3D12CommandList *init_lists[] = {init_list.ptr};
  direct->ExecuteCommandLists(1, init_lists);

  // Copy the source mapping to the destination after the source data upload.
  // The compute queue waits on the signal below, which also waits for the
  // sparse mapping queue's event in DXMT's direct-queue implementation.
  direct->CopyTileMappings(destination.ptr, &coordinate, source.ptr, &coordinate, &region,
                            D3D12_TILE_MAPPING_FLAG_NONE);
  Check("mapping signal", direct->Signal(mapping_done.ptr, 1));
  Check("compute waits mapping", compute->Wait(mapping_done.ptr, 1));

  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check("compute allocator", device->CreateCommandAllocator(
                                  D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator.ptr)));
  Check("compute list", device->CreateCommandList(
                            0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.ptr, pso.ptr,
                            IID_PPV_ARGS(&list.ptr)));
  list->SetComputeRootSignature(root.ptr);
  ID3D12DescriptorHeap *heaps[] = {descriptors.ptr};
  list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
  Transition(list.ptr, destination.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  list->Dispatch(1, 1, 1);
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, sizeof(UINT));
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
  Transition(list.ptr, destination.ptr, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
  Check("compute close", list->Close());
  ID3D12CommandList *lists[] = {list.ptr};
  compute->ExecuteCommandLists(1, lists);
  Check("compute signal", compute->Signal(compute_done.ptr, 1));
  Wait(compute_done.ptr, 1);

  UINT *result = nullptr;
  Check("readback map", readback->Map(0, nullptr, reinterpret_cast<void **>(&result)));
  const UINT actual = result[0];
  readback->Unmap(0, nullptr);
  if (actual != Expected) {
    std::cerr << "CopyTileMappings shader expected 0x" << std::hex << Expected
              << ", got 0x" << actual << std::dec << "\n";
    throw std::runtime_error("CopyTileMappings shader result mismatch");
  }
  std::cout << "Reserved buffer CopyTileMappings shader semantics passed\n";
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_reserved_buffer_copy_mapping <shader.cs.cso>\n";
    return 2;
  }
  try {
    return Run(argv[1]);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}

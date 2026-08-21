#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <iostream>

namespace {

bool CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

UINT64 AlignUp(UINT64 value, UINT64 alignment) {
  return (value + alignment - 1) / alignment * alignment;
}

} // namespace

int main() {
  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12Fence *fence = nullptr;
  ID3D12Heap *upload_heap = nullptr;
  ID3D12Heap *readback_heap = nullptr;
  ID3D12Heap *texture_heap = nullptr;
  ID3D12Resource *source_zero = nullptr;
  ID3D12Resource *source_placed = nullptr;
  ID3D12Resource *destination = nullptr;
  ID3D12Resource *placed_texture = nullptr;
  HANDLE event = nullptr;

  auto cleanup = [&]() {
    if (event)
      CloseHandle(event);
    if (fence)
      fence->Release();
    if (list)
      list->Release();
    if (destination)
      destination->Release();
    if (placed_texture)
      placed_texture->Release();
    if (source_placed)
      source_placed->Release();
    if (source_zero)
      source_zero->Release();
    if (readback_heap)
      readback_heap->Release();
    if (texture_heap)
      texture_heap->Release();
    if (upload_heap)
      upload_heap->Release();
    if (allocator)
      allocator->Release();
    if (queue)
      queue->Release();
    if (device)
      device->Release();
  };

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
    cleanup();
    return 1;
  }

  D3D12_RESOURCE_DESC buffer_desc = {};
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = 256;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  D3D12_RESOURCE_ALLOCATION_INFO buffer_info = device->GetResourceAllocationInfo(0, 1, &buffer_desc);
  if (!buffer_info.SizeInBytes || buffer_info.SizeInBytes == UINT64_MAX ||
      buffer_info.Alignment < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
    std::cerr << "invalid buffer allocation info: size=" << buffer_info.SizeInBytes
              << " alignment=" << buffer_info.Alignment << "\n";
    cleanup();
    return 1;
  }

  D3D12_RESOURCE_DESC texture_desc = {};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = 64;
  texture_desc.Height = 64;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  D3D12_RESOURCE_ALLOCATION_INFO texture_info = device->GetResourceAllocationInfo(0, 1, &texture_desc);
  if (!texture_info.SizeInBytes || texture_info.SizeInBytes == UINT64_MAX ||
      texture_info.Alignment < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
    std::cerr << "invalid texture allocation info: size=" << texture_info.SizeInBytes
              << " alignment=" << texture_info.Alignment << "\n";
    cleanup();
    return 1;
  }

  texture_desc.SampleDesc.Count = 4;
  D3D12_RESOURCE_ALLOCATION_INFO msaa_info = device->GetResourceAllocationInfo(0, 1, &texture_desc);
  if (!msaa_info.SizeInBytes || msaa_info.SizeInBytes == UINT64_MAX ||
      msaa_info.Alignment < D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT) {
    std::cerr << "invalid MSAA allocation info: size=" << msaa_info.SizeInBytes
              << " alignment=" << msaa_info.Alignment << "\n";
    cleanup();
    return 1;
  }

  D3D12_HEAP_PROPERTIES upload_properties = {};
  upload_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_properties.CreationNodeMask = 1;
  upload_properties.VisibleNodeMask = 1;
  D3D12_HEAP_DESC upload_desc = {};
  upload_desc.SizeInBytes = AlignUp(buffer_info.Alignment + buffer_info.SizeInBytes, buffer_info.Alignment);
  upload_desc.Properties = upload_properties;
  upload_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  if (!CheckHR("CreateUploadHeap", device->CreateHeap(&upload_desc, IID_PPV_ARGS(&upload_heap)))) {
    cleanup();
    return 1;
  }

  D3D12_HEAP_PROPERTIES readback_properties = {};
  readback_properties.Type = D3D12_HEAP_TYPE_READBACK;
  readback_properties.CreationNodeMask = 1;
  readback_properties.VisibleNodeMask = 1;
  D3D12_HEAP_DESC readback_desc = {};
  readback_desc.SizeInBytes = AlignUp(buffer_info.SizeInBytes, buffer_info.Alignment);
  readback_desc.Properties = readback_properties;
  readback_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  if (!CheckHR("CreateReadbackHeap", device->CreateHeap(&readback_desc, IID_PPV_ARGS(&readback_heap)))) {
    cleanup();
    return 1;
  }

  texture_desc.SampleDesc.Count = 1;
  D3D12_HEAP_DESC texture_heap_desc = {};
  texture_heap_desc.SizeInBytes = AlignUp(texture_info.SizeInBytes, texture_info.Alignment);
  texture_heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  texture_heap_desc.Properties.CreationNodeMask = 1;
  texture_heap_desc.Properties.VisibleNodeMask = 1;
  texture_heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  if (!CheckHR("CreateTextureHeap", device->CreateHeap(&texture_heap_desc, IID_PPV_ARGS(&texture_heap)))) {
    cleanup();
    return 1;
  }
  if (!CheckHR("CreatePlacedTexture",
               device->CreatePlacedResource(texture_heap, 0, &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&placed_texture)))) {
    cleanup();
    return 1;
  }
  D3D12_HEAP_PROPERTIES texture_properties = {};
  D3D12_HEAP_FLAGS texture_flags = D3D12_HEAP_FLAG_NONE;
  if (!CheckHR("GetPlacedTextureHeapProperties",
               placed_texture->GetHeapProperties(&texture_properties, &texture_flags)) ||
      texture_properties.Type != D3D12_HEAP_TYPE_DEFAULT ||
      texture_flags != D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES) {
    std::cerr << "placed texture heap properties mismatch\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("CreatePlacedSourceZero",
               device->CreatePlacedResource(upload_heap, 0, &buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&source_zero))) ||
      !CheckHR("CreatePlacedSource",
               device->CreatePlacedResource(upload_heap, buffer_info.Alignment, &buffer_desc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&source_placed))) ||
      !CheckHR("CreatePlacedDestination",
               device->CreatePlacedResource(readback_heap, 0, &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&destination)))) {
    std::cerr << "allocation size=" << buffer_info.SizeInBytes << " alignment=" << buffer_info.Alignment
              << " upload heap size=" << upload_desc.SizeInBytes << " readback heap size=" << readback_desc.SizeInBytes
              << "\n";
    cleanup();
    return 1;
  }

#ifdef _WIN64
  if (source_placed->GetGPUVirtualAddress() - source_zero->GetGPUVirtualAddress() != buffer_info.Alignment) {
    std::cerr << "placed resource GPU address does not include heap offset\n";
    cleanup();
    return 1;
  }
#else
  if (!source_zero->GetGPUVirtualAddress() || !source_placed->GetGPUVirtualAddress() ||
      source_zero->GetGPUVirtualAddress() == source_placed->GetGPUVirtualAddress()) {
    std::cerr << "x86 placed resource GPU addresses are invalid\n";
    cleanup();
    return 1;
  }
#endif

  D3D12_HEAP_PROPERTIES queried_properties = {};
  D3D12_HEAP_FLAGS queried_flags = D3D12_HEAP_FLAG_NONE;
  if (!CheckHR("GetHeapProperties", source_placed->GetHeapProperties(&queried_properties, &queried_flags)) ||
      queried_properties.Type != D3D12_HEAP_TYPE_UPLOAD || queried_flags != D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS) {
    std::cerr << "placed resource heap properties mismatch\n";
    cleanup();
    return 1;
  }

  UINT32 value = 0x12345678;
  void *mapped = nullptr;
  if (!CheckHR("MapPlacedSource", source_placed->Map(0, nullptr, &mapped))) {
    cleanup();
    return 1;
  }
  if (!mapped) {
    std::cerr << "MapPlacedSource returned a null pointer\n";
    cleanup();
    return 1;
  }
  *static_cast<UINT32 *>(mapped) = value;
  source_placed->Unmap(0, nullptr);

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !CheckHR("CreateCommandAllocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
      !CheckHR("CreateCommandList", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                                                IID_PPV_ARGS(&list)))) {
    cleanup();
    return 1;
  }
  list->CopyBufferRegion(destination, 0, source_placed, 0, sizeof(value));
  if (!CheckHR("Close", list->Close())) {
    cleanup();
    return 1;
  }

  ID3D12CommandList *lists[] = {list};
  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
      !CheckHR("Signal", queue->Signal(fence, 1))) {
    cleanup();
    return 1;
  }
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event || !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event))) {
    cleanup();
    return 1;
  }
  WaitForSingleObject(event, INFINITE);

  if (!CheckHR("MapPlacedDestination", destination->Map(0, nullptr, &mapped))) {
    cleanup();
    return 1;
  }
  if (!mapped) {
    std::cerr << "MapPlacedDestination returned a null pointer\n";
    cleanup();
    return 1;
  }
  auto result = *static_cast<UINT32 *>(mapped);
  destination->Unmap(0, nullptr);
  cleanup();
  if (result != value) {
    std::cerr << "placed resource copy mismatch: 0x" << std::hex << result << "\n";
    return 1;
  }

  std::cout << "D3D12 allocation and placed resource tests passed\n";
  return 0;
}

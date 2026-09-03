#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

bool CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

template <typename T>
bool CheckFeature(ID3D12Device *device, D3D12_FEATURE feature, T *data, const char *name) {
  return CheckHR(name, device->CheckFeatureSupport(feature, data, sizeof(*data)));
}

UINT64 AlignUp(UINT64 value, UINT64 alignment) {
  return (value + alignment - 1) / alignment * alignment;
}

} // namespace

int main() {
  ID3D12Device *device = nullptr;
  ID3D12InfoQueue *info_queue = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12Fence *fence = nullptr;
  ID3D12Fence *multiple_fence = nullptr;
  ID3D12Heap *upload_heap = nullptr;
  ID3D12Heap *readback_heap = nullptr;
  ID3D12Heap *unaligned_heap = nullptr;
  ID3D12Heap *texture_heap = nullptr;
  ID3D12Heap *rt_texture_heap = nullptr;
  ID3D12Heap *alias_heap = nullptr;
  ID3D12Heap *reserved_heap = nullptr;
  ID3D12Heap *reserved_texture_heap = nullptr;
  ID3D12Heap *reserved_rt_texture_heap = nullptr;
  ID3D12Resource *source_zero = nullptr;
  ID3D12Resource *source_placed = nullptr;
  ID3D12Resource *invalid_committed = nullptr;
  ID3D12Resource *invalid_placed = nullptr;
  ID3D12Resource *destination = nullptr;
  ID3D12Resource *alias_before = nullptr;
  ID3D12Resource *alias_after = nullptr;
  ID3D12Resource *placed_texture = nullptr;
  ID3D12Resource *rt_placed_texture = nullptr;
  ID3D12Resource *bc_texture = nullptr;
  ID3D12Resource *bc_upload = nullptr;
  ID3D12Resource *bc_readback = nullptr;
  ID3D12Resource *array_render_target = nullptr;
  ID3D12Resource *array_readback = nullptr;
  ID3D12Resource *array_partial_readback = nullptr;
  ID3D12Resource *array_upload = nullptr;
  ID3D12Resource *texture3d = nullptr;
  ID3D12Resource *texture3d_readback = nullptr;
  ID3D12Resource *depth_texture = nullptr;
  ID3D12Resource *depth_copy = nullptr;
  ID3D12Resource *depth_readback = nullptr;
  ID3D12DescriptorHeap *dsv_heap = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  ID3D12DescriptorHeap *shader_heap = nullptr;
  ID3D12CommandSignature *command_signature = nullptr;
  ID3D12CommandSignature *invalid_command_signature = nullptr;
  ID3D12Device1 *device1 = nullptr;
  ID3D12Device4 *device4 = nullptr;
  ID3D12Device10 *device10 = nullptr;
  ID3D12Resource *committed1_resource = nullptr;
  ID3D12Resource *reserved_resource = nullptr;
  ID3D12Resource *reserved_resource_copy = nullptr;
  ID3D12Resource *reserved_texture = nullptr;
  ID3D12Resource *reserved_texture_copy = nullptr;
  ID3D12Resource *reserved_texture_v2 = nullptr;
  ID3D12Resource *reserved_texture_mips = nullptr;
  ID3D12Resource *reserved_texture_packed_mips = nullptr;
  ID3D12Resource *reserved_rt_texture = nullptr;
  ID3D12Resource *copy_tiles_upload = nullptr;
  ID3D12Resource *copy_tiles_readback = nullptr;
  ID3D12Resource *copy_tiles_mips_readback = nullptr;
  ID3D12Resource *copy_tiles_remap_readback = nullptr;
  ID3D12Resource *copy_tiles_texture_upload = nullptr;
  ID3D12Resource *copy_tiles_texture_readback = nullptr;
  ID3D12Resource *copy_tiles_rt_texture_readback = nullptr;
  ID3D12GraphicsCommandList2 *list2 = nullptr;
  HANDLE event = nullptr;
  HANDLE multiple_event = nullptr;
  D3D12_RESOURCE_BARRIER aliasing_barrier = {};
  D3D12_RESOURCE_BARRIER depth_barrier = {};

  auto cleanup = [&]() {
    if (event)
      CloseHandle(event);
    if (multiple_event)
      CloseHandle(multiple_event);
    if (fence)
      fence->Release();
    if (multiple_fence)
      multiple_fence->Release();
    if (list2)
      list2->Release();
    if (committed1_resource)
      committed1_resource->Release();
    if (reserved_resource_copy)
      reserved_resource_copy->Release();
    if (reserved_resource)
      reserved_resource->Release();
    if (reserved_texture_copy)
      reserved_texture_copy->Release();
    if (reserved_texture)
      reserved_texture->Release();
    if (reserved_texture_v2)
      reserved_texture_v2->Release();
    if (reserved_texture_mips)
      reserved_texture_mips->Release();
    if (reserved_texture_packed_mips)
      reserved_texture_packed_mips->Release();
    if (reserved_rt_texture)
      reserved_rt_texture->Release();
    if (copy_tiles_readback)
      copy_tiles_readback->Release();
    if (copy_tiles_upload)
      copy_tiles_upload->Release();
    if (copy_tiles_mips_readback)
      copy_tiles_mips_readback->Release();
    if (copy_tiles_remap_readback)
      copy_tiles_remap_readback->Release();
    if (copy_tiles_texture_readback)
      copy_tiles_texture_readback->Release();
    if (copy_tiles_texture_upload)
      copy_tiles_texture_upload->Release();
    if (copy_tiles_rt_texture_readback)
      copy_tiles_rt_texture_readback->Release();
    if (list)
      list->Release();
    if (destination)
      destination->Release();
    if (alias_after)
      alias_after->Release();
    if (alias_before)
      alias_before->Release();
    if (placed_texture)
      placed_texture->Release();
    if (rt_placed_texture)
      rt_placed_texture->Release();
    if (bc_readback)
      bc_readback->Release();
    if (bc_upload)
      bc_upload->Release();
    if (bc_texture)
      bc_texture->Release();
    if (array_readback)
      array_readback->Release();
    if (array_partial_readback)
      array_partial_readback->Release();
    if (array_upload)
      array_upload->Release();
    if (array_render_target)
      array_render_target->Release();
    if (texture3d_readback)
      texture3d_readback->Release();
    if (texture3d)
      texture3d->Release();
    if (depth_readback)
      depth_readback->Release();
    if (depth_copy)
      depth_copy->Release();
    if (depth_texture)
      depth_texture->Release();
    if (dsv_heap)
      dsv_heap->Release();
    if (rtv_heap)
      rtv_heap->Release();
    if (shader_heap)
      shader_heap->Release();
    if (command_signature)
      command_signature->Release();
    if (invalid_command_signature)
      invalid_command_signature->Release();
    if (device4)
      device4->Release();
    if (device10)
      device10->Release();
    if (device1)
      device1->Release();
    if (info_queue)
      info_queue->Release();
    if (source_placed)
      source_placed->Release();
    if (invalid_committed)
      invalid_committed->Release();
    if (invalid_placed)
      invalid_placed->Release();
    if (source_zero)
      source_zero->Release();
    if (readback_heap)
      readback_heap->Release();
    if (unaligned_heap)
      unaligned_heap->Release();
    if (texture_heap)
      texture_heap->Release();
    if (rt_texture_heap)
      rt_texture_heap->Release();
    if (alias_heap)
      alias_heap->Release();
    if (reserved_heap)
      reserved_heap->Release();
    if (reserved_texture_heap)
      reserved_texture_heap->Release();
    if (reserved_rt_texture_heap)
      reserved_rt_texture_heap->Release();
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
  if (D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr) != E_INVALIDARG ||
      D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr) != E_INVALIDARG) {
    std::cerr << "unsupported D3D12 feature level was accepted\n";
    cleanup();
    return 1;
  }
  if (!CheckHR("QueryDevice1", device->QueryInterface(IID_PPV_ARGS(&device1)))) {
    cleanup();
    return 1;
  }
  if (!CheckHR("QueryDevice4", device->QueryInterface(IID_PPV_ARGS(&device4)))) {
    cleanup();
    return 1;
  }
  if (!CheckHR("QueryDevice10", device->QueryInterface(IID_PPV_ARGS(&device10)))) {
    cleanup();
    return 1;
  }
  if (!CheckHR("QueryInfoQueue", device->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
    cleanup();
    return 1;
  }
  info_queue->SetMuteDebugOutput(TRUE);

  auto check_unsupported_output = [](const char *name, HRESULT hr, void *output) {
    if (hr == E_NOTIMPL && output == nullptr)
      return true;
    std::cerr << name << " did not return E_NOTIMPL with a null output\n";
    return false;
  };
  void *unsupported_output = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
  HRESULT unsupported_hr = device4->OpenExistingHeapFromAddress(
      nullptr, __uuidof(ID3D12Heap), &unsupported_output);
  if (!check_unsupported_output("OpenExistingHeapFromAddress", unsupported_hr,
                                unsupported_output)) {
    cleanup();
    return 1;
  }
  unsupported_output = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
  unsupported_hr = device4->OpenExistingHeapFromFileMapping(
      nullptr, __uuidof(ID3D12Heap), &unsupported_output);
  if (!check_unsupported_output("OpenExistingHeapFromFileMapping",
                                unsupported_hr, unsupported_output)) {
    cleanup();
    return 1;
  }
  unsupported_output = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
  unsupported_hr = device4->CreateCommandList1(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE,
      __uuidof(ID3D12GraphicsCommandList), &unsupported_output);
  if (unsupported_hr != S_OK || unsupported_output == nullptr) {
    std::cerr << "CreateCommandList1 failed: 0x" << std::hex
              << static_cast<unsigned long>(unsupported_hr) << std::dec << "\n";
    cleanup();
    return 1;
  }
  auto *command_list1 = reinterpret_cast<ID3D12GraphicsCommandList *>(unsupported_output);
  if (command_list1->Close() != E_FAIL) {
    std::cerr << "CreateCommandList1 did not return a closed command list\n";
    command_list1->Release();
    cleanup();
    return 1;
  }
  command_list1->Release();
  unsupported_output = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
  unsupported_hr = device4->CreateProtectedResourceSession(
      nullptr, __uuidof(ID3D12ProtectedResourceSession), &unsupported_output);
  if (!check_unsupported_output("CreateProtectedResourceSession",
                                unsupported_hr, unsupported_output)) {
    cleanup();
    return 1;
  }
  unsupported_output = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
  unsupported_hr = device4->CreateHeap1(nullptr, nullptr, __uuidof(ID3D12Heap),
                                         &unsupported_output);
  if (unsupported_hr != E_INVALIDARG || unsupported_output != nullptr) {
    std::cerr << "CreateHeap1 mishandled a null descriptor: 0x" << std::hex
              << static_cast<unsigned long>(unsupported_hr) << std::dec << "\n";
    cleanup();
    return 1;
  }
  HANDLE unsupported_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));
  unsupported_hr = device->CreateSharedHandle(nullptr, nullptr, 0, nullptr,
                                              &unsupported_handle);
  if (!check_unsupported_output("CreateSharedHandle", unsupported_hr,
                                unsupported_handle)) {
    cleanup();
    return 1;
  }
  unsupported_output = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
  unsupported_hr = device->OpenSharedHandle(nullptr, __uuidof(ID3D12Heap),
                                            &unsupported_output);
  if (!check_unsupported_output("OpenSharedHandle", unsupported_hr,
                                unsupported_output)) {
    cleanup();
    return 1;
  }
  unsupported_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));
  unsupported_hr =
      device->OpenSharedHandleByName(nullptr, 0, &unsupported_handle);
  if (!check_unsupported_output("OpenSharedHandleByName", unsupported_hr,
                                unsupported_handle)) {
    cleanup();
    return 1;
  }

  D3D12_HEAP_PROPERTIES committed1_properties = {};
  committed1_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  committed1_properties.CreationNodeMask = 1;
  committed1_properties.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC committed1_desc = {};
  committed1_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  committed1_desc.Width = 16ull * 1024 * 1024;
  committed1_desc.Height = 1;
  committed1_desc.DepthOrArraySize = 1;
  committed1_desc.MipLevels = 1;
  committed1_desc.SampleDesc.Count = 1;
  committed1_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!CheckHR(
          "CreateCommittedResource1",
          device4->CreateCommittedResource1(
              &committed1_properties, D3D12_HEAP_FLAG_NONE, &committed1_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
              nullptr, IID_PPV_ARGS(&committed1_resource)
          )
      )) {
    cleanup();
    return 1;
  }
  if (committed1_resource->GetDesc(nullptr) != nullptr) {
    std::cerr << "buffer GetDesc accepted a null output\n";
    cleanup();
    return 1;
  }

  auto expect_invalid_buffer_desc = [&](const char *name, const D3D12_RESOURCE_DESC &desc) {
    invalid_committed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
    const HRESULT hr = device->CreateCommittedResource(
        &committed1_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&invalid_committed)
    );
    if (hr != E_INVALIDARG || invalid_committed != nullptr) {
      std::cerr << name << " was accepted: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
      cleanup();
      return false;
    }
    return true;
  };
  D3D12_RESOURCE_DESC invalid_buffer_desc = committed1_desc;
  invalid_buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  if (!expect_invalid_buffer_desc("buffer with non-row-major layout", invalid_buffer_desc))
    return 1;
  invalid_buffer_desc = committed1_desc;
  invalid_buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_UNKNOWN;
  if (!expect_invalid_buffer_desc("resource with unknown dimension", invalid_buffer_desc))
    return 1;
  invalid_buffer_desc = committed1_desc;
  invalid_buffer_desc.SampleDesc.Quality = 1;
  if (!expect_invalid_buffer_desc("buffer with non-zero sample quality", invalid_buffer_desc))
    return 1;
  invalid_buffer_desc = committed1_desc;
  invalid_buffer_desc.Flags = static_cast<D3D12_RESOURCE_FLAGS>(0x40u);
  if (!expect_invalid_buffer_desc("buffer with unknown resource flag", invalid_buffer_desc))
    return 1;
  invalid_buffer_desc = committed1_desc;
  invalid_buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (!expect_invalid_buffer_desc("buffer with render-target flag", invalid_buffer_desc))
    return 1;
  invalid_buffer_desc = committed1_desc;
  invalid_buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  if (!expect_invalid_buffer_desc("buffer with depth-stencil flag", invalid_buffer_desc))
    return 1;
  invalid_buffer_desc = committed1_desc;
  invalid_buffer_desc.Flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  if (!expect_invalid_buffer_desc("buffer with deny-shader-resource flag", invalid_buffer_desc))
    return 1;
  invalid_buffer_desc = committed1_desc;
  invalid_buffer_desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
  if (!expect_invalid_buffer_desc("buffer with small-resource alignment", invalid_buffer_desc))
    return 1;
  D3D12_RESOURCE_ALLOCATION_INFO1 invalid_buffer_info1 = {1, 1, 1};
  D3D12_RESOURCE_ALLOCATION_INFO invalid_buffer_allocation_info = device4->GetResourceAllocationInfo1(
      0, 1, &invalid_buffer_desc, &invalid_buffer_info1
  );
  if (invalid_buffer_allocation_info.SizeInBytes != UINT64_MAX || invalid_buffer_info1.Offset ||
      invalid_buffer_info1.SizeInBytes || invalid_buffer_info1.Alignment) {
    std::cerr << "invalid allocation info leaked per-resource output\n";
    cleanup();
    return 1;
  }
  invalid_buffer_desc = committed1_desc;
  invalid_buffer_desc.Flags = static_cast<D3D12_RESOURCE_FLAGS>(0x40u);
  invalid_buffer_info1 = {1, 1, 1};
  invalid_buffer_allocation_info = device4->GetResourceAllocationInfo1(0, 1, &invalid_buffer_desc, &invalid_buffer_info1);
  if (invalid_buffer_allocation_info.SizeInBytes != UINT64_MAX || invalid_buffer_info1.Offset ||
      invalid_buffer_info1.SizeInBytes || invalid_buffer_info1.Alignment) {
    std::cerr << "unknown buffer flag produced allocation info\n";
    cleanup();
    return 1;
  }

  if (!CheckHR(
          "CreateReservedResource",
          device->CreateReservedResource(
              &committed1_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
              reinterpret_cast<void **>(&reserved_resource)
          )
      ) ||
      !reserved_resource) {
    std::cerr << "CreateReservedResource did not create a reserved buffer\n";
    cleanup();
    return 1;
  }
  if (!CheckHR(
          "CreateReservedResource1",
          device4->CreateReservedResource1(
              &committed1_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr, __uuidof(ID3D12Resource),
              reinterpret_cast<void **>(&reserved_resource_copy)
          )
      ) ||
      !reserved_resource_copy) {
    std::cerr << "CreateReservedResource1 did not create a reserved buffer\n";
    cleanup();
    return 1;
  }
  D3D12_RESOURCE_DESC cross_reserved_desc = committed1_desc;
  cross_reserved_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
  void *cross_reserved_output = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
  if (device->CreateReservedResource(
          &cross_reserved_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
          &cross_reserved_output
      ) != E_INVALIDARG ||
      cross_reserved_output != nullptr) {
    std::cerr << "reserved cross-adapter resource was accepted\n";
    cleanup();
    return 1;
  }
  D3D12_RESOURCE_DESC reserved_desc = {};
  reserved_resource->GetDesc(&reserved_desc);
  D3D12_HEAP_PROPERTIES reserved_heap_properties = {};
  if (reserved_resource->GetHeapProperties(&reserved_heap_properties, nullptr) != E_INVALIDARG ||
      reserved_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || reserved_desc.Width != committed1_desc.Width) {
    std::cerr << "reserved buffer resource contract mismatch\n";
    cleanup();
    return 1;
  }

  D3D12_HEAP_DESC reserved_heap_desc = {};
  reserved_heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  reserved_heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  reserved_heap_desc.Properties.CreationNodeMask = 1;
  reserved_heap_desc.Properties.VisibleNodeMask = 1;
  reserved_heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  reserved_heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  if (!CheckHR("CreateReservedHeap", device->CreateHeap(&reserved_heap_desc, IID_PPV_ARGS(&reserved_heap)))) {
    cleanup();
    return 1;
  }

  UINT total_tile_count = ~static_cast<UINT>(0);
  D3D12_PACKED_MIP_INFO packed_mip_info = {
      static_cast<UINT8>(~static_cast<UINT>(0)), static_cast<UINT8>(~static_cast<UINT>(0)), ~static_cast<UINT>(0),
      ~static_cast<UINT>(0)};
  D3D12_TILE_SHAPE standard_tile_shape = {~static_cast<UINT>(0), ~static_cast<UINT>(0), ~static_cast<UINT>(0)};
  UINT subresource_tiling_count = 1;
  D3D12_SUBRESOURCE_TILING subresource_tiling = {};
  device->GetResourceTiling(
      committed1_resource, &total_tile_count, &packed_mip_info, &standard_tile_shape, &subresource_tiling_count, 0,
      &subresource_tiling
  );
  if (total_tile_count || packed_mip_info.NumStandardMips || packed_mip_info.NumPackedMips ||
      packed_mip_info.NumTilesForPackedMips || packed_mip_info.StartTileIndexInOverallResource ||
      standard_tile_shape.WidthInTexels || standard_tile_shape.HeightInTexels || standard_tile_shape.DepthInTexels ||
      subresource_tiling_count) {
    std::cerr << "unsupported GetResourceTiling did not clear its outputs\n";
    cleanup();
    return 1;
  }

  UINT reserved_total_tile_count = 0;
  D3D12_PACKED_MIP_INFO reserved_packed_mip_info = {};
  D3D12_TILE_SHAPE reserved_tile_shape = {};
  UINT reserved_subresource_tiling_count = 1;
  D3D12_SUBRESOURCE_TILING reserved_subresource_tiling = {};
  device->GetResourceTiling(
      reserved_resource, &reserved_total_tile_count, &reserved_packed_mip_info, &reserved_tile_shape,
      &reserved_subresource_tiling_count, 0, &reserved_subresource_tiling
  );
  const UINT expected_reserved_tile_count =
      static_cast<UINT>((committed1_desc.Width - 1) / D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT + 1);
  if (reserved_total_tile_count != expected_reserved_tile_count || reserved_subresource_tiling_count != 1 ||
      reserved_subresource_tiling.WidthInTiles != expected_reserved_tile_count ||
      reserved_subresource_tiling.HeightInTiles != 1 || reserved_subresource_tiling.DepthInTiles != 1 ||
      reserved_subresource_tiling.StartTileIndexInOverallResource != 0) {
    std::cerr << "reserved buffer tiling contract mismatch\n";
    cleanup();
    return 1;
  }

  D3D12_RESOURCE_DESC reserved_texture_desc = {};
  reserved_texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  reserved_texture_desc.Width = 192;
  reserved_texture_desc.Height = 128;
  reserved_texture_desc.DepthOrArraySize = 2;
  reserved_texture_desc.MipLevels = 1;
  reserved_texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  reserved_texture_desc.SampleDesc.Count = 1;
  reserved_texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  if (!CheckHR(
          "CreateReservedTexture",
          device->CreateReservedResource(
              &reserved_texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
              reinterpret_cast<void **>(&reserved_texture)
          )
      ) ||
      !reserved_texture ||
      !CheckHR(
          "CreateReservedTexture1",
          device4->CreateReservedResource1(
              &reserved_texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr, __uuidof(ID3D12Resource),
              reinterpret_cast<void **>(&reserved_texture_copy)
          )
      ) ||
      !reserved_texture_copy ||
      !CheckHR(
          "CreateReservedTexture2",
          device10->CreateReservedResource2(
              &reserved_texture_desc, D3D12_BARRIER_LAYOUT_COMMON, nullptr, nullptr, 0, nullptr,
              __uuidof(ID3D12Resource), reinterpret_cast<void **>(&reserved_texture_v2)
          )
      ) ||
      !reserved_texture_v2) {
    std::cerr << "reserved texture creation failed\n";
    cleanup();
    return 1;
  }

  D3D12_RESOURCE_DESC reserved_texture_result = {};
  reserved_texture->GetDesc(&reserved_texture_result);
  if (reserved_texture->GetHeapProperties(nullptr, nullptr) != E_INVALIDARG ||
      reserved_texture->GetGPUVirtualAddress() != 0 || reserved_texture_result.Dimension != reserved_texture_desc.Dimension ||
      reserved_texture_result.Width != reserved_texture_desc.Width ||
      reserved_texture_result.Height != reserved_texture_desc.Height ||
      reserved_texture_result.DepthOrArraySize != reserved_texture_desc.DepthOrArraySize) {
    std::cerr << "reserved texture resource contract mismatch\n";
    cleanup();
    return 1;
  }

  D3D12_HEAP_DESC reserved_texture_heap_desc = {};
  reserved_texture_heap_desc.SizeInBytes = 4ull * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  reserved_texture_heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  reserved_texture_heap_desc.Properties.CreationNodeMask = 1;
  reserved_texture_heap_desc.Properties.VisibleNodeMask = 1;
  reserved_texture_heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  reserved_texture_heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  if (!CheckHR(
          "CreateReservedTextureHeap",
          device->CreateHeap(&reserved_texture_heap_desc, IID_PPV_ARGS(&reserved_texture_heap))
      )) {
    cleanup();
    return 1;
  }

  invalid_committed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
  if (device->CreateCommittedResource(
          &reserved_texture_heap_desc.Properties, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, &committed1_desc,
          D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&invalid_committed)
      ) != E_INVALIDARG ||
      invalid_committed != nullptr) {
    std::cerr << "buffer was accepted by an RT/DS-only committed heap\n";
    cleanup();
    return 1;
  }
  invalid_committed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
  if (device->CreateCommittedResource(
          &reserved_texture_heap_desc.Properties, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, &reserved_texture_desc,
          D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&invalid_committed)
      ) != E_INVALIDARG ||
      invalid_committed != nullptr) {
    std::cerr << "texture was accepted by a buffer-only committed heap\n";
    cleanup();
    return 1;
  }

  D3D12_RESOURCE_DESC reserved_rt_texture_desc = reserved_texture_desc;
  reserved_rt_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_CLEAR_VALUE reserved_rt_texture_clear = {};
  reserved_rt_texture_clear.Format = reserved_rt_texture_desc.Format;
  reserved_rt_texture_clear.Color[3] = 1.0f;
  if (!CheckHR(
          "CreateReservedRenderTargetTexture",
          device->CreateReservedResource(
              &reserved_rt_texture_desc, D3D12_RESOURCE_STATE_COMMON, &reserved_rt_texture_clear,
              __uuidof(ID3D12Resource), reinterpret_cast<void **>(&reserved_rt_texture)
          )
      ) ||
      !reserved_rt_texture) {
    std::cerr << "CreateReservedRenderTargetTexture did not create a reserved texture\n";
    cleanup();
    return 1;
  }

  D3D12_HEAP_DESC reserved_rt_texture_heap_desc = reserved_texture_heap_desc;
  reserved_rt_texture_heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  reserved_rt_texture_heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
  if (!CheckHR(
          "CreateReservedRenderTargetTextureHeap",
          device->CreateHeap(&reserved_rt_texture_heap_desc, IID_PPV_ARGS(&reserved_rt_texture_heap))
      )) {
    cleanup();
    return 1;
  }

  D3D12_RESOURCE_DESC reserved_texture_packed_mip_desc = reserved_texture_desc;
  reserved_texture_packed_mip_desc.Width = 192;
  reserved_texture_packed_mip_desc.MipLevels = 2;
  if (!CheckHR(
          "CreateReservedTexturePackedMips",
          device->CreateReservedResource(
              &reserved_texture_packed_mip_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
              reinterpret_cast<void **>(&reserved_texture_packed_mips)
          )
      ) ||
      !reserved_texture_packed_mips) {
    std::cerr << "packed reserved texture creation failed\n";
    cleanup();
    return 1;
  }

  constexpr UINT packed_tile = ~static_cast<UINT>(0);
  UINT reserved_texture_packed_total_tile_count = 0;
  D3D12_PACKED_MIP_INFO reserved_texture_packed_info = {};
  D3D12_TILE_SHAPE reserved_texture_packed_shape = {};
  UINT reserved_texture_packed_subresource_count = 4;
  D3D12_SUBRESOURCE_TILING reserved_texture_packed_tilings[4] = {};
  device->GetResourceTiling(
      reserved_texture_packed_mips, &reserved_texture_packed_total_tile_count, &reserved_texture_packed_info,
      &reserved_texture_packed_shape, &reserved_texture_packed_subresource_count, 0,
      reserved_texture_packed_tilings
  );
  if (reserved_texture_packed_total_tile_count != 6 || reserved_texture_packed_info.NumStandardMips != 1 ||
      reserved_texture_packed_info.NumPackedMips != 1 || reserved_texture_packed_info.NumTilesForPackedMips != 1 ||
      reserved_texture_packed_info.StartTileIndexInOverallResource != 2 ||
      reserved_texture_packed_shape.WidthInTexels != 128 || reserved_texture_packed_shape.HeightInTexels != 128 ||
      reserved_texture_packed_shape.DepthInTexels != 1 || reserved_texture_packed_subresource_count != 4 ||
      reserved_texture_packed_tilings[0].WidthInTiles != 2 ||
      reserved_texture_packed_tilings[0].HeightInTiles != 1 ||
      reserved_texture_packed_tilings[0].DepthInTiles != 1 ||
      reserved_texture_packed_tilings[0].StartTileIndexInOverallResource != 0 ||
      reserved_texture_packed_tilings[1].WidthInTiles || reserved_texture_packed_tilings[1].HeightInTiles ||
      reserved_texture_packed_tilings[1].DepthInTiles ||
      reserved_texture_packed_tilings[1].StartTileIndexInOverallResource != packed_tile ||
      reserved_texture_packed_tilings[2].WidthInTiles != 2 ||
      reserved_texture_packed_tilings[2].HeightInTiles != 1 ||
      reserved_texture_packed_tilings[2].DepthInTiles != 1 ||
      reserved_texture_packed_tilings[2].StartTileIndexInOverallResource != 3 ||
      reserved_texture_packed_tilings[3].WidthInTiles || reserved_texture_packed_tilings[3].HeightInTiles ||
      reserved_texture_packed_tilings[3].DepthInTiles ||
      reserved_texture_packed_tilings[3].StartTileIndexInOverallResource != packed_tile) {
    std::cerr << "reserved texture packed mip tiling contract mismatch\n";
    cleanup();
    return 1;
  }

  D3D12_TILED_RESOURCE_COORDINATE packed_mip_coordinate = {};
  packed_mip_coordinate.Subresource = 1;
  D3D12_TILE_REGION_SIZE packed_mip_region = {};
  packed_mip_region.NumTiles = 1;
  UINT packed_mip_heap_tile_offset = 0;
  UINT packed_mip_range_tile_count = 1;

  D3D12_RESOURCE_DESC reserved_texture_mip_desc = reserved_texture_desc;
  reserved_texture_mip_desc.Width = 512;
  reserved_texture_mip_desc.Height = 512;
  reserved_texture_mip_desc.MipLevels = 3;
  if (!CheckHR(
          "CreateReservedTextureStandardMips",
          device->CreateReservedResource(
              &reserved_texture_mip_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
              reinterpret_cast<void **>(&reserved_texture_mips)
          )
      ) ||
      !reserved_texture_mips) {
    cleanup();
    return 1;
  }

  UINT reserved_texture_mip_total_tile_count = 0;
  D3D12_PACKED_MIP_INFO reserved_texture_mip_packed_mip_info = {};
  D3D12_TILE_SHAPE reserved_texture_mip_tile_shape = {};
  UINT reserved_texture_mip_subresource_count = 6;
  D3D12_SUBRESOURCE_TILING reserved_texture_mip_tilings[6] = {};
  device->GetResourceTiling(
      reserved_texture_mips, &reserved_texture_mip_total_tile_count, &reserved_texture_mip_packed_mip_info,
      &reserved_texture_mip_tile_shape, &reserved_texture_mip_subresource_count, 0,
      reserved_texture_mip_tilings
  );
  if (reserved_texture_mip_total_tile_count != 42 || reserved_texture_mip_packed_mip_info.NumStandardMips != 3 ||
      reserved_texture_mip_packed_mip_info.NumPackedMips ||
      reserved_texture_mip_packed_mip_info.NumTilesForPackedMips ||
      reserved_texture_mip_packed_mip_info.StartTileIndexInOverallResource ||
      reserved_texture_mip_tile_shape.WidthInTexels != 128 ||
      reserved_texture_mip_tile_shape.HeightInTexels != 128 || reserved_texture_mip_tile_shape.DepthInTexels != 1 ||
      reserved_texture_mip_subresource_count != 6 || reserved_texture_mip_tilings[0].WidthInTiles != 4 ||
      reserved_texture_mip_tilings[0].HeightInTiles != 4 ||
      reserved_texture_mip_tilings[0].StartTileIndexInOverallResource != 0 ||
      reserved_texture_mip_tilings[1].WidthInTiles != 2 || reserved_texture_mip_tilings[1].HeightInTiles != 2 ||
      reserved_texture_mip_tilings[1].StartTileIndexInOverallResource != 16 ||
      reserved_texture_mip_tilings[2].WidthInTiles != 1 || reserved_texture_mip_tilings[2].HeightInTiles != 1 ||
      reserved_texture_mip_tilings[2].StartTileIndexInOverallResource != 20 ||
      reserved_texture_mip_tilings[3].WidthInTiles != 4 || reserved_texture_mip_tilings[3].HeightInTiles != 4 ||
      reserved_texture_mip_tilings[3].StartTileIndexInOverallResource != 21 ||
      reserved_texture_mip_tilings[4].WidthInTiles != 2 || reserved_texture_mip_tilings[4].HeightInTiles != 2 ||
      reserved_texture_mip_tilings[4].StartTileIndexInOverallResource != 37 ||
      reserved_texture_mip_tilings[5].WidthInTiles != 1 || reserved_texture_mip_tilings[5].HeightInTiles != 1 ||
      reserved_texture_mip_tilings[5].StartTileIndexInOverallResource != 41 ||
      reserved_texture_mip_tilings[0].DepthInTiles != 1 || reserved_texture_mip_tilings[1].DepthInTiles != 1 ||
      reserved_texture_mip_tilings[2].DepthInTiles != 1 || reserved_texture_mip_tilings[3].DepthInTiles != 1 ||
      reserved_texture_mip_tilings[4].DepthInTiles != 1 || reserved_texture_mip_tilings[5].DepthInTiles != 1) {
    std::cerr << "reserved texture standard mip tiling contract mismatch\n";
    cleanup();
    return 1;
  }

  UINT reserved_texture_mip_partial_count = 2;
  D3D12_SUBRESOURCE_TILING reserved_texture_mip_partial_tilings[2] = {};
  device->GetResourceTiling(
      reserved_texture_mips, nullptr, nullptr, nullptr, &reserved_texture_mip_partial_count, 1,
      reserved_texture_mip_partial_tilings
  );
  if (reserved_texture_mip_partial_count != 2 ||
      reserved_texture_mip_partial_tilings[0].StartTileIndexInOverallResource != 16 ||
      reserved_texture_mip_partial_tilings[1].StartTileIndexInOverallResource != 20) {
    std::cerr << "reserved texture partial mip tiling contract mismatch\n";
    cleanup();
    return 1;
  }

  UINT reserved_texture_total_tile_count = 0;
  D3D12_PACKED_MIP_INFO reserved_texture_packed_mip_info = {};
  D3D12_TILE_SHAPE reserved_texture_tile_shape = {};
  UINT reserved_texture_subresource_count = 2;
  D3D12_SUBRESOURCE_TILING reserved_texture_tilings[2] = {};
  device->GetResourceTiling(
      reserved_texture, &reserved_texture_total_tile_count, &reserved_texture_packed_mip_info,
      &reserved_texture_tile_shape, &reserved_texture_subresource_count, 0, reserved_texture_tilings
  );
  if (reserved_texture_total_tile_count != 4 || reserved_texture_packed_mip_info.NumStandardMips != 1 ||
      reserved_texture_packed_mip_info.NumPackedMips || reserved_texture_packed_mip_info.NumTilesForPackedMips ||
      reserved_texture_packed_mip_info.StartTileIndexInOverallResource ||
      reserved_texture_tile_shape.WidthInTexels != 128 || reserved_texture_tile_shape.HeightInTexels != 128 ||
      reserved_texture_tile_shape.DepthInTexels != 1 || reserved_texture_subresource_count != 2 ||
      reserved_texture_tilings[0].WidthInTiles != 2 || reserved_texture_tilings[0].HeightInTiles != 1 ||
      reserved_texture_tilings[0].DepthInTiles != 1 || reserved_texture_tilings[0].StartTileIndexInOverallResource != 0 ||
      reserved_texture_tilings[1].WidthInTiles != 2 || reserved_texture_tilings[1].HeightInTiles != 1 ||
      reserved_texture_tilings[1].DepthInTiles != 1 || reserved_texture_tilings[1].StartTileIndexInOverallResource != 2) {
    std::cerr << "reserved texture tiling contract mismatch\n";
    cleanup();
    return 1;
  }

  D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
  architecture.NodeIndex = 0;
  D3D12_FEATURE_DATA_ARCHITECTURE1 architecture1 = {};
  architecture1.NodeIndex = 0;
  const D3D_FEATURE_LEVEL expected_max_feature_level =
      SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_1, __uuidof(ID3D12Device), nullptr))
          ? D3D_FEATURE_LEVEL_11_1
          : D3D_FEATURE_LEVEL_11_0;
  D3D_FEATURE_LEVEL requested_levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1};
  D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels = {2, requested_levels, {}};
  D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {D3D_SHADER_MODEL_6_0};
  D3D12_FEATURE_DATA_SHADER_MODEL shader_model_5_1 = {D3D_SHADER_MODEL_5_1};
  D3D12_FEATURE_DATA_SHADER_MODEL invalid_shader_model = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS8 options8 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS10 options10 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS11 options11 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS13 options13 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS14 options14 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS15 options15 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS17 options17 = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS18 options18 = {};
  D3D12_FEATURE_DATA_SHADER_CACHE shader_cache = {};
  D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY queue_priority = {};
  D3D12_FEATURE_DATA_EXISTING_HEAPS existing_heaps = {};
  D3D12_FEATURE_DATA_SERIALIZATION serialization = {};
  D3D12_FEATURE_DATA_CROSS_NODE cross_node = {};
  D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT protected_support = {};
  protected_support.NodeIndex = 0;
  D3D12_FEATURE_DATA_DISPLAYABLE displayable = {};
  D3D12_FEATURE_DATA_FORMAT_INFO depth_format_info = {DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 0};
  D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gpu_va = {};
  D3D12_HEAP_PROPERTIES custom_default = {};
  queue_priority.CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue_priority.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

  if (!CheckFeature(device, D3D12_FEATURE_ARCHITECTURE, &architecture, "CheckArchitecture") ||
      !CheckFeature(device, D3D12_FEATURE_ARCHITECTURE1, &architecture1, "CheckArchitecture1") ||
      !CheckFeature(device, D3D12_FEATURE_FEATURE_LEVELS, &feature_levels, "CheckFeatureLevels") ||
      !CheckFeature(device, D3D12_FEATURE_SHADER_MODEL, &shader_model, "CheckShaderModel") ||
      !CheckFeature(device, D3D12_FEATURE_SHADER_MODEL, &shader_model_5_1, "CheckShaderModel5_1") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS, &options, "CheckOptions") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS1, &options1, "CheckOptions1") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS2, &options2, "CheckOptions2") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS3, &options3, "CheckOptions3") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS4, &options4, "CheckOptions4") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS5, &options5, "CheckOptions5") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS6, &options6, "CheckOptions6") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, "CheckOptions7") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS8, &options8, "CheckOptions8") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS9, &options9, "CheckOptions9") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS10, &options10, "CheckOptions10") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS11, &options11, "CheckOptions11") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS12, &options12, "CheckOptions12") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS13, &options13, "CheckOptions13") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS14, &options14, "CheckOptions14") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS15, &options15, "CheckOptions15") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS16, &options16, "CheckOptions16") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS17, &options17, "CheckOptions17") ||
      !CheckFeature(device, D3D12_FEATURE_D3D12_OPTIONS18, &options18, "CheckOptions18") ||
      !CheckFeature(device, D3D12_FEATURE_SHADER_CACHE, &shader_cache, "CheckShaderCache") ||
      !CheckFeature(device, D3D12_FEATURE_COMMAND_QUEUE_PRIORITY, &queue_priority, "CheckQueuePriority") ||
      !CheckFeature(device, D3D12_FEATURE_EXISTING_HEAPS, &existing_heaps, "CheckExistingHeaps") ||
      !CheckFeature(device, D3D12_FEATURE_SERIALIZATION, &serialization, "CheckSerialization") ||
      !CheckFeature(device, D3D12_FEATURE_CROSS_NODE, &cross_node, "CheckCrossNode") ||
      !CheckFeature(device, D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT, &protected_support,
                   "CheckProtectedResourceSupport") ||
      !CheckFeature(device, D3D12_FEATURE_DISPLAYABLE, &displayable, "CheckDisplayable") ||
      !CheckFeature(device, D3D12_FEATURE_FORMAT_INFO, &depth_format_info, "CheckDepthFormatInfo") ||
      !CheckFeature(device, D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &gpu_va, "CheckGPUVA")) {
    cleanup();
    return 1;
  }
  if (!architecture.TileBasedRenderer || !architecture.UMA || !architecture1.TileBasedRenderer ||
       !architecture1.UMA || feature_levels.MaxSupportedFeatureLevel != expected_max_feature_level ||
      shader_model.HighestShaderModel != D3D_SHADER_MODEL_6_0 || options.ResourceBindingTier != D3D12_RESOURCE_BINDING_TIER_2 ||
       shader_model_5_1.HighestShaderModel != D3D_SHADER_MODEL_5_1 ||
       options.TiledResourcesTier != D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED ||
       options.ResourceHeapTier != D3D12_RESOURCE_HEAP_TIER_2 || options.ROVsSupported || options1.WaveOps ||
       options3.CopyQueueTimestampQueriesSupported != TRUE || !options3.CastingFullyTypedFormatSupported ||
       options5.RenderPassesTier != D3D12_RENDER_PASS_TIER_0 ||
      options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED || shader_cache.SupportFlags ||
      !queue_priority.PriorityForTypeIsSupported || existing_heaps.Supported ||
      serialization.HeapSerializationTier != D3D12_HEAP_SERIALIZATION_TIER_0 || cross_node.SharingTier != D3D12_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED ||
      protected_support.Support != D3D12_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_NONE || displayable.DisplayableTexture ||
      depth_format_info.PlaneCount != 2 || gpu_va.MaxGPUVirtualAddressBitsPerResource != 48 ||
      gpu_va.MaxGPUVirtualAddressBitsPerProcess != 48) {
    std::cerr << "unexpected D3D12 device feature support values\n";
    cleanup();
    return 1;
  }
  if (device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, nullptr, 0) != E_INVALIDARG ||
      device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &invalid_shader_model, sizeof(invalid_shader_model)) !=
          E_INVALIDARG ||
      device->GetNodeCount() != 1 || device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) != 32 ||
      !device->GetCustomHeapProperties(&custom_default, 0, D3D12_HEAP_TYPE_DEFAULT) ||
      custom_default.Type != D3D12_HEAP_TYPE_CUSTOM ||
      custom_default.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE) {
    std::cerr << "unexpected D3D12 device contract values\n";
    cleanup();
    return 1;
  }

  D3D12_INDIRECT_ARGUMENT_DESC draw_argument = {};
  draw_argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
  D3D12_COMMAND_SIGNATURE_DESC command_signature_desc = {};
  command_signature_desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
  command_signature_desc.NumArgumentDescs = 1;
  command_signature_desc.pArgumentDescs = &draw_argument;
  if (!CheckHR("CreateCommandSignature",
               device->CreateCommandSignature(
                   &command_signature_desc, nullptr, IID_PPV_ARGS(&command_signature)))) {
    cleanup();
    return 1;
  }
  auto undersized_signature_desc = command_signature_desc;
  undersized_signature_desc.ByteStride -= sizeof(uint32_t);
  if (device->CreateCommandSignature(
          &undersized_signature_desc, nullptr, IID_PPV_ARGS(&invalid_command_signature)
      ) != E_INVALIDARG || invalid_command_signature) {
    std::cerr << "undersized command signature was accepted\n";
    cleanup();
    return 1;
  }
  ID3D12Pageable *priority_objects[] = {command_signature};
  D3D12_RESIDENCY_PRIORITY priority_values[] = {D3D12_RESIDENCY_PRIORITY_NORMAL};
  if (!CheckHR("SetResidencyPriority", device1->SetResidencyPriority(1, priority_objects, priority_values))) {
    cleanup();
    return 1;
  }

  D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support = {};
  format_support.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  if (!CheckHR("CheckFormatSupport", device->CheckFeatureSupport(
                                          D3D12_FEATURE_FORMAT_SUPPORT, &format_support, sizeof(format_support))) ||
      !(format_support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) ||
      !(format_support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) || format_support.Support1 == UINT_MAX ||
      format_support.Support2 == UINT_MAX) {
    std::cerr << "unexpected format support: support1=0x" << std::hex << format_support.Support1
              << " support2=0x" << format_support.Support2 << std::dec << "\n";
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
  D3D12_RESOURCE_ALLOCATION_INFO1 buffer_info1 = {};
  D3D12_RESOURCE_ALLOCATION_INFO buffer_info4 =
      device4->GetResourceAllocationInfo1(0, 1, &buffer_desc, &buffer_info1);
  if (!buffer_info4.SizeInBytes || buffer_info4.SizeInBytes == UINT64_MAX ||
      !buffer_info1.SizeInBytes || buffer_info1.SizeInBytes == UINT64_MAX || buffer_info1.Offset ||
      buffer_info1.Alignment < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
    std::cerr << "invalid Device4 allocation info: size=" << buffer_info4.SizeInBytes
              << " resource_size=" << buffer_info1.SizeInBytes << " offset=" << buffer_info1.Offset
              << " alignment=" << buffer_info1.Alignment << "\n";
    cleanup();
    return 1;
  }
  D3D12_RESOURCE_DESC simultaneous_buffer_desc = buffer_desc;
  simultaneous_buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  D3D12_RESOURCE_ALLOCATION_INFO1 simultaneous_buffer_info1 = {};
  D3D12_RESOURCE_ALLOCATION_INFO simultaneous_buffer_info4 =
      device4->GetResourceAllocationInfo1(0, 1, &simultaneous_buffer_desc, &simultaneous_buffer_info1);
  if (simultaneous_buffer_info4.SizeInBytes != UINT64_MAX || simultaneous_buffer_info1.SizeInBytes ||
      simultaneous_buffer_info1.Offset || simultaneous_buffer_info1.Alignment) {
    std::cerr << "allocation info accepted invalid buffer flags\n";
    cleanup();
    return 1;
  }
  D3D12_RESOURCE_DESC texture_only_buffer_desc = buffer_desc;
  texture_only_buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_RESOURCE_ALLOCATION_INFO1 texture_only_buffer_info1 = {1, 1, 1};
  D3D12_RESOURCE_ALLOCATION_INFO texture_only_buffer_info4 = device4->GetResourceAllocationInfo1(
      0, 1, &texture_only_buffer_desc, &texture_only_buffer_info1
  );
  if (texture_only_buffer_info4.SizeInBytes != UINT64_MAX || texture_only_buffer_info1.SizeInBytes ||
      texture_only_buffer_info1.Offset || texture_only_buffer_info1.Alignment) {
    std::cerr << "allocation info accepted a texture-only buffer flag\n";
    cleanup();
    return 1;
  }

  D3D12_RESOURCE_DESC texture_desc = {};
  D3D12_RESOURCE_DESC depth_desc = {};
  D3D12_RESOURCE_DESC depth_readback_desc = {};
  D3D12_RESOURCE_DESC bc_desc = {};
  D3D12_RESOURCE_DESC bc_buffer_desc = {};
  D3D12_RESOURCE_DESC array_render_target_desc = {};
  D3D12_RESOURCE_DESC array_readback_desc = {};
  D3D12_RESOURCE_DESC array_partial_readback_desc = {};
  D3D12_RESOURCE_DESC array_upload_desc = {};
  D3D12_RESOURCE_DESC texture3d_desc = {};
  D3D12_RESOURCE_DESC texture3d_readback_desc = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT depth_footprints[2] = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT bc_footprint = {};
  UINT depth_rows[2] = {};
  UINT64 depth_row_sizes[2] = {};
  UINT64 depth_total = 0;
  UINT bc_rows = 0;
  UINT64 bc_row_size = 0;
  UINT64 bc_total = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT array_footprint = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT array_partial_footprint = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT array_buffer_copy_dst_footprint = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT array_upload_footprint = {};
  UINT array_rows = 0;
  UINT64 array_row_size = 0;
  UINT64 array_total = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT texture3d_footprint = {};
  UINT texture3d_rows = 0;
  UINT64 texture3d_row_size = 0;
  UINT64 texture3d_total = 0;
  D3D12_CLEAR_VALUE depth_clear = {};
  D3D12_CLEAR_VALUE array_clear = {};
  D3D12_CLEAR_VALUE texture3d_clear = {};
  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
  D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
  D3D12_CPU_DESCRIPTOR_HANDLE null_dsv_handle = {};
  D3D12_CPU_DESCRIPTOR_HANDLE array_rtv_handle = {};
  D3D12_CPU_DESCRIPTOR_HANDLE texture3d_rtv_handle = {};
  D3D12_CPU_DESCRIPTOR_HANDLE texture3d_uav_handle = {};
  BYTE *mapped_depth = nullptr;
  BYTE *mapped_bc_upload = nullptr;
  BYTE *mapped_bc_readback = nullptr;
  BYTE *mapped_array_readback = nullptr;
  BYTE *mapped_array_partial_readback = nullptr;
  BYTE *mapped_array_upload = nullptr;
  BYTE *mapped_texture3d_readback = nullptr;
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = 64;
  texture_desc.Height = 64;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  auto expect_invalid_texture_desc = [&](const char *name, const D3D12_RESOURCE_DESC &desc) {
    invalid_committed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
    const HRESULT hr = device->CreateCommittedResource(
        &committed1_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&invalid_committed)
    );
    if (hr != E_INVALIDARG || invalid_committed != nullptr) {
      std::cerr << name << " was accepted: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
      cleanup();
      return false;
    }
    return true;
  };
  D3D12_RESOURCE_DESC invalid_texture_desc = texture_desc;
  invalid_texture_desc.Width = 0;
  if (!expect_invalid_texture_desc("zero-width texture", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
  invalid_texture_desc.Height = 2;
  if (!expect_invalid_texture_desc("1D texture with non-unit height", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.MipLevels = 8;
  if (!expect_invalid_texture_desc("texture with excessive mip levels", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.SampleDesc.Quality = 1;
  if (!expect_invalid_texture_desc("single-sample texture with quality", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.SampleDesc.Count = 4;
  invalid_texture_desc.MipLevels = 2;
  if (!expect_invalid_texture_desc("MSAA texture with multiple mips", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  invalid_texture_desc.DepthOrArraySize = 2;
  invalid_texture_desc.SampleDesc.Count = 2;
  if (!expect_invalid_texture_desc("3D multisample texture", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.SampleDesc.Count = 4;
  invalid_texture_desc.SampleDesc.Quality = 1;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (!expect_invalid_texture_desc("MSAA texture with non-zero quality", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.SampleDesc.Count = 3;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (!expect_invalid_texture_desc("texture with unsupported sample count", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  if (!expect_invalid_texture_desc("texture with render-target and depth-stencil flags", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!expect_invalid_texture_desc("texture with depth-stencil and UAV flags", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Flags = static_cast<D3D12_RESOURCE_FLAGS>(0x40u);
  if (!expect_invalid_texture_desc("texture with unknown resource flag", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  if (!expect_invalid_texture_desc("texture denying shader access without depth-stencil", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.SampleDesc.Count = 4;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!expect_invalid_texture_desc("MSAA texture with UAV flag", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.SampleDesc.Count = 4;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  if (!expect_invalid_texture_desc("MSAA texture with simultaneous-access flag", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
  if (!expect_invalid_texture_desc("row-major texture with UAV flag", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!expect_invalid_texture_desc("unsupported row-major texture", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  if (!expect_invalid_texture_desc("unsupported undefined-swizzle texture", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE;
  if (!expect_invalid_texture_desc("unsupported standard-swizzle texture", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Format = DXGI_FORMAT_UNKNOWN;
  if (!expect_invalid_texture_desc("texture with unknown format", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Format = DXGI_FORMAT_AYUV;
  if (!expect_invalid_texture_desc("texture with unsupported format", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Alignment = 1;
  if (!expect_invalid_texture_desc("texture with invalid alignment", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Alignment = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
  if (!expect_invalid_texture_desc("single-sample texture with MSAA alignment", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Width = 256;
  invalid_texture_desc.Height = 256;
  invalid_texture_desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
  if (!expect_invalid_texture_desc("oversized texture with small alignment", invalid_texture_desc))
    return 1;
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Width = 1024;
  invalid_texture_desc.Height = 1024;
  invalid_texture_desc.SampleDesc.Count = 4;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  invalid_texture_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  if (!expect_invalid_texture_desc("oversized MSAA texture with default alignment", invalid_texture_desc))
    return 1;

  D3D12_RESOURCE_DESC cross_buffer_desc = buffer_desc;
  cross_buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
  D3D12_RESOURCE_DESC cross_texture_desc = texture_desc;
  cross_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
  auto expect_invalid_cross_adapter_committed = [&](const char *name, const D3D12_RESOURCE_DESC &desc) {
    invalid_committed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
    const HRESULT hr = device->CreateCommittedResource(
        &committed1_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&invalid_committed)
    );
    if (hr != E_INVALIDARG || invalid_committed != nullptr) {
      std::cerr << name << " was accepted: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
      cleanup();
      return false;
    }
    return true;
  };
  if (!expect_invalid_cross_adapter_committed("committed cross-adapter buffer", cross_buffer_desc) ||
      !expect_invalid_cross_adapter_committed("committed cross-adapter texture", cross_texture_desc))
    return 1;

  D3D12_RESOURCE_ALLOCATION_INFO texture_info = device->GetResourceAllocationInfo(0, 1, &texture_desc);
  if (!texture_info.SizeInBytes || texture_info.SizeInBytes == UINT64_MAX ||
      texture_info.Alignment < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
    std::cerr << "invalid texture allocation info: size=" << texture_info.SizeInBytes
              << " alignment=" << texture_info.Alignment << "\n";
    cleanup();
    return 1;
  }
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.MipLevels = 8;
  D3D12_RESOURCE_ALLOCATION_INFO invalid_texture_info =
      device->GetResourceAllocationInfo(0, 1, &invalid_texture_desc);
  if (invalid_texture_info.SizeInBytes != UINT64_MAX) {
    std::cerr << "allocation info accepted excessive texture mip levels\n";
    cleanup();
    return 1;
  }
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE;
  invalid_texture_info = device->GetResourceAllocationInfo(0, 1, &invalid_texture_desc);
  if (invalid_texture_info.SizeInBytes != UINT64_MAX) {
    std::cerr << "allocation info accepted unsupported texture layout\n";
    cleanup();
    return 1;
  }
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Format = DXGI_FORMAT_AYUV;
  invalid_texture_info = device->GetResourceAllocationInfo(0, 1, &invalid_texture_desc);
  if (invalid_texture_info.SizeInBytes != UINT64_MAX) {
    std::cerr << "allocation info accepted unsupported texture format\n";
    cleanup();
    return 1;
  }
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.SampleDesc.Count = 4;
  invalid_texture_desc.SampleDesc.Quality = 1;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  invalid_texture_info = device->GetResourceAllocationInfo(0, 1, &invalid_texture_desc);
  if (invalid_texture_info.SizeInBytes != UINT64_MAX) {
    std::cerr << "allocation info accepted non-zero MSAA quality\n";
    cleanup();
    return 1;
  }
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.SampleDesc.Count = 3;
  invalid_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  invalid_texture_info = device->GetResourceAllocationInfo(0, 1, &invalid_texture_desc);
  if (invalid_texture_info.SizeInBytes != UINT64_MAX) {
    std::cerr << "allocation info accepted unsupported sample count\n";
    cleanup();
    return 1;
  }
  invalid_texture_desc = texture_desc;
  invalid_texture_desc.Flags = static_cast<D3D12_RESOURCE_FLAGS>(0x40u);
  invalid_texture_info = device->GetResourceAllocationInfo(0, 1, &invalid_texture_desc);
  if (invalid_texture_info.SizeInBytes != UINT64_MAX) {
    std::cerr << "allocation info accepted unknown texture flag\n";
    cleanup();
    return 1;
  }
  D3D12_RESOURCE_DESC small_texture_desc = texture_desc;
  small_texture_desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
  D3D12_RESOURCE_ALLOCATION_INFO small_texture_info =
      device->GetResourceAllocationInfo(0, 1, &small_texture_desc);
  if (!small_texture_info.SizeInBytes || small_texture_info.SizeInBytes == UINT64_MAX ||
      small_texture_info.Alignment < D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
    std::cerr << "invalid small texture allocation info: size=" << small_texture_info.SizeInBytes
              << " alignment=" << small_texture_info.Alignment << "\n";
    cleanup();
    return 1;
  }

  D3D12_RESOURCE_DESC footprint_desc = texture_desc;
  footprint_desc.Width = 5;
  footprint_desc.Height = 5;
  footprint_desc.DepthOrArraySize = 2;
  footprint_desc.MipLevels = 3;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_layouts[6] = {};
  UINT footprint_rows[6] = {};
  UINT64 footprint_row_sizes[6] = {};
  UINT64 footprint_total = 0;
  device->GetCopyableFootprints(
      &footprint_desc, 0, 6, 0, footprint_layouts, footprint_rows, footprint_row_sizes, &footprint_total
  );
  if (footprint_total != 4864 || footprint_layouts[1].Offset != 1536 || footprint_layouts[3].Offset != 2560 ||
      footprint_layouts[0].Footprint.Width != 5 || footprint_layouts[0].Footprint.Height != 5 ||
      footprint_rows[0] != 5 || footprint_row_sizes[0] != 20) {
    std::cerr << "unexpected RGBA8 footprint: total=" << footprint_total << " first_offset="
              << footprint_layouts[0].Offset << " second_offset=" << footprint_layouts[1].Offset << " array_offset="
              << footprint_layouts[3].Offset << "\n";
    cleanup();
    return 1;
  }

  auto expect_invalid_footprint_desc = [&](const char *name, const D3D12_RESOURCE_DESC &desc) {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT invalid_layout = {};
    invalid_layout.Offset = 1;
    invalid_layout.Footprint.Format = DXGI_FORMAT_R8_UNORM;
    invalid_layout.Footprint.Width = 1;
    invalid_layout.Footprint.Height = 1;
    invalid_layout.Footprint.Depth = 1;
    invalid_layout.Footprint.RowPitch = 1;
    UINT invalid_rows = 1;
    UINT64 invalid_row_size = 1;
    UINT64 invalid_total = 1;
    device->GetCopyableFootprints(
        &desc, 0, 1, 0, &invalid_layout, &invalid_rows, &invalid_row_size, &invalid_total
    );
    if (invalid_layout.Offset != UINT64_MAX || invalid_layout.Footprint.Format != static_cast<DXGI_FORMAT>(~0u) ||
        invalid_layout.Footprint.Width != UINT_MAX || invalid_layout.Footprint.Height != UINT_MAX ||
        invalid_layout.Footprint.Depth != UINT_MAX || invalid_layout.Footprint.RowPitch != UINT_MAX ||
        invalid_rows != UINT_MAX || invalid_row_size != UINT64_MAX || invalid_total != UINT64_MAX) {
      std::cerr << name << " produced a footprint for an invalid resource description\n";
      cleanup();
      return false;
    }
    return true;
  };
  D3D12_RESOURCE_DESC invalid_footprint_desc = footprint_desc;
  invalid_footprint_desc.MipLevels = 8;
  if (!expect_invalid_footprint_desc("excessive footprint mip levels", invalid_footprint_desc))
    return 1;
  invalid_footprint_desc = footprint_desc;
  invalid_footprint_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!expect_invalid_footprint_desc("row-major texture footprint", invalid_footprint_desc))
    return 1;
  invalid_footprint_desc = footprint_desc;
  invalid_footprint_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  if (!expect_invalid_footprint_desc("conflicting texture footprint flags", invalid_footprint_desc))
    return 1;
  invalid_footprint_desc = buffer_desc;
  invalid_footprint_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  if (!expect_invalid_footprint_desc("non-row-major buffer footprint", invalid_footprint_desc))
    return 1;

  footprint_desc.Width = 5;
  footprint_desc.Height = 5;
  footprint_desc.DepthOrArraySize = 1;
  footprint_desc.MipLevels = 1;
  footprint_desc.Format = DXGI_FORMAT_BC1_UNORM;
  std::memset(footprint_layouts, 0, sizeof(footprint_layouts));
  std::memset(footprint_rows, 0, sizeof(footprint_rows));
  std::memset(footprint_row_sizes, 0, sizeof(footprint_row_sizes));
  footprint_total = 0;
  device->GetCopyableFootprints(
      &footprint_desc, 0, 1, 0, footprint_layouts, footprint_rows, footprint_row_sizes, &footprint_total
  );
  if (footprint_total != 512 || footprint_layouts[0].Footprint.Width != 5 ||
      footprint_layouts[0].Footprint.Height != 5 || footprint_rows[0] != 2 || footprint_row_sizes[0] != 16) {
    std::cerr << "unexpected BC1 footprint: total=" << footprint_total << " rows=" << footprint_rows[0]
              << " row_size=" << footprint_row_sizes[0] << "\n";
    cleanup();
    return 1;
  }

  texture_desc.SampleDesc.Count = 4;
  texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_RESOURCE_ALLOCATION_INFO msaa_info = device->GetResourceAllocationInfo(0, 1, &texture_desc);
  if (!msaa_info.SizeInBytes || msaa_info.SizeInBytes == UINT64_MAX ||
      msaa_info.Alignment < D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT) {
    std::cerr << "invalid MSAA allocation info: size=" << msaa_info.SizeInBytes
              << " alignment=" << msaa_info.Alignment << "\n";
    cleanup();
    return 1;
  }
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

  D3D12_HEAP_PROPERTIES upload_properties = {};
  upload_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_properties.CreationNodeMask = 1;
  upload_properties.VisibleNodeMask = 1;
  D3D12_HEAP_DESC upload_desc = {};
  upload_desc.SizeInBytes = AlignUp(buffer_info.Alignment + buffer_info.SizeInBytes, buffer_info.Alignment);
  upload_desc.Properties = upload_properties;
  upload_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  auto expect_heap_parity = [&](const char *name, const D3D12_HEAP_DESC &desc, HRESULT expected) {
    constexpr uintptr_t sentinel_value = 1;
    void *create_heap_output = reinterpret_cast<void *>(sentinel_value);
    const HRESULT create_heap_hr = device->CreateHeap(&desc, __uuidof(ID3D12Heap), &create_heap_output);
    const bool create_heap_created = create_heap_output && create_heap_output != reinterpret_cast<void *>(sentinel_value);
    if (create_heap_created)
      reinterpret_cast<ID3D12Heap *>(create_heap_output)->Release();

    void *create_heap1_output = reinterpret_cast<void *>(sentinel_value);
    const HRESULT create_heap1_hr =
        device4->CreateHeap1(&desc, nullptr, __uuidof(ID3D12Heap), &create_heap1_output);
    const bool create_heap1_created =
        create_heap1_output && create_heap1_output != reinterpret_cast<void *>(sentinel_value);
    if (create_heap1_created)
      reinterpret_cast<ID3D12Heap *>(create_heap1_output)->Release();

    if (create_heap_hr != expected || create_heap1_hr != create_heap_hr || create_heap_output != nullptr ||
        create_heap1_output != nullptr || create_heap_created || create_heap1_created) {
      std::cerr << name << " CreateHeap/CreateHeap1 parity mismatch: CreateHeap=0x" << std::hex
                << static_cast<unsigned long>(create_heap_hr) << " CreateHeap1=0x"
                << static_cast<unsigned long>(create_heap1_hr) << std::dec << "\n";
      cleanup();
      return false;
    }
    return true;
  };

  D3D12_HEAP_DESC heap_parity_desc = upload_desc;
  heap_parity_desc.Properties.CreationNodeMask = 2;
  if (!expect_heap_parity("invalid creation node mask", heap_parity_desc, E_INVALIDARG))
    return 1;
  heap_parity_desc = upload_desc;
  heap_parity_desc.Properties.VisibleNodeMask = 2;
  if (!expect_heap_parity("invalid visible node mask", heap_parity_desc, E_INVALIDARG))
    return 1;
  heap_parity_desc = upload_desc;
  heap_parity_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_parity_desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
  if (!expect_heap_parity("invalid CPU page property", heap_parity_desc, E_INVALIDARG))
    return 1;
  heap_parity_desc = upload_desc;
  heap_parity_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_parity_desc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L1;
  if (!expect_heap_parity("invalid memory pool", heap_parity_desc, E_INVALIDARG))
    return 1;
  heap_parity_desc = upload_desc;
  heap_parity_desc.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
  heap_parity_desc.Properties.CPUPageProperty = static_cast<D3D12_CPU_PAGE_PROPERTY>(0x4u);
  heap_parity_desc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
  if (!expect_heap_parity("unknown custom CPU page property", heap_parity_desc, E_INVALIDARG))
    return 1;
  heap_parity_desc = upload_desc;
  heap_parity_desc.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
  heap_parity_desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
  heap_parity_desc.Properties.MemoryPoolPreference = static_cast<D3D12_MEMORY_POOL>(0x3u);
  if (!expect_heap_parity("unknown custom memory pool", heap_parity_desc, E_INVALIDARG))
    return 1;
  heap_parity_desc = upload_desc;
  heap_parity_desc.Flags = D3D12_HEAP_FLAG_SHARED;
  if (!expect_heap_parity("unsupported shared heap", heap_parity_desc, E_NOTIMPL))
    return 1;
  heap_parity_desc = upload_desc;
  heap_parity_desc.Flags = D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
  if (!expect_heap_parity("unsupported cross-adapter heap", heap_parity_desc, E_NOTIMPL))
    return 1;
  heap_parity_desc = upload_desc;
  heap_parity_desc.Flags = static_cast<D3D12_HEAP_FLAGS>(0x2u);
  if (!expect_heap_parity("unknown heap flag", heap_parity_desc, E_INVALIDARG))
    return 1;

  if (!CheckHR("CreateUploadHeap", device->CreateHeap(&upload_desc, IID_PPV_ARGS(&upload_heap)))) {
    cleanup();
    return 1;
  }
  invalid_placed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
  if (device->CreatePlacedResource(
          upload_heap, 0, &cross_buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&invalid_placed)
      ) != E_INVALIDARG ||
      invalid_placed != nullptr) {
    std::cerr << "placed cross-adapter buffer was accepted\n";
    cleanup();
    return 1;
  }

  D3D12_HEAP_DESC zero_node_mask_desc = upload_desc;
  zero_node_mask_desc.Properties.CreationNodeMask = 0;
  zero_node_mask_desc.Properties.VisibleNodeMask = 0;
  ID3D12Heap *zero_node_mask_heap = nullptr;
  if (!CheckHR("CreateZeroNodeMaskHeap", device->CreateHeap(&zero_node_mask_desc, IID_PPV_ARGS(&zero_node_mask_heap))) ||
      !zero_node_mask_heap) {
    std::cerr << "zero node masks were not treated as the single device node\n";
    cleanup();
    return 1;
  }
  zero_node_mask_heap->Release();

  D3D12_HEAP_DESC invalid_node_mask_desc = upload_desc;
  void *invalid_heap_output = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
  invalid_node_mask_desc.Properties.CreationNodeMask = 2;
  if (device->CreateHeap(&invalid_node_mask_desc, __uuidof(ID3D12Heap), &invalid_heap_output) != E_INVALIDARG ||
      invalid_heap_output != nullptr) {
    std::cerr << "unsupported creation node mask was accepted\n";
    cleanup();
    return 1;
  }
  invalid_node_mask_desc = upload_desc;
  invalid_node_mask_desc.Properties.VisibleNodeMask = 2;
  invalid_heap_output = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
  if (device->CreateHeap(&invalid_node_mask_desc, __uuidof(ID3D12Heap), &invalid_heap_output) != E_INVALIDARG ||
      invalid_heap_output != nullptr) {
    std::cerr << "unsupported visible node mask was accepted\n";
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

  invalid_buffer_desc = buffer_desc;
  invalid_buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  D3D12_RESOURCE_ALLOCATION_INFO invalid_buffer_info = device->GetResourceAllocationInfo(0, 1, &invalid_buffer_desc);
  if (invalid_buffer_info.SizeInBytes != UINT64_MAX) {
    std::cerr << "allocation info accepted a non-row-major buffer\n";
    cleanup();
    return 1;
  }

  invalid_committed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
  if (device->CreateCommittedResource(
          &upload_properties, D3D12_HEAP_FLAG_NONE, &committed1_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&invalid_committed)
      ) != E_INVALIDARG ||
      invalid_committed != nullptr) {
    std::cerr << "upload resource accepted a non-GENERIC_READ initial state\n";
    cleanup();
    return 1;
  }
  invalid_committed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
  if (device->CreateCommittedResource(
          &readback_properties, D3D12_HEAP_FLAG_NONE, &committed1_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
          IID_PPV_ARGS(&invalid_committed)
      ) != E_INVALIDARG ||
      invalid_committed != nullptr) {
    std::cerr << "readback resource accepted a non-COPY_DEST initial state\n";
    cleanup();
    return 1;
  }
  invalid_committed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
  const auto invalid_resource_state = static_cast<D3D12_RESOURCE_STATES>(0x80000000u);
  if (device->CreateCommittedResource(
          &committed1_properties, D3D12_HEAP_FLAG_NONE, &committed1_desc, invalid_resource_state, nullptr,
          IID_PPV_ARGS(&invalid_committed)
      ) != E_INVALIDARG ||
      invalid_committed != nullptr) {
    std::cerr << "resource creation accepted an unknown initial state bit\n";
    cleanup();
    return 1;
  }

  D3D12_RESOURCE_DESC copy_tiles_buffer_desc = buffer_desc;
  copy_tiles_buffer_desc.Width = 2ull * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  if (!CheckHR(
          "CreateCopyTilesUpload",
          device->CreateCommittedResource(
              &upload_properties, D3D12_HEAP_FLAG_NONE, &copy_tiles_buffer_desc,
              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&copy_tiles_upload)
          )
      ) ||
      !CheckHR(
          "CreateCopyTilesReadback",
          device->CreateCommittedResource(
              &readback_properties, D3D12_HEAP_FLAG_NONE, &copy_tiles_buffer_desc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&copy_tiles_readback)
          )
      ) ||
      !CheckHR(
          "CreateCopyTilesMipsReadback",
          device->CreateCommittedResource(
              &readback_properties, D3D12_HEAP_FLAG_NONE, &copy_tiles_buffer_desc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&copy_tiles_mips_readback)
          )
      ) ||
      !CheckHR(
          "CreateCopyTilesRemapReadback",
          device->CreateCommittedResource(
              &readback_properties, D3D12_HEAP_FLAG_NONE, &copy_tiles_buffer_desc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&copy_tiles_remap_readback)
          )
      )) {
    cleanup();
    return 1;
  }
  BYTE *mapped_copy_tiles_upload = nullptr;
  if (!CheckHR(
          "MapCopyTilesUpload",
          copy_tiles_upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped_copy_tiles_upload))
      )) {
    cleanup();
    return 1;
  }
  for (UINT i = 0; i < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; i++) {
    mapped_copy_tiles_upload[i] = static_cast<BYTE>(i ^ 0x5a);
    mapped_copy_tiles_upload[D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT + i] = 0;
  }
  copy_tiles_upload->Unmap(0, nullptr);

  D3D12_RESOURCE_DESC copy_tiles_texture_buffer_desc = copy_tiles_buffer_desc;
  copy_tiles_texture_buffer_desc.Width = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  if (!CheckHR(
          "CreateCopyTilesTextureUpload",
          device->CreateCommittedResource(
              &upload_properties, D3D12_HEAP_FLAG_NONE, &copy_tiles_texture_buffer_desc,
              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&copy_tiles_texture_upload)
          )
      ) ||
      !CheckHR(
          "CreateCopyTilesTextureReadback",
          device->CreateCommittedResource(
              &readback_properties, D3D12_HEAP_FLAG_NONE, &copy_tiles_texture_buffer_desc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&copy_tiles_texture_readback)
          )
      )) {
    cleanup();
    return 1;
  }
  if (!CheckHR(
          "CreateCopyTilesRenderTargetTextureReadback",
          device->CreateCommittedResource(
              &readback_properties, D3D12_HEAP_FLAG_NONE, &copy_tiles_texture_buffer_desc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&copy_tiles_rt_texture_readback)
          )
      )) {
    cleanup();
    return 1;
  }
  BYTE *mapped_copy_tiles_texture_upload = nullptr;
  if (!CheckHR(
          "MapCopyTilesTextureUpload",
          copy_tiles_texture_upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped_copy_tiles_texture_upload))
      )) {
    cleanup();
    return 1;
  }
  for (UINT i = 0; i < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; i++)
    mapped_copy_tiles_texture_upload[i] = static_cast<BYTE>(i ^ 0xa5);
  copy_tiles_texture_upload->Unmap(0, nullptr);

  D3D12_HEAP_DESC unaligned_heap_desc = upload_desc;
  --unaligned_heap_desc.SizeInBytes;
  if (!CheckHR("CreateUnalignedHeap", device->CreateHeap(&unaligned_heap_desc, IID_PPV_ARGS(&unaligned_heap))) ||
      !unaligned_heap) {
    std::cerr << "non-aligned heap size was rejected\n";
    cleanup();
    return 1;
  }
  unaligned_heap->Release();
  unaligned_heap = nullptr;

  depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  depth_desc.Width = 4;
  depth_desc.Height = 4;
  depth_desc.DepthOrArraySize = 1;
  depth_desc.MipLevels = 1;
  depth_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  depth_clear.Format = depth_desc.Format;
  depth_clear.DepthStencil.Depth = 0.25f;
  depth_clear.DepthStencil.Stencil = 0x5a;
  device->GetCopyableFootprints(
      &depth_desc, 0, 2, 0, depth_footprints, depth_rows, depth_row_sizes, &depth_total
  );
  if (depth_total != 2048 || depth_footprints[0].Offset != 0 || depth_footprints[1].Offset != 1024 ||
      depth_footprints[0].Footprint.RowPitch != 256 || depth_footprints[1].Footprint.RowPitch != 256 ||
      depth_rows[0] != 4 || depth_rows[1] != 4 || depth_row_sizes[0] != 16 || depth_row_sizes[1] != 4) {
    std::cerr << "invalid depth/stencil footprints: total=" << depth_total
              << " depth_offset=" << depth_footprints[0].Offset << " stencil_offset=" << depth_footprints[1].Offset
              << "\n";
    cleanup();
    return 1;
  }

  D3D12_HEAP_PROPERTIES depth_properties = {};
  depth_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  depth_properties.CreationNodeMask = 1;
  depth_properties.VisibleNodeMask = 1;
  if (!CheckHR("CreateDepthTexture",
               device->CreateCommittedResource(
                   &depth_properties, D3D12_HEAP_FLAG_NONE, &depth_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                   &depth_clear, IID_PPV_ARGS(&depth_texture)))) {
    cleanup();
    return 1;
  }
  ID3D12Pageable *resident_objects[] = {depth_texture};
  if (!CheckHR("MakeResident", device->MakeResident(1, resident_objects)) ||
      !CheckHR("Evict", device->Evict(1, resident_objects))) {
    cleanup();
    return 1;
  }
  if (!CheckHR("CreateDepthCopy",
               device->CreateCommittedResource(
                   &depth_properties, D3D12_HEAP_FLAG_NONE, &depth_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                   IID_PPV_ARGS(&depth_copy)))) {
    cleanup();
    return 1;
  }

  depth_readback_desc = buffer_desc;
  depth_readback_desc.Width = depth_total;
  if (!CheckHR("CreateDepthReadback",
               device->CreateCommittedResource(
                   &readback_properties, D3D12_HEAP_FLAG_NONE, &depth_readback_desc,
                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&depth_readback)))) {
    cleanup();
    return 1;
  }

  array_render_target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  array_render_target_desc.Width = 4;
  array_render_target_desc.Height = 4;
  array_render_target_desc.DepthOrArraySize = 2;
  array_render_target_desc.MipLevels = 1;
  array_render_target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  array_render_target_desc.SampleDesc.Count = 1;
  array_render_target_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  array_render_target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  array_clear.Format = array_render_target_desc.Format;
  array_clear.Color[3] = 1.0f;
  if (!CheckHR("CreateArrayRenderTarget",
               device->CreateCommittedResource(
                   &depth_properties, D3D12_HEAP_FLAG_NONE,
                   &array_render_target_desc,
                   D3D12_RESOURCE_STATE_RENDER_TARGET, &array_clear,
                   IID_PPV_ARGS(&array_render_target)))) {
    cleanup();
    return 1;
  }
  device->GetCopyableFootprints(&array_render_target_desc, 1, 1, 0,
                                &array_footprint, &array_rows, &array_row_size,
                                &array_total);
  array_readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  array_readback_desc.Width = array_total;
  array_readback_desc.Height = 1;
  array_readback_desc.DepthOrArraySize = 1;
  array_readback_desc.MipLevels = 1;
  array_readback_desc.SampleDesc.Count = 1;
  array_readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!array_total ||
      !CheckHR("CreateArrayReadback",
               device->CreateCommittedResource(
                   &readback_properties, D3D12_HEAP_FLAG_NONE,
                   &array_readback_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                   nullptr, IID_PPV_ARGS(&array_readback)))) {
    cleanup();
    return 1;
  }
  array_partial_readback_desc = array_readback_desc;
  device->GetCopyableFootprints(&array_render_target_desc, 1, 1, 0,
                                &array_partial_footprint, &array_rows, &array_row_size,
                                &array_total);
  array_partial_readback_desc.Width = array_total + 2048;
  array_buffer_copy_dst_footprint = array_partial_footprint;
  array_buffer_copy_dst_footprint.Offset = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
  array_buffer_copy_dst_footprint.Footprint.RowPitch += D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
  if (!CheckHR("CreateArrayPartialReadback",
               device->CreateCommittedResource(
                   &readback_properties, D3D12_HEAP_FLAG_NONE,
                   &array_partial_readback_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                   nullptr, IID_PPV_ARGS(&array_partial_readback)))) {
    cleanup();
    return 1;
  }
  array_upload_desc = array_readback_desc;
  if (!CheckHR("CreateArrayUpload",
               device->CreateCommittedResource(
                   &upload_properties, D3D12_HEAP_FLAG_NONE, &array_upload_desc,
                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                   IID_PPV_ARGS(&array_upload)))) {
    cleanup();
    return 1;
  }
  device->GetCopyableFootprints(&array_render_target_desc, 1, 1, 0,
                                &array_upload_footprint, &array_rows,
                                &array_row_size, &array_total);
  if (!CheckHR("MapArrayUpload",
               array_upload->Map(0, nullptr,
                                 reinterpret_cast<void **>(&mapped_array_upload)))) {
    cleanup();
    return 1;
  }
  std::memset(mapped_array_upload, 0, static_cast<size_t>(array_upload_desc.Width));
  for (UINT row = 0; row < 2; row++) {
    for (UINT column = 0; column < 2; column++) {
      UINT32 red = 0xff0000ffu;
      std::memcpy(mapped_array_upload + array_upload_footprint.Offset +
                                      row * array_upload_footprint.Footprint.RowPitch +
                                      column * sizeof(red),
                  &red, sizeof(red));
    }
  }
  array_upload->Unmap(0, nullptr);

  bc_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  bc_desc.Width = 5;
  bc_desc.Height = 5;
  bc_desc.DepthOrArraySize = 1;
  bc_desc.MipLevels = 1;
  bc_desc.Format = DXGI_FORMAT_BC1_UNORM;
  bc_desc.SampleDesc.Count = 1;
  bc_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  if (!CheckHR("CreateBCTexture",
               device->CreateCommittedResource(
                   &depth_properties, D3D12_HEAP_FLAG_NONE, &bc_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                   IID_PPV_ARGS(&bc_texture)))) {
    cleanup();
    return 1;
  }
  D3D12_BOX bc_transfer_box = {0, 0, 0, 4, 4, 1};
  BYTE bc_transfer_data[8] = {};
  if (bc_texture->WriteToSubresource(0, &bc_transfer_box, bc_transfer_data, 7, 0) != E_INVALIDARG ||
      bc_texture->ReadFromSubresource(bc_transfer_data, 7, 0, 0, &bc_transfer_box) != E_INVALIDARG) {
    std::cerr << "BC texture accepted an undersized row pitch\n";
    cleanup();
    return 1;
  }
  device->GetCopyableFootprints(&bc_desc, 0, 1, 0, &bc_footprint, &bc_rows, &bc_row_size, &bc_total);
  if (bc_total != 512 || bc_rows != 2 || bc_row_size != 16 || bc_footprint.Footprint.RowPitch != 256) {
    std::cerr << "unexpected BC1 copy footprint: total=" << bc_total << " rows=" << bc_rows
              << " row_size=" << bc_row_size << " row_pitch=" << bc_footprint.Footprint.RowPitch << "\n";
    cleanup();
    return 1;
  }
  bc_buffer_desc = buffer_desc;
  bc_buffer_desc.Width = bc_total;
  if (!CheckHR("CreateBCUpload",
               device->CreateCommittedResource(
                   &upload_properties, D3D12_HEAP_FLAG_NONE, &bc_buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                   nullptr, IID_PPV_ARGS(&bc_upload))) ||
      !CheckHR("CreateBCReadback",
               device->CreateCommittedResource(
                   &readback_properties, D3D12_HEAP_FLAG_NONE, &bc_buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                   nullptr, IID_PPV_ARGS(&bc_readback))) ||
      !CheckHR("MapBCUpload", bc_upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped_bc_upload)))) {
    cleanup();
    return 1;
  }
  const uint8_t bc_data[32] = {
      0x00, 0x01, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
      0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
  };
  std::memset(mapped_bc_upload, 0, static_cast<size_t>(bc_buffer_desc.Width));
  for (UINT row = 0; row < bc_rows; row++)
    std::memcpy(mapped_bc_upload + bc_footprint.Offset + row * bc_footprint.Footprint.RowPitch,
                bc_data + row * bc_row_size, static_cast<size_t>(bc_row_size));
  bc_upload->Unmap(0, nullptr);

  texture3d_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  texture3d_desc.Width = 1;
  texture3d_desc.Height = 2;
  texture3d_desc.DepthOrArraySize = 2;
  texture3d_desc.MipLevels = 1;
  texture3d_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture3d_desc.SampleDesc.Count = 1;
  texture3d_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  texture3d_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  texture3d_clear.Format = texture3d_desc.Format;
  texture3d_clear.Color[3] = 1.0f;
  if (!CheckHR("Create3DTexture",
               device->CreateCommittedResource(
                   &depth_properties, D3D12_HEAP_FLAG_NONE, &texture3d_desc,
                   D3D12_RESOURCE_STATE_RENDER_TARGET, &texture3d_clear,
                   IID_PPV_ARGS(&texture3d)))) {
    cleanup();
    return 1;
  }
  D3D12_BOX texture3d_transfer_box = {0, 0, 0, 1, 2, 2};
  BYTE texture3d_transfer_data[32] = {};
  if (texture3d->WriteToSubresource(0, &texture3d_transfer_box, texture3d_transfer_data, 3, 4) != E_INVALIDARG ||
      texture3d->WriteToSubresource(0, &texture3d_transfer_box, texture3d_transfer_data, 4, 3) != E_INVALIDARG ||
      texture3d->WriteToSubresource(0, &texture3d_transfer_box, texture3d_transfer_data, 8, 15) != E_INVALIDARG ||
      texture3d->ReadFromSubresource(texture3d_transfer_data, 3, 4, 0, &texture3d_transfer_box) != E_INVALIDARG ||
      texture3d->ReadFromSubresource(texture3d_transfer_data, 4, 3, 0, &texture3d_transfer_box) != E_INVALIDARG ||
      texture3d->ReadFromSubresource(texture3d_transfer_data, 8, 15, 0, &texture3d_transfer_box) != E_INVALIDARG) {
    std::cerr << "3D texture accepted an undersized row or slice pitch\n";
    cleanup();
    return 1;
  }
  device->GetCopyableFootprints(&texture3d_desc, 0, 1, 0, &texture3d_footprint,
                                &texture3d_rows, &texture3d_row_size,
                                &texture3d_total);
  texture3d_readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  texture3d_readback_desc.Width = texture3d_total;
  texture3d_readback_desc.Height = 1;
  texture3d_readback_desc.DepthOrArraySize = 1;
  texture3d_readback_desc.MipLevels = 1;
  texture3d_readback_desc.SampleDesc.Count = 1;
  texture3d_readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!texture3d_total ||
      !CheckHR("Create3DReadback",
               device->CreateCommittedResource(
                   &readback_properties, D3D12_HEAP_FLAG_NONE,
                   &texture3d_readback_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                   nullptr, IID_PPV_ARGS(&texture3d_readback)))) {
    cleanup();
    return 1;
  }

  dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsv_heap_desc.NumDescriptors = 2;
  if (!CheckHR("CreateDSVHeap", device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap)))) {
    cleanup();
    return 1;
  }
  dsv_handle = dsv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateDepthStencilView(depth_texture, nullptr, dsv_handle);
  null_dsv_handle = dsv_handle;
  null_dsv_handle.ptr +=
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 3;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = {};
  if (!CheckHR("CreateRTVHeap", device->CreateDescriptorHeap(
                                    &rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)))) {
    cleanup();
    return 1;
  }
  rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(nullptr, nullptr, rtv_handle);
  array_rtv_handle = rtv_handle;
  array_rtv_handle.ptr +=
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_RENDER_TARGET_VIEW_DESC array_rtv = {};
  array_rtv.Format = array_render_target_desc.Format;
  array_rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
  array_rtv.Texture2DArray.MipSlice = 0;
  array_rtv.Texture2DArray.FirstArraySlice = 1;
  array_rtv.Texture2DArray.ArraySize = ~0u;
  device->CreateRenderTargetView(array_render_target, &array_rtv,
                                 array_rtv_handle);
  array_rtv.Texture2DArray.PlaneSlice = 1;
  device->CreateRenderTargetView(array_render_target, &array_rtv,
                                 array_rtv_handle);
  texture3d_rtv_handle = rtv_handle;
  texture3d_rtv_handle.ptr += 2 * device->GetDescriptorHandleIncrementSize(
                                      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_RENDER_TARGET_VIEW_DESC texture3d_rtv = {};
  texture3d_rtv.Format = texture3d_desc.Format;
  texture3d_rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
  texture3d_rtv.Texture3D.MipSlice = 0;
  texture3d_rtv.Texture3D.FirstWSlice = 1;
  texture3d_rtv.Texture3D.WSize = ~0u;
  device->CreateRenderTargetView(texture3d, &texture3d_rtv,
                                 texture3d_rtv_handle);
  texture3d_rtv.Texture3D.FirstWSlice = 2;
  device->CreateRenderTargetView(texture3d, &texture3d_rtv,
                                 texture3d_rtv_handle);
  device->CreateDepthStencilView(nullptr, nullptr, null_dsv_handle);

  D3D12_DESCRIPTOR_HEAP_DESC shader_heap_desc = {};
  shader_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  shader_heap_desc.NumDescriptors = 3;
  D3D12_CPU_DESCRIPTOR_HANDLE shader_handle = {};
  if (!CheckHR("CreateShaderHeap",
               device->CreateDescriptorHeap(&shader_heap_desc,
                                            IID_PPV_ARGS(&shader_heap)))) {
    cleanup();
    return 1;
  }
  shader_handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC null_srv = {};
  null_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  null_srv.Format = DXGI_FORMAT_UNKNOWN;
  null_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  device->CreateShaderResourceView(nullptr, &null_srv, shader_handle);
  device->CreateShaderResourceView(nullptr, &null_srv, rtv_handle);
  D3D12_CPU_DESCRIPTOR_HANDLE depth_srv_handle = shader_handle;
  depth_srv_handle.ptr += device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC depth_srv = {};
  depth_srv.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
  depth_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  depth_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  depth_srv.Texture2D.MipLevels = 1;
  depth_srv.Texture2D.PlaneSlice = 0;
  device->CreateShaderResourceView(depth_texture, &depth_srv, depth_srv_handle);
  depth_srv.Format = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
  depth_srv.Texture2D.PlaneSlice = 1;
  device->CreateShaderResourceView(depth_texture, &depth_srv, depth_srv_handle);
  depth_srv.Texture2D.PlaneSlice = 2;
  device->CreateShaderResourceView(depth_texture, &depth_srv, depth_srv_handle);
  texture3d_uav_handle = shader_handle;
  texture3d_uav_handle.ptr += 2 * device->GetDescriptorHandleIncrementSize(
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_UNORDERED_ACCESS_VIEW_DESC texture3d_uav = {};
  texture3d_uav.Format = texture3d_desc.Format;
  texture3d_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
  texture3d_uav.Texture3D.MipSlice = 0;
  texture3d_uav.Texture3D.FirstWSlice = 0;
  texture3d_uav.Texture3D.WSize = 2;
  device->CreateUnorderedAccessView(texture3d, nullptr, &texture3d_uav,
                                    texture3d_uav_handle);
  D3D12_UNORDERED_ACCESS_VIEW_DESC null_uav = {};
  null_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  null_uav.Format = DXGI_FORMAT_UNKNOWN;
  device->CreateUnorderedAccessView(nullptr, nullptr, &null_uav, shader_handle);
  device->CreateUnorderedAccessView(nullptr, nullptr, &null_uav,
                                    null_dsv_handle);
  device->CopyDescriptorsSimple(2, shader_handle, shader_handle,
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  device->CopyDescriptorsSimple(1, shader_handle, rtv_handle,
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

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
  invalid_placed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
  if (device->CreatePlacedResource(
          texture_heap, 0, &cross_texture_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&invalid_placed)
      ) != E_INVALIDARG ||
      invalid_placed != nullptr) {
    std::cerr << "placed cross-adapter texture was accepted\n";
    cleanup();
    return 1;
  }
  if (!CheckHR("CreatePlacedTexture",
                device->CreatePlacedResource(texture_heap, 0, &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&placed_texture)))) {
    cleanup();
    return 1;
  }
  if (placed_texture->GetDesc(nullptr) != nullptr) {
    std::cerr << "texture GetDesc accepted a null output\n";
    cleanup();
    return 1;
  }
  BYTE texture_subresource_data[4] = {};
  D3D12_BOX texture_transfer_box = {0, 0, 0, 4, 4, 1};
  if (placed_texture->WriteToSubresource(1, nullptr, texture_subresource_data, sizeof(texture_subresource_data), 0) !=
          E_INVALIDARG ||
      placed_texture->ReadFromSubresource(texture_subresource_data, sizeof(texture_subresource_data), 0, 1, nullptr) !=
          E_INVALIDARG ||
      placed_texture->WriteToSubresource(0, nullptr, nullptr, 0, 0) != E_INVALIDARG ||
      placed_texture->ReadFromSubresource(nullptr, 0, 0, 0, nullptr) != E_INVALIDARG ||
      placed_texture->WriteToSubresource(0, &texture_transfer_box, texture_subresource_data, 15, 0) != E_INVALIDARG ||
      placed_texture->ReadFromSubresource(texture_subresource_data, 15, 0, 0, &texture_transfer_box) != E_INVALIDARG) {
    std::cerr << "texture subresource range, null data, or row pitch was accepted\n";
    cleanup();
    return 1;
  }

  D3D12_HEAP_DESC rt_texture_heap_desc = texture_heap_desc;
  rt_texture_heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
  if (!CheckHR("CreateRTTextureHeap", device->CreateHeap(&rt_texture_heap_desc, IID_PPV_ARGS(&rt_texture_heap)))) {
    cleanup();
    return 1;
  }
  D3D12_RESOURCE_DESC rt_texture_desc = texture_desc;
  rt_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (!CheckHR("CreatePlacedRenderTargetTexture",
               device->CreatePlacedResource(rt_texture_heap, 0, &rt_texture_desc,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                             IID_PPV_ARGS(&rt_placed_texture)))) {
    cleanup();
    return 1;
  }
  if (device->CreatePlacedResource(rt_texture_heap, 0, &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                   IID_PPV_ARGS(&invalid_placed)) != E_INVALIDARG ||
      invalid_placed != nullptr) {
    std::cerr << "non-RT texture was accepted on an RT/DS-only heap\n";
    cleanup();
    return 1;
  }
  if (device->CreatePlacedResource(texture_heap, 0, &rt_texture_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                   IID_PPV_ARGS(&invalid_placed)) != E_INVALIDARG ||
      invalid_placed != nullptr) {
    std::cerr << "RT texture was accepted on a non-RT/DS-only heap\n";
    cleanup();
    return 1;
  }

  D3D12_HEAP_DESC alias_heap_desc = {};
  alias_heap_desc.SizeInBytes = AlignUp(buffer_info.SizeInBytes, buffer_info.Alignment);
  alias_heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  alias_heap_desc.Properties.CreationNodeMask = 1;
  alias_heap_desc.Properties.VisibleNodeMask = 1;
  alias_heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  if (!CheckHR("CreateAliasHeap", device->CreateHeap(&alias_heap_desc, IID_PPV_ARGS(&alias_heap))) ||
      !CheckHR("CreateAliasBefore",
               device->CreatePlacedResource(alias_heap, 0, &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&alias_before))) ||
      !CheckHR("CreateAliasAfter",
               device->CreatePlacedResource(alias_heap, 0, &buffer_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                                             IID_PPV_ARGS(&alias_after)))) {
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

  invalid_placed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
  if (device->CreatePlacedResource(upload_heap, 0, &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                   IID_PPV_ARGS(&invalid_placed)) != E_INVALIDARG ||
      invalid_placed != nullptr) {
    std::cerr << "placed upload resource accepted a non-GENERIC_READ initial state\n";
    cleanup();
    return 1;
  }
  invalid_placed = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
  if (device->CreatePlacedResource(readback_heap, 0, &buffer_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                   IID_PPV_ARGS(&invalid_placed)) != E_INVALIDARG ||
      invalid_placed != nullptr) {
    std::cerr << "placed readback resource accepted a non-COPY_DEST initial state\n";
    cleanup();
    return 1;
  }

  if (device->CreatePlacedResource(upload_heap, buffer_info.Alignment / 2, &buffer_desc,
                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&invalid_placed)) !=
          E_INVALIDARG ||
      invalid_placed != nullptr) {
    std::cerr << "misaligned placed resource offset was accepted\n";
    cleanup();
    return 1;
  }

  if (!source_zero->GetGPUVirtualAddress() || !source_placed->GetGPUVirtualAddress() ||
      source_zero->GetGPUVirtualAddress() == source_placed->GetGPUVirtualAddress()) {
    std::cerr << "placed resource GPU addresses are invalid\n";
    cleanup();
    return 1;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE invalid_descriptor = {};
  device->CreateShaderResourceView(source_zero, nullptr, invalid_descriptor);
  device->CreateUnorderedAccessView(source_zero, nullptr, nullptr,
                                    invalid_descriptor);

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
  if (!CheckHR("CreateCommandQueue",
               device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !CheckHR("CreateCommandAllocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator))) ||
      !CheckHR("CreateCommandList",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator, nullptr,
                                         IID_PPV_ARGS(&list)))) {
    cleanup();
    return 1;
  }
  if (!CheckHR("QueryCommandList2", list->QueryInterface(IID_PPV_ARGS(&list2)))) {
    cleanup();
    return 1;
  }
  queue->UpdateTileMappings(
      reserved_texture_packed_mips, 1, &packed_mip_coordinate, &packed_mip_region, reserved_texture_heap, 1, nullptr,
      &packed_mip_heap_tile_offset, &packed_mip_range_tile_count, D3D12_TILE_MAPPING_FLAG_NONE
  );
  D3D12_TILE_RANGE_FLAGS null_range_flags = D3D12_TILE_RANGE_FLAG_NULL;
  queue->UpdateTileMappings(
      reserved_resource, 1, nullptr, nullptr, nullptr, 1, &null_range_flags, nullptr, nullptr,
      D3D12_TILE_MAPPING_FLAG_NONE
  );
  D3D12_TILED_RESOURCE_COORDINATE tile_coordinate = {};
  D3D12_TILE_REGION_SIZE tile_region = {};
  tile_region.NumTiles = 1;
  UINT heap_tile_offset = 0;
  queue->UpdateTileMappings(
      reserved_resource, 1, &tile_coordinate, &tile_region, reserved_heap, 1, nullptr, &heap_tile_offset, nullptr,
      D3D12_TILE_MAPPING_FLAG_NONE
  );
  D3D12_TILE_RANGE_FLAGS skip_range_flags = D3D12_TILE_RANGE_FLAG_SKIP;
  queue->UpdateTileMappings(
      reserved_resource, 1, &tile_coordinate, &tile_region, nullptr, 1, &skip_range_flags, nullptr, nullptr,
      D3D12_TILE_MAPPING_FLAG_NONE
  );
  D3D12_TILED_RESOURCE_COORDINATE reserved_resource_second_tile = tile_coordinate;
  reserved_resource_second_tile.X = 1;
  D3D12_TILE_RANGE_FLAGS reserved_resource_reuse_range_flag = D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
  UINT reserved_resource_reuse_tile_count = 1;
  queue->UpdateTileMappings(
      reserved_resource, 1, &reserved_resource_second_tile, &tile_region, reserved_heap, 1,
      &reserved_resource_reuse_range_flag, &heap_tile_offset, &reserved_resource_reuse_tile_count,
      D3D12_TILE_MAPPING_FLAG_NONE
  );

  D3D12_RESOURCE_BARRIER reserved_copy_barrier = {};
  reserved_copy_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  reserved_copy_barrier.Transition.pResource = reserved_resource;
  reserved_copy_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  reserved_copy_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  reserved_copy_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &reserved_copy_barrier);
  list->CopyTiles(
      reserved_resource, &tile_coordinate, &tile_region, copy_tiles_upload, 0,
      D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE
  );
  reserved_copy_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  reserved_copy_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &reserved_copy_barrier);
  list->CopyTiles(
      reserved_resource, &tile_coordinate, &tile_region, copy_tiles_readback, 0,
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  D3D12_TILED_RESOURCE_COORDINATE unmapped_tile_coordinate = tile_coordinate;
  unmapped_tile_coordinate.X = 1;
  list->CopyTiles(
      reserved_resource, &unmapped_tile_coordinate, &tile_region, copy_tiles_readback,
      D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  D3D12_RESOURCE_BARRIER reserved_resource_copy_barrier = {};
  reserved_resource_copy_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  reserved_resource_copy_barrier.Transition.pResource = reserved_resource_copy;
  reserved_resource_copy_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  reserved_resource_copy_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  reserved_resource_copy_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &reserved_resource_copy_barrier);
  list->CopyTiles(
      reserved_resource_copy, &reserved_resource_second_tile, &tile_region, copy_tiles_readback,
      D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );

  D3D12_TILED_RESOURCE_COORDINATE mip_tile_coordinate = {};
  mip_tile_coordinate.Subresource = 2;
  D3D12_TILE_REGION_SIZE mip_tile_region = {};
  mip_tile_region.NumTiles = 2;
  UINT mip_heap_tile_offset = 2;
  UINT mip_range_tile_count = 2;
  queue->UpdateTileMappings(
      reserved_texture_mips, 1, &mip_tile_coordinate, &mip_tile_region, reserved_texture_heap, 1, nullptr,
      &mip_heap_tile_offset, &mip_range_tile_count, D3D12_TILE_MAPPING_FLAG_NONE
  );
  D3D12_RESOURCE_BARRIER reserved_texture_mips_copy_barrier = {};
  reserved_texture_mips_copy_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  reserved_texture_mips_copy_barrier.Transition.pResource = reserved_texture_mips;
  reserved_texture_mips_copy_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  reserved_texture_mips_copy_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  reserved_texture_mips_copy_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &reserved_texture_mips_copy_barrier);
  list->CopyTiles(
      reserved_texture_mips, &mip_tile_coordinate, &mip_tile_region, copy_tiles_upload, 0,
      D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE
  );
  reserved_texture_mips_copy_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  reserved_texture_mips_copy_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &reserved_texture_mips_copy_barrier);
  list->CopyTiles(
      reserved_texture_mips, &mip_tile_coordinate, &mip_tile_region, copy_tiles_mips_readback, 0,
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );

  D3D12_TILED_RESOURCE_COORDINATE texture_tile_coordinate = {};
  D3D12_TILE_REGION_SIZE texture_tile_region = {};
  texture_tile_region.NumTiles = 2;
  D3D12_TILE_RANGE_FLAGS texture_range_flags[] = {D3D12_TILE_RANGE_FLAG_NONE, D3D12_TILE_RANGE_FLAG_NULL};
  UINT texture_heap_tile_offsets[] = {0, 0};
  UINT texture_range_tile_counts[] = {1, 1};
  queue->UpdateTileMappings(
      reserved_texture, 1, &texture_tile_coordinate, &texture_tile_region, reserved_texture_heap, 2,
      texture_range_flags, texture_heap_tile_offsets, texture_range_tile_counts, D3D12_TILE_MAPPING_FLAG_NONE
  );
  D3D12_TILE_REGION_SIZE texture_copy_tile_region = texture_tile_region;
  texture_copy_tile_region.NumTiles = 1;
  D3D12_RESOURCE_BARRIER reserved_texture_copy_barrier = {};
  reserved_texture_copy_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  reserved_texture_copy_barrier.Transition.pResource = reserved_texture;
  reserved_texture_copy_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  reserved_texture_copy_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  reserved_texture_copy_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &reserved_texture_copy_barrier);
  list->CopyTiles(
      reserved_texture, &texture_tile_coordinate, &texture_copy_tile_region, copy_tiles_texture_upload, 0,
      D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE
  );
  reserved_texture_copy_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  reserved_texture_copy_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &reserved_texture_copy_barrier);
  list->CopyTiles(
      reserved_texture, &texture_tile_coordinate, &texture_copy_tile_region, copy_tiles_texture_readback, 0,
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  texture_tile_coordinate.Subresource = 1;
  D3D12_TILE_RANGE_FLAGS reuse_range_flag = D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
  UINT reuse_range_tile_count = 2;
  queue->UpdateTileMappings(
      reserved_texture, 1, &texture_tile_coordinate, &texture_tile_region, reserved_texture_heap, 1,
      &reuse_range_flag, texture_heap_tile_offsets, &reuse_range_tile_count, D3D12_TILE_MAPPING_FLAG_NONE
  );
  D3D12_TILE_RANGE_FLAGS texture_skip_range_flag = D3D12_TILE_RANGE_FLAG_SKIP;
  UINT texture_skip_tile_count = 1;
  queue->UpdateTileMappings(
      reserved_texture, 1, &texture_tile_coordinate, &texture_tile_region, nullptr, 1, &texture_skip_range_flag,
      nullptr, &texture_skip_tile_count, D3D12_TILE_MAPPING_FLAG_NONE
  );
  D3D12_TILE_REGION_SIZE texture_box_region = {};
  texture_box_region.NumTiles = 2;
  texture_box_region.UseBox = TRUE;
  texture_box_region.Width = 2;
  texture_box_region.Height = 1;
  texture_box_region.Depth = 1;
  D3D12_TILE_RANGE_FLAGS texture_box_skip_range_flag = D3D12_TILE_RANGE_FLAG_SKIP;
  UINT texture_box_skip_tile_count = 2;
  queue->UpdateTileMappings(
      reserved_texture, 1, &texture_tile_coordinate, &texture_box_region, nullptr, 1,
      &texture_box_skip_range_flag, nullptr, &texture_box_skip_tile_count, D3D12_TILE_MAPPING_FLAG_NONE
  );
  D3D12_TILED_RESOURCE_COORDINATE texture_copy_coordinate = {};
  texture_copy_coordinate.Subresource = 1;
  queue->CopyTileMappings(
      reserved_texture_copy, &texture_copy_coordinate, reserved_texture, &texture_copy_coordinate,
      &texture_box_region, D3D12_TILE_MAPPING_FLAG_NONE
  );
  queue->CopyTileMappings(
      reserved_resource, nullptr, reserved_texture, nullptr, &tile_region, D3D12_TILE_MAPPING_FLAG_NONE
  );
  queue->CopyTileMappings(
      reserved_texture, nullptr, reserved_resource, nullptr, &tile_region, D3D12_TILE_MAPPING_FLAG_NONE
  );
  queue->UpdateTileMappings(
      reserved_resource, 1, &tile_coordinate, &tile_region, reserved_heap, 1, nullptr, &heap_tile_offset, nullptr,
      D3D12_TILE_MAPPING_FLAG_NONE
  );

  const FLOAT texture3d_clear_color[] = {1.0f, 0.0f, 0.0f, 1.0f};
  list->OMSetRenderTargets(1, &texture3d_rtv_handle, FALSE, nullptr);
  list->ClearRenderTargetView(texture3d_rtv_handle, texture3d_clear_color, 0,
                              nullptr);
  D3D12_RESOURCE_BARRIER texture3d_barrier = {};
  texture3d_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  texture3d_barrier.Transition.pResource = texture3d;
  texture3d_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  texture3d_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  texture3d_barrier.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &texture3d_barrier);
  D3D12_TEXTURE_COPY_LOCATION texture3d_copy_dst = {};
  texture3d_copy_dst.pResource = texture3d_readback;
  texture3d_copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  texture3d_copy_dst.PlacedFootprint = texture3d_footprint;
  D3D12_TEXTURE_COPY_LOCATION texture3d_copy_src = {};
  texture3d_copy_src.pResource = texture3d;
  texture3d_copy_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  texture3d_copy_src.SubresourceIndex = 0;
  list->CopyTextureRegion(&texture3d_copy_dst, 0, 0, 0, &texture3d_copy_src,
                          nullptr);

  const FLOAT array_clear_color[] = {0.0f, 1.0f, 0.0f, 1.0f};
  list->OMSetRenderTargets(1, &array_rtv_handle, FALSE, nullptr);
  list->ClearRenderTargetView(array_rtv_handle, array_clear_color, 0, nullptr);
  D3D12_RESOURCE_BARRIER array_barrier = {};
  array_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  array_barrier.Transition.pResource = array_render_target;
  array_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  array_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  array_barrier.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &array_barrier);
  D3D12_TEXTURE_COPY_LOCATION array_copy_dst = {};
  array_copy_dst.pResource = array_readback;
  array_copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  array_copy_dst.PlacedFootprint = array_footprint;
  D3D12_TEXTURE_COPY_LOCATION array_copy_src = {};
  array_copy_src.pResource = array_render_target;
  array_copy_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  array_copy_src.SubresourceIndex = 1;
  list->CopyTextureRegion(&array_copy_dst, 0, 0, 0, &array_copy_src, nullptr);
  D3D12_TEXTURE_COPY_LOCATION array_partial_copy_dst = {};
  array_partial_copy_dst.pResource = array_partial_readback;
  array_partial_copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  array_partial_copy_dst.PlacedFootprint = array_partial_footprint;
  D3D12_BOX array_partial_box = {1, 1, 0, 3, 3, 1};
  list->CopyTextureRegion(&array_partial_copy_dst, 0, 0, 0, &array_copy_src,
                          &array_partial_box);
  array_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  array_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  list->ResourceBarrier(1, &array_barrier);
  D3D12_RESOURCE_BARRIER array_upload_barrier = {};
  array_upload_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  array_upload_barrier.Transition.pResource = array_upload;
  array_upload_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
  array_upload_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  array_upload_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &array_upload_barrier);
  D3D12_TEXTURE_COPY_LOCATION array_buffer_copy_src = {};
  array_buffer_copy_src.pResource = array_upload;
  array_buffer_copy_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  array_buffer_copy_src.PlacedFootprint = array_upload_footprint;
  D3D12_TEXTURE_COPY_LOCATION array_texture_copy_dst = {};
  array_texture_copy_dst.pResource = array_render_target;
  array_texture_copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  array_texture_copy_dst.SubresourceIndex = 1;
  D3D12_BOX array_upload_box = {0, 0, 0, 2, 2, 1};
  list->CopyTextureRegion(&array_texture_copy_dst, 1, 1, 0,
                          &array_buffer_copy_src, &array_upload_box);
  D3D12_TEXTURE_COPY_LOCATION array_buffer_copy_dst = {};
  array_buffer_copy_dst.pResource = array_partial_readback;
  array_buffer_copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  array_buffer_copy_dst.PlacedFootprint = array_buffer_copy_dst_footprint;
  list->CopyTextureRegion(&array_buffer_copy_dst, 2, 0, 0,
                          &array_buffer_copy_src, &array_upload_box);
  array_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  array_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &array_barrier);
  list->CopyTextureRegion(&array_copy_dst, 0, 0, 0, &array_copy_src, nullptr);

  D3D12_RESOURCE_BARRIER bc_upload_barrier = {};
  bc_upload_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  bc_upload_barrier.Transition.pResource = bc_upload;
  bc_upload_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
  bc_upload_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  bc_upload_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &bc_upload_barrier);
  D3D12_TEXTURE_COPY_LOCATION bc_texture_dst = {};
  bc_texture_dst.pResource = bc_texture;
  bc_texture_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  bc_texture_dst.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION bc_upload_src = {};
  bc_upload_src.pResource = bc_upload;
  bc_upload_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  bc_upload_src.PlacedFootprint = bc_footprint;
  list->CopyTextureRegion(&bc_texture_dst, 0, 0, 0, &bc_upload_src, nullptr);
  D3D12_RESOURCE_BARRIER bc_texture_barrier = {};
  bc_texture_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  bc_texture_barrier.Transition.pResource = bc_texture;
  bc_texture_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  bc_texture_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  bc_texture_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &bc_texture_barrier);
  D3D12_TEXTURE_COPY_LOCATION bc_readback_dst = {};
  bc_readback_dst.pResource = bc_readback;
  bc_readback_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  bc_readback_dst.PlacedFootprint = bc_footprint;
  D3D12_TEXTURE_COPY_LOCATION bc_texture_src = {};
  bc_texture_src.pResource = bc_texture;
  bc_texture_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  bc_texture_src.SubresourceIndex = 0;
  list->CopyTextureRegion(&bc_readback_dst, 0, 0, 0, &bc_texture_src, nullptr);

  list->OMSetRenderTargets(0, nullptr, FALSE, &null_dsv_handle);
  list->ClearDepthStencilView(null_dsv_handle,
                              D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                              0.25f, 0x5a, 0, nullptr);
  list->OMSetRenderTargets(0, nullptr, FALSE, &dsv_handle);
  D3D12_CPU_DESCRIPTOR_HANDLE invalid_dsv = dsv_handle;
  invalid_dsv.ptr += 2 * device->GetDescriptorHandleIncrementSize(
                             D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  list->ClearDepthStencilView(invalid_dsv,
                              D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                              0.25f, 0x5a, 0, nullptr);
  list->ClearDepthStencilView(
      dsv_handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.25f, 0x5a, 0, nullptr
  );
  depth_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  depth_barrier.Transition.pResource = depth_texture;
  depth_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  depth_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  depth_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &depth_barrier);
  list->CopyResource(depth_copy, depth_texture);
  depth_barrier.Transition.pResource = depth_copy;
  depth_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  depth_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &depth_barrier);
  for (UINT plane = 0; plane < 2; plane++) {
    D3D12_TEXTURE_COPY_LOCATION depth_dst = {};
    depth_dst.pResource = depth_readback;
    depth_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    depth_dst.PlacedFootprint = depth_footprints[plane];

    D3D12_TEXTURE_COPY_LOCATION depth_src = {};
    depth_src.pResource = depth_copy;
    depth_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    depth_src.SubresourceIndex = plane;
    list->CopyTextureRegion(&depth_dst, 0, 0, 0, &depth_src, nullptr);
  }

  aliasing_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
  list->CopyBufferRegion(alias_before, 0, source_placed, 0, sizeof(value));
  aliasing_barrier.Aliasing.pResourceBefore = alias_before;
  aliasing_barrier.Aliasing.pResourceAfter = alias_after;
  list->ResourceBarrier(1, &aliasing_barrier);
  list->CopyBufferRegion(destination, 0, alias_after, 0, sizeof(value));
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

  if (!CheckHR("CreateMultipleFence",
               device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&multiple_fence)))) {
    cleanup();
    return 1;
  }
  multiple_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  ID3D12Fence *multiple_fences[] = {fence, multiple_fence};
  UINT64 multiple_values[] = {1, 1};
  if (!multiple_event ||
      !CheckHR("SetEventOnMultipleFenceCompletion",
               device1->SetEventOnMultipleFenceCompletion(
                   multiple_fences, multiple_values, 2,
                   D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE, multiple_event)) ||
      WaitForSingleObject(multiple_event, 0) != WAIT_TIMEOUT ||
      !CheckHR("SignalMultipleFence", queue->Signal(multiple_fence, 1)) ||
      WaitForSingleObject(multiple_event, 5000) != WAIT_OBJECT_0) {
    std::cerr << "WAIT_ALL multiple fence completion failed\n";
    cleanup();
    return 1;
  }

  ResetEvent(multiple_event);
  UINT64 any_values[] = {3, 4};
  if (!CheckHR("SetEventOnAnyFenceCompletion",
               device1->SetEventOnMultipleFenceCompletion(
                   multiple_fences, any_values, 2,
                   D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, multiple_event)) ||
      WaitForSingleObject(multiple_event, 0) != WAIT_TIMEOUT ||
      !CheckHR("SignalAnyFence", queue->Signal(multiple_fence, 4)) ||
      WaitForSingleObject(multiple_event, 5000) != WAIT_OBJECT_0) {
    std::cerr << "WAIT_ANY multiple fence completion failed\n";
    cleanup();
    return 1;
  }

  BYTE *mapped_copy_tiles_remap_readback = nullptr;
  if (!CheckHR("MapCopyTilesRemapReadback", copy_tiles_remap_readback->Map(
                                                 0, nullptr,
                                                 reinterpret_cast<void **>(&mapped_copy_tiles_remap_readback)))) {
    cleanup();
    return 1;
  }
  std::memset(mapped_copy_tiles_remap_readback, 0xcd, static_cast<size_t>(copy_tiles_buffer_desc.Width));
  copy_tiles_remap_readback->Unmap(0, nullptr);

  queue->UpdateTileMappings(
      reserved_resource, 1, &tile_coordinate, &tile_region, reserved_heap, 1, nullptr, &heap_tile_offset, nullptr,
      D3D12_TILE_MAPPING_FLAG_NONE
  );
  if (!CheckHR("ResetForCopyTilesRemap", list->Reset(allocator, nullptr))) {
    cleanup();
    return 1;
  }
  D3D12_RESOURCE_BARRIER remap_barrier = {};
  remap_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  remap_barrier.Transition.pResource = reserved_resource;
  remap_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  remap_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  remap_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &remap_barrier);
  list->CopyTiles(
      reserved_resource, &tile_coordinate, &tile_region, copy_tiles_upload, 0,
      D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE
  );
  remap_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  remap_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &remap_barrier);
  list->CopyTiles(
      reserved_resource, &tile_coordinate, &tile_region, copy_tiles_remap_readback, 0,
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  queue->UpdateTileMappings(
      reserved_resource, 1, &tile_coordinate, &tile_region, nullptr, 1, &null_range_flags, nullptr, nullptr,
      D3D12_TILE_MAPPING_FLAG_NONE
  );
  if (!CheckHR("CloseCopyTilesRemap", list->Close())) {
    cleanup();
    return 1;
  }
  ID3D12CommandList *remap_lists[] = {list};
  queue->ExecuteCommandLists(1, remap_lists);
  if (!CheckHR("SignalCopyTilesRemap", queue->Signal(fence, 2)) ||
      !CheckHR("SetEventOnCopyTilesRemap", fence->SetEventOnCompletion(2, event))) {
    cleanup();
    return 1;
  }
  WaitForSingleObject(event, INFINITE);

  if (!CheckHR("MapCopyTilesRemapResult", copy_tiles_remap_readback->Map(
                                                   0, nullptr,
                                                   reinterpret_cast<void **>(&mapped_copy_tiles_remap_readback)))) {
    cleanup();
    return 1;
  }
  bool copy_tiles_remap_is_zero = true;
  for (UINT i = 0; i < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; i++) {
    if (mapped_copy_tiles_remap_readback[i] != 0) {
      copy_tiles_remap_is_zero = false;
      break;
    }
  }
  copy_tiles_remap_readback->Unmap(0, nullptr);
  if (!copy_tiles_remap_is_zero) {
    std::cerr << "CopyTiles used a mapping captured before execution\n";
    cleanup();
    return 1;
  }

  D3D12_TILED_RESOURCE_COORDINATE boxed_coordinate = {};
  D3D12_TILE_REGION_SIZE boxed_region = {};
  boxed_region.NumTiles = 2;
  boxed_region.UseBox = TRUE;
  boxed_region.Width = 1;
  boxed_region.Height = 1;
  boxed_region.Depth = 2;
  UINT boxed_heap_offset = 0;
  UINT boxed_tile_count = 2;
  queue->UpdateTileMappings(
      reserved_texture_mips, 1, &boxed_coordinate, &boxed_region, reserved_texture_heap, 1, nullptr,
      &boxed_heap_offset, &boxed_tile_count, D3D12_TILE_MAPPING_FLAG_NONE
  );
  if (!CheckHR("ResetForCopyTilesBox", list->Reset(allocator, nullptr))) {
    cleanup();
    return 1;
  }
  D3D12_RESOURCE_BARRIER boxed_barrier = {};
  boxed_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  boxed_barrier.Transition.pResource = reserved_texture_mips;
  boxed_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  boxed_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  boxed_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &boxed_barrier);
  list->CopyTiles(
      reserved_texture_mips, &boxed_coordinate, &boxed_region, copy_tiles_upload, 0,
      D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE
  );
  boxed_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  boxed_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &boxed_barrier);
  list->CopyTiles(
      reserved_texture_mips, &boxed_coordinate, &boxed_region, copy_tiles_remap_readback, 0,
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  if (!CheckHR("CloseCopyTilesBox", list->Close())) {
    cleanup();
    return 1;
  }
  ID3D12CommandList *boxed_lists[] = {list};
  queue->ExecuteCommandLists(1, boxed_lists);
  if (!CheckHR("SignalCopyTilesBox", queue->Signal(fence, 3)) ||
      !CheckHR("SetEventOnCopyTilesBox", fence->SetEventOnCompletion(3, event))) {
    cleanup();
    return 1;
  }
  WaitForSingleObject(event, INFINITE);

  if (!CheckHR("MapCopyTilesBoxResult", copy_tiles_remap_readback->Map(
                                               0, nullptr,
                                               reinterpret_cast<void **>(&mapped_copy_tiles_remap_readback)))) {
    cleanup();
    return 1;
  }
  bool copy_tiles_box_matches = true;
  for (UINT i = 0; i < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; i++) {
    if (mapped_copy_tiles_remap_readback[i] != static_cast<BYTE>(i ^ 0x5a) ||
        mapped_copy_tiles_remap_readback[D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT + i] != 0) {
      copy_tiles_box_matches = false;
      break;
    }
  }
  copy_tiles_remap_readback->Unmap(0, nullptr);
  if (!copy_tiles_box_matches) {
    std::cerr << "boxed multi-mip Texture2D array CopyTiles readback mismatch\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("MapCopyTilesInvalidBoxReadback", copy_tiles_remap_readback->Map(
                                                        0, nullptr,
                                                        reinterpret_cast<void **>(&mapped_copy_tiles_remap_readback)))) {
    cleanup();
    return 1;
  }
  std::memset(mapped_copy_tiles_remap_readback, 0xcd, static_cast<size_t>(copy_tiles_buffer_desc.Width));
  copy_tiles_remap_readback->Unmap(0, nullptr);

  if (!CheckHR("ResetForCopyTilesInvalidBox", list->Reset(allocator, nullptr))) {
    cleanup();
    return 1;
  }
  D3D12_TILED_RESOURCE_COORDINATE invalid_box_coordinate = {};
  invalid_box_coordinate.Subresource = 3;
  D3D12_TILE_REGION_SIZE invalid_box_region = boxed_region;
  list->CopyTiles(
      reserved_texture_mips, &invalid_box_coordinate, &invalid_box_region, copy_tiles_remap_readback, 0,
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  if (list->Close() != E_FAIL) {
    std::cerr << "invalid boxed Texture2D array CopyTiles was not rejected at recording\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("MapCopyTilesInvalidBoxResult", copy_tiles_remap_readback->Map(
                                                       0, nullptr,
                                                       reinterpret_cast<void **>(&mapped_copy_tiles_remap_readback)))) {
    cleanup();
    return 1;
  }
  bool copy_tiles_invalid_box_preserved = true;
  for (UINT i = 0; i < copy_tiles_buffer_desc.Width; i++) {
    if (mapped_copy_tiles_remap_readback[i] != 0xcd) {
      copy_tiles_invalid_box_preserved = false;
      break;
    }
  }
  copy_tiles_remap_readback->Unmap(0, nullptr);
  if (!copy_tiles_invalid_box_preserved) {
    std::cerr << "invalid boxed Texture2D array CopyTiles modified the buffer\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("ResetForCopyTilesInvalidZ", list->Reset(allocator, nullptr))) {
    cleanup();
    return 1;
  }
  D3D12_TILED_RESOURCE_COORDINATE invalid_z_coordinate = boxed_coordinate;
  invalid_z_coordinate.Z = 1;
  D3D12_TILE_REGION_SIZE single_tile_region = {};
  single_tile_region.NumTiles = 1;
  single_tile_region.UseBox = TRUE;
  single_tile_region.Width = 1;
  single_tile_region.Height = 1;
  single_tile_region.Depth = 1;
  list->CopyTiles(
      reserved_texture_mips, &invalid_z_coordinate, &single_tile_region, copy_tiles_remap_readback, 0,
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  if (list->Close() != E_FAIL) {
    std::cerr << "non-zero Z boxed Texture2D CopyTiles was not rejected at recording\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("MapCopyTilesInvalidZResult", copy_tiles_remap_readback->Map(
                                                     0, nullptr,
                                                     reinterpret_cast<void **>(&mapped_copy_tiles_remap_readback)))) {
    cleanup();
    return 1;
  }
  bool copy_tiles_invalid_z_preserved = true;
  for (UINT i = 0; i < copy_tiles_buffer_desc.Width; i++) {
    if (mapped_copy_tiles_remap_readback[i] != 0xcd) {
      copy_tiles_invalid_z_preserved = false;
      break;
    }
  }
  copy_tiles_remap_readback->Unmap(0, nullptr);
  if (!copy_tiles_invalid_z_preserved) {
    std::cerr << "non-zero Z boxed Texture2D CopyTiles modified the buffer\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("ResetForCopyTilesOutOfBounds", list->Reset(allocator, nullptr))) {
    cleanup();
    return 1;
  }
  D3D12_TILE_REGION_SIZE valid_copy_region = {};
  valid_copy_region.NumTiles = 1;
  list->CopyTiles(
      reserved_texture_mips, &boxed_coordinate, &valid_copy_region, copy_tiles_remap_readback,
      copy_tiles_buffer_desc.Width, D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  if (list->Close() != E_FAIL) {
    std::cerr << "out-of-bounds CopyTiles buffer range was not rejected at recording\n";
    cleanup();
    return 1;
  }

  auto expect_copy_tiles_recording_failure = [&](const char *name, ID3D12Resource *tiled_resource,
                                                  const D3D12_TILED_RESOURCE_COORDINATE *coordinate,
                                                  const D3D12_TILE_REGION_SIZE *region_size, ID3D12Resource *buffer,
                                                  UINT64 buffer_offset, D3D12_TILE_COPY_FLAGS flags) {
    if (!CheckHR(name, list->Reset(allocator, nullptr))) {
      cleanup();
      return false;
    }
    list->CopyTiles(tiled_resource, coordinate, region_size, buffer, buffer_offset, flags);
    if (list->Close() != E_FAIL) {
      std::cerr << name << " was accepted at recording\n";
      cleanup();
      return false;
    }
    return true;
  };
  const auto invalid_copy_tiles_flags = static_cast<D3D12_TILE_COPY_FLAGS>(1u << 3);
  const auto conflicting_copy_tiles_flags = static_cast<D3D12_TILE_COPY_FLAGS>(
      D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE |
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  if (!expect_copy_tiles_recording_failure(
          "null CopyTiles tiled resource", nullptr, &boxed_coordinate, &valid_copy_region, copy_tiles_remap_readback, 0,
          D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
      ) ||
      !expect_copy_tiles_recording_failure(
          "null CopyTiles tile region", reserved_texture_mips, &boxed_coordinate, nullptr, copy_tiles_remap_readback,
          0, D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
      ) ||
      !expect_copy_tiles_recording_failure(
          "null CopyTiles buffer", reserved_texture_mips, &boxed_coordinate, &valid_copy_region, nullptr, 0,
          D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
      ) ||
      !expect_copy_tiles_recording_failure(
          "unsupported CopyTiles flags", reserved_texture_mips, &boxed_coordinate, &valid_copy_region,
          copy_tiles_remap_readback, 0, invalid_copy_tiles_flags
      ) ||
      !expect_copy_tiles_recording_failure(
          "conflicting CopyTiles directions", reserved_texture_mips, &boxed_coordinate, &valid_copy_region,
          copy_tiles_remap_readback, 0, conflicting_copy_tiles_flags
      ) ||
      !expect_copy_tiles_recording_failure(
          "unaligned CopyTiles buffer offset", reserved_texture_mips, &boxed_coordinate, &valid_copy_region,
          copy_tiles_remap_readback, 1, D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
      ) ||
      !expect_copy_tiles_recording_failure(
          "non-reserved CopyTiles tiled resource", depth_texture, &boxed_coordinate, &valid_copy_region,
          copy_tiles_remap_readback, 0, D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
      ))
    return 1;

  if (!CheckHR("ResetForCopyTilesNullCoordinate", list->Reset(allocator, nullptr))) {
    cleanup();
    return 1;
  }
  list->CopyTiles(
      reserved_texture_mips, nullptr, &valid_copy_region, copy_tiles_remap_readback, 0,
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  if (list->Close() != E_FAIL) {
    std::cerr << "CopyTiles accepted a null tile-region start coordinate\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("ResetForCopyTilesPackedMip", list->Reset(allocator, nullptr))) {
    cleanup();
    return 1;
  }
  list->CopyTiles(
      reserved_texture_packed_mips, &packed_mip_coordinate, &packed_mip_region, copy_tiles_remap_readback, 0,
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  if (list->Close() != E_FAIL) {
    std::cerr << "CopyTiles accepted a packed mip tile\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("MapCopyTilesDefaultDirectionReadback", copy_tiles_remap_readback->Map(
                                                           0, nullptr,
                                                           reinterpret_cast<void **>(&mapped_copy_tiles_remap_readback)))) {
    cleanup();
    return 1;
  }
  std::memset(mapped_copy_tiles_remap_readback, 0xcd, static_cast<size_t>(copy_tiles_buffer_desc.Width));
  copy_tiles_remap_readback->Unmap(0, nullptr);

  UINT default_direction_tile_count = 1;
  queue->UpdateTileMappings(
      reserved_texture_copy, 1, &boxed_coordinate, &valid_copy_region, reserved_texture_heap, 1, nullptr,
      &boxed_heap_offset, &default_direction_tile_count, D3D12_TILE_MAPPING_FLAG_NONE
  );
  if (!CheckHR("ResetForCopyTilesDefaultDirection", list->Reset(allocator, nullptr))) {
    cleanup();
    return 1;
  }
  D3D12_RESOURCE_BARRIER default_direction_barrier = {};
  default_direction_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  default_direction_barrier.Transition.pResource = reserved_texture_copy;
  default_direction_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  default_direction_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  default_direction_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &default_direction_barrier);
  list->CopyTiles(
      reserved_texture_copy, &boxed_coordinate, &valid_copy_region, copy_tiles_remap_readback, 0,
      D3D12_TILE_COPY_FLAG_NONE
  );
  if (!CheckHR("CloseCopyTilesDefaultDirection", list->Close())) {
    cleanup();
    return 1;
  }
  ID3D12CommandList *default_direction_lists[] = {list};
  queue->ExecuteCommandLists(1, default_direction_lists);
  if (!CheckHR("SignalCopyTilesDefaultDirection", queue->Signal(fence, 6)) ||
      !CheckHR("SetEventOnCopyTilesDefaultDirection", fence->SetEventOnCompletion(6, event))) {
    cleanup();
    return 1;
  }
  WaitForSingleObject(event, INFINITE);

  if (!CheckHR("MapCopyTilesDefaultDirectionResult", copy_tiles_remap_readback->Map(
                                                            0, nullptr,
                                                            reinterpret_cast<void **>(&mapped_copy_tiles_remap_readback)))) {
    cleanup();
    return 1;
  }
  bool copy_tiles_default_direction_matches = true;
  for (UINT i = 0; i < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; i++) {
    if (mapped_copy_tiles_remap_readback[i] != static_cast<BYTE>(i ^ 0x5a)) {
      copy_tiles_default_direction_matches = false;
      break;
    }
  }
  copy_tiles_remap_readback->Unmap(0, nullptr);
  if (!copy_tiles_default_direction_matches) {
    std::cerr << "CopyTiles FLAG_NONE did not copy tiled resource data to the buffer\n";
    cleanup();
    return 1;
  }

  UINT reserved_rt_heap_tile_offset = 0;
  queue->UpdateTileMappings(
      reserved_rt_texture, 1, &tile_coordinate, &tile_region, reserved_rt_texture_heap, 1, nullptr,
      &reserved_rt_heap_tile_offset, nullptr, D3D12_TILE_MAPPING_FLAG_NONE
  );
  if (!CheckHR("ResetForReservedRenderTargetCopyTiles", list->Reset(allocator, nullptr))) {
    cleanup();
    return 1;
  }
  D3D12_RESOURCE_BARRIER reserved_rt_texture_barrier = {};
  reserved_rt_texture_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  reserved_rt_texture_barrier.Transition.pResource = reserved_rt_texture;
  reserved_rt_texture_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  reserved_rt_texture_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  reserved_rt_texture_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &reserved_rt_texture_barrier);
  list->CopyTiles(
      reserved_rt_texture, &tile_coordinate, &tile_region, copy_tiles_upload, 0,
      D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE
  );
  reserved_rt_texture_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  reserved_rt_texture_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &reserved_rt_texture_barrier);
  list->CopyTiles(
      reserved_rt_texture, &tile_coordinate, &tile_region, copy_tiles_rt_texture_readback, 0,
      D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
  );
  if (!CheckHR("CloseReservedRenderTargetCopyTiles", list->Close())) {
    cleanup();
    return 1;
  }
  ID3D12CommandList *reserved_rt_lists[] = {list};
  queue->ExecuteCommandLists(1, reserved_rt_lists);
  if (!CheckHR("SignalReservedRenderTargetCopyTiles", queue->Signal(fence, 7)) ||
      !CheckHR("SetEventOnReservedRenderTargetCopyTiles", fence->SetEventOnCompletion(7, event))) {
    cleanup();
    return 1;
  }
  WaitForSingleObject(event, INFINITE);

  BYTE *mapped_copy_tiles_rt_texture_readback = nullptr;
  if (!CheckHR(
          "MapReservedRenderTargetCopyTiles",
          copy_tiles_rt_texture_readback->Map(
              0, nullptr, reinterpret_cast<void **>(&mapped_copy_tiles_rt_texture_readback)
          )
      )) {
    cleanup();
    return 1;
  }
  bool copy_tiles_rt_texture_matches = true;
  for (UINT i = 0; i < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; i++) {
    if (mapped_copy_tiles_rt_texture_readback[i] != static_cast<BYTE>(i ^ 0x5a)) {
      copy_tiles_rt_texture_matches = false;
      break;
    }
  }
  copy_tiles_rt_texture_readback->Unmap(0, nullptr);
  if (!copy_tiles_rt_texture_matches) {
    std::cerr << "reserved RT texture CopyTiles mapping mismatch\n";
    cleanup();
    return 1;
  }

  queue->UpdateTileMappings(
      reserved_resource_copy, 1, &tile_coordinate, &tile_region, reserved_heap, 1, nullptr, &heap_tile_offset,
      nullptr, D3D12_TILE_MAPPING_FLAG_NONE
  );
  UINT copy_tile_mappings_test_fence = 8;
  auto expect_copy_tile_mappings_no_mutation = [&](const char *name,
                                                    const D3D12_TILED_RESOURCE_COORDINATE *dst_coordinate,
                                                    const D3D12_TILED_RESOURCE_COORDINATE *src_coordinate,
                                                    const D3D12_TILE_REGION_SIZE *region_size,
                                                    D3D12_TILE_MAPPING_FLAGS flags) {
    queue->UpdateTileMappings(
        reserved_resource, 1, &tile_coordinate, &tile_region, nullptr, 1, &null_range_flags, nullptr, nullptr,
        D3D12_TILE_MAPPING_FLAG_NONE
    );
    queue->CopyTileMappings(
         reserved_resource, dst_coordinate, reserved_resource_copy, src_coordinate, region_size,
         flags
     );
    if (!CheckHR(name, list->Reset(allocator, nullptr))) {
      cleanup();
      return false;
    }
    list->CopyTiles(
        reserved_resource, &tile_coordinate, &tile_region, copy_tiles_readback,
        D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
        D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER
    );
    if (!CheckHR(name, list->Close())) {
      cleanup();
      return false;
    }
    ID3D12CommandList *copy_tile_mappings_lists[] = {list};
    queue->ExecuteCommandLists(1, copy_tile_mappings_lists);
    if (!CheckHR(name, queue->Signal(fence, copy_tile_mappings_test_fence)) ||
        !CheckHR(name, fence->SetEventOnCompletion(copy_tile_mappings_test_fence, event))) {
      cleanup();
      return false;
    }
    WaitForSingleObject(event, INFINITE);
    ++copy_tile_mappings_test_fence;

    BYTE *mapping_readback = nullptr;
    if (!CheckHR(name, copy_tiles_readback->Map(0, nullptr, reinterpret_cast<void **>(&mapping_readback)))) {
      cleanup();
      return false;
    }
    bool unchanged = true;
    for (UINT i = 0; i < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; i++) {
      if (mapping_readback[D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT + i] != 0) {
        unchanged = false;
        break;
      }
    }
    copy_tiles_readback->Unmap(0, nullptr);
    if (!unchanged) {
      std::cerr << name << " modified a resource mapping\n";
      cleanup();
      return false;
    }
    return true;
  };
  D3D12_TILED_RESOURCE_COORDINATE invalid_copy_tile_coordinate = tile_coordinate;
  invalid_copy_tile_coordinate.X = reserved_total_tile_count;
  D3D12_TILED_RESOURCE_COORDINATE last_copy_tile_coordinate = tile_coordinate;
  last_copy_tile_coordinate.X = reserved_total_tile_count - 1;
  D3D12_TILE_REGION_SIZE invalid_copy_tile_region = tile_region;
  invalid_copy_tile_region.NumTiles = 2;
  const auto invalid_copy_tile_flags = static_cast<D3D12_TILE_MAPPING_FLAGS>(1u << 1);
  if (!expect_copy_tile_mappings_no_mutation("null CopyTileMappings destination coordinate", nullptr, &tile_coordinate,
                                             &tile_region, D3D12_TILE_MAPPING_FLAG_NONE) ||
      !expect_copy_tile_mappings_no_mutation("null CopyTileMappings source coordinate", &tile_coordinate, nullptr,
                                             &tile_region, D3D12_TILE_MAPPING_FLAG_NONE) ||
      !expect_copy_tile_mappings_no_mutation("null CopyTileMappings region size", &tile_coordinate, &tile_coordinate,
                                             nullptr, D3D12_TILE_MAPPING_FLAG_NONE) ||
      !expect_copy_tile_mappings_no_mutation(
          "out-of-bounds CopyTileMappings destination", &invalid_copy_tile_coordinate, &tile_coordinate, &tile_region,
          D3D12_TILE_MAPPING_FLAG_NONE
      ) ||
      !expect_copy_tile_mappings_no_mutation(
          "out-of-bounds CopyTileMappings source", &tile_coordinate, &invalid_copy_tile_coordinate, &tile_region,
          D3D12_TILE_MAPPING_FLAG_NONE
      ) ||
      !expect_copy_tile_mappings_no_mutation(
          "out-of-bounds CopyTileMappings region", &last_copy_tile_coordinate, &last_copy_tile_coordinate,
          &invalid_copy_tile_region, D3D12_TILE_MAPPING_FLAG_NONE
      ) ||
      !expect_copy_tile_mappings_no_mutation(
          "invalid CopyTileMappings flags", &tile_coordinate, &tile_coordinate, &tile_region, invalid_copy_tile_flags
      ))
    return 1;

  if (!CheckHR("MapDepthReadback", depth_readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_depth)))) {
    cleanup();
    return 1;
  }
  UINT32 depth_bits = 0;
  std::memcpy(&depth_bits, mapped_depth + depth_footprints[0].Offset, sizeof(depth_bits));
  UINT8 stencil_value = *(mapped_depth + depth_footprints[1].Offset);
  depth_readback->Unmap(0, nullptr);
  if (depth_bits != 0x3e800000u || stencil_value != 0x5a) {
    std::cerr << "depth/stencil copy mismatch: depth=0x" << std::hex << depth_bits
              << " stencil=0x" << static_cast<unsigned>(stencil_value) << std::dec << "\n";
    cleanup();
    return 1;
  }

  if (!CheckHR(
          "MapArrayReadback",
          array_readback->Map(
              0, nullptr, reinterpret_cast<void **>(&mapped_array_readback)))) {
    cleanup();
    return 1;
  }
  UINT32 array_pixel = 0;
  std::memcpy(&array_pixel, mapped_array_readback + array_footprint.Offset,
              sizeof(array_pixel));
  if ((array_pixel & 0x00ffffffu) != 0x0000ff00u) {
    std::cerr << "array RTV readback mismatch: 0x" << std::hex << array_pixel
              << std::dec << "\n";
    array_readback->Unmap(0, nullptr);
    cleanup();
    return 1;
  }
  UINT32 array_offset_pixel = 0;
  std::memcpy(&array_offset_pixel,
              mapped_array_readback + array_footprint.Offset +
                  array_footprint.Footprint.RowPitch + sizeof(UINT32),
              sizeof(array_offset_pixel));
  if ((array_offset_pixel & 0x00ffffffu) != 0x000000ffu) {
    std::cerr << "buffer to array RTV copy mismatch: 0x" << std::hex
              << array_offset_pixel << std::dec << "\n";
    array_readback->Unmap(0, nullptr);
    cleanup();
    return 1;
  }
  array_readback->Unmap(0, nullptr);

  if (!CheckHR("MapArrayPartialReadback",
               array_partial_readback->Map(
                   0, nullptr,
                   reinterpret_cast<void **>(&mapped_array_partial_readback)))) {
    cleanup();
    return 1;
  }
  UINT32 array_partial_pixel = 0;
  std::memcpy(&array_partial_pixel, mapped_array_partial_readback +
                                      array_partial_footprint.Offset,
              sizeof(array_partial_pixel));
  UINT32 array_buffer_copy_pixel = 0;
  UINT32 array_buffer_copy_pixel_next_row = 0;
  std::memcpy(&array_buffer_copy_pixel,
              mapped_array_partial_readback + array_buffer_copy_dst_footprint.Offset + 2 * sizeof(UINT32),
              sizeof(array_buffer_copy_pixel));
  std::memcpy(&array_buffer_copy_pixel_next_row,
              mapped_array_partial_readback + array_buffer_copy_dst_footprint.Offset +
                  array_buffer_copy_dst_footprint.Footprint.RowPitch + 2 * sizeof(UINT32),
              sizeof(array_buffer_copy_pixel_next_row));
  array_partial_readback->Unmap(0, nullptr);
  if ((array_partial_pixel & 0x00ffffffu) != 0x0000ff00u) {
    std::cerr << "partial array RTV readback mismatch: 0x" << std::hex
              << array_partial_pixel << std::dec << "\n";
    cleanup();
    return 1;
  }
  if (array_buffer_copy_pixel != 0xff0000ffu || array_buffer_copy_pixel_next_row != 0xff0000ffu) {
    std::cerr << "buffer footprint copy mismatch: 0x" << std::hex << array_buffer_copy_pixel
              << ", 0x" << array_buffer_copy_pixel_next_row << std::dec << "\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("MapBCReadback", bc_readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_bc_readback)))) {
    cleanup();
    return 1;
  }
  bool bc_copy_matches = true;
  for (UINT row = 0; row < bc_rows; row++) {
    if (std::memcmp(mapped_bc_readback + bc_footprint.Offset + row * bc_footprint.Footprint.RowPitch,
                    bc_data + row * bc_row_size, static_cast<size_t>(bc_row_size)) != 0) {
      bc_copy_matches = false;
      break;
    }
  }
  bc_readback->Unmap(0, nullptr);
  if (!bc_copy_matches) {
    std::cerr << "BC1 buffer-to-texture-to-buffer copy mismatch\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("Map3DReadback",
               texture3d_readback->Map(
                   0, nullptr,
                   reinterpret_cast<void **>(&mapped_texture3d_readback)))) {
    cleanup();
    return 1;
  }
  UINT32 texture3d_pixel = 0;
  std::memcpy(&texture3d_pixel,
              mapped_texture3d_readback + texture3d_footprint.Offset +
                  texture3d_footprint.Footprint.RowPitch *
                      texture3d_footprint.Footprint.Height,
              sizeof(texture3d_pixel));
  texture3d_readback->Unmap(0, nullptr);
  if ((texture3d_pixel & 0x00ffffffu) != 0x000000ffu) {
    std::cerr << "3D RTV readback mismatch: 0x" << std::hex << texture3d_pixel
              << std::dec << "\n";
    cleanup();
    return 1;
  }

  if (!CheckHR("MapPlacedDestination", destination->Map(0, nullptr, &mapped))) {
    cleanup();
    return 1;
  }
  if (!mapped) {
    std::cerr << "MapPlacedDestination returned a null pointer\n";
    cleanup();
    return 1;
  }

  BYTE *mapped_copy_tiles_readback = nullptr;
  if (!CheckHR(
          "MapCopyTilesReadback",
          copy_tiles_readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_copy_tiles_readback))
      )) {
    cleanup();
    return 1;
  }
  bool copy_tiles_matches = true;
  for (UINT i = 0; i < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; i++) {
    if (mapped_copy_tiles_readback[i] != static_cast<BYTE>(i ^ 0x5a) ||
        mapped_copy_tiles_readback[D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT + i] != 0) {
      copy_tiles_matches = false;
      break;
    }
  }
  copy_tiles_readback->Unmap(0, nullptr);
  if (!copy_tiles_matches) {
    std::cerr << "reserved buffer CopyTiles mismatch\n";
    cleanup();
    return 1;
  }

  BYTE *mapped_copy_tiles_texture_readback = nullptr;
  if (!CheckHR(
          "MapCopyTilesTextureReadback",
          copy_tiles_texture_readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_copy_tiles_texture_readback))
      )) {
    cleanup();
    return 1;
  }
  bool copy_tiles_texture_matches = true;
  for (UINT i = 0; i < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; i++) {
    if (mapped_copy_tiles_texture_readback[i] != static_cast<BYTE>(i ^ 0xa5)) {
      copy_tiles_texture_matches = false;
      break;
    }
  }
  copy_tiles_texture_readback->Unmap(0, nullptr);
  if (!copy_tiles_texture_matches) {
    std::cerr << "reserved texture CopyTiles mismatch\n";
    cleanup();
    return 1;
  }

  BYTE *mapped_copy_tiles_mips_readback = nullptr;
  if (!CheckHR(
          "MapCopyTilesMipsReadback",
          copy_tiles_mips_readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_copy_tiles_mips_readback))
      )) {
    cleanup();
    return 1;
  }
  bool copy_tiles_mips_matches = true;
  for (UINT i = 0; i < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; i++) {
    if (mapped_copy_tiles_mips_readback[i] != static_cast<BYTE>(i ^ 0x5a) ||
        mapped_copy_tiles_mips_readback[D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT + i] != 0) {
      copy_tiles_mips_matches = false;
      break;
    }
  }
  copy_tiles_mips_readback->Unmap(0, nullptr);
  if (!copy_tiles_mips_matches) {
    std::cerr << "reserved texture multi-mip CopyTiles mismatch\n";
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

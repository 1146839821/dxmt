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
  ID3D12Heap *alias_heap = nullptr;
  ID3D12Resource *source_zero = nullptr;
  ID3D12Resource *source_placed = nullptr;
  ID3D12Resource *invalid_placed = nullptr;
  ID3D12Resource *destination = nullptr;
  ID3D12Resource *alias_before = nullptr;
  ID3D12Resource *alias_after = nullptr;
  ID3D12Resource *placed_texture = nullptr;
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
  ID3D12Device1 *device1 = nullptr;
  ID3D12Device4 *device4 = nullptr;
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
    if (device4)
      device4->Release();
    if (device1)
      device1->Release();
    if (info_queue)
      info_queue->Release();
    if (source_placed)
      source_placed->Release();
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
    if (alias_heap)
      alias_heap->Release();
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
  if (!CheckHR("QueryDevice1", device->QueryInterface(IID_PPV_ARGS(&device1)))) {
    cleanup();
    return 1;
  }
  if (!CheckHR("QueryDevice4", device->QueryInterface(IID_PPV_ARGS(&device4)))) {
    cleanup();
    return 1;
  }
  if (!CheckHR("QueryInfoQueue", device->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
    cleanup();
    return 1;
  }
  info_queue->SetMuteDebugOutput(TRUE);

  D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
  architecture.NodeIndex = 0;
  D3D12_FEATURE_DATA_ARCHITECTURE1 architecture1 = {};
  architecture1.NodeIndex = 0;
  D3D_FEATURE_LEVEL requested_levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1};
  D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels = {2, requested_levels, {}};
  D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {};
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
      !architecture1.UMA || feature_levels.MaxSupportedFeatureLevel != D3D_FEATURE_LEVEL_11_0 ||
      shader_model.HighestShaderModel != D3D_SHADER_MODEL_6_0 || options.ResourceBindingTier != D3D12_RESOURCE_BINDING_TIER_2 ||
      options.ResourceHeapTier != D3D12_RESOURCE_HEAP_TIER_2 || options.ROVsSupported || options1.WaveOps ||
      options3.CopyQueueTimestampQueriesSupported != TRUE || options5.RenderPassesTier != D3D12_RENDER_PASS_TIER_0 ||
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
  D3D12_RESOURCE_ALLOCATION_INFO texture_info = device->GetResourceAllocationInfo(0, 1, &texture_desc);
  if (!texture_info.SizeInBytes || texture_info.SizeInBytes == UINT64_MAX ||
      texture_info.Alignment < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
    std::cerr << "invalid texture allocation info: size=" << texture_info.SizeInBytes
              << " alignment=" << texture_info.Alignment << "\n";
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
  if (!depth_total || !depth_footprints[1].Offset || !depth_row_sizes[0] || !depth_row_sizes[1]) {
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
  texture3d_desc.Height = 1;
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
  if (!CheckHR("CreatePlacedTexture",
                device->CreatePlacedResource(texture_heap, 0, &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&placed_texture)))) {
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

  if (device->CreatePlacedResource(upload_heap, buffer_info.Alignment / 2, &buffer_desc,
                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&invalid_placed)) !=
          E_INVALIDARG ||
      invalid_placed != nullptr) {
    std::cerr << "misaligned placed resource offset was accepted\n";
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
  UINT64 any_values[] = {2, 3};
  if (!CheckHR("SetEventOnAnyFenceCompletion",
               device1->SetEventOnMultipleFenceCompletion(
                   multiple_fences, any_values, 2,
                   D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, multiple_event)) ||
      WaitForSingleObject(multiple_event, 0) != WAIT_TIMEOUT ||
      !CheckHR("SignalAnyFence", queue->Signal(multiple_fence, 3)) ||
      WaitForSingleObject(multiple_event, 5000) != WAIT_OBJECT_0) {
    std::cerr << "WAIT_ANY multiple fence completion failed\n";
    cleanup();
    return 1;
  }

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

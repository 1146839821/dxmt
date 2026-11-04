#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr UINT TileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT ValueA = 0x11112222;
constexpr UINT ValueB = 0x33334444;

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

D3D12_RESOURCE_DESC PackedTextureDesc(bool copy_upload) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  // Keep the whole resource in the packed tail. This makes mip 1 a direct
  // packed-mip shader access while avoiding an assumed D3D/Metal tile index.
  desc.Width = 64;
  desc.Height = 64;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 4;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  desc.Flags = copy_upload ? D3D12_RESOURCE_FLAG_NONE
                           : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
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
  Check("SetEventOnCompletion", fence->SetEventOnCompletion(value, event));
  const DWORD status = WaitForSingleObject(event, 30000);
  CloseHandle(event);
  if (status != WAIT_OBJECT_0)
    throw std::runtime_error("GPU completion timeout");
}

void CreateBuffer(ID3D12Device *device, Owned<ID3D12Resource> &resource, UINT64 bytes, D3D12_HEAP_TYPE type,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
  const auto properties = Properties(type);
  const auto desc = BufferDesc(bytes, flags);
  Check("CreateBuffer", device->CreateCommittedResource(
                            &properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource.ptr)));
}

// Fault modes deliberately avoid shader access after a failed mapping. The
// private native test event drains submitted work before terminal cleanup.
int CheckMappingFailure(const char *layout, bool unmap, bool copy) {
  const char *phase =
      std::getenv(copy ? "DXMT_TEST_SPARSE_TEXTURE_COPY_FAILURE"
                       : "DXMT_TEST_SPARSE_TEXTURE_UPDATE_FAILURE");
  const char *at =
      std::getenv(copy ? "DXMT_TEST_SPARSE_TEXTURE_COPY_FAIL_AT"
                       : "DXMT_TEST_SPARSE_TEXTURE_UPDATE_FAIL_AT");
  if (!phase ||
      (std::strcmp(phase, "prepare") && std::strcmp(phase, "allocation") &&
       std::strcmp(phase, "submitted") &&
       (!copy || std::strcmp(phase, "snapshot"))) ||
      (at && std::strcmp(at, "1") && std::strcmp(at, "2")))
    throw std::runtime_error(
        "fault mode requires an exact phase and FAIL_AT=1|2");
  const bool second = at && !std::strcmp(at, "2");
  for (const auto &entry : std::array<std::pair<const char *, const char *>, 2>{
           {{"d3d12.dll", "D3D12CreateDevice"},
            {"winemetal.dll",
             copy ? "SparseMappingQueue_tryCopyTextureMappings"
                  : "SparseMappingQueue_tryUpdateTextureMappings"}}}) {
    const auto module = GetModuleHandleA(entry.first);
    char path[2048] = {};
    const auto code = reinterpret_cast<const unsigned char *>(
        GetProcAddress(module, entry.second));
    if (!module || !code || !GetModuleFileNameA(module, path, sizeof(path)))
      throw std::runtime_error("missing runtime identity");
    std::cout << "LOADED_MODULE " << entry.first << " " << path << "\n";
    std::cout << "LOADED_CODE " << entry.first << " " << entry.second << " ";
    for (UINT i = 0; i < 16; ++i)
      std::printf("%02x", code[i]);
    std::cout << "\n";
  }
  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12Fence> fence;
  Owned<ID3D12Resource> texture, destination;
  Owned<ID3D12Heap> heap_a, heap_b;
  Check("Create fault device",
        D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                          IID_PPV_ARGS(&device.ptr)));
  D3D12_COMMAND_QUEUE_DESC qdesc = {};
  Check("Create fault queue",
        device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue.ptr)));
  Check("Create fault fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                  IID_PPV_ARGS(&fence.ptr)));
  auto desc = PackedTextureDesc(std::strcmp(layout, "mixed") != 0);
  if (!std::strcmp(layout, "standard")) {
    desc.Width = desc.Height = 256;
    desc.MipLevels = 1;
  }
  Check("Create fault reserved texture",
        device->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_COMMON,
                                       nullptr, IID_PPV_ARGS(&texture.ptr)));
  if (copy)
    Check("Create copy destination",
          device->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_COMMON,
                                         nullptr,
                                         IID_PPV_ARGS(&destination.ptr)));
  UINT total = 0;
  D3D12_PACKED_MIP_INFO packed = {};
  D3D12_TILE_SHAPE shape = {};
  device->GetResourceTiling(texture.ptr, &total, &packed, &shape, nullptr, 0,
                            nullptr);
  std::cout << "FAULT_TILING layout=" << layout << " total=" << total
            << " standard_mips=" << UINT(packed.NumStandardMips)
            << " packed_mips=" << UINT(packed.NumPackedMips)
            << " packed_tiles=" << packed.NumTilesForPackedMips << "\n";
  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  D3D12_TILE_REGION_SIZE region = {};
  if (!std::strcmp(layout, "standard")) {
    if (packed.NumPackedMips || total < 2)
      throw std::runtime_error(
          "standard fault fixture needs two standard tiles");
    region.NumTiles = 2;
  } else if (!std::strcmp(layout, "packed")) {
    if (!packed.NumPackedMips || !packed.NumTilesForPackedMips)
      throw std::runtime_error(
          "packed fault fixture needs a real complete tail");
    coordinate.Subresource = packed.NumStandardMips;
    region.NumTiles = packed.NumTilesForPackedMips;
  } else {
    if (!packed.NumStandardMips || !packed.NumPackedMips)
      throw std::runtime_error(
          "mixed fault fixture needs standard tiles and a tail");
    region.NumTiles = total;
  }
  D3D12_HEAP_DESC hdesc = {};
  hdesc.SizeInBytes = UINT64(total) * TileBytes;
  hdesc.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  hdesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  Check("Create fault heap A",
        device->CreateHeap(&hdesc, IID_PPV_ARGS(&heap_a.ptr)));
  Check("Create fault heap B",
        device->CreateHeap(&hdesc, IID_PPV_ARGS(&heap_b.ptr)));
  const UINT offset = 0;
  auto update = [&](ID3D12Heap *heap, D3D12_TILE_RANGE_FLAGS range,
                    D3D12_TILE_MAPPING_FLAGS flags) {
    queue->UpdateTileMappings(texture.ptr, 1, &coordinate, &region, heap, 1,
                              &range, &offset, nullptr, flags);
  };
  UINT64 serial = 0;
  const auto drain = [&] {
    Check("fault drain Signal", queue->Signal(fence.ptr, ++serial));
    Wait(fence.ptr, serial);
  };
  if (copy) {
    update(unmap ? nullptr : heap_a.ptr,
           unmap ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE,
           D3D12_TILE_MAPPING_FLAG_NONE);
    const auto mapped = D3D12_TILE_RANGE_FLAG_NONE;
    queue->UpdateTileMappings(destination.ptr, 1, &coordinate, &region,
                              heap_b.ptr, 1, &mapped, &offset, nullptr,
                              D3D12_TILE_MAPPING_FLAG_NONE);
    Check("copy setup healthy", device->GetDeviceRemovedReason());
    drain();
  }
  const auto operate = [&](D3D12_TILE_MAPPING_FLAGS flags) {
    if (copy)
      queue->CopyTileMappings(destination.ptr, &coordinate, texture.ptr,
                              &coordinate, &region, flags);
    else
      update(unmap ? nullptr : heap_b.ptr,
             unmap ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE,
             flags);
  };
  operate(static_cast<D3D12_TILE_MAPPING_FLAGS>(0x80000000u));
  Check("invalid flags remain nonterminal", device->GetDeviceRemovedReason());
  if (copy) {
    const D3D12_TILE_REGION_SIZE empty = {};
    queue->CopyTileMappings(destination.ptr, &coordinate, texture.ptr,
                            &coordinate, &empty, D3D12_TILE_MAPPING_FLAG_NONE);
    Check("empty copy remains nonterminal", device->GetDeviceRemovedReason());
    std::cout << "fault preflight invalid/empty passed\n";
  } else {
    update(nullptr, D3D12_TILE_RANGE_FLAG_SKIP, D3D12_TILE_MAPPING_FLAG_NONE);
    Check("SKIP remains nonterminal", device->GetDeviceRemovedReason());
    std::cout << "fault preflight invalid/SKIP passed\n";
  }
  if (second) {
    if (copy)
      operate(D3D12_TILE_MAPPING_FLAG_NONE);
    else
      update(heap_a.ptr, D3D12_TILE_RANGE_FLAG_NONE,
             D3D12_TILE_MAPPING_FLAG_NONE);
    Check("first operation stays healthy", device->GetDeviceRemovedReason());
    drain();
  }
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    throw std::runtime_error("fault CreateEvent");
  Check("pending removal fence", fence->SetEventOnCompletion(100, event));
  operate(D3D12_TILE_MAPPING_FLAG_NONE);
  const HRESULT reason = device->GetDeviceRemovedReason();
  if (SUCCEEDED(reason)) {
    // Safe ignore-result control: drain; never read inconsistent mappings.
    drain();
    CloseHandle(event);
    throw std::runtime_error(copy ? "missing terminal texture copy failure"
                                  : "missing terminal texture update failure");
  }
  const DWORD wake = WaitForSingleObject(event, 30000);
  CloseHandle(event);
  const HRESULT expected =
      (!std::strcmp(phase, "prepare") || !std::strcmp(phase, "snapshot"))
          ? E_OUTOFMEMORY
          : DXGI_ERROR_DEVICE_REMOVED;
  const auto completed = fence->GetCompletedValue();
  const HRESULT later_signal = queue->Signal(fence.ptr, 3);
  if (reason != expected || wake != WAIT_OBJECT_0 || completed != UINT64_MAX ||
      later_signal != DXGI_ERROR_DEVICE_REMOVED)
    throw std::runtime_error("wrong texture removal/fence/Signal contract");
  std::cout << (copy ? "Texture copy fault contract passed phase="
                     : "Texture update fault contract passed phase=")
            << phase << " at=" << (second ? 2 : 1) << " layout=" << layout
            << " unmap=" << unmap << " reason=0x" << std::hex
            << static_cast<unsigned long>(reason) << std::dec << "\n";
  return 0;
}

int Run(const char *read_path, const char *write_a_path,
        const char *write_b_path, bool copy_upload, bool full_mips,
        bool metadata_only, bool copy_mappings, bool bad_alias_oracle,
        bool perf_update, bool perf_copy) {
  const auto read_shader = ReadShader(read_path);
  const auto write_a_shader = ReadShader(write_a_path);
  const auto write_b_shader = ReadShader(write_b_path);

  Owned<ID3D12Device> device;
  Check("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)));

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Owned<ID3D12CommandQueue> queue;
  Check("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue.ptr)));

  Owned<ID3D12Fence> fence;
  Check("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.ptr)));

  Owned<ID3D12Resource> texture;
  auto texture_desc = PackedTextureDesc(copy_upload);
  Owned<ID3D12Resource> explicit_texture;
  if (full_mips) {
    texture_desc.MipLevels = 7;
    const HRESULT explicit_hr = device->CreateReservedResource(
        &texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&explicit_texture.ptr));
    if (explicit_hr == E_NOTIMPL || explicit_hr == E_NOINTERFACE || explicit_hr == DXGI_ERROR_UNSUPPORTED) {
      std::cout << "SKIP: explicit full-chain reserved texture is unavailable\n";
      return 77;
    }
    Check("Create explicit full-chain reserved texture", explicit_hr);
    texture_desc.MipLevels = 0;
    const HRESULT probe = device->CreateReservedResource(
        &texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), nullptr);
    if (probe != S_FALSE)
      throw std::runtime_error("implicit full-chain capability probe did not return S_FALSE");
  }
  const HRESULT reserved_hr = device->CreateReservedResource(
      &texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture.ptr));
  if (!full_mips && FAILED(reserved_hr) &&
      (reserved_hr == E_NOTIMPL || reserved_hr == E_NOINTERFACE || reserved_hr == DXGI_ERROR_UNSUPPORTED)) {
    std::cout << "SKIP: packed reserved texture is unavailable\n";
    return 77;
  }
  Check("CreateReservedResource", reserved_hr);
  if (full_mips) {
    const auto actual_desc = texture->GetDesc();
    if (actual_desc.MipLevels != 7)
      throw std::runtime_error("implicit full-chain GetDesc mip count mismatch");
    UINT implicit_total = 0, explicit_total = 0, implicit_count = 7, explicit_count = 7;
    D3D12_PACKED_MIP_INFO implicit_packed = {}, explicit_packed = {};
    D3D12_TILE_SHAPE implicit_shape = {}, explicit_shape = {};
    std::array<D3D12_SUBRESOURCE_TILING, 7> implicit_tilings = {}, explicit_tilings = {};
    device->GetResourceTiling(texture.ptr, &implicit_total, &implicit_packed, &implicit_shape,
                              &implicit_count, 0, implicit_tilings.data());
    device->GetResourceTiling(explicit_texture.ptr, &explicit_total, &explicit_packed, &explicit_shape,
                              &explicit_count, 0, explicit_tilings.data());
    if (implicit_total != explicit_total || implicit_count != explicit_count || implicit_count != 7 ||
        implicit_packed.NumStandardMips != explicit_packed.NumStandardMips ||
        implicit_packed.NumPackedMips != explicit_packed.NumPackedMips ||
        implicit_packed.NumTilesForPackedMips != explicit_packed.NumTilesForPackedMips ||
        implicit_packed.StartTileIndexInOverallResource != explicit_packed.StartTileIndexInOverallResource ||
        implicit_shape.WidthInTexels != explicit_shape.WidthInTexels ||
        implicit_shape.HeightInTexels != explicit_shape.HeightInTexels ||
        implicit_shape.DepthInTexels != explicit_shape.DepthInTexels)
      throw std::runtime_error("implicit full-chain tiling mismatch");
    for (UINT i = 0; i < implicit_count; ++i) {
      const auto &a = implicit_tilings[i];
      const auto &b = explicit_tilings[i];
      if (a.WidthInTiles != b.WidthInTiles || a.HeightInTiles != b.HeightInTiles ||
          a.DepthInTiles != b.DepthInTiles || a.StartTileIndexInOverallResource != b.StartTileIndexInOverallResource)
        throw std::runtime_error("implicit full-chain subresource tiling mismatch");
    }
    auto single_desc = texture_desc;
    single_desc.Width = single_desc.Height = 1;
    Owned<ID3D12Resource> single_mip;
    Check("Create single-mip implicit chain", device->CreateReservedResource(
        &single_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&single_mip.ptr)));
    if (single_mip->GetDesc().MipLevels != 1)
      throw std::runtime_error("single-texel implicit mip count mismatch");
    if (metadata_only) {
      std::cout << "implicit and explicit full-chain packed tiling match\n";
      return 0;
    }
    texture_desc.MipLevels = actual_desc.MipLevels;
  }

  UINT total_tiles = 0;
  D3D12_PACKED_MIP_INFO packed_info = {};
  D3D12_TILE_SHAPE tile_shape = {};
  UINT subresource_count = texture_desc.MipLevels;
  std::vector<D3D12_SUBRESOURCE_TILING> tilings(subresource_count);
  device->GetResourceTiling(texture.ptr, &total_tiles, &packed_info, &tile_shape, &subresource_count, 0,
                            tilings.data());
  std::cout << "texture tiling: total=" << total_tiles
            << " standard_mips=" << static_cast<unsigned>(packed_info.NumStandardMips)
            << " packed_mips=" << static_cast<unsigned>(packed_info.NumPackedMips)
            << " packed_tiles=" << packed_info.NumTilesForPackedMips << "\n";
  if (!packed_info.NumPackedMips || packed_info.NumPackedMips + packed_info.NumStandardMips != texture_desc.MipLevels ||
      packed_info.NumTilesForPackedMips != 1 ||
      packed_info.StartTileIndexInOverallResource != packed_info.NumStandardMips ||
      total_tiles != packed_info.NumStandardMips + packed_info.NumTilesForPackedMips ||
      subresource_count != texture_desc.MipLevels) {
    throw std::runtime_error("packed texture tiling matrix mismatch");
  }
  for (UINT mip = 0; mip < texture_desc.MipLevels; mip++) {
    if (mip < packed_info.NumStandardMips) {
      if (!tile_shape.WidthInTexels || !tile_shape.HeightInTexels || tile_shape.DepthInTexels != 1 ||
          tilings[mip].WidthInTiles != 1 || tilings[mip].HeightInTiles != 1 || tilings[mip].DepthInTiles != 1 ||
          tilings[mip].StartTileIndexInOverallResource != mip)
        throw std::runtime_error("standard texture subresource tiling mismatch");
      continue;
    }
    if (tilings[mip].WidthInTiles || tilings[mip].HeightInTiles || tilings[mip].DepthInTiles ||
        tilings[mip].StartTileIndexInOverallResource != ~static_cast<UINT>(0))
      throw std::runtime_error("packed texture subresource tiling mismatch");
  }

  Owned<ID3D12Resource> output;
  Owned<ID3D12Resource> readback;
  CreateBuffer(device.ptr, output, 2 * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
  CreateBuffer(device.ptr, readback, 2 * sizeof(UINT), D3D12_HEAP_TYPE_READBACK,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

  // CopyTextureRegion can populate a packed mip without requiring a UAV-capable
  // texture. Shader-write usage can change the native sparse-tail boundary.
  constexpr UINT upload_row_pitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
  constexpr UINT upload_height = 32;
  constexpr UINT upload_size = upload_row_pitch * upload_height;
  Owned<ID3D12Resource> upload;
  if (copy_upload) {
    CreateBuffer(device.ptr, upload, 2 * upload_size, D3D12_HEAP_TYPE_UPLOAD,
                 D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    UINT *mapped = nullptr;
    Check("MapPackedUpload",
          upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
    for (UINT i = 0; i < 2 * upload_size / sizeof(UINT); ++i)
      mapped[i] = i < upload_size / sizeof(UINT) ? ValueA : ValueB;
    upload->Unmap(0, nullptr);
  }

  Owned<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc = {};
  descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_desc.NumDescriptors = 3;
  descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check("CreateDescriptorHeap", device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&descriptors.ptr)));
  const UINT descriptor_stride = device->GetDescriptorHandleIncrementSize(descriptor_desc.Type);
  auto descriptor_cpu = descriptors->GetCPUDescriptorHandleForHeapStart();

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = texture_desc.MipLevels;
  device->CreateShaderResourceView(texture.ptr, &srv, descriptor_cpu);

  D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
  output_uav.Format = DXGI_FORMAT_UNKNOWN;
  output_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  output_uav.Buffer.NumElements = 2;
  output_uav.Buffer.StructureByteStride = sizeof(UINT);
  descriptor_cpu.ptr += descriptor_stride;
  device->CreateUnorderedAccessView(output.ptr, nullptr, &output_uav, descriptor_cpu);

  D3D12_UNORDERED_ACCESS_VIEW_DESC tail_uav = {};
  tail_uav.Format = DXGI_FORMAT_R32_UINT;
  tail_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  tail_uav.Texture2D.MipSlice = 1;
  descriptor_cpu.ptr += descriptor_stride;
  device->CreateUnorderedAccessView(copy_upload ? nullptr : texture.ptr,
                                    nullptr, &tail_uav, descriptor_cpu);

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 1};
  D3D12_ROOT_PARAMETER root_parameter = {};
  root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameter.DescriptorTable.NumDescriptorRanges = 2;
  root_parameter.DescriptorTable.pDescriptorRanges = ranges;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &root_parameter;
  Owned<ID3DBlob> root_blob;
  Owned<ID3DBlob> root_errors;
  Check("D3D12SerializeRootSignature", D3D12SerializeRootSignature(
                                             &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob.ptr,
                                             &root_errors.ptr));
  Owned<ID3D12RootSignature> root;
  Check("CreateRootSignature", device->CreateRootSignature(
                                    0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                    IID_PPV_ARGS(&root.ptr)));

  Owned<ID3D12PipelineState> read_pso;
  Owned<ID3D12PipelineState> write_a_pso;
  Owned<ID3D12PipelineState> write_b_pso;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.ptr;
  pipeline_desc.CS = {read_shader.data(), read_shader.size()};
  Check("CreateReadPSO", device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&read_pso.ptr)));
  if (!copy_upload) {
    pipeline_desc.CS = {write_a_shader.data(), write_a_shader.size()};
    Check("CreateWriteAPSO",
          device->CreateComputePipelineState(&pipeline_desc,
                                             IID_PPV_ARGS(&write_a_pso.ptr)));
    pipeline_desc.CS = {write_b_shader.data(), write_b_shader.size()};
    Check("CreateWriteBPSO",
          device->CreateComputePipelineState(&pipeline_desc,
                                             IID_PPV_ARGS(&write_b_pso.ptr)));
  }

  Owned<ID3D12Heap> heap_a;
  Owned<ID3D12Heap> heap_b;
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = UINT64(total_tiles) * TileBytes;
  heap_desc.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  Check("CreatePackedHeapA", device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap_a.ptr)));
  Check("CreatePackedHeapB", device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap_b.ptr)));
  const ULONG heap_a_public_ref_after_create = heap_a->AddRef();
  heap_a->Release();

  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  coordinate.Subresource = 0;
  D3D12_TILE_REGION_SIZE region = {};
  region.NumTiles = total_tiles;
  UINT heap_tile = 0;

  auto map_tail = [&](ID3D12Heap *heap) {
    const D3D12_TILE_RANGE_FLAGS range_flag =
        heap ? D3D12_TILE_RANGE_FLAG_NONE : D3D12_TILE_RANGE_FLAG_NULL;
    queue->UpdateTileMappings(texture.ptr, 1, &coordinate, &region, heap, 1,
                              &range_flag, &heap_tile, nullptr,
                              D3D12_TILE_MAPPING_FLAG_NONE);
  };

  if (perf_update) {
    // Separate completion fence: normal shader phases retain their own serials.
    Owned<ID3D12Fence> perf_fence;
    Check("Create update timing fence",
          device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                              IID_PPV_ARGS(&perf_fence.ptr)));
    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);
    constexpr UINT Calls = 64;
    for (UINT sample = 0; sample < 9; ++sample) {
      LARGE_INTEGER begin = {}, end = {};
      QueryPerformanceCounter(&begin);
      for (UINT call = 0; call < Calls; ++call)
        map_tail((call & 1) ? heap_a.ptr : heap_b.ptr);
      QueryPerformanceCounter(&end);
      Check("timing device", device->GetDeviceRemovedReason());
      Check("timing Signal", queue->Signal(perf_fence.ptr, sample + 1));
      Wait(perf_fence.ptr, sample + 1);
      std::cout << "PERF_TEXTURE_UPDATE sample=" << sample << " calls=" << Calls
                << " us_per_call="
                << double(end.QuadPart - begin.QuadPart) * 1e6 /
                       frequency.QuadPart / Calls
                << "\n";
    }
  }

  // Create both views before the first mapping. The physical mapping must not
  // alter the descriptor's virtual resource identity.
  map_tail(heap_a.ptr);
  if (packed_info.NumStandardMips) {
    Owned<ID3D12Resource> tile_upload, tile_readback;
    CreateBuffer(device.ptr, tile_upload, TileBytes, D3D12_HEAP_TYPE_UPLOAD,
                 D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    CreateBuffer(device.ptr, tile_readback, TileBytes, D3D12_HEAP_TYPE_READBACK,
                 D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    UINT *data = nullptr;
    Check("Map standard tile upload", tile_upload->Map(0, nullptr, reinterpret_cast<void **>(&data)));
    for (UINT i = 0; i < TileBytes / sizeof(UINT); ++i)
      data[i] = 0x76543210u + i;
    tile_upload->Unmap(0, nullptr);

    Owned<ID3D12CommandAllocator> tile_allocator;
    Owned<ID3D12GraphicsCommandList> tile_list;
    Check("Create tile allocator", device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tile_allocator.ptr)));
    Check("Create tile list", device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, tile_allocator.ptr, nullptr, IID_PPV_ARGS(&tile_list.ptr)));
    D3D12_TILE_REGION_SIZE single_tile = {};
    single_tile.NumTiles = 1;
    Transition(tile_list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    tile_list->CopyTiles(texture.ptr, &coordinate, &single_tile, tile_upload.ptr, 0,
                         D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
    Transition(tile_list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    tile_list->CopyTiles(texture.ptr, &coordinate, &single_tile, tile_readback.ptr, 0,
                         D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
    Transition(tile_list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    Check("Close standard tile list", tile_list->Close());
    ID3D12CommandList *tile_lists[] = {tile_list.ptr};
    queue->ExecuteCommandLists(1, tile_lists);
    Owned<ID3D12Fence> tile_fence;
    Check("Create standard tile fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tile_fence.ptr)));
    Check("Signal standard tile fence", queue->Signal(tile_fence.ptr, 1));
    Wait(tile_fence.ptr, 1);
    Check("Map standard tile readback", tile_readback->Map(0, nullptr, reinterpret_cast<void **>(&data)));
    bool valid = true;
    for (UINT y = 0; y < 64; ++y)
      for (UINT x = 0; x < 64; ++x) {
        const UINT index = y * tile_shape.WidthInTexels + x;
        valid &= data[index] == 0x76543210u + index;
      }
    tile_readback->Unmap(0, nullptr);
    if (!valid)
      throw std::runtime_error("small standard mip CopyTiles readback mismatch");
    std::cout << "small standard mip CopyTiles readback passed\n";
  }
  const ULONG heap_a_public_ref_after_mapping = heap_a->AddRef();
  heap_a->Release();
  if (heap_a_public_ref_after_mapping != heap_a_public_ref_after_create) {
    std::cerr << "packed texture mapping changed heap A public ref count from "
              << heap_a_public_ref_after_create - 1 << " to " << heap_a_public_ref_after_mapping - 1 << "\n";
    throw std::runtime_error("packed texture mapping retained a public heap reference");
  }
  std::cout << "packed texture mapping kept heap A public ref count at "
            << heap_a_public_ref_after_mapping - 1 << "\n";

  auto submit = [&](const char *name, ID3D12PipelineState *pso, bool read, UINT expected) -> bool {
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Check("CreateCommandAllocator", device->CreateCommandAllocator(
                                        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr)));
    Check("CreateCommandList", device->CreateCommandList(
                                  0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, pso,
                                  IID_PPV_ARGS(&list.ptr)));
    list->SetComputeRootSignature(root.ptr);
    ID3D12DescriptorHeap *bound_heaps[] = {descriptors.ptr};
    list->SetDescriptorHeaps(1, bound_heaps);
    list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
    if (read) {
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON,
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->Dispatch(1, 1, 1);
      Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, 2 * sizeof(UINT));
      Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATE_COMMON);
    } else if (copy_upload) {
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON,
                 D3D12_RESOURCE_STATE_COPY_DEST);
      D3D12_TEXTURE_COPY_LOCATION source = {};
      source.pResource = upload.ptr;
      source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      source.PlacedFootprint.Offset = expected == ValueA ? 0 : upload_size;
      source.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_UINT, 32,
                                          upload_height, 1, upload_row_pitch};
      D3D12_TEXTURE_COPY_LOCATION destination = {};
      destination.pResource = texture.ptr;
      destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      destination.SubresourceIndex = 1;
      list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COPY_DEST,
                 D3D12_RESOURCE_STATE_COMMON);
    } else {
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->Dispatch(1, 1, 1);
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    }
    Check("Close", list->Close());
    ID3D12CommandList *lists[] = {list.ptr};
    queue->ExecuteCommandLists(1, lists);
    static UINT64 fence_value = 0;
    ++fence_value;
    Check("Signal", queue->Signal(fence.ptr, fence_value));
    Wait(fence.ptr, fence_value);

    if (read) {
      UINT *mapped = nullptr;
      Check("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
      const UINT first = mapped[0];
      const UINT last = mapped[1];
      readback->Unmap(0, nullptr);
      if (first != expected || last != expected) {
        std::cerr << name << " expected 0x" << std::hex << expected << "/" << expected
                  << ", got 0x" << first << "/" << last << std::dec << "\n";
        return false;
      }
    }
    if (read)
      std::cout << name << " passed\n";
    return true;
  };

  if (!submit("packed tail write on heap A", write_a_pso.ptr, false, ValueA) ||
      !submit("packed tail read on heap A", read_pso.ptr, true, ValueA)) {
    throw std::runtime_error("packed tail heap A GPU result mismatch");
  }

  // A reserved resource does not retain one application reference for each
  // mapped heap. Keep heap B alive until the remap and all GPU work that uses
  // it have completed; releasing the final heap reference earlier is invalid
  // D3D12 usage and can remove the device on native hardware.
  map_tail(heap_b.ptr);
  if (!submit("packed tail write on heap B", write_b_pso.ptr, false, ValueB) ||
      !submit("packed tail read on heap B", read_pso.ptr, true, ValueB)) {
    throw std::runtime_error("packed tail heap B GPU result mismatch");
  }
  map_tail(nullptr);
  if (!submit("unmapped packed tail reads zero", read_pso.ptr, true, 0))
    throw std::runtime_error("unmapped packed tail GPU result mismatch");
  if (!copy_mappings) {
    heap_b.ptr->Release();
    heap_b.ptr = nullptr;
  }
  map_tail(heap_a.ptr);
  if (!submit("packed tail remap to heap A preserves data", read_pso.ptr, true, ValueA)) {
    throw std::runtime_error("packed tail remap GPU result mismatch");
  }
  if (copy_mappings) {
    Owned<ID3D12Resource> other;
    Check("Create matching alias texture", device->CreateReservedResource(
        &texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&other.ptr)));
    if (perf_copy) {
      Owned<ID3D12Fence> timing_fence;
      Check("Create copy timing fence",
            device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                IID_PPV_ARGS(&timing_fence.ptr)));
      LARGE_INTEGER frequency = {};
      QueryPerformanceFrequency(&frequency);
      constexpr UINT Calls = 64;
      for (UINT sample = 0; sample < 9; ++sample) {
        LARGE_INTEGER begin = {}, end = {};
        QueryPerformanceCounter(&begin);
        for (UINT call = 0; call < Calls; ++call)
          queue->CopyTileMappings(other.ptr, &coordinate, texture.ptr,
                                  &coordinate, &region,
                                  D3D12_TILE_MAPPING_FLAG_NONE);
        QueryPerformanceCounter(&end);
        Check("copy timing device", device->GetDeviceRemovedReason());
        Check("copy timing Signal",
              queue->Signal(timing_fence.ptr, sample + 1));
        Wait(timing_fence.ptr, sample + 1);
        std::cout << "PERF_TEXTURE_COPY sample=" << sample << " calls=" << Calls
                  << " us_per_call="
                  << double(end.QuadPart - begin.QuadPart) * 1e6 /
                         frequency.QuadPart / Calls
                  << "\n";
      }
    }
    queue->CopyTileMappings(other.ptr, &coordinate, texture.ptr, &coordinate, &region, D3D12_TILE_MAPPING_FLAG_NONE);

    const auto switch_alias = [&](ID3D12Resource *before, ID3D12Resource *after, bool update_views = true) {
      Owned<ID3D12CommandAllocator> alias_allocator;
      Owned<ID3D12GraphicsCommandList> alias_list;
      Check("Create alias allocator", device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alias_allocator.ptr)));
      Check("Create alias list", device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, alias_allocator.ptr, nullptr, IID_PPV_ARGS(&alias_list.ptr)));
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
      barrier.Aliasing = {before, after};
      alias_list->ResourceBarrier(1, &barrier);
      Check("Close alias list", alias_list->Close());
      ID3D12CommandList *lists[] = {alias_list.ptr};
      queue->ExecuteCommandLists(1, lists);
      Owned<ID3D12Fence> alias_fence;
      Check("Create alias fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&alias_fence.ptr)));
      Check("Signal alias fence", queue->Signal(alias_fence.ptr, 1));
      Wait(alias_fence.ptr, 1);
      Check("Alias device status", device->GetDeviceRemovedReason());
      if (update_views) {
        std::swap(texture.ptr, other.ptr);
        auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
        device->CreateShaderResourceView(texture.ptr, &srv, cpu);
        cpu.ptr += 2 * descriptor_stride;
        device->CreateUnorderedAccessView(copy_upload ? nullptr : texture.ptr, nullptr, &tail_uav, cpu);
      }
    };

    if (!copy_upload) {
      // Exercise the barrier within one command list: no CPU fence separates
      // the source write and the destination read.
      Owned<ID3D12DescriptorHeap> alias_descriptors;
      Check("Create alias descriptors", device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&alias_descriptors.ptr)));
      auto cpu = alias_descriptors->GetCPUDescriptorHandleForHeapStart();
      device->CreateShaderResourceView(other.ptr, &srv, cpu);
      cpu.ptr += descriptor_stride;
      device->CreateUnorderedAccessView(output.ptr, nullptr, &output_uav, cpu);
      cpu.ptr += descriptor_stride;
      device->CreateUnorderedAccessView(other.ptr, nullptr, &tail_uav, cpu);

      Owned<ID3D12CommandAllocator> allocator;
      Owned<ID3D12GraphicsCommandList> list;
      Check("Create inline alias allocator", device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr)));
      Check("Create inline alias list", device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, write_b_pso.ptr, IID_PPV_ARGS(&list.ptr)));
      list->SetComputeRootSignature(root.ptr);
      ID3D12DescriptorHeap *source_heaps[] = {descriptors.ptr};
      list->SetDescriptorHeaps(1, source_heaps);
      list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->Dispatch(1, 1, 1);
      Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
      D3D12_RESOURCE_BARRIER alias = {};
      alias.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
      alias.Aliasing = {texture.ptr, other.ptr};
      list->ResourceBarrier(1, &alias);
      Transition(list.ptr, other.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      ID3D12DescriptorHeap *destination_heaps[] = {alias_descriptors.ptr};
      list->SetDescriptorHeaps(1, destination_heaps);
      list->SetComputeRootDescriptorTable(0, alias_descriptors->GetGPUDescriptorHandleForHeapStart());
      list->SetPipelineState(read_pso.ptr);
      list->Dispatch(1, 1, 1);
      Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, 2 * sizeof(UINT));
      Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
      Transition(list.ptr, other.ptr, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
      Check("Close inline alias list", list->Close());
      ID3D12CommandList *lists[] = {list.ptr};
      queue->ExecuteCommandLists(1, lists);
      Owned<ID3D12Fence> inline_fence;
      Check("Create inline alias fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&inline_fence.ptr)));
      Check("Signal inline alias fence", queue->Signal(inline_fence.ptr, 1));
      Wait(inline_fence.ptr, 1);
      UINT *mapped = nullptr;
      Check("Map inline alias readback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
      const bool correct = mapped[0] == ValueB && mapped[1] == ValueB;
      readback->Unmap(0, nullptr);
      if (!correct)
        throw std::runtime_error("single-list alias visibility mismatch");
      std::cout << "single-list alias write/read passed\n";
      switch_alias(other.ptr, texture.ptr, false);
      if (!submit("restore source value", write_a_pso.ptr, false, ValueA))
        throw std::runtime_error("restore alias source failed");
    }

    switch_alias(texture.ptr, other.ptr);
    const UINT inherited_expected = bad_alias_oracle ? ValueA ^ 1u : ValueA;
    if (!submit("copied mapping inherits heap A", read_pso.ptr, true, inherited_expected) ||
        !submit("write copied mapping", write_b_pso.ptr, false, ValueB) ||
        !submit("read copied mapping", read_pso.ptr, true, ValueB))
      throw std::runtime_error("copied mapping GPU result mismatch");
    switch_alias(texture.ptr, other.ptr);
    if (!submit("original mapping inherits alias write", read_pso.ptr, true, ValueB))
      throw std::runtime_error("reverse alias data inheritance mismatch");
    // A mapping copy is a snapshot: remapping the source must not alter the
    // destination's existing mapping to heap A.
    map_tail(heap_b.ptr);
    if (!submit("write remapped source", write_a_pso.ptr, false, ValueA) ||
        !submit("read remapped source", read_pso.ptr, true, ValueA))
      throw std::runtime_error("source remapping GPU result mismatch");
    switch_alias(texture.ptr, other.ptr);
    if (!submit("copied mapping survives source remap", read_pso.ptr, true, ValueB))
      throw std::runtime_error("copied mapping changed with source remap");

    const D3D12_TILE_RANGE_FLAGS null_range = D3D12_TILE_RANGE_FLAG_NULL;
    queue->UpdateTileMappings(other.ptr, 1, &coordinate, &region, nullptr, 1,
                              &null_range, &heap_tile, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
    queue->CopyTileMappings(texture.ptr, &coordinate, other.ptr, &coordinate, &region, D3D12_TILE_MAPPING_FLAG_NONE);
    if (!submit("copied NULL mapping reads zero", read_pso.ptr, true, 0))
      throw std::runtime_error("copied NULL mapping GPU result mismatch");
    std::cout << "packed mapping copy and bidirectional alias inheritance passed\n";
  }
  std::cout << "Packed mip sparse-tail mapping, remap, and lifetime tests passed\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  bool copy_upload = false, full_mips = false, metadata_only = false, copy_mappings = false, bad_alias_oracle = false;
  bool perf_update = false, perf_copy = false, fault_unmap = false,
       fault_copy = false;
  const char *fault_layout = nullptr;
  for (int i = 4; i < argc; ++i) {
    if (std::strcmp(argv[i], "--copy-upload") == 0)
      copy_upload = true;
    else if (std::strcmp(argv[i], "--full-mips") == 0)
      full_mips = true;
    else if (std::strcmp(argv[i], "--metadata-only") == 0)
      metadata_only = true;
    else if (std::strcmp(argv[i], "--copy-mappings") == 0)
      copy_mappings = true;
    else if (std::strcmp(argv[i], "--bad-alias-oracle") == 0)
      bad_alias_oracle = copy_mappings = true;
    else if (std::strcmp(argv[i], "--perf-update") == 0)
      perf_update = true;
    else if (std::strcmp(argv[i], "--update-fault-standard") == 0)
      fault_layout = "standard";
    else if (std::strcmp(argv[i], "--update-fault-packed") == 0)
      fault_layout = "packed";
    else if (std::strcmp(argv[i], "--update-fault-mixed") == 0)
      fault_layout = "mixed";
    else if (std::strcmp(argv[i], "--copy-fault-standard") == 0) {
      fault_layout = "standard";
      fault_copy = true;
    } else if (std::strcmp(argv[i], "--copy-fault-packed") == 0) {
      fault_layout = "packed";
      fault_copy = true;
    } else if (std::strcmp(argv[i], "--copy-fault-mixed") == 0) {
      fault_layout = "mixed";
      fault_copy = true;
    } else if (std::strcmp(argv[i], "--perf-copy") == 0)
      perf_copy = copy_mappings = true;
    else if (std::strcmp(argv[i], "--unmap") == 0)
      fault_unmap = true;
    else {
      std::cerr << "unknown option: " << argv[i] << "\n";
      return 2;
    }
  }
  if (argc < 4 || (metadata_only && (!full_mips || copy_mappings)) ||
      (fault_unmap && !fault_layout) || (perf_update && perf_copy) ||
      (fault_layout &&
       (perf_update || perf_copy || metadata_only || copy_mappings))) {
    std::cerr << "usage: dx12_reserved_texture_packed <read.cso> <write_a.cso> "
                 "<write_b.cso> [--copy-upload] [--full-mips] "
                 "[--metadata-only|--copy-mappings|--bad-alias-oracle]\n"
                 "[--perf-update] "
                 "[--update-fault-standard|--update-fault-packed|--update-"
                 "fault-mixed] "
                 "[--copy-fault-standard|--copy-fault-packed|--copy-fault-"
                 "mixed] [--perf-copy] [--unmap]\n";
    return 2;
  }
  try {
    if (fault_layout)
      return CheckMappingFailure(fault_layout, fault_unmap, fault_copy);
    return Run(argv[1], argv[2], argv[3], copy_upload, full_mips, metadata_only,
               copy_mappings, bad_alias_oracle, perf_update, perf_copy);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}

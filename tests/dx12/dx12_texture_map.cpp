#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <stdexcept>

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
};

void Check(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    throw std::runtime_error(name);
  }
}

void Expect(const char *name, HRESULT actual, HRESULT expected) {
  if (actual != expected) {
    std::cerr << name << " returned 0x" << std::hex << static_cast<unsigned long>(actual)
              << ", expected 0x" << static_cast<unsigned long>(expected) << std::dec << "\n";
    throw std::runtime_error(name);
  }
}

D3D12_HEAP_PROPERTIES DefaultHeap() {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_HEAP_PROPERTIES CustomHeap(D3D12_CPU_PAGE_PROPERTY cpu_page_property) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_CUSTOM;
  properties.CPUPageProperty = cpu_page_property;
  properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC Texture2D(UINT width, UINT height, UINT16 array_size, UINT16 mip_levels, DXGI_FORMAT format,
                              UINT sample_count = 1, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = array_size;
  desc.MipLevels = mip_levels;
  desc.Format = format;
  desc.SampleDesc.Count = sample_count;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags;
  return desc;
}

D3D12_RESOURCE_DESC Texture3D(UINT width, UINT height, UINT16 depth, UINT16 mip_levels, DXGI_FORMAT format) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = depth;
  desc.MipLevels = mip_levels;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  return desc;
}

void ExpectMap(ID3D12Resource *resource, UINT subresource, const D3D12_RANGE *range, HRESULT expected,
               const char *name) {
  const HRESULT actual = resource->Map(subresource, range, nullptr);
  Expect(name, actual, expected);
  if (SUCCEEDED(actual))
    resource->Unmap(subresource, nullptr);
}

void CheckDefaultTextureCpuAccess(ID3D12Device *device) {
  Owned<ID3D12Resource> texture;
  const auto heap = DefaultHeap();
  const auto desc = Texture2D(4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
  Check("CreateTexture", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                           D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                           IID_PPV_ARGS(&texture.ptr)));

  ExpectMap(texture.ptr, 0, nullptr, E_INVALIDARG, "default-map-null");
  D3D12_RANGE empty_range = {4, 4};
  ExpectMap(texture.ptr, 0, &empty_range, E_INVALIDARG, "default-map-empty-range");
  D3D12_RANGE reversed_range = {8, 4};
  ExpectMap(texture.ptr, 0, &reversed_range, E_INVALIDARG, "default-map-reversed-range");
  D3D12_RANGE non_empty_range = {0, 1};
  ExpectMap(texture.ptr, 0, &non_empty_range, E_INVALIDARG, "default-map-range");
  ExpectMap(texture.ptr, 1, nullptr, E_INVALIDARG, "default-map-subresource");

  void *data = reinterpret_cast<void *>(UINT_PTR(0xfeedface));
  Expect("default-map-pointer", texture.ptr->Map(0, nullptr, &data), E_INVALIDARG);
  if (data != reinterpret_cast<void *>(UINT_PTR(0xfeedface))) {
    std::cerr << "Map failure modified the caller output\n";
    throw std::runtime_error("default-map-pointer-output");
  }

  uint32_t source[16] = {};
  for (uint32_t i = 0; i < 16; i++)
    source[i] = 0x10000000u + i;
  Expect("default-WriteToSubresource",
         texture.ptr->WriteToSubresource(0, nullptr, source, 4 * sizeof(uint32_t), 0), E_INVALIDARG);
  uint32_t destination[16] = {};
  Expect("default-ReadFromSubresource",
         texture.ptr->ReadFromSubresource(destination, 4 * sizeof(uint32_t), 0, 0, nullptr), E_INVALIDARG);
}

void CheckCpuVisibleCustomTexture(ID3D12Device *device, D3D12_CPU_PAGE_PROPERTY page_property,
                                  D3D12_RESOURCE_STATES initial_state, const char *label) {
  Owned<ID3D12Resource> texture;
  const auto heap = CustomHeap(page_property);
  const auto desc = Texture2D(4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
  const HRESULT create_hr = device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, initial_state, nullptr, IID_PPV_ARGS(&texture.ptr));
  if (FAILED(create_hr)) {
    std::cout << label << " unavailable: 0x" << std::hex
              << static_cast<unsigned long>(create_hr) << std::dec << "\n";
    return;
  }

  const auto name = [label](const char *suffix) { return std::string(label) + "-" + suffix; };

  ExpectMap(texture.ptr, 0, nullptr, S_OK, name("map-null").c_str());
  D3D12_RANGE empty_range = {4, 4};
  ExpectMap(texture.ptr, 0, &empty_range, S_OK, name("map-empty-range").c_str());
  D3D12_RANGE non_empty_range = {0, 1};
  ExpectMap(texture.ptr, 0, &non_empty_range, E_INVALIDARG, name("map-range").c_str());
  ExpectMap(texture.ptr, 1, nullptr, E_INVALIDARG, name("map-subresource").c_str());

  void *data = reinterpret_cast<void *>(UINT_PTR(0xfeedface));
  Expect(name("map-pointer").c_str(), texture.ptr->Map(0, nullptr, &data), E_INVALIDARG);
  if (data != reinterpret_cast<void *>(UINT_PTR(0xfeedface))) {
    std::cerr << "Map failure modified the caller output\n";
    throw std::runtime_error(std::string(label) + "-map-pointer-output");
  }

  Check(name("map-for-transfer").c_str(), texture.ptr->Map(0, nullptr, nullptr));
  uint32_t source[16] = {};
  for (uint32_t i = 0; i < 16; i++)
    source[i] = 0x10000000u + i;
  Check(name("WriteToSubresource").c_str(),
        texture.ptr->WriteToSubresource(0, nullptr, source, 4 * sizeof(uint32_t), 0));
  uint32_t destination[16] = {};
  Check(name("ReadFromSubresource").c_str(),
        texture.ptr->ReadFromSubresource(destination, 4 * sizeof(uint32_t), 0, 0, nullptr));
  texture.ptr->Unmap(0, nullptr);
  if (std::memcmp(source, destination, sizeof(source)) != 0) {
    std::cerr << label << " transfer did not round-trip\n";
    throw std::runtime_error(std::string(label) + "-texture-transfer");
  }
}

void CheckTextureTransferBoxes(ID3D12Device *device) {
  Owned<ID3D12Resource> texture;
  const auto heap = CustomHeap(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);
  const auto desc = Texture2D(4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
  Check("CreateBoxTexture", device->CreateCommittedResource(
                                 &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                 IID_PPV_ARGS(&texture.ptr)));

  uint32_t source[16] = {};
  uint32_t destination[16] = {};
  const D3D12_BOX empty_boxes[] = {
      {1, 0, 0, 1, 4, 1},
      {0, 1, 0, 4, 1, 1},
      {0, 0, 0, 4, 4, 0},
  };
  for (size_t i = 0; i < sizeof(empty_boxes) / sizeof(empty_boxes[0]); i++) {
    Expect("empty-box-write", texture.ptr->WriteToSubresource(
                                  0, &empty_boxes[i], source, 4 * sizeof(uint32_t), 0), S_OK);
    Expect("empty-box-read", texture.ptr->ReadFromSubresource(
                                 destination, 4 * sizeof(uint32_t), 0, 0, &empty_boxes[i]), S_OK);
  }

  const D3D12_BOX reversed_boxes[] = {
      {3, 0, 0, 1, 4, 1},
      {0, 3, 0, 4, 1, 1},
      {0, 0, 1, 4, 4, 0},
  };
  for (size_t i = 0; i < sizeof(reversed_boxes) / sizeof(reversed_boxes[0]); i++) {
    Expect("reversed-box-write", texture.ptr->WriteToSubresource(
                                    0, &reversed_boxes[i], source, 4 * sizeof(uint32_t), 0), E_INVALIDARG);
    Expect("reversed-box-read", texture.ptr->ReadFromSubresource(
                                   destination, 4 * sizeof(uint32_t), 0, 0, &reversed_boxes[i]), E_INVALIDARG);
  }
}

void CheckCpuInvisibleCustomTexture(ID3D12Device *device) {
  Owned<ID3D12Resource> texture;
  const auto heap = CustomHeap(D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE);
  const auto desc = Texture2D(4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
  const HRESULT create_hr = device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture.ptr));
  if (FAILED(create_hr)) {
    std::cout << "CUSTOM CPU-invisible texture unavailable: 0x" << std::hex
              << static_cast<unsigned long>(create_hr) << std::dec << "\n";
    return;
  }

  ExpectMap(texture.ptr, 0, nullptr, E_INVALIDARG, "custom-invisible-map");
  void *map_data = reinterpret_cast<void *>(UINT_PTR(0xfeedface));
  Expect("custom-invisible-map-pointer", texture.ptr->Map(0, nullptr, &map_data), E_INVALIDARG);
  if (map_data != reinterpret_cast<void *>(UINT_PTR(0xfeedface))) {
    std::cerr << "Map failure modified the caller output\n";
    throw std::runtime_error("custom-invisible-map-pointer-output");
  }
  uint32_t data[16] = {};
  Expect("custom-invisible-WriteToSubresource",
         texture.ptr->WriteToSubresource(0, nullptr, data, 4 * sizeof(uint32_t), 0), E_INVALIDARG);
  Expect("custom-invisible-ReadFromSubresource",
         texture.ptr->ReadFromSubresource(data, 4 * sizeof(uint32_t), 0, 0, nullptr), E_INVALIDARG);
}

void CheckNonCommonCpuVisibleTexture(ID3D12Device *device) {
  Owned<ID3D12Resource> texture;
  const auto heap = CustomHeap(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);
  const auto desc = Texture2D(4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
  const HRESULT create_hr = device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture.ptr));
  if (FAILED(create_hr)) {
    std::cout << "CUSTOM non-COMMON texture unavailable: 0x" << std::hex
              << static_cast<unsigned long>(create_hr) << std::dec << "\n";
    return;
  }

  uint32_t source[16] = {};
  uint32_t destination[16] = {};
  for (uint32_t i = 0; i < 16; i++)
    source[i] = 0x20000000u + i;
  Expect("custom-non-common-WriteToSubresource-before-map",
         texture.ptr->WriteToSubresource(0, nullptr, source, 4 * sizeof(uint32_t), 0), S_OK);
  Expect("custom-non-common-ReadFromSubresource-before-map",
         texture.ptr->ReadFromSubresource(destination, 4 * sizeof(uint32_t), 0, 0, nullptr), S_OK);
  if (std::memcmp(source, destination, sizeof(source)) != 0)
    throw std::runtime_error("custom-non-common-before-map-transfer");

  ExpectMap(texture.ptr, 0, nullptr, S_OK, "custom-non-common-map");
  void *map_data = reinterpret_cast<void *>(UINT_PTR(0xfeedface));
  Expect("custom-non-common-map-pointer", texture.ptr->Map(0, nullptr, &map_data), E_INVALIDARG);
  if (map_data != reinterpret_cast<void *>(UINT_PTR(0xfeedface))) {
    std::cerr << "Map failure modified the caller output\n";
    throw std::runtime_error("custom-non-common-map-pointer-output");
  }
  Expect("custom-non-common-WriteToSubresource-after-map",
         texture.ptr->WriteToSubresource(0, nullptr, source, 4 * sizeof(uint32_t), 0), S_OK);
  Expect("custom-non-common-ReadFromSubresource",
         texture.ptr->ReadFromSubresource(destination, 4 * sizeof(uint32_t), 0, 0, nullptr), S_OK);
  if (std::memcmp(source, destination, sizeof(source)) != 0)
    throw std::runtime_error("custom-non-common-after-map-transfer");
}

void CheckReservedTexture(ID3D12Device *device) {
  Owned<ID3D12Resource> texture;
  auto desc = Texture2D(64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
  desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  const auto hr = device->CreateReservedResource(
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture.ptr));
  if (FAILED(hr)) {
    std::cout << "Reserved texture unavailable: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return;
  }

  ExpectMap(texture.ptr, 0, nullptr, E_INVALIDARG, "reserved-map");
  uint32_t data[16] = {};
  Expect("reserved-WriteToSubresource",
         texture.ptr->WriteToSubresource(0, nullptr, data, 4 * sizeof(uint32_t), 0), E_INVALIDARG);
  Expect("reserved-ReadFromSubresource",
         texture.ptr->ReadFromSubresource(data, 4 * sizeof(uint32_t), 0, 0, nullptr), E_INVALIDARG);
}

void CheckSubresourceAndFormatRestrictions(ID3D12Device *device) {
  const auto heap = CustomHeap(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);
  auto upload_heap = DefaultHeap();
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  auto readback_heap = DefaultHeap();
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  const auto heap_texture_desc = Texture2D(4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
  ID3D12Resource *unsupported_texture = nullptr;
  Expect("upload-texture-heap", device->CreateCommittedResource(
                                    &upload_heap, D3D12_HEAP_FLAG_NONE, &heap_texture_desc,
                                    D3D12_RESOURCE_STATE_COMMON, nullptr,
                                    IID_PPV_ARGS(&unsupported_texture)), E_INVALIDARG);
  if (unsupported_texture)
    unsupported_texture->Release();
  Expect("readback-texture-heap", device->CreateCommittedResource(
                                      &readback_heap, D3D12_HEAP_FLAG_NONE, &heap_texture_desc,
                                      D3D12_RESOURCE_STATE_COMMON, nullptr,
                                      IID_PPV_ARGS(&unsupported_texture)), E_INVALIDARG);
  if (unsupported_texture)
    unsupported_texture->Release();

  Owned<ID3D12Resource> array_texture;
  const auto array_desc = Texture2D(4, 4, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);
  Check("CreateArrayTexture", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &array_desc,
                                                                D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                IID_PPV_ARGS(&array_texture.ptr)));
  ExpectMap(array_texture.ptr, 0, nullptr, S_OK, "array-map-0");
  ExpectMap(array_texture.ptr, 3, nullptr, S_OK, "array-map-3");
  ExpectMap(array_texture.ptr, 4, nullptr, E_INVALIDARG, "array-map-out-of-range");

  Owned<ID3D12Resource> volume_texture;
  const auto volume_desc = Texture3D(4, 4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
  Check("CreateVolumeTexture", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &volume_desc,
                                                                 D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                 IID_PPV_ARGS(&volume_texture.ptr)));
  ExpectMap(volume_texture.ptr, 0, nullptr, S_OK, "volume-map");

  Owned<ID3D12Resource> mipped_volume;
  const auto mipped_volume_desc = Texture3D(4, 4, 4, 2, DXGI_FORMAT_R8G8B8A8_UNORM);
  Check("CreateMippedVolume", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                                                                &mipped_volume_desc, D3D12_RESOURCE_STATE_COMMON,
                                                                nullptr, IID_PPV_ARGS(&mipped_volume.ptr)));
  ExpectMap(mipped_volume.ptr, 0, nullptr, E_INVALIDARG, "mipped-volume-map");

  Owned<ID3D12Resource> msaa_texture;
  const auto msaa_desc = Texture2D(4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 2,
                                   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  Check("CreateMSAATexture", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &msaa_desc,
                                                               D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                               IID_PPV_ARGS(&msaa_texture.ptr)));
  ExpectMap(msaa_texture.ptr, 0, nullptr, E_INVALIDARG, "msaa-map");

  Owned<ID3D12Resource> depth_texture;
  const auto depth_desc = Texture2D(4, 4, 1, 1, DXGI_FORMAT_D32_FLOAT, 1,
                                   D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
  Check("CreateDepthTexture", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
                                                                D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr,
                                                                IID_PPV_ARGS(&depth_texture.ptr)));
  ExpectMap(depth_texture.ptr, 0, nullptr, E_INVALIDARG, "depth-map");

  Owned<ID3D12Resource> bc_texture;
  const auto bc_desc = Texture2D(8, 8, 1, 1, DXGI_FORMAT_BC1_UNORM);
  Check("CreateBCTexture", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bc_desc,
                                                             D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                             IID_PPV_ARGS(&bc_texture.ptr)));
  ExpectMap(bc_texture.ptr, 0, nullptr, S_OK, "bc-map");
}

} // namespace

int main() {
  Owned<ID3D12Device> device;
  Check("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)));
  try {
    CheckDefaultTextureCpuAccess(device.ptr);
    CheckCpuVisibleCustomTexture(
        device.ptr, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_RESOURCE_STATE_COMMON, "custom-writeback-common");
    CheckCpuVisibleCustomTexture(
        device.ptr, D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_RESOURCE_STATE_COMMON, "custom-writecombine-common");
    CheckTextureTransferBoxes(device.ptr);
    CheckCpuInvisibleCustomTexture(device.ptr);
    CheckNonCommonCpuVisibleTexture(device.ptr);
    CheckReservedTexture(device.ptr);
    CheckSubresourceAndFormatRestrictions(device.ptr);
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
  std::cout << "D3D12 texture CPU access semantics passed\n";
  return 0;
}

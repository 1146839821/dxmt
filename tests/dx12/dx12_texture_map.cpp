#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <cstring>
#include <iostream>
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
  void *data = reinterpret_cast<void *>(UINT_PTR(0xfeedface));
  void **output = expected == E_INVALIDARG && std::strcmp(name, "map-pointer") == 0 ? &data : nullptr;
  const HRESULT actual = resource->Map(subresource, range, output);
  Expect(name, actual, expected);
  if (SUCCEEDED(actual))
    resource->Unmap(subresource, nullptr);
}

void CheckOpaqueTextureMap(ID3D12Device *device) {
  Owned<ID3D12Resource> texture;
  const auto heap = DefaultHeap();
  const auto desc = Texture2D(4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
  Check("CreateTexture", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                           D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                           IID_PPV_ARGS(&texture.ptr)));

  ExpectMap(texture.ptr, 0, nullptr, S_OK, "map-null");
  D3D12_RANGE empty_range = {4, 4};
  ExpectMap(texture.ptr, 0, &empty_range, S_OK, "map-empty-range");
  D3D12_RANGE reversed_range = {8, 4};
  ExpectMap(texture.ptr, 0, &reversed_range, S_OK, "map-reversed-range");
  D3D12_RANGE non_empty_range = {0, 1};
  ExpectMap(texture.ptr, 0, &non_empty_range, E_INVALIDARG, "map-range");
  ExpectMap(texture.ptr, 1, nullptr, E_INVALIDARG, "map-subresource");

  void *data = reinterpret_cast<void *>(UINT_PTR(0xfeedface));
  Expect("map-pointer", texture.ptr->Map(0, nullptr, &data), E_INVALIDARG);
  if (data) {
    std::cerr << "Map failure did not clear the caller output\n";
    throw std::runtime_error("map-pointer-output");
  }

  uint32_t source[16] = {};
  for (uint32_t i = 0; i < 16; i++)
    source[i] = 0x10000000u + i;
  Check("WriteToSubresource", texture.ptr->WriteToSubresource(0, nullptr, source, 4 * sizeof(uint32_t), 0));
  uint32_t destination[16] = {};
  Check("ReadFromSubresource", texture.ptr->ReadFromSubresource(destination, 4 * sizeof(uint32_t), 0, 0, nullptr));
  if (std::memcmp(source, destination, sizeof(source)) != 0) {
    std::cerr << "opaque texture transfer did not round-trip\n";
    throw std::runtime_error("texture-transfer");
  }
}

void CheckSubresourceAndFormatRestrictions(ID3D12Device *device) {
  const auto heap = DefaultHeap();
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
    CheckOpaqueTextureMap(device.ptr);
    CheckSubresourceAndFormatRestrictions(device.ptr);
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
  std::cout << "D3D12 opaque texture Map semantics passed\n";
  return 0;
}

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

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

void PrintHR(const char *name, HRESULT hr) {
  std::cout << name << "=0x" << std::hex << std::setw(8) << std::setfill('0')
            << static_cast<unsigned long>(hr) << std::dec << std::setfill(' ') << "\n";
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

D3D12_RESOURCE_DESC Texture2D(D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 4;
  desc.Height = 4;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Layout = layout;
  return desc;
}

void ProbeResource(ID3D12Device *device, const char *name, const D3D12_HEAP_PROPERTIES &heap,
                   const D3D12_RESOURCE_DESC &desc, D3D12_RESOURCE_STATES state) {
  Owned<ID3D12Resource> resource;
  const HRESULT create_hr = device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource.ptr));
  PrintHR(name, create_hr);
  if (FAILED(create_hr))
    return;

  const auto prefix = [name](const char *operation) {
    std::string result(name);
    result += ".";
    result += operation;
    return result;
  };

  HRESULT hr = resource.ptr->Map(0, nullptr, nullptr);
  PrintHR(prefix("Map.null").c_str(), hr);
  if (SUCCEEDED(hr))
    resource.ptr->Unmap(0, nullptr);

  void *data = reinterpret_cast<void *>(UINT_PTR(0xfeedface));
  hr = resource.ptr->Map(0, nullptr, &data);
  PrintHR(prefix("Map.pointer").c_str(), hr);
  std::cout << prefix("Map.pointer.value") << "=0x" << std::hex
            << reinterpret_cast<UINT_PTR>(data) << std::dec << "\n";
  if (SUCCEEDED(hr))
    resource.ptr->Unmap(0, nullptr);

  D3D12_RANGE empty_range = {4, 4};
  hr = resource.ptr->Map(0, &empty_range, nullptr);
  PrintHR(prefix("Map.empty").c_str(), hr);
  if (SUCCEEDED(hr))
    resource.ptr->Unmap(0, nullptr);

  D3D12_RANGE non_empty_range = {0, 1};
  hr = resource.ptr->Map(0, &non_empty_range, nullptr);
  PrintHR(prefix("Map.nonempty").c_str(), hr);
  if (SUCCEEDED(hr))
    resource.ptr->Unmap(0, nullptr);

  uint32_t source[16] = {};
  for (uint32_t i = 0; i < 16; i++)
    source[i] = 0x10000000u + i;
  uint32_t destination[16] = {};

  if (std::strcmp(name, "custom.writeback.common") == 0 ||
      std::strcmp(name, "custom.writeback.copydest") == 0) {
    const D3D12_BOX empty_boxes[] = {
        {1, 0, 0, 1, 4, 1},
        {0, 1, 0, 4, 1, 1},
        {0, 0, 0, 4, 4, 0},
    };
    const char *empty_names[] = {"Write.empty.x", "Write.empty.y", "Write.empty.z"};
    for (size_t i = 0; i < sizeof(empty_boxes) / sizeof(empty_boxes[0]); i++) {
      PrintHR(prefix(empty_names[i]).c_str(), resource.ptr->WriteToSubresource(
                                                  0, &empty_boxes[i], source, 4 * sizeof(uint32_t), 0));
      const std::string read_name = std::string("Read.empty.") + (i == 0 ? "x" : i == 1 ? "y" : "z");
      PrintHR(prefix(read_name.c_str()).c_str(), resource.ptr->ReadFromSubresource(
                                                    destination, 4 * sizeof(uint32_t), 0, 0, &empty_boxes[i]));
    }

    const D3D12_BOX reversed_boxes[] = {
        {3, 0, 0, 1, 4, 1},
        {0, 3, 0, 4, 1, 1},
        {0, 0, 1, 4, 4, 0},
    };
    const char *reversed_axes[] = {"x", "y", "z"};
    for (size_t i = 0; i < sizeof(reversed_boxes) / sizeof(reversed_boxes[0]); i++) {
      const std::string write_name = std::string("Write.reversed.") + reversed_axes[i];
      const std::string read_name = std::string("Read.reversed.") + reversed_axes[i];
      PrintHR(prefix(write_name.c_str()).c_str(), resource.ptr->WriteToSubresource(
                                                     0, &reversed_boxes[i], source, 4 * sizeof(uint32_t), 0));
      PrintHR(prefix(read_name.c_str()).c_str(), resource.ptr->ReadFromSubresource(
                                                    destination, 4 * sizeof(uint32_t), 0, 0, &reversed_boxes[i]));
    }
  }

  hr = resource.ptr->WriteToSubresource(0, nullptr, source, 4 * sizeof(uint32_t), 0);
  PrintHR(prefix("Write.before-map").c_str(), hr);
  hr = resource.ptr->ReadFromSubresource(destination, 4 * sizeof(uint32_t), 0, 0, nullptr);
  PrintHR(prefix("Read.before-map").c_str(), hr);

  hr = resource.ptr->Map(0, nullptr, nullptr);
  PrintHR(prefix("Map.for-transfer").c_str(), hr);
  if (SUCCEEDED(hr)) {
    hr = resource.ptr->WriteToSubresource(0, nullptr, source, 4 * sizeof(uint32_t), 0);
    PrintHR(prefix("Write.after-map").c_str(), hr);
    hr = resource.ptr->ReadFromSubresource(destination, 4 * sizeof(uint32_t), 0, 0, nullptr);
    PrintHR(prefix("Read.after-map").c_str(), hr);
    std::cout << prefix("roundtrip") << "="
              << (std::memcmp(source, destination, sizeof(source)) == 0 ? "true" : "false") << "\n";
    resource.ptr->Unmap(0, nullptr);
  }
}

} // namespace

int main() {
  Owned<ID3D12Device> device;
  const HRESULT device_hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr));
  PrintHR("D3D12CreateDevice", device_hr);
  if (FAILED(device_hr))
    return 1;

  const auto desc = Texture2D();
  ProbeResource(device.ptr, "default.common", DefaultHeap(), desc, D3D12_RESOURCE_STATE_COMMON);
  ProbeResource(device.ptr, "custom.writeback.common", CustomHeap(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK), desc,
                D3D12_RESOURCE_STATE_COMMON);
  ProbeResource(device.ptr, "custom.writecombine.common", CustomHeap(D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE), desc,
                D3D12_RESOURCE_STATE_COMMON);
  ProbeResource(device.ptr, "custom.notavailable.common", CustomHeap(D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE), desc,
                D3D12_RESOURCE_STATE_COMMON);
  ProbeResource(device.ptr, "custom.writeback.copydest", CustomHeap(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK), desc,
                D3D12_RESOURCE_STATE_COPY_DEST);

  auto reserved_desc = desc;
  reserved_desc.Width = 64;
  reserved_desc.Height = 64;
  reserved_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  Owned<ID3D12Resource> reserved;
  const HRESULT reserved_hr = device.ptr->CreateReservedResource(
      &reserved_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&reserved.ptr));
  PrintHR("reserved.common", reserved_hr);
  if (SUCCEEDED(reserved_hr)) {
    void *data = nullptr;
    PrintHR("reserved.Map.null", reserved.ptr->Map(0, nullptr, nullptr));
    PrintHR("reserved.Map.pointer", reserved.ptr->Map(0, nullptr, &data));
    uint32_t transfer[16] = {};
    PrintHR("reserved.Write", reserved.ptr->WriteToSubresource(0, nullptr, transfer, 4 * sizeof(uint32_t), 0));
    PrintHR("reserved.Read", reserved.ptr->ReadFromSubresource(transfer, 4 * sizeof(uint32_t), 0, 0, nullptr));
  }
  return 0;
}

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_2.h>

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

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

void PrintHR(const char *name, HRESULT hr) {
  std::cout << name << "=0x" << std::hex << std::setw(8) << std::setfill('0')
            << static_cast<unsigned long>(hr) << std::dec << std::setfill(' ') << "\n";
}

std::string AdapterName(const DXGI_ADAPTER_DESC1 &desc) {
  char buffer[256] = {};
  const int length = WideCharToMultiByte(
      CP_UTF8, 0, desc.Description, -1, buffer, static_cast<int>(sizeof(buffer)), nullptr, nullptr
  );
  return length > 0 ? std::string(buffer) : std::string("<unavailable>");
}

D3D12_RESOURCE_DESC PackedTexture(UINT64 width, UINT height, UINT array_size, UINT mip_levels) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = static_cast<UINT16>(array_size);
  desc.MipLevels = static_cast<UINT16>(mip_levels);
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  return desc;
}

void ProbeTiling(ID3D12Device *device, const char *name, const D3D12_RESOURCE_DESC &desc) {
  Owned<ID3D12Resource> resource;
  const HRESULT create_hr = device->CreateReservedResource(
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource.ptr)
  );
  PrintHR((std::string(name) + ".create").c_str(), create_hr);
  if (FAILED(create_hr))
    return;

  UINT total_tiles = 0;
  D3D12_PACKED_MIP_INFO packed = {};
  D3D12_TILE_SHAPE shape = {};
  UINT tiling_count = static_cast<UINT>(desc.MipLevels) * desc.DepthOrArraySize;
  std::vector<D3D12_SUBRESOURCE_TILING> tilings(tiling_count);
  device->GetResourceTiling(resource.ptr, &total_tiles, &packed, &shape, &tiling_count, 0, tilings.data());
  std::cout << name << ".total_tiles=" << total_tiles << "\n";
  std::cout << name << ".num_standard_mips=" << packed.NumStandardMips << "\n";
  std::cout << name << ".num_packed_mips=" << packed.NumPackedMips << "\n";
  std::cout << name << ".num_tiles_for_packed_mips=" << packed.NumTilesForPackedMips << "\n";
  std::cout << name << ".start_tile_index=" << packed.StartTileIndexInOverallResource << "\n";
  std::cout << name << ".standard_tile_shape=" << shape.WidthInTexels << "x" << shape.HeightInTexels << "x"
            << shape.DepthInTexels << "\n";
  std::cout << name << ".returned_tiling_count=" << tiling_count << "\n";
  for (UINT index = 0; index < tiling_count; index++) {
    const auto &tiling = tilings[index];
    std::cout << name << ".tiling." << index << "=" << tiling.WidthInTiles << "x" << tiling.HeightInTiles << "x"
              << tiling.DepthInTiles << " start=" << tiling.StartTileIndexInOverallResource << "\n";
  }
}

} // namespace

int main() {
  Owned<IDXGIFactory1> factory;
  PrintHR("CreateDXGIFactory1", CreateDXGIFactory1(IID_PPV_ARGS(&factory.ptr)));

  Owned<IDXGIAdapter1> adapter;
  if (factory.ptr) {
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter.ptr) != DXGI_ERROR_NOT_FOUND; index++) {
      DXGI_ADAPTER_DESC1 desc = {};
      if (FAILED(adapter->GetDesc1(&desc))) {
        adapter.ptr->Release();
        adapter.ptr = nullptr;
        continue;
      }
      if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
        std::cout << "adapter.name=" << AdapterName(desc) << "\n";
        std::cout << "adapter.vendor=0x" << std::hex << desc.VendorId << std::dec << "\n";
        std::cout << "adapter.device=0x" << std::hex << desc.DeviceId << std::dec << "\n";
        break;
      }
      adapter.ptr->Release();
      adapter.ptr = nullptr;
    }
  }

  Owned<ID3D12Device> device;
  const HRESULT device_hr = D3D12CreateDevice(
      adapter.ptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)
  );
  PrintHR("D3D12CreateDevice", device_hr);
  if (FAILED(device_hr))
    return 1;

  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  const HRESULT options_hr = device->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)
  );
  PrintHR("CheckFeatureSupport.D3D12_OPTIONS", options_hr);
  if (SUCCEEDED(options_hr))
    std::cout << "feature.tiled_resources_tier=" << options.TiledResourcesTier << "\n";

  ProbeTiling(device.ptr, "packed.64x64.r32.mips4.array1", PackedTexture(64, 64, 1, 4));
  ProbeTiling(device.ptr, "packed.192x128.r32.mips2.array1", PackedTexture(192, 128, 1, 2));
  ProbeTiling(device.ptr, "packed.192x128.r32.mips2.array2", PackedTexture(192, 128, 2, 2));
  return 0;
}

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <iostream>

namespace {

template <typename T>
void Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

template <typename T>
bool ExpectQuery(IUnknown *object, const char *name, HRESULT expected) {
  T *queried = nullptr;
  const HRESULT actual = object->QueryInterface(IID_PPV_ARGS(&queried));
  const bool has_result = queried != nullptr;
  const bool expected_result = expected == S_OK;
  if (actual != expected || has_result != expected_result) {
    std::cerr << name << " returned 0x" << std::hex << static_cast<unsigned long>(actual)
              << ", expected 0x" << static_cast<unsigned long>(expected) << std::dec
              << ", output pointer is " << (has_result ? "non-null" : "null") << "\n";
    Release(queried);
    return false;
  }
  Release(queried);
  return true;
}

template <typename T>
bool ExpectFeature(ID3D12Device *device, D3D12_FEATURE feature, T *data, const char *name) {
  const HRESULT actual = device->CheckFeatureSupport(feature, data, sizeof(*data));
  if (actual != S_OK) {
    std::cerr << name << " returned 0x" << std::hex << static_cast<unsigned long>(actual)
              << ", expected 0x" << static_cast<unsigned long>(S_OK) << std::dec << "\n";
    return false;
  }
  return true;
}

} // namespace

int main() {
  constexpr GUID clsid_d3d12_sdk_configuration = {
      0x7cda6aca, 0xa03e, 0x49c8, {0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce}
  };
  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  IDXGIFactory7 *factory = nullptr;
  ID3D12GraphicsCommandList3 *list3 = nullptr;
  bool passed = true;

  if (D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)) != S_OK) {
    std::cerr << "D3D12CreateDevice failed\n";
    return 1;
  }

  passed &= ExpectQuery<ID3D12Device1>(device, "ID3D12Device1", S_OK);
  passed &= ExpectQuery<ID3D12Device2>(device, "ID3D12Device2", S_OK);
  passed &= ExpectQuery<ID3D12Device3>(device, "ID3D12Device3", S_OK);
  passed &= ExpectQuery<ID3D12Device4>(device, "ID3D12Device4", S_OK);
  passed &= ExpectQuery<ID3D12Device5>(device, "ID3D12Device5", E_NOINTERFACE);
  passed &= ExpectQuery<ID3D12Device6>(device, "ID3D12Device6", E_NOINTERFACE);
  passed &= ExpectQuery<ID3D12Device7>(device, "ID3D12Device7", E_NOINTERFACE);
  passed &= ExpectQuery<ID3D12Device8>(device, "ID3D12Device8", E_NOINTERFACE);
  passed &= ExpectQuery<ID3D12Device9>(device, "ID3D12Device9", E_NOINTERFACE);
  passed &= ExpectQuery<ID3D12Device10>(device, "ID3D12Device10", E_NOINTERFACE);

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)) != S_OK) {
    std::cerr << "CreateCommandQueue failed\n";
    passed = false;
  } else {
    passed &= ExpectQuery<ID3D12CommandQueue>(queue, "ID3D12CommandQueue", S_OK);
  }

  if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)) != S_OK) {
    std::cerr << "CreateCommandAllocator failed\n";
    passed = false;
  } else if (device->CreateCommandList(
                 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list)
             ) != S_OK) {
    std::cerr << "CreateCommandList failed\n";
    passed = false;
  } else {
    passed &= ExpectQuery<ID3D12GraphicsCommandList2>(list, "ID3D12GraphicsCommandList2", S_OK);
    passed &= ExpectQuery<ID3D12GraphicsCommandList3>(list, "ID3D12GraphicsCommandList3", S_OK);
    passed &= ExpectQuery<ID3D12GraphicsCommandList4>(list, "ID3D12GraphicsCommandList4", E_NOINTERFACE);
    passed &= ExpectQuery<ID3D12GraphicsCommandList5>(list, "ID3D12GraphicsCommandList5", E_NOINTERFACE);
    passed &= ExpectQuery<ID3D12GraphicsCommandList6>(list, "ID3D12GraphicsCommandList6", E_NOINTERFACE);
    passed &= ExpectQuery<ID3D12GraphicsCommandList7>(list, "ID3D12GraphicsCommandList7", E_NOINTERFACE);
    if (list->QueryInterface(IID_PPV_ARGS(&list3)) != S_OK || !list3) {
      std::cerr << "ID3D12GraphicsCommandList3 vtable query failed\n";
      passed = false;
    } else {
      list3->SetProtectedResourceSession(nullptr);
    }
  }

  if (CreateDXGIFactory1(IID_PPV_ARGS(&factory)) != S_OK) {
    std::cerr << "CreateDXGIFactory1 failed\n";
    passed = false;
  } else {
    passed &= ExpectQuery<IDXGIFactory>(factory, "IDXGIFactory", S_OK);
    passed &= ExpectQuery<IDXGIFactory1>(factory, "IDXGIFactory1", S_OK);
    passed &= ExpectQuery<IDXGIFactory2>(factory, "IDXGIFactory2", S_OK);
    passed &= ExpectQuery<IDXGIFactory3>(factory, "IDXGIFactory3", S_OK);
    passed &= ExpectQuery<IDXGIFactory4>(factory, "IDXGIFactory4", S_OK);
    passed &= ExpectQuery<IDXGIFactory5>(factory, "IDXGIFactory5", S_OK);
    passed &= ExpectQuery<IDXGIFactory6>(factory, "IDXGIFactory6", S_OK);
    passed &= ExpectQuery<IDXGIFactory7>(factory, "IDXGIFactory7", S_OK);
  }

  IUnknown *device_factory = nullptr;
  const HRESULT device_factory_hr = D3D12GetInterface(
      clsid_d3d12_sdk_configuration, __uuidof(ID3D12DeviceFactory), reinterpret_cast<void **>(&device_factory)
  );
  if (device_factory_hr != E_NOINTERFACE || device_factory != nullptr) {
    std::cerr << "ID3D12DeviceFactory returned 0x" << std::hex << static_cast<unsigned long>(device_factory_hr)
              << ", output pointer is " << (device_factory ? "non-null" : "null") << std::dec << "\n";
    passed = false;
  }
  Release(device_factory);

#define CHECK_OPTIONS(number) \
  do { \
    D3D12_FEATURE_DATA_D3D12_OPTIONS##number options = {}; \
    passed &= ExpectFeature( \
        device, D3D12_FEATURE_D3D12_OPTIONS##number, &options, "D3D12_FEATURE_D3D12_OPTIONS" #number \
    ); \
  } while (false)
  CHECK_OPTIONS(8);
  CHECK_OPTIONS(9);
  CHECK_OPTIONS(10);
  CHECK_OPTIONS(11);
  CHECK_OPTIONS(12);
  CHECK_OPTIONS(13);
  CHECK_OPTIONS(14);
  CHECK_OPTIONS(15);
  CHECK_OPTIONS(16);
  CHECK_OPTIONS(17);
  CHECK_OPTIONS(18);
#undef CHECK_OPTIONS

  Release(factory);
  Release(list3);
  Release(list);
  Release(allocator);
  Release(queue);
  Release(device);

  if (!passed)
    return 1;
  std::cout << "D3D12 interface support contract passed\n";
  return 0;
}

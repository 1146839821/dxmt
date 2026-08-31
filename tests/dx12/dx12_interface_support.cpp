#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdint>
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
  ID3D12Device5 *device5 = nullptr;
  ID3D12Device6 *device6 = nullptr;
  ID3D12Device7 *device7 = nullptr;
  ID3D12Device8 *device8 = nullptr;
  ID3D12Device9 *device9 = nullptr;
  ID3D12Device10 *device10 = nullptr;
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
  passed &= ExpectQuery<ID3D12Device5>(device, "ID3D12Device5", S_OK);
  passed &= ExpectQuery<ID3D12Device6>(device, "ID3D12Device6", S_OK);
  passed &= ExpectQuery<ID3D12Device7>(device, "ID3D12Device7", S_OK);
  passed &= ExpectQuery<ID3D12Device8>(device, "ID3D12Device8", S_OK);
  passed &= ExpectQuery<ID3D12Device9>(device, "ID3D12Device9", S_OK);
  passed &= ExpectQuery<ID3D12Device10>(device, "ID3D12Device10", S_OK);
  if (device->QueryInterface(IID_PPV_ARGS(&device5)) != S_OK || !device5) {
    std::cerr << "ID3D12Device5 vtable query failed\n";
    passed = false;
  } else {
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {1, 1, 1};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(nullptr, &info);
    if (info.ResultDataMaxSizeInBytes || info.ScratchDataSizeInBytes || info.UpdateScratchDataSizeInBytes) {
      std::cerr << "ID3D12Device5 returned unsupported raytracing info\n";
      passed = false;
    }
    IUnknown *state_object = nullptr;
    const HRESULT state_object_hr = device5->CreateStateObject(
        nullptr, __uuidof(ID3D12StateObject), reinterpret_cast<void **>(&state_object)
    );
    if (state_object_hr != E_NOTIMPL || state_object != nullptr) {
      std::cerr << "ID3D12Device5::CreateStateObject returned 0x" << std::hex
                << static_cast<unsigned long>(state_object_hr) << std::dec << "\n";
      passed = false;
    }
    Release(state_object);
  }
  if (device->QueryInterface(IID_PPV_ARGS(&device6)) != S_OK || !device6) {
    std::cerr << "ID3D12Device6 vtable query failed\n";
    passed = false;
  } else {
    WINBOOL further_measurements_desired = TRUE;
    const HRESULT background_processing_hr = device6->SetBackgroundProcessingMode(
        D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED, D3D12_MEASUREMENTS_ACTION_KEEP_ALL, nullptr,
        &further_measurements_desired
    );
    if (background_processing_hr != E_NOTIMPL || further_measurements_desired != FALSE) {
      std::cerr << "ID3D12Device6::SetBackgroundProcessingMode returned 0x" << std::hex
                << static_cast<unsigned long>(background_processing_hr) << std::dec << "\n";
      passed = false;
    }
  }
  if (device->QueryInterface(IID_PPV_ARGS(&device7)) != S_OK || !device7) {
    std::cerr << "ID3D12Device7 vtable query failed\n";
    passed = false;
  } else {
    IUnknown *const output_sentinel = reinterpret_cast<IUnknown *>(static_cast<uintptr_t>(1));
    IUnknown *new_state_object = output_sentinel;
    const HRESULT add_to_state_object_hr = device7->AddToStateObject(
        nullptr, nullptr, __uuidof(ID3D12StateObject), reinterpret_cast<void **>(&new_state_object)
    );
    if (add_to_state_object_hr != E_NOTIMPL || new_state_object != nullptr) {
      std::cerr << "ID3D12Device7::AddToStateObject returned 0x" << std::hex
                << static_cast<unsigned long>(add_to_state_object_hr) << std::dec << "\n";
      passed = false;
    }
    if (new_state_object != output_sentinel)
      Release(new_state_object);

    IUnknown *protected_session = output_sentinel;
    const HRESULT protected_session_hr = device7->CreateProtectedResourceSession1(
        nullptr, __uuidof(ID3D12ProtectedResourceSession), reinterpret_cast<void **>(&protected_session)
    );
    if (protected_session_hr != E_NOTIMPL || protected_session != nullptr) {
      std::cerr << "ID3D12Device7::CreateProtectedResourceSession1 returned 0x" << std::hex
                << static_cast<unsigned long>(protected_session_hr) << std::dec << "\n";
      passed = false;
    }
    if (protected_session != output_sentinel)
      Release(protected_session);
  }
  if (device->QueryInterface(IID_PPV_ARGS(&device8)) != S_OK || !device8) {
    std::cerr << "ID3D12Device8 vtable query failed\n";
    passed = false;
  } else {
    D3D12_RESOURCE_DESC1 allocation_desc = {};
    D3D12_RESOURCE_ALLOCATION_INFO1 allocation_info1 = {1, 1, 1};
    const D3D12_RESOURCE_ALLOCATION_INFO allocation_info = device8->GetResourceAllocationInfo2(
        1, 1, &allocation_desc, &allocation_info1
    );
    if (allocation_info.SizeInBytes != UINT64_MAX || allocation_info.Alignment != UINT64_MAX) {
      std::cerr << "ID3D12Device8::GetResourceAllocationInfo2 returned a usable allocation\n";
      passed = false;
    }
    if (allocation_info1.Offset || allocation_info1.Alignment || allocation_info1.SizeInBytes) {
      std::cerr << "ID3D12Device8::GetResourceAllocationInfo2 returned allocation details\n";
      passed = false;
    }

    void *const output_sentinel = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
    void *committed_resource = output_sentinel;
    const HRESULT committed_resource_hr = device8->CreateCommittedResource2(
        nullptr, D3D12_HEAP_FLAG_NONE, nullptr, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr,
        __uuidof(ID3D12Resource), &committed_resource
    );
    if (committed_resource_hr != E_NOTIMPL || committed_resource != nullptr) {
      std::cerr << "ID3D12Device8::CreateCommittedResource2 returned 0x" << std::hex
                << static_cast<unsigned long>(committed_resource_hr) << std::dec << "\n";
      passed = false;
    }
    if (committed_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(committed_resource);
      Release(resource);
    }

    void *placed_resource = output_sentinel;
    const HRESULT placed_resource_hr = device8->CreatePlacedResource1(
        nullptr, 0, nullptr, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), &placed_resource
    );
    if (placed_resource_hr != E_NOTIMPL || placed_resource != nullptr) {
      std::cerr << "ID3D12Device8::CreatePlacedResource1 returned 0x" << std::hex
                << static_cast<unsigned long>(placed_resource_hr) << std::dec << "\n";
      passed = false;
    }
    if (placed_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(placed_resource);
      Release(resource);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
    device8->CreateSamplerFeedbackUnorderedAccessView(nullptr, nullptr, descriptor);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    layout.Offset = 1;
    layout.Footprint.Width = 1;
    UINT num_rows = 1;
    UINT64 row_size = 1;
    UINT64 total_bytes = 1;
    device8->GetCopyableFootprints1(nullptr, 0, 1, 0, &layout, &num_rows, &row_size, &total_bytes);
    if (layout.Offset != UINT64_MAX || layout.Footprint.Width != UINT_MAX || num_rows != UINT_MAX ||
        row_size != UINT64_MAX || total_bytes != UINT64_MAX) {
      std::cerr << "ID3D12Device8::GetCopyableFootprints1 returned invalid output\n";
      passed = false;
    }
  }
  if (device->QueryInterface(IID_PPV_ARGS(&device9)) != S_OK || !device9) {
    std::cerr << "ID3D12Device9 vtable query failed\n";
    passed = false;
  } else {
    void *const output_sentinel = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
    void *shader_cache_session = output_sentinel;
    const HRESULT shader_cache_session_hr = device9->CreateShaderCacheSession(
        nullptr, __uuidof(IUnknown), &shader_cache_session
    );
    if (shader_cache_session_hr != E_NOTIMPL || shader_cache_session != nullptr) {
      std::cerr << "ID3D12Device9::CreateShaderCacheSession returned 0x" << std::hex
                << static_cast<unsigned long>(shader_cache_session_hr) << std::dec << "\n";
      passed = false;
    }
    if (shader_cache_session != output_sentinel) {
      IUnknown *session = reinterpret_cast<IUnknown *>(shader_cache_session);
      Release(session);
    }

    const HRESULT shader_cache_control_hr = device9->ShaderCacheControl(
        static_cast<D3D12_SHADER_CACHE_KIND_FLAGS>(0), static_cast<D3D12_SHADER_CACHE_CONTROL_FLAGS>(0)
    );
    if (shader_cache_control_hr != E_NOTIMPL) {
      std::cerr << "ID3D12Device9::ShaderCacheControl returned 0x" << std::hex
                << static_cast<unsigned long>(shader_cache_control_hr) << std::dec << "\n";
      passed = false;
    }

    D3D12_COMMAND_QUEUE_DESC queue1_desc = {};
    queue1_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    void *command_queue1 = output_sentinel;
    const HRESULT command_queue1_hr = device9->CreateCommandQueue1(
        &queue1_desc, __uuidof(IUnknown), __uuidof(ID3D12CommandQueue), &command_queue1
    );
    if (command_queue1_hr != E_NOTIMPL || command_queue1 != nullptr) {
      std::cerr << "ID3D12Device9::CreateCommandQueue1 returned 0x" << std::hex
                << static_cast<unsigned long>(command_queue1_hr) << std::dec << "\n";
      passed = false;
    }
    if (command_queue1 != output_sentinel) {
      IUnknown *queue1 = reinterpret_cast<IUnknown *>(command_queue1);
      Release(queue1);
    }
  }
  if (device->QueryInterface(IID_PPV_ARGS(&device10)) != S_OK || !device10) {
    std::cerr << "ID3D12Device10 vtable query failed\n";
    passed = false;
  } else {
    void *const output_sentinel = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
    void *committed_resource = output_sentinel;
    const HRESULT committed_resource_hr = device10->CreateCommittedResource3(
        nullptr, D3D12_HEAP_FLAG_NONE, nullptr, static_cast<D3D12_BARRIER_LAYOUT>(0), nullptr, nullptr, 0,
        nullptr, __uuidof(ID3D12Resource), &committed_resource
    );
    if (committed_resource_hr != E_NOTIMPL || committed_resource != nullptr) {
      std::cerr << "ID3D12Device10::CreateCommittedResource3 returned 0x" << std::hex
                << static_cast<unsigned long>(committed_resource_hr) << std::dec << "\n";
      passed = false;
    }
    if (committed_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(committed_resource);
      Release(resource);
    }

    void *placed_resource = output_sentinel;
    const HRESULT placed_resource_hr = device10->CreatePlacedResource2(
        nullptr, 0, nullptr, static_cast<D3D12_BARRIER_LAYOUT>(0), nullptr, 0, nullptr,
        __uuidof(ID3D12Resource), &placed_resource
    );
    if (placed_resource_hr != E_NOTIMPL || placed_resource != nullptr) {
      std::cerr << "ID3D12Device10::CreatePlacedResource2 returned 0x" << std::hex
                << static_cast<unsigned long>(placed_resource_hr) << std::dec << "\n";
      passed = false;
    }
    if (placed_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(placed_resource);
      Release(resource);
    }

    void *reserved_resource = output_sentinel;
    const HRESULT reserved_resource_hr = device10->CreateReservedResource2(
        nullptr, static_cast<D3D12_BARRIER_LAYOUT>(0), nullptr, nullptr, 0, nullptr,
        __uuidof(ID3D12Resource), &reserved_resource
    );
    if (reserved_resource_hr != E_NOTIMPL || reserved_resource != nullptr) {
      std::cerr << "ID3D12Device10::CreateReservedResource2 returned 0x" << std::hex
                << static_cast<unsigned long>(reserved_resource_hr) << std::dec << "\n";
      passed = false;
    }
    if (reserved_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(reserved_resource);
      Release(resource);
    }
  }

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
  Release(device10);
  Release(device9);
  Release(device8);
  Release(device7);
  Release(device6);
  Release(device5);
  Release(allocator);
  Release(queue);
  Release(device);

  if (!passed)
    return 1;
  std::cout << "D3D12 interface support contract passed\n";
  return 0;
}

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

template <typename T, typename Record>
bool ExpectUnsupportedCommand(ID3D12Device *device, const char *name, Record &&record) {
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  T *typed_list = nullptr;
  bool passed = true;

  if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)) != S_OK) {
    std::cerr << name << " could not create command allocator\n";
    passed = false;
  } else if (device->CreateCommandList(
                 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list)
             ) != S_OK) {
    std::cerr << name << " could not create command list\n";
    passed = false;
  } else if (list->QueryInterface(IID_PPV_ARGS(&typed_list)) != S_OK || !typed_list) {
    std::cerr << name << " could not query command list interface\n";
    passed = false;
  } else {
    record(typed_list);
    if (list->Close() != E_FAIL) {
      std::cerr << name << " did not fail Close\n";
      passed = false;
    }
  }

  Release(typed_list);
  Release(list);
  Release(allocator);
  return passed;
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
  ID3D12GraphicsCommandList4 *list4 = nullptr;
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
    allocation_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    allocation_desc.Width = 4096;
    allocation_desc.Height = 1;
    allocation_desc.DepthOrArraySize = 1;
    allocation_desc.MipLevels = 1;
    allocation_desc.SampleDesc.Count = 1;
    allocation_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const D3D12_RESOURCE_ALLOCATION_INFO empty_allocation = device8->GetResourceAllocationInfo2(
        1, 0, nullptr, nullptr
    );
    if (empty_allocation.SizeInBytes || empty_allocation.Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
      std::cerr << "ID3D12Device8::GetResourceAllocationInfo2 mishandled an empty query\n";
      passed = false;
    }
    D3D12_RESOURCE_ALLOCATION_INFO1 missing_desc_info = {1, 1, 1};
    const D3D12_RESOURCE_ALLOCATION_INFO missing_desc_allocation = device8->GetResourceAllocationInfo2(
        1, 1, nullptr, &missing_desc_info
    );
    if (missing_desc_allocation.SizeInBytes != UINT64_MAX || missing_desc_allocation.Alignment != UINT64_MAX ||
        missing_desc_info.Offset || missing_desc_info.Alignment || missing_desc_info.SizeInBytes) {
      std::cerr << "ID3D12Device8::GetResourceAllocationInfo2 mishandled a missing descriptor\n";
      passed = false;
    }
    D3D12_RESOURCE_DESC1 allocation_descs[2] = {allocation_desc, allocation_desc};
    D3D12_RESOURCE_ALLOCATION_INFO1 allocation_info1[2] = {{1, 1, 1}, {1, 1, 1}};
    const D3D12_RESOURCE_ALLOCATION_INFO allocation_info = device8->GetResourceAllocationInfo2(
        1, 2, allocation_descs, allocation_info1
    );
    if (!allocation_info.SizeInBytes || !allocation_info.Alignment || allocation_info.SizeInBytes == UINT64_MAX ||
        allocation_info.Alignment == UINT64_MAX) {
      std::cerr << "ID3D12Device8::GetResourceAllocationInfo2 did not return a valid allocation\n";
      passed = false;
    }
    if (allocation_info1[0].Offset || allocation_info1[0].Alignment != allocation_info.Alignment ||
        !allocation_info1[0].SizeInBytes || allocation_info1[0].SizeInBytes == UINT64_MAX ||
        allocation_info1[1].Offset != allocation_info1[0].SizeInBytes ||
        allocation_info1[1].Alignment != allocation_info.Alignment ||
        allocation_info1[1].SizeInBytes != allocation_info1[0].SizeInBytes) {
      std::cerr << "ID3D12Device8::GetResourceAllocationInfo2 returned incorrect allocation details\n";
      passed = false;
    }
    const D3D12_RESOURCE_ALLOCATION_INFO allocation_without_details = device8->GetResourceAllocationInfo2(
        1, 2, allocation_descs, nullptr
    );
    if (allocation_without_details.SizeInBytes != allocation_info.SizeInBytes ||
        allocation_without_details.Alignment != allocation_info.Alignment) {
      std::cerr << "ID3D12Device8::GetResourceAllocationInfo2 mishandled a null details array\n";
      passed = false;
    }
    D3D12_RESOURCE_DESC1 invalid_allocation_descs[2] = {allocation_desc, allocation_desc};
    invalid_allocation_descs[1].Width = 0;
    D3D12_RESOURCE_ALLOCATION_INFO1 invalid_allocation_info[2] = {{1, 1, 1}, {1, 1, 1}};
    const D3D12_RESOURCE_ALLOCATION_INFO invalid_allocation = device8->GetResourceAllocationInfo2(
        1, 2, invalid_allocation_descs, invalid_allocation_info
    );
    if (invalid_allocation.SizeInBytes != UINT64_MAX ||
        invalid_allocation.Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT ||
        invalid_allocation_info[0].Offset || invalid_allocation_info[0].Alignment ||
        invalid_allocation_info[0].SizeInBytes || invalid_allocation_info[1].Offset ||
        invalid_allocation_info[1].Alignment || invalid_allocation_info[1].SizeInBytes) {
      std::cerr << "ID3D12Device8::GetResourceAllocationInfo2 leaked details from an invalid query\n";
      passed = false;
    }

    D3D12_HEAP_PROPERTIES heap_properties = {};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_properties.CreationNodeMask = 1;
    heap_properties.VisibleNodeMask = 1;
    ID3D12Resource *committed_resource = nullptr;
    const HRESULT committed_resource_hr = device8->CreateCommittedResource2(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &allocation_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr,
        IID_PPV_ARGS(&committed_resource)
    );
    if (committed_resource_hr != S_OK || !committed_resource) {
      std::cerr << "ID3D12Device8::CreateCommittedResource2 returned 0x" << std::hex
                << static_cast<unsigned long>(committed_resource_hr) << std::dec << "\n";
      passed = false;
    }
    Release(committed_resource);
    const HRESULT committed_resource_probe_hr = device8->CreateCommittedResource2(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &allocation_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr,
        __uuidof(ID3D12Resource), nullptr
    );
    if (committed_resource_probe_hr != S_FALSE) {
      std::cerr << "ID3D12Device8::CreateCommittedResource2 did not support a null output pointer\n";
      passed = false;
    }
    ID3D12Resource *const null_desc_committed_resource_sentinel =
        reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
    ID3D12Resource *null_desc_committed_resource = null_desc_committed_resource_sentinel;
    const HRESULT null_desc_committed_resource_hr = device8->CreateCommittedResource2(
        &heap_properties, D3D12_HEAP_FLAG_NONE, nullptr, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr,
        IID_PPV_ARGS(&null_desc_committed_resource)
    );
    if (null_desc_committed_resource_hr != E_INVALIDARG || null_desc_committed_resource != nullptr) {
      std::cerr << "ID3D12Device8::CreateCommittedResource2 mishandled a null descriptor\n";
      passed = false;
    }
    if (null_desc_committed_resource != null_desc_committed_resource_sentinel)
      Release(null_desc_committed_resource);

    ID3D12ProtectedResourceSession *const protected_session = reinterpret_cast<ID3D12ProtectedResourceSession *>(
        static_cast<uintptr_t>(1)
    );
    ID3D12Resource *const protected_resource_sentinel = reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
    ID3D12Resource *protected_resource = protected_resource_sentinel;
    const HRESULT protected_resource_hr = device8->CreateCommittedResource2(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &allocation_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, protected_session,
        IID_PPV_ARGS(&protected_resource)
    );
    if (protected_resource_hr != E_NOTIMPL || protected_resource != nullptr) {
      std::cerr << "ID3D12Device8::CreateCommittedResource2 did not reject protected resources explicitly\n";
      passed = false;
    }
    if (protected_resource != protected_resource_sentinel)
      Release(protected_resource);

    ID3D12Heap *heap = nullptr;
    D3D12_HEAP_DESC heap_desc = {allocation_info.SizeInBytes, heap_properties, D3D12_HEAP_FLAG_NONE};
    if (!allocation_info.SizeInBytes || allocation_info.SizeInBytes == UINT64_MAX) {
      std::cerr << "Skipping ID3D12Device8::CreatePlacedResource1 with invalid allocation\n";
      passed = false;
    } else if (device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap)) != S_OK) {
      std::cerr << "CreateHeap for ID3D12Device8::CreatePlacedResource1 failed\n";
      passed = false;
    } else if (!heap) {
      std::cerr << "CreateHeap returned a null heap for ID3D12Device8::CreatePlacedResource1\n";
      passed = false;
    } else {
      ID3D12Resource *const null_desc_resource_sentinel =
          reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
      ID3D12Resource *null_desc_resource = null_desc_resource_sentinel;
      const HRESULT null_desc_resource_hr = device8->CreatePlacedResource1(
          heap, 0, nullptr, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&null_desc_resource)
      );
      if (null_desc_resource_hr != E_INVALIDARG || null_desc_resource != nullptr) {
        std::cerr << "ID3D12Device8::CreatePlacedResource1 mishandled a null descriptor\n";
        passed = false;
      }
      if (null_desc_resource != null_desc_resource_sentinel)
        Release(null_desc_resource);

      ID3D12Resource *placed_resource = nullptr;
      const HRESULT placed_resource_hr = device8->CreatePlacedResource1(
          heap, 0, &allocation_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
          reinterpret_cast<void **>(&placed_resource)
      );
      if (placed_resource_hr != S_OK || !placed_resource) {
        std::cerr << "ID3D12Device8::CreatePlacedResource1 returned 0x" << std::hex
                  << static_cast<unsigned long>(placed_resource_hr) << std::dec << "\n";
        passed = false;
      }
      Release(placed_resource);
      const HRESULT placed_resource_probe_hr = device8->CreatePlacedResource1(
          heap, 0, &allocation_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), nullptr
      );
      if (placed_resource_probe_hr != S_FALSE) {
        std::cerr << "ID3D12Device8::CreatePlacedResource1 did not support a null output pointer\n";
        passed = false;
      }
    }
    Release(heap);

    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
    device8->CreateSamplerFeedbackUnorderedAccessView(nullptr, nullptr, descriptor);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    layout.Offset = 1;
    layout.Footprint.Format = DXGI_FORMAT_R8_UNORM;
    layout.Footprint.Width = 1;
    layout.Footprint.Height = 1;
    layout.Footprint.Depth = 1;
    layout.Footprint.RowPitch = 1;
    UINT num_rows = 1;
    UINT64 row_size = 1;
    UINT64 total_bytes = 1;
    device8->GetCopyableFootprints1(nullptr, 0, 1, 0, &layout, &num_rows, &row_size, &total_bytes);
    if (layout.Offset != UINT64_MAX || layout.Footprint.Width != UINT_MAX || num_rows != UINT_MAX ||
        layout.Footprint.Format != static_cast<DXGI_FORMAT>(~0u) || layout.Footprint.Height != UINT_MAX ||
        layout.Footprint.Depth != UINT_MAX || layout.Footprint.RowPitch != UINT_MAX ||
        row_size != UINT64_MAX || total_bytes != UINT64_MAX) {
      std::cerr << "ID3D12Device8::GetCopyableFootprints1 returned invalid output\n";
      passed = false;
    }

    D3D12_RESOURCE_DESC base_desc = {};
    base_desc.Dimension = allocation_desc.Dimension;
    base_desc.Alignment = allocation_desc.Alignment;
    base_desc.Width = allocation_desc.Width;
    base_desc.Height = allocation_desc.Height;
    base_desc.DepthOrArraySize = allocation_desc.DepthOrArraySize;
    base_desc.MipLevels = allocation_desc.MipLevels;
    base_desc.Format = allocation_desc.Format;
    base_desc.SampleDesc = allocation_desc.SampleDesc;
    base_desc.Layout = allocation_desc.Layout;
    base_desc.Flags = allocation_desc.Flags;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT expected_layout = {};
    UINT expected_num_rows = 0;
    UINT64 expected_row_size = 0;
    UINT64 expected_total_bytes = 0;
    device->GetCopyableFootprints(
        &base_desc, 0, 1, 0, &expected_layout, &expected_num_rows, &expected_row_size, &expected_total_bytes
    );
    const auto footprints_match = [](const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &actual,
                                     const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &expected) {
      return actual.Offset == expected.Offset && actual.Footprint.Format == expected.Footprint.Format &&
             actual.Footprint.Width == expected.Footprint.Width && actual.Footprint.Height == expected.Footprint.Height &&
             actual.Footprint.Depth == expected.Footprint.Depth && actual.Footprint.RowPitch == expected.Footprint.RowPitch;
    };
    layout = {};
    num_rows = 0;
    row_size = 0;
    total_bytes = 0;
    device8->GetCopyableFootprints1(
        &allocation_desc, 0, 1, 0, &layout, &num_rows, &row_size, &total_bytes
    );
    if (!footprints_match(layout, expected_layout) || num_rows != expected_num_rows ||
        row_size != expected_row_size || total_bytes != expected_total_bytes) {
      std::cerr << "ID3D12Device8::GetCopyableFootprints1 disagrees with the base footprint query\n";
      passed = false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT empty_layout = {};
    empty_layout.Offset = 1;
    empty_layout.Footprint.Format = DXGI_FORMAT_R8_UNORM;
    empty_layout.Footprint.Width = 1;
    empty_layout.Footprint.Height = 1;
    empty_layout.Footprint.Depth = 1;
    empty_layout.Footprint.RowPitch = 1;
    UINT empty_num_rows = 1;
    UINT64 empty_row_size = 1;
    UINT64 empty_total_bytes = 1;
    device8->GetCopyableFootprints1(
        &allocation_desc, 0, 0, 0, &empty_layout, &empty_num_rows, &empty_row_size, &empty_total_bytes
    );
    if (empty_layout.Offset != 1 || empty_layout.Footprint.Format != DXGI_FORMAT_R8_UNORM ||
        empty_layout.Footprint.Width != 1 || empty_layout.Footprint.Height != 1 || empty_layout.Footprint.Depth != 1 ||
        empty_layout.Footprint.RowPitch != 1 || empty_num_rows != 1 || empty_row_size != 1 || empty_total_bytes != 0) {
      std::cerr << "ID3D12Device8::GetCopyableFootprints1 mishandled an empty query\n";
      passed = false;
    }

    UINT optional_num_rows = 0;
    UINT64 optional_row_size = 0;
    UINT64 optional_total_bytes = 0;
    device8->GetCopyableFootprints1(
        &allocation_desc, 0, 1, 0, nullptr, &optional_num_rows, &optional_row_size, &optional_total_bytes
    );
    if (optional_num_rows != expected_num_rows || optional_row_size != expected_row_size ||
        optional_total_bytes != expected_total_bytes) {
      std::cerr << "ID3D12Device8::GetCopyableFootprints1 mishandled a null layouts array\n";
      passed = false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT optional_layout = {};
    optional_row_size = 0;
    optional_total_bytes = 0;
    device8->GetCopyableFootprints1(
        &allocation_desc, 0, 1, 0, &optional_layout, nullptr, &optional_row_size, &optional_total_bytes
    );
    if (!footprints_match(optional_layout, expected_layout) || optional_row_size != expected_row_size ||
        optional_total_bytes != expected_total_bytes) {
      std::cerr << "ID3D12Device8::GetCopyableFootprints1 mishandled a null row-count array\n";
      passed = false;
    }

    optional_layout = {};
    optional_num_rows = 0;
    optional_total_bytes = 0;
    device8->GetCopyableFootprints1(
        &allocation_desc, 0, 1, 0, &optional_layout, &optional_num_rows, nullptr, &optional_total_bytes
    );
    if (!footprints_match(optional_layout, expected_layout) || optional_num_rows != expected_num_rows ||
        optional_total_bytes != expected_total_bytes) {
      std::cerr << "ID3D12Device8::GetCopyableFootprints1 mishandled a null row-size array\n";
      passed = false;
    }

    optional_layout = {};
    optional_num_rows = 0;
    optional_row_size = 0;
    device8->GetCopyableFootprints1(
        &allocation_desc, 0, 1, 0, &optional_layout, &optional_num_rows, &optional_row_size, nullptr
    );
    if (!footprints_match(optional_layout, expected_layout) || optional_num_rows != expected_num_rows ||
        optional_row_size != expected_row_size) {
      std::cerr << "ID3D12Device8::GetCopyableFootprints1 mishandled a null total-size output\n";
      passed = false;
    }

    D3D12_RESOURCE_DESC1 sampler_feedback_desc = allocation_desc;
    sampler_feedback_desc.SamplerFeedbackMipRegion.Width = 1;
    D3D12_RESOURCE_ALLOCATION_INFO1 sampler_feedback_info = {1, 1, 1};
    const D3D12_RESOURCE_ALLOCATION_INFO sampler_feedback_allocation = device8->GetResourceAllocationInfo2(
        1, 1, &sampler_feedback_desc, &sampler_feedback_info
    );
    if (sampler_feedback_allocation.SizeInBytes != UINT64_MAX ||
        sampler_feedback_allocation.Alignment != UINT64_MAX || sampler_feedback_info.Offset ||
        sampler_feedback_info.Alignment || sampler_feedback_info.SizeInBytes) {
      std::cerr << "ID3D12Device8 accepted sampler-feedback allocation details\n";
      passed = false;
    }

    ID3D12Resource *const sampler_feedback_resource_sentinel =
        reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
    ID3D12Resource *sampler_feedback_resource = sampler_feedback_resource_sentinel;
    const HRESULT sampler_feedback_resource_hr = device8->CreateCommittedResource2(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &sampler_feedback_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        nullptr, IID_PPV_ARGS(&sampler_feedback_resource)
    );
    if (sampler_feedback_resource_hr != E_NOTIMPL || sampler_feedback_resource != nullptr) {
      std::cerr << "ID3D12Device8 accepted sampler-feedback resource creation\n";
      passed = false;
    }
    if (sampler_feedback_resource != sampler_feedback_resource_sentinel)
      Release(sampler_feedback_resource);

    ID3D12Heap *sampler_feedback_heap = nullptr;
    if (device->CreateHeap(&heap_desc, IID_PPV_ARGS(&sampler_feedback_heap)) != S_OK || !sampler_feedback_heap) {
      std::cerr << "CreateHeap for sampler-feedback rejection failed\n";
      passed = false;
    } else {
      ID3D12Resource *const sampler_feedback_placed_resource_sentinel =
          reinterpret_cast<ID3D12Resource *>(static_cast<uintptr_t>(1));
      ID3D12Resource *sampler_feedback_placed_resource = sampler_feedback_placed_resource_sentinel;
      const HRESULT sampler_feedback_placed_hr = device8->CreatePlacedResource1(
          sampler_feedback_heap, 0, &sampler_feedback_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
          __uuidof(ID3D12Resource), reinterpret_cast<void **>(&sampler_feedback_placed_resource)
      );
      if (sampler_feedback_placed_hr != E_NOTIMPL || sampler_feedback_placed_resource != nullptr) {
        std::cerr << "ID3D12Device8 accepted sampler-feedback placed resource creation\n";
        passed = false;
      }
      if (sampler_feedback_placed_resource != sampler_feedback_placed_resource_sentinel)
        Release(sampler_feedback_placed_resource);
    }
    Release(sampler_feedback_heap);

    layout = {};
    layout.Offset = 1;
    layout.Footprint.Format = DXGI_FORMAT_R8_UNORM;
    layout.Footprint.Width = 1;
    layout.Footprint.Height = 1;
    layout.Footprint.Depth = 1;
    layout.Footprint.RowPitch = 1;
    num_rows = 1;
    row_size = 1;
    total_bytes = 1;
    device8->GetCopyableFootprints1(
        &sampler_feedback_desc, 0, 1, 0, &layout, &num_rows, &row_size, &total_bytes
    );
    if (layout.Offset != UINT64_MAX || layout.Footprint.Format != static_cast<DXGI_FORMAT>(~0u) ||
        layout.Footprint.Width != UINT_MAX || layout.Footprint.Height != UINT_MAX ||
        layout.Footprint.Depth != UINT_MAX || layout.Footprint.RowPitch != UINT_MAX ||
        num_rows != UINT_MAX || row_size != UINT64_MAX || total_bytes != UINT64_MAX) {
      std::cerr << "ID3D12Device8 accepted sampler-feedback footprint details\n";
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

    D3D12_HEAP_DESC heap1_desc = {};
    heap1_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap1_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap1_desc.Properties.CreationNodeMask = 1;
    heap1_desc.Properties.VisibleNodeMask = 1;
    void *heap1 = output_sentinel;
    const HRESULT heap1_hr = device9->CreateHeap1(&heap1_desc, nullptr, __uuidof(ID3D12Heap), &heap1);
    if (heap1_hr != S_OK || heap1 == nullptr) {
      std::cerr << "ID3D12Device9::CreateHeap1 returned 0x" << std::hex << static_cast<unsigned long>(heap1_hr)
                << std::dec << "\n";
      passed = false;
    }
    if (heap1 != output_sentinel) {
      IUnknown *heap = reinterpret_cast<IUnknown *>(heap1);
      Release(heap);
    }

    void *command_list1 = output_sentinel;
    const HRESULT command_list1_hr = device9->CreateCommandList1(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, __uuidof(ID3D12GraphicsCommandList),
        &command_list1
    );
    if (command_list1_hr != S_OK || command_list1 == nullptr) {
      std::cerr << "ID3D12Device9::CreateCommandList1 returned 0x" << std::hex
                << static_cast<unsigned long>(command_list1_hr) << std::dec << "\n";
      passed = false;
    } else {
      auto *list1 = reinterpret_cast<ID3D12GraphicsCommandList *>(command_list1);
      if (list1->Close() != E_FAIL) {
        std::cerr << "ID3D12Device9::CreateCommandList1 did not return a closed command list\n";
        passed = false;
      }
    }
    if (command_list1 != output_sentinel) {
      IUnknown *list = reinterpret_cast<IUnknown *>(command_list1);
      Release(list);
    }

    D3D12_COMMAND_QUEUE_DESC queue1_desc = {};
    queue1_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    void *command_queue1 = output_sentinel;
    const HRESULT command_queue1_hr = device9->CreateCommandQueue1(
        &queue1_desc, __uuidof(IUnknown), __uuidof(ID3D12CommandQueue), &command_queue1
    );
    if (command_queue1_hr != S_OK || command_queue1 == nullptr) {
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
    D3D12_RESOURCE_DESC1 resource_desc = {};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = 4096;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    void *committed_resource = output_sentinel;
    const HRESULT committed_resource_hr = device10->CreateCommittedResource3(
        nullptr, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, nullptr, 0, nullptr,
        __uuidof(ID3D12Resource), &committed_resource
    );
    if (committed_resource_hr != E_INVALIDARG || committed_resource != nullptr) {
      std::cerr << "ID3D12Device10::CreateCommittedResource3 returned 0x" << std::hex
                << static_cast<unsigned long>(committed_resource_hr) << std::dec << "\n";
      passed = false;
    }
    if (committed_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(committed_resource);
      Release(resource);
    }

    D3D12_HEAP_PROPERTIES heap_properties = {};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_properties.CreationNodeMask = 1;
    heap_properties.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC base_desc = {};
    base_desc.Dimension = resource_desc.Dimension;
    base_desc.Width = resource_desc.Width;
    base_desc.Height = resource_desc.Height;
    base_desc.DepthOrArraySize = resource_desc.DepthOrArraySize;
    base_desc.MipLevels = resource_desc.MipLevels;
    base_desc.Format = resource_desc.Format;
    base_desc.SampleDesc = resource_desc.SampleDesc;
    base_desc.Layout = resource_desc.Layout;
    base_desc.Flags = resource_desc.Flags;
    D3D12_RESOURCE_ALLOCATION_INFO allocation_info = {};
    device->GetResourceAllocationInfo(&allocation_info, 1, 1, &base_desc);
    D3D12_HEAP_DESC heap_desc = {allocation_info.SizeInBytes, heap_properties, D3D12_HEAP_FLAG_NONE};
    ID3D12Heap *heap = nullptr;
    if (!allocation_info.SizeInBytes || allocation_info.SizeInBytes == UINT64_MAX ||
        device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap)) != S_OK || !heap) {
      std::cerr << "CreateHeap for ID3D12Device10::CreatePlacedResource2 failed\n";
      passed = false;
    } else {
      void *placed_resource = output_sentinel;
      void *null_desc_resource = output_sentinel;
      const HRESULT null_desc_resource_hr = device10->CreatePlacedResource2(
          heap, 0, nullptr, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0, nullptr,
          __uuidof(ID3D12Resource), &null_desc_resource
      );
      if (null_desc_resource_hr != E_INVALIDARG || null_desc_resource != nullptr) {
        std::cerr << "ID3D12Device10::CreatePlacedResource2 mishandled a null descriptor\n";
        passed = false;
      }
      if (null_desc_resource != output_sentinel) {
        IUnknown *resource = reinterpret_cast<IUnknown *>(null_desc_resource);
        Release(resource);
      }

      const HRESULT placed_resource_hr = device10->CreatePlacedResource2(
          heap, 0, &resource_desc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0, nullptr,
          __uuidof(ID3D12Resource), &placed_resource
      );
      if (placed_resource_hr != S_OK || !placed_resource) {
        std::cerr << "ID3D12Device10::CreatePlacedResource2 returned 0x" << std::hex
                  << static_cast<unsigned long>(placed_resource_hr) << std::dec << "\n";
        passed = false;
      }
      if (placed_resource != output_sentinel) {
        IUnknown *resource = reinterpret_cast<IUnknown *>(placed_resource);
        Release(resource);
      }

      const HRESULT placed_resource_probe_hr = device10->CreatePlacedResource2(
          heap, 0, &resource_desc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0, nullptr,
          __uuidof(ID3D12Resource), nullptr
      );
      if (placed_resource_probe_hr != S_FALSE) {
        std::cerr << "ID3D12Device10::CreatePlacedResource2 did not support a null output pointer\n";
        passed = false;
      }

      void *invalid_layout_resource = output_sentinel;
      const HRESULT invalid_layout_hr = device10->CreatePlacedResource2(
          heap, 0, &resource_desc, D3D12_BARRIER_LAYOUT_COMMON, nullptr, 0, nullptr,
          __uuidof(ID3D12Resource), &invalid_layout_resource
      );
      if (invalid_layout_hr != E_NOTIMPL || invalid_layout_resource != nullptr) {
        std::cerr << "ID3D12Device10::CreatePlacedResource2 accepted a buffer layout\n";
        passed = false;
      }
      if (invalid_layout_resource != output_sentinel) {
        IUnknown *resource = reinterpret_cast<IUnknown *>(invalid_layout_resource);
        Release(resource);
      }

      DXGI_FORMAT castable_format = DXGI_FORMAT_R8_UNORM;
      void *castable_resource = output_sentinel;
      const HRESULT castable_resource_hr = device10->CreatePlacedResource2(
          heap, 0, &resource_desc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 1, &castable_format,
          __uuidof(ID3D12Resource), &castable_resource
      );
      if (castable_resource_hr != E_NOTIMPL || castable_resource != nullptr) {
        std::cerr << "ID3D12Device10::CreatePlacedResource2 accepted castable formats\n";
        passed = false;
      }
      if (castable_resource != output_sentinel) {
        IUnknown *resource = reinterpret_cast<IUnknown *>(castable_resource);
        Release(resource);
      }
    }
    Release(heap);

    D3D12_RESOURCE_DESC1 texture_desc = resource_desc;
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = 4;
    texture_desc.Height = 4;
    texture_desc.Format = DXGI_FORMAT_R8_UNORM;
    texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    void *texture_resource = output_sentinel;
    const HRESULT texture_resource_hr = device10->CreateCommittedResource3(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &texture_desc, D3D12_BARRIER_LAYOUT_COMMON, nullptr, nullptr, 0,
        nullptr, __uuidof(ID3D12Resource), &texture_resource
    );
    if (texture_resource_hr != S_OK || texture_resource == nullptr) {
      std::cerr << "ID3D12Device10::CreateCommittedResource3 rejected a common texture\n";
      passed = false;
    }
    if (texture_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(texture_resource);
      Release(resource);
    }

    D3D12_RESOURCE_DESC texture_base_desc = base_desc;
    texture_base_desc.Dimension = texture_desc.Dimension;
    texture_base_desc.Width = texture_desc.Width;
    texture_base_desc.Height = texture_desc.Height;
    texture_base_desc.DepthOrArraySize = texture_desc.DepthOrArraySize;
    texture_base_desc.MipLevels = texture_desc.MipLevels;
    texture_base_desc.Format = texture_desc.Format;
    texture_base_desc.SampleDesc = texture_desc.SampleDesc;
    texture_base_desc.Layout = texture_desc.Layout;
    texture_base_desc.Flags = texture_desc.Flags;
    D3D12_RESOURCE_ALLOCATION_INFO texture_allocation_info = {};
    device->GetResourceAllocationInfo(&texture_allocation_info, 1, 1, &texture_base_desc);
    D3D12_HEAP_DESC texture_heap_desc = {
        texture_allocation_info.SizeInBytes, heap_properties, D3D12_HEAP_FLAG_NONE
    };
    ID3D12Heap *texture_heap = nullptr;
    if (!texture_allocation_info.SizeInBytes || texture_allocation_info.SizeInBytes == UINT64_MAX ||
        device->CreateHeap(&texture_heap_desc, IID_PPV_ARGS(&texture_heap)) != S_OK || !texture_heap) {
      std::cerr << "CreateHeap for ID3D12Device10 texture resource failed\n";
      passed = false;
    } else {
      void *placed_texture_resource = output_sentinel;
      const HRESULT placed_texture_resource_hr = device10->CreatePlacedResource2(
          texture_heap, 0, &texture_desc, D3D12_BARRIER_LAYOUT_COMMON, nullptr, 0, nullptr,
          __uuidof(ID3D12Resource), &placed_texture_resource
      );
      if (placed_texture_resource_hr != S_OK || placed_texture_resource == nullptr) {
        std::cerr << "ID3D12Device10::CreatePlacedResource2 rejected a common texture\n";
        passed = false;
      }
      if (placed_texture_resource != output_sentinel) {
        IUnknown *resource = reinterpret_cast<IUnknown *>(placed_texture_resource);
        Release(resource);
      }
    }
    Release(texture_heap);

    const D3D12_BARRIER_LAYOUT video_layouts[] = {
        D3D12_BARRIER_LAYOUT_VIDEO_DECODE_READ,
        D3D12_BARRIER_LAYOUT_VIDEO_DECODE_WRITE,
        D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_READ,
        D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_WRITE,
        D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_READ,
        D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_WRITE,
    };
    for (const auto layout : video_layouts) {
      void *video_resource = output_sentinel;
      const HRESULT video_resource_hr = device10->CreateCommittedResource3(
          &heap_properties, D3D12_HEAP_FLAG_NONE, &texture_desc, layout, nullptr, nullptr, 0, nullptr,
          __uuidof(ID3D12Resource), &video_resource
      );
      if (video_resource_hr != S_OK || video_resource == nullptr) {
        std::cerr << "ID3D12Device10::CreateCommittedResource3 rejected a video layout\n";
        passed = false;
      }
      if (video_resource != output_sentinel) {
        IUnknown *resource = reinterpret_cast<IUnknown *>(video_resource);
        Release(resource);
      }
    }

    void *invalid_layout_resource = output_sentinel;
    const HRESULT invalid_layout_hr = device10->CreateCommittedResource3(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_BARRIER_LAYOUT_COMMON, nullptr, nullptr, 0,
        nullptr, __uuidof(ID3D12Resource), &invalid_layout_resource
    );
    if (invalid_layout_hr != E_NOTIMPL || invalid_layout_resource != nullptr) {
      std::cerr << "ID3D12Device10::CreateCommittedResource3 accepted a buffer layout\n";
      passed = false;
    }
    if (invalid_layout_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(invalid_layout_resource);
      Release(resource);
    }

    const HRESULT committed_resource_probe_hr = device10->CreateCommittedResource3(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, nullptr, 0,
        nullptr, __uuidof(ID3D12Resource), nullptr
    );
    if (committed_resource_probe_hr != S_FALSE) {
      std::cerr << "ID3D12Device10::CreateCommittedResource3 did not support a null output pointer\n";
      passed = false;
    }

    void *null_desc_resource = output_sentinel;
    const HRESULT null_desc_resource_hr = device10->CreateCommittedResource3(
        &heap_properties, D3D12_HEAP_FLAG_NONE, nullptr, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, nullptr, 0,
        nullptr, __uuidof(ID3D12Resource), &null_desc_resource
    );
    if (null_desc_resource_hr != E_INVALIDARG || null_desc_resource != nullptr) {
      std::cerr << "ID3D12Device10::CreateCommittedResource3 mishandled a null descriptor\n";
      passed = false;
    }
    if (null_desc_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(null_desc_resource);
      Release(resource);
    }

    ID3D12ProtectedResourceSession *const protected_session = reinterpret_cast<ID3D12ProtectedResourceSession *>(
        static_cast<uintptr_t>(1)
    );
    void *protected_resource = output_sentinel;
    const HRESULT protected_resource_hr = device10->CreateCommittedResource3(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr,
        protected_session, 0, nullptr, __uuidof(ID3D12Resource), &protected_resource
    );
    if (protected_resource_hr != E_NOTIMPL || protected_resource != nullptr) {
      std::cerr << "ID3D12Device10::CreateCommittedResource3 accepted protected resources\n";
      passed = false;
    }
    if (protected_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(protected_resource);
      Release(resource);
    }

    DXGI_FORMAT castable_format = DXGI_FORMAT_R8_UNORM;
    void *castable_resource = output_sentinel;
    const HRESULT castable_resource_hr = device10->CreateCommittedResource3(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, nullptr, 1,
        &castable_format, __uuidof(ID3D12Resource), &castable_resource
    );
    if (castable_resource_hr != E_NOTIMPL || castable_resource != nullptr) {
      std::cerr << "ID3D12Device10::CreateCommittedResource3 accepted castable formats\n";
      passed = false;
    }
    if (castable_resource != output_sentinel) {
      IUnknown *resource = reinterpret_cast<IUnknown *>(castable_resource);
      Release(resource);
    }

    void *reserved_resource = output_sentinel;
    const HRESULT reserved_resource_hr = device10->CreateReservedResource2(
        nullptr, D3D12_BARRIER_LAYOUT_COMMON, nullptr, nullptr, 0, nullptr,
        __uuidof(ID3D12Resource), &reserved_resource
    );
    if (reserved_resource_hr != E_INVALIDARG || reserved_resource != nullptr) {
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
    passed &= ExpectQuery<ID3D12GraphicsCommandList4>(list, "ID3D12GraphicsCommandList4", S_OK);
    passed &= ExpectQuery<ID3D12GraphicsCommandList5>(list, "ID3D12GraphicsCommandList5", S_OK);
    passed &= ExpectQuery<ID3D12GraphicsCommandList6>(list, "ID3D12GraphicsCommandList6", S_OK);
    passed &= ExpectQuery<ID3D12GraphicsCommandList7>(list, "ID3D12GraphicsCommandList7", S_OK);
    if (list->QueryInterface(IID_PPV_ARGS(&list3)) != S_OK || !list3) {
      std::cerr << "ID3D12GraphicsCommandList3 vtable query failed\n";
      passed = false;
    } else {
      list3->SetProtectedResourceSession(nullptr);
    }
    if (list->QueryInterface(IID_PPV_ARGS(&list4)) != S_OK || !list4) {
      std::cerr << "ID3D12GraphicsCommandList4 vtable query failed\n";
      passed = false;
    } else {
      list4->BeginRenderPass(0, nullptr, nullptr, static_cast<D3D12_RENDER_PASS_FLAGS>(0));
      list4->EndRenderPass();
      list4->InitializeMetaCommand(nullptr, nullptr, 0);
      list4->ExecuteMetaCommand(nullptr, nullptr, 0);
      list4->BuildRaytracingAccelerationStructure(nullptr, 0, nullptr);
      list4->EmitRaytracingAccelerationStructurePostbuildInfo(nullptr, 0, nullptr);
      list4->CopyRaytracingAccelerationStructure(
          0, 0, static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE>(0)
      );
      list4->SetPipelineState1(nullptr);
      list4->DispatchRays(nullptr);
      if (list->Close() != E_FAIL) {
        std::cerr << "ID3D12GraphicsCommandList4 unsupported command did not fail Close\n";
        passed = false;
      }
    }
  }

  passed &= ExpectUnsupportedCommand<ID3D12GraphicsCommandList5>(
      device, "RSSetShadingRate", [](ID3D12GraphicsCommandList5 *list) {
        list->RSSetShadingRate(static_cast<D3D12_SHADING_RATE>(0), nullptr);
      }
  );
  passed &= ExpectUnsupportedCommand<ID3D12GraphicsCommandList5>(
      device, "RSSetShadingRateImage", [](ID3D12GraphicsCommandList5 *list) {
        list->RSSetShadingRateImage(nullptr);
      }
  );
  passed &= ExpectUnsupportedCommand<ID3D12GraphicsCommandList6>(
      device, "DispatchMesh", [](ID3D12GraphicsCommandList6 *list) {
        list->DispatchMesh(0, 0, 0);
      }
  );
  {
    D3D12_HEAP_PROPERTIES heap_properties = {};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC buffer_desc = {};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = 4096;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_RESOURCE_DESC texture_desc = {};
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = 4;
    texture_desc.Height = 4;
    texture_desc.DepthOrArraySize = 2;
    texture_desc.MipLevels = 2;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ID3D12Resource *buffer = nullptr;
    ID3D12Resource *texture = nullptr;
    ID3D12CommandAllocator *barrier_allocator = nullptr;
    ID3D12GraphicsCommandList *barrier_list = nullptr;
    ID3D12GraphicsCommandList7 *barrier_list7 = nullptr;

    if (device->CreateCommittedResource(
            &heap_properties, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&buffer)
        ) != S_OK ||
        device->CreateCommittedResource(
            &heap_properties, D3D12_HEAP_FLAG_NONE, &texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&texture)
        ) != S_OK) {
      std::cerr << "Barrier resource creation failed\n";
      passed = false;
    } else if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&barrier_allocator)) != S_OK ||
               device->CreateCommandList(
                   0, D3D12_COMMAND_LIST_TYPE_DIRECT, barrier_allocator, nullptr, IID_PPV_ARGS(&barrier_list)
               ) != S_OK ||
               barrier_list->QueryInterface(IID_PPV_ARGS(&barrier_list7)) != S_OK || !barrier_list7) {
      std::cerr << "Barrier command list creation failed\n";
      passed = false;
    } else {
      barrier_list7->Barrier(0, nullptr);

      D3D12_GLOBAL_BARRIER global_barrier = {};
      global_barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
      global_barrier.SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
      global_barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
      global_barrier.AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
      D3D12_BARRIER_GROUP global_group = {};
      global_group.Type = D3D12_BARRIER_TYPE_GLOBAL;
      global_group.NumBarriers = 1;
      global_group.pGlobalBarriers = &global_barrier;
      barrier_list7->Barrier(1, &global_group);

      D3D12_BUFFER_BARRIER buffer_barrier = {};
      buffer_barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
      buffer_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
      buffer_barrier.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
      buffer_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
      buffer_barrier.pResource = buffer;
      buffer_barrier.Size = UINT64_MAX;
      D3D12_BARRIER_GROUP buffer_group = {};
      buffer_group.Type = D3D12_BARRIER_TYPE_BUFFER;
      buffer_group.NumBarriers = 1;
      buffer_group.pBufferBarriers = &buffer_barrier;
      barrier_list7->Barrier(1, &buffer_group);

      D3D12_TEXTURE_BARRIER texture_barrier = {};
      texture_barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
      texture_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
      texture_barrier.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
      texture_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
      texture_barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
      texture_barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_DEST;
      texture_barrier.pResource = texture;
      texture_barrier.Subresources = {D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, 0, 0, 0, 0, 0};
      D3D12_BARRIER_GROUP texture_group = {};
      texture_group.Type = D3D12_BARRIER_TYPE_TEXTURE;
      texture_group.NumBarriers = 1;
      texture_group.pTextureBarriers = &texture_barrier;
      barrier_list7->Barrier(1, &texture_group);

      texture_barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
      texture_barrier.SyncAfter = D3D12_BARRIER_SYNC_PIXEL_SHADING;
      texture_barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
      texture_barrier.AccessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
      texture_barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST;
      texture_barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
      texture_barrier.Subresources = {0, 1, 0, 1, 0, 1};
      barrier_list7->Barrier(1, &texture_group);

      if (barrier_list->Close() != S_OK) {
        std::cerr << "Barrier command list did not close successfully\n";
        passed = false;
      }
    }

    Release(barrier_list7);
    Release(barrier_list);
    Release(barrier_allocator);
    Release(texture);
    Release(buffer);
  }
  passed &= ExpectUnsupportedCommand<ID3D12GraphicsCommandList7>(
      device, "Barrier(null groups)", [](ID3D12GraphicsCommandList7 *list) {
        list->Barrier(1, nullptr);
      }
  );

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
  Release(list4);
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

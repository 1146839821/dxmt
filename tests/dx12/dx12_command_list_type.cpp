#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <iostream>

namespace {

bool CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

} // namespace

int main() {
  ID3D12Device *device = nullptr;
  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return 1;

  const D3D12_COMMAND_LIST_TYPE types[] = {
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      D3D12_COMMAND_LIST_TYPE_COMPUTE,
      D3D12_COMMAND_LIST_TYPE_COPY,
      D3D12_COMMAND_LIST_TYPE_BUNDLE,
  };

  int result = 0;
  for (auto type : types) {
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    if (!CheckHR("CreateCommandAllocator", device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator))) ||
        !CheckHR("CreateCommandList", device->CreateCommandList(0, type, allocator, nullptr, IID_PPV_ARGS(&list)))) {
      result = 1;
      if (list)
        list->Release();
      if (allocator)
        allocator->Release();
      break;
    }
    if (list->GetType() != type) {
      std::cerr << "GetType mismatch for command list type " << type << "\n";
      result = 1;
    }
    if (!CheckHR("Close", list->Close()))
      result = 1;
    list->Release();
    allocator->Release();
  }

  ID3D12CommandAllocator *copy_allocator = nullptr;
  ID3D12GraphicsCommandList *copy_list = nullptr;
  HRESULT mismatch_hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&copy_allocator));
  if (!CheckHR("CreateCopyAllocator", mismatch_hr) ||
      device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, copy_allocator, nullptr, IID_PPV_ARGS(&copy_list)) != E_INVALIDARG) {
    std::cerr << "allocator/list type mismatch was accepted\n";
    result = 1;
  }
  if (copy_list)
    copy_list->Release();
  if (copy_allocator)
    copy_allocator->Release();

  ID3D12CommandAllocator *invalid_allocator = nullptr;
  ID3D12GraphicsCommandList *invalid_list = nullptr;
  if (!CheckHR("CreateInvalidAllocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&invalid_allocator))) ||
      !CheckHR("CreateInvalidList", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, invalid_allocator, nullptr, IID_PPV_ARGS(&invalid_list)))) {
    result = 1;
  } else {
    invalid_list->Dispatch(1, 1, 1);
    if (invalid_list->Close() != E_FAIL) {
      std::cerr << "invalid copy-list Dispatch was not rejected\n";
      result = 1;
    }
  }
  if (invalid_list)
    invalid_list->Release();
  if (invalid_allocator)
    invalid_allocator->Release();

  ID3D12CommandAllocator *bundle_allocator = nullptr;
  ID3D12GraphicsCommandList *bundle_list = nullptr;
  ID3D12CommandAllocator *parent_allocator = nullptr;
  ID3D12GraphicsCommandList *parent_list = nullptr;
  if (!CheckHR("CreateBundleAllocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&bundle_allocator))) ||
      !CheckHR("CreateBundleList",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, bundle_allocator, nullptr,
                                         IID_PPV_ARGS(&bundle_list))) ||
      !CheckHR("CloseBundle", bundle_list->Close()) ||
      !CheckHR("CreateParentAllocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&parent_allocator))) ||
      !CheckHR("CreateParentList",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, parent_allocator, nullptr,
                                         IID_PPV_ARGS(&parent_list)))) {
    result = 1;
  } else {
    parent_list->ExecuteBundle(bundle_list);
    if (parent_list->Close() != E_FAIL) {
      std::cerr << "unsupported ExecuteBundle was silently accepted\n";
      result = 1;
    }
  }
  if (parent_list)
    parent_list->Release();
  if (parent_allocator)
    parent_allocator->Release();
  if (bundle_list)
    bundle_list->Release();
  if (bundle_allocator)
    bundle_allocator->Release();

  D3D12_HEAP_PROPERTIES predication_heap = {};
  predication_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  predication_heap.CreationNodeMask = 1;
  predication_heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC predication_desc = {};
  predication_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  predication_desc.Width = 4;
  predication_desc.Height = 1;
  predication_desc.DepthOrArraySize = 1;
  predication_desc.MipLevels = 1;
  predication_desc.SampleDesc.Count = 1;
  predication_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ID3D12Resource *predication_buffer = nullptr;
  if (!CheckHR("CreatePredicationBuffer",
               device->CreateCommittedResource(
                   &predication_heap, D3D12_HEAP_FLAG_NONE, &predication_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                   IID_PPV_ARGS(&predication_buffer)))) {
    result = 1;
  }

  ID3D12CommandAllocator *unsupported_allocator = nullptr;
  ID3D12GraphicsCommandList *unsupported_list = nullptr;
  if (!predication_buffer ||
      !CheckHR("CreateUnsupportedAllocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&unsupported_allocator))) ||
      !CheckHR("CreateUnsupportedList",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, unsupported_allocator, nullptr,
                                         IID_PPV_ARGS(&unsupported_list)))) {
    result = 1;
  } else {
    unsupported_list->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    unsupported_list->SetPredication(predication_buffer, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    if (unsupported_list->Close() != E_FAIL) {
      std::cerr << "unsupported command was silently accepted\n";
      result = 1;
    }
  }
  if (unsupported_list)
    unsupported_list->Release();
  if (unsupported_allocator)
    unsupported_allocator->Release();
  if (predication_buffer)
    predication_buffer->Release();

  ID3D12CommandQueue *queue = nullptr;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))) {
    result = 1;
  } else {
    queue->ExecuteCommandLists(0, nullptr);
    queue->ExecuteCommandLists(1, nullptr);
    ID3D12CommandList *null_list = nullptr;
    queue->ExecuteCommandLists(1, &null_list);
  }
  if (queue)
    queue->Release();

  ID3D12Device *other_device = nullptr;
  ID3D12CommandAllocator *foreign_allocator = nullptr;
  ID3D12GraphicsCommandList *foreign_list = nullptr;
  if (!CheckHR("CreateOtherDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&other_device))) ||
      !CheckHR(
          "CreateForeignAllocator",
          other_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&foreign_allocator))
      )) {
    result = 1;
  } else if (device->CreateCommandList(
                 0, D3D12_COMMAND_LIST_TYPE_DIRECT, foreign_allocator, nullptr, IID_PPV_ARGS(&foreign_list)
             ) != E_INVALIDARG) {
    std::cerr << "cross-device command allocator was accepted\n";
    result = 1;
  }
  if (foreign_list)
    foreign_list->Release();
  if (foreign_allocator)
    foreign_allocator->Release();
  if (other_device)
    other_device->Release();

  device->Release();
  if (!result)
    std::cout << "D3D12 command list type tests passed\n";
  return result;
}

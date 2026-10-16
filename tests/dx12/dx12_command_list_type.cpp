#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <iostream>
#include <cstdint>
#include <thread>
#include <atomic>

namespace {

bool CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

template <typename T> struct Owned {
  T *ptr = nullptr;
  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
  ~Owned() { if (ptr) ptr->Release(); }
  T *operator->() const { return ptr; }
};

bool TestResetAllocatorSubmission(ID3D12Device *device, unsigned removal_mode = 0) {
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> stale, fresh;
  Owned<ID3D12Resource> source, readback;
  Owned<ID3D12Fence> fence;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  if (!CheckHR("Reset test queue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR("Reset test allocator", device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr))))
    return false;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 256;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  if (!CheckHR("Reset test upload", device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&source.ptr))))
    return false;
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  if (!CheckHR("Reset test readback", device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&readback.ptr))))
    return false;
  uint32_t *mapped = nullptr;
  if (!CheckHR("Reset test map source", source->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    return false;
  mapped[0] = 0x11111111;
  mapped[1] = 0x22222222;
  source->Unmap(0, nullptr);
  if (!CheckHR("Reset test map readback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    return false;
  *mapped = 0;
  readback->Unmap(0, nullptr);

  if (!CheckHR("Reset test stale list", device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, nullptr, IID_PPV_ARGS(&stale.ptr))))
    return false;
  stale->CopyBufferRegion(readback.ptr, 0, source.ptr, 0, sizeof(uint32_t));
  if (!CheckHR("Reset test close stale", stale->Close()) ||
      !CheckHR("Reset test allocator reset", allocator->Reset()) ||
      !CheckHR("Reset test fresh list", device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, nullptr, IID_PPV_ARGS(&fresh.ptr))))
    return false;
  if (allocator->Reset() != E_FAIL) {
    std::cerr << "Reset test accepted an allocator reset while recording\n";
    return false;
  }
  fresh->CopyBufferRegion(readback.ptr, 0, source.ptr, sizeof(uint32_t), sizeof(uint32_t));
  if (!CheckHR("Reset test close fresh", fresh->Close()) ||
      !CheckHR("Reset test fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.ptr))))
    return false;
  struct Event {
    HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Event() { if (handle) CloseHandle(handle); }
  } event;
  if (!event.handle)
    return false;
  UINT64 fence_value = 0;
  const auto submit = [&](const char *name, ID3D12CommandList *const *lists, UINT count, uint32_t expected) {
    queue->ExecuteCommandLists(count, lists);
    if (!CheckHR("Reset test signal", queue->Signal(fence.ptr, ++fence_value)) ||
        !CheckHR("Reset test event", fence->SetEventOnCompletion(fence_value, event.handle)) ||
        WaitForSingleObject(event.handle, 5000) != WAIT_OBJECT_0) {
      std::cerr << name << " queue wait failed\n";
      return false;
    }
    if (!CheckHR("Reset test map result", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
      return false;
    const auto actual = *mapped;
    readback->Unmap(0, nullptr);
    if (actual != expected) {
      std::cerr << name << " readback 0x" << std::hex << actual << ", expected 0x" << expected << std::dec << "\n";
      return false;
    }
    std::cout << name << " readback passed: 0x" << std::hex << actual << std::dec << "\n";
    return true;
  };
  ID3D12CommandList *stale_list = stale.ptr;
  ID3D12CommandList *fresh_list = fresh.ptr;
  if (!submit("fresh allocator recording", &fresh_list, 1, 0x22222222))
    return false;
  if (removal_mode) {
    if (!CheckHR("Reset removed-device readback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
      return false;
    *mapped = 0;
    readback->Unmap(0, nullptr);
    ID3D12CommandList *batch[] = {fresh.ptr, stale.ptr};
    queue->ExecuteCommandLists(removal_mode == 2 ? 2 : 1, removal_mode == 2 ? batch : &stale_list);
    const HRESULT reason = device->GetDeviceRemovedReason();
    if (SUCCEEDED(reason) || fence->GetCompletedValue() != UINT64_MAX ||
        queue->Signal(fence.ptr, ++fence_value) != DXGI_ERROR_DEVICE_REMOVED ||
        !CheckHR("Map dropped invalid batch", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)))) {
      std::cerr << "invalid allocator submission did not remove device and complete fence\n";
      return false;
    }
    const bool dropped = *mapped == 0;
    readback->Unmap(0, nullptr);
    Owned<ID3D12Device5> device5;
    if (!CheckHR("Removed device interface", device->QueryInterface(IID_PPV_ARGS(&device5.ptr))))
      return false;
    device5->RemoveDevice();
    if (!dropped || device->GetDeviceRemovedReason() != reason) {
      std::cerr << "invalid batch executed or removal reason was overwritten\n";
      return false;
    }
    std::cout << (removal_mode == 2 ? "mixed" : "single") << " stale allocator batch removed device; readback remains zero\n";
    return true;
  }
  if (!CheckHR("Reset test rerecord stale", stale->Reset(allocator.ptr, nullptr)))
    return false;
  stale->CopyBufferRegion(readback.ptr, 0, source.ptr, 0, sizeof(uint32_t));
  return CheckHR("Reset test close rerecorded", stale->Close()) &&
         submit("rerecorded command list", &stale_list, 1, 0x11111111);
}

bool TestDeviceRemovalWaits() {
  Owned<ID3D12Device5> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12Fence> fence, other, created_after;
  if (!CheckHR("Removal device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr))))
    return false;
  D3D12_COMMAND_QUEUE_DESC desc = {};
  if (!CheckHR("Removal queue", device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR("Removal fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.ptr))) ||
      !CheckHR("Removal other fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&other.ptr))))
    return false;
  struct Events {
    HANDLE values[7] = {};
    ~Events() { for (auto event : values) if (event) CloseHandle(event); }
  } events;
  for (auto &event : events.values) {
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event)
      return false;
  }
  ID3D12Fence *fences[] = {fence.ptr, other.ptr};
  UINT64 values[] = {UINT64_MAX, UINT64_MAX};
  if (!CheckHR("Pending removal event", fence->SetEventOnCompletion(UINT64_MAX, events.values[0])) ||
      !CheckHR("Pending removal ALL", device->SetEventOnMultipleFenceCompletion(
          fences, values, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE, events.values[1])) ||
      !CheckHR("Pending removal ANY", device->SetEventOnMultipleFenceCompletion(
          fences, values, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, events.values[2])) ||
      !CheckHR("Pending removal GPU wait", queue->Wait(fence.ptr, 1)))
    return false;
  // The wait occupies the first of 32 submission slots; fill the other 31.
  for (UINT64 value = 1; value < 32; value++)
    if (!CheckHR("Fill removal queue", queue->Signal(other.ptr, value))) {
      fence->Signal(UINT64_MAX);
      return false;
    }
  HRESULT cpu_result = E_FAIL, producer_result = E_FAIL;
  std::thread cpu_wait([&] {
    cpu_result = fence->SetEventOnCompletion(UINT64_MAX, nullptr);
    SetEvent(events.values[3]);
  });
  std::thread producer([&] {
    SetEvent(events.values[4]);
    producer_result = queue->Signal(other.ptr, 32);
    SetEvent(events.values[5]);
  });
  bool ok = WaitForSingleObject(events.values[4], 5000) == WAIT_OBJECT_0 &&
            WaitForSingleObject(events.values[5], 0) == WAIT_TIMEOUT;
  device->RemoveDevice();
  const HRESULT reason = device->GetDeviceRemovedReason();
  ok &= FAILED(reason) && fence->GetCompletedValue() == UINT64_MAX && other->GetCompletedValue() == UINT64_MAX;
  for (unsigned i = 0; i < 4; i++)
    ok &= WaitForSingleObject(events.values[i], 5000) == WAIT_OBJECT_0;
  ok &= WaitForSingleObject(events.values[5], 5000) == WAIT_OBJECT_0;
  if (FAILED(reason)) {
    ok &= queue->Signal(other.ptr, 33) == DXGI_ERROR_DEVICE_REMOVED;
    ok &= queue->Wait(fence.ptr, 1) == DXGI_ERROR_DEVICE_REMOVED;
    ok &= fence->Signal(0) == DXGI_ERROR_DEVICE_REMOVED && fence->GetCompletedValue() == UINT64_MAX;
    ok &= CheckHR("New removal event", fence->SetEventOnCompletion(UINT64_MAX, events.values[0]));
    ok &= WaitForSingleObject(events.values[0], 0) == WAIT_OBJECT_0;
    ok &= CheckHR("New removal null wait", fence->SetEventOnCompletion(UINT64_MAX, nullptr));
    ok &= CheckHR("New removal ALL", device->SetEventOnMultipleFenceCompletion(
        fences, values, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE, events.values[1]));
    ok &= WaitForSingleObject(events.values[1], 0) == WAIT_OBJECT_0;
    ok &= CheckHR("Fence created after removal", device->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&created_after.ptr)));
    ok &= created_after.ptr && created_after->GetCompletedValue() == UINT64_MAX;
    device->RemoveDevice();
    ok &= device->GetDeviceRemovedReason() == reason;
  }
  // On the old runtime this recovery releases all dependencies before joining.
  fence->Signal(UINT64_MAX);
  other->Signal(UINT64_MAX);
  cpu_wait.join();
  producer.join();
  ok &= cpu_result == S_OK && producer_result == DXGI_ERROR_DEVICE_REMOVED;
  auto released_queue = queue.ptr;
  queue.ptr = nullptr;
  std::thread destroy_queue([&] {
    released_queue->Release();
    SetEvent(events.values[6]);
  });
  ok &= WaitForSingleObject(events.values[6], 5000) == WAIT_OBJECT_0;
  destroy_queue.join();
  if (!ok)
    std::cerr << "device removal failed to release pending/new CPU/GPU waits or submission backpressure\n";
  else
    std::cout << "D3D12 removal event/null/ALL/ANY/GPU waits, backpressure and queue destruction passed\n";
  return ok;
}

bool TestConcurrentRemovalListeners() {
  Owned<ID3D12Device5> device;
  if (!CheckHR("Concurrent removal device", D3D12CreateDevice(
          nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr))))
    return false;
  std::atomic<unsigned> ready{0};
  std::atomic<bool> start{false}, success{true};
  std::thread workers[4];
  for (auto &worker : workers)
    worker = std::thread([&] {
      Owned<ID3D12Fence> initial;
      if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&initial.ptr))))
        success.store(false);
      ready.fetch_add(1);
      while (!start.load())
        std::this_thread::yield();
      // These states already exist when removal races their last COM release.
      if (initial.ptr) {
        initial.ptr->Release();
        initial.ptr = nullptr;
      }
      for (unsigned i = 0; i < 64; i++) {
        Owned<ID3D12Fence> fence;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.ptr)))) {
          if (event) CloseHandle(event);
          success.store(false);
          return;
        }
        const HRESULT registration = fence->SetEventOnCompletion(UINT64_MAX, event);
        const HRESULT signal = fence->Signal(UINT64_MAX);
        if (FAILED(registration) || (FAILED(signal) && signal != DXGI_ERROR_DEVICE_REMOVED) ||
            WaitForSingleObject(event, 5000) != WAIT_OBJECT_0 || fence->GetCompletedValue() != UINT64_MAX)
          success.store(false);
        CloseHandle(event);
      }
    });
  while (ready.load() != 4)
    std::this_thread::yield();
  start.store(true);
  device->RemoveDevice();
  for (auto &worker : workers)
    worker.join();
  if (!success.load() || SUCCEEDED(device->GetDeviceRemovedReason()))
    return false;
  std::cout << "D3D12 concurrent fence registration/destruction around removal passed\n";
  return true;
}

struct ExecuteContext {
  ID3D12CommandQueue *queue;
  ID3D12CommandList *command_list;
  LONG returned;
};

struct FenceWaitContext {
  ID3D12CommandQueue *queue;
  ID3D12Fence *fence;
  HRESULT result;
  LONG returned;
};

DWORD WINAPI ExecuteCommandListsThread(void *arg) {
  auto *context = static_cast<ExecuteContext *>(arg);
  context->queue->ExecuteCommandLists(1, &context->command_list);
  InterlockedExchange(&context->returned, 1);
  return 0;
}

DWORD WINAPI QueueWaitThread(void *arg) {
  auto *context = static_cast<FenceWaitContext *>(arg);
  context->result = context->queue->Wait(context->fence, 1);
  InterlockedExchange(&context->returned, 1);
  return 0;
}

bool TestConcurrentCommandSubmission(ID3D12Device *device) {
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator_a = nullptr;
  ID3D12CommandAllocator *allocator_b = nullptr;
  ID3D12GraphicsCommandList *list_a = nullptr;
  ID3D12GraphicsCommandList *list_b = nullptr;
  HANDLE first_thread = nullptr;
  HANDLE second_thread = nullptr;
  HANDLE entered = nullptr;
  HANDLE resume = nullptr;
  bool result = false;

  auto cleanup = [&] {
    if (resume)
      SetEvent(resume);
    if (first_thread) {
      WaitForSingleObject(first_thread, INFINITE);
      CloseHandle(first_thread);
    }
    if (second_thread) {
      WaitForSingleObject(second_thread, INFINITE);
      CloseHandle(second_thread);
    }
    SetEnvironmentVariableA("DXMT_TEST_PAUSE_QUEUE_TRANSLATION", nullptr);
    if (resume)
      CloseHandle(resume);
    if (entered)
      CloseHandle(entered);
    if (list_b)
      list_b->Release();
    if (list_a)
      list_a->Release();
    if (allocator_b)
      allocator_b->Release();
    if (allocator_a)
      allocator_a->Release();
    if (queue)
      queue->Release();
    return result;
  };

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  if (!CheckHR("CreateConcurrentQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !CheckHR(
          "CreateConcurrentAllocatorA",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_a))
      ) ||
      !CheckHR(
          "CreateConcurrentAllocatorB",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_b))
      ) ||
      !CheckHR(
          "CreateConcurrentListA",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_a, nullptr, IID_PPV_ARGS(&list_a))
      ) ||
      !CheckHR(
          "CreateConcurrentListB",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_b, nullptr, IID_PPV_ARGS(&list_b))
      ) ||
      !CheckHR("CloseConcurrentListA", list_a->Close()) ||
      !CheckHR("CloseConcurrentListB", list_b->Close()))
    return cleanup();

  entered = CreateEventA(nullptr, FALSE, FALSE, "DXMT.Test.QueueTranslationEntered");
  resume = CreateEventA(nullptr, TRUE, FALSE, "DXMT.Test.QueueTranslationResume");
  if (!entered || !resume || !SetEnvironmentVariableA("DXMT_TEST_PAUSE_QUEUE_TRANSLATION", "1"))
    return cleanup();

  ExecuteContext first = {queue, list_a, 0};
  first_thread = CreateThread(nullptr, 0, ExecuteCommandListsThread, &first, 0, nullptr);
  if (!first_thread || WaitForSingleObject(entered, 5000) != WAIT_OBJECT_0)
    return cleanup();

  ExecuteContext second = {queue, list_b, 0};
  second_thread = CreateThread(nullptr, 0, ExecuteCommandListsThread, &second, 0, nullptr);
  if (!second_thread)
    return cleanup();
  Sleep(50);
  if (InterlockedCompareExchange(&second.returned, 0, 0) != 0) {
    std::cerr << "second command submission completed during first translation\n";
    return cleanup();
  }

  result = true;
  return cleanup();
}

} // namespace

int main() {
  ID3D12Device *device = nullptr;
  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return 1;

  if (!TestResetAllocatorSubmission(device)) {
    device->Release();
    return 1;
  }

  for (unsigned mode = 1; mode <= 2; mode++) {
    Owned<ID3D12Device> isolated;
    if (!CheckHR("Isolated removal device", D3D12CreateDevice(
            nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&isolated.ptr))) ||
        !TestResetAllocatorSubmission(isolated.ptr, mode)) {
      device->Release();
      return 1;
    }
  }
  if (!TestDeviceRemovalWaits() || !TestConcurrentRemovalListeners()) {
    device->Release();
    return 1;
  }

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

    ID3D12CommandAllocator *async_allocator = nullptr;
    ID3D12GraphicsCommandList *async_list = nullptr;
    ID3D12Fence *async_fence = nullptr;
    HANDLE execute_thread = nullptr;
    HANDLE backpressure_thread = nullptr;
    if (!CheckHR(
            "CreateAsyncAllocator",
            device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&async_allocator))
        ) ||
        !CheckHR(
            "CreateAsyncList",
            device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, async_allocator, nullptr, IID_PPV_ARGS(&async_list)
            )
        ) ||
        !CheckHR("CloseAsyncList", async_list->Close()) ||
        !CheckHR("CreateAsyncFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&async_fence))) ||
        !CheckHR("QueueWait", queue->Wait(async_fence, 1))) {
      result = 1;
    } else {
      ExecuteContext context = {queue, async_list, 0};
      execute_thread = CreateThread(nullptr, 0, ExecuteCommandListsThread, &context, 0, nullptr);
      if (!execute_thread) {
        std::cerr << "CreateThread failed while testing asynchronous command submission\n";
        result = 1;
      } else {
        Sleep(50);
        if (InterlockedCompareExchange(&context.returned, 0, 0) == 0) {
          std::cerr << "ExecuteCommandLists waited for GPU completion\n";
          result = 1;
        }
        if (!CheckHR("ResetAsyncListWhileAllocatorIsInFlight", async_list->Reset(async_allocator, nullptr)) ||
            !CheckHR("CloseResetAsyncList", async_list->Close()))
          result = 1;
        for (unsigned i = 0; i < 30; i++) {
          if (!CheckHR("FillSubmissionQueue", queue->Wait(async_fence, 1))) {
            result = 1;
            break;
          }
        }
        FenceWaitContext backpressure = {queue, async_fence, E_FAIL, 0};
        backpressure_thread = CreateThread(nullptr, 0, QueueWaitThread, &backpressure, 0, nullptr);
        if (!backpressure_thread) {
          std::cerr << "CreateThread failed while testing submission backpressure\n";
          result = 1;
        } else {
          Sleep(50);
          if (InterlockedCompareExchange(&backpressure.returned, 0, 0) != 0) {
            std::cerr << "submission queue accepted more than 32 in-flight submissions\n";
            result = 1;
          }
        }
        if (!CheckHR("SignalAsyncFence", async_fence->Signal(1)))
          result = 1;
        if (backpressure_thread) {
          if (WaitForSingleObject(backpressure_thread, 5000) != WAIT_OBJECT_0) {
            std::cerr << "submission backpressure thread did not finish\n";
            result = 1;
          } else if (!CheckHR("BackpressureWait", backpressure.result)) {
            result = 1;
          }
        }
        if (WaitForSingleObject(execute_thread, INFINITE) != WAIT_OBJECT_0) {
          std::cerr << "ExecuteCommandLists thread did not finish\n";
          result = 1;
        } else if (InterlockedCompareExchange(&context.returned, 0, 0) == 0) {
          std::cerr << "ExecuteCommandLists thread did not return\n";
          result = 1;
        }
      }
    }
    if (execute_thread)
      CloseHandle(execute_thread);
    if (backpressure_thread)
      CloseHandle(backpressure_thread);
    if (async_fence)
      async_fence->Signal(1);
    if (async_list)
      async_list->Release();
    if (queue) {
      queue->Release();
      queue = nullptr;
    }
    if (async_fence)
      async_fence->Release();
    if (async_allocator) {
      if (result == 0 && async_allocator->Reset() != S_OK) {
        std::cerr << "command allocator did not become resettable after completion\n";
        result = 1;
      }
      async_allocator->Release();
    }
  }
  if (queue)
    queue->Release();

  if (!TestConcurrentCommandSubmission(device))
    result = 1;

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

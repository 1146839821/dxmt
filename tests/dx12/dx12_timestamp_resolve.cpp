#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
template <typename T> struct Owned {
  T *p = nullptr;
  ~Owned() { if (p) p->Release(); }
  T *operator->() const { return p; }
};
void Check(HRESULT hr) {
  if (FAILED(hr)) throw std::runtime_error("D3D12 operation failed");
}
void Buffer(ID3D12Device *device, D3D12_HEAP_TYPE heap, ID3D12Resource **out) {
  D3D12_HEAP_PROPERTIES hp = {}; hp.Type = heap;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 64; desc.Height = 1; desc.DepthOrArraySize = 1;
  desc.MipLevels = 1; desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
      heap == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, IID_PPV_ARGS(out)));
}
struct GateRelease {
  ID3D12Fence *gate;
  HANDLE returned;
  LONG timed_out = 0;
  static DWORD WINAPI Run(void *data) {
    auto self = static_cast<GateRelease *>(data);
    if (WaitForSingleObject(self->returned, 2000) != WAIT_OBJECT_0)
      InterlockedExchange(&self->timed_out, 1);
    self->gate->Signal(1);
    return 0;
  }
};
}

// Diagnostic, not an acceptance workaround: invalid GPU timestamps remain red.
int main(int argc, char **argv) try {
  const bool fail_translation = argc == 2 && !std::strcmp(argv[1], "--fail-translation");
  const bool many_resolves = argc == 2 && !std::strcmp(argv[1], "--many-resolves");
  if (argc > 2 || (argc == 2 && !fail_translation && !many_resolves)) throw std::runtime_error("invalid argument");
  Owned<ID3D12Device> device;
  Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)));
  Owned<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC qd = {}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue.p)));
  Owned<ID3D12Fence> fence;
  Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.p)));
  Owned<ID3D12Fence> gate;
  Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate.p)));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) throw std::runtime_error("event allocation failed");
  struct EventOwner { HANDLE value; ~EventOwner() { CloseHandle(value); } } event_owner{event};
  if (fail_translation) {
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&allocator.p)));
    Check(device->CreateCommandList(0, qd.Type, allocator.p, nullptr, IID_PPV_ARGS(&list.p)));
    Check(list->Close());
    if (!SetEnvironmentVariableA("DXMT_TEST_FAIL_QUEUE_TRANSLATION", "1"))
      throw std::runtime_error("failure injection setup failed");
    ID3D12CommandList *commands[] = {list.p};
    queue->ExecuteCommandLists(1, commands);
    // Either enqueue wins the race, or removal is already published. Both must
    // release a subsequent fence listener and report a removed device.
    const HRESULT signaled = queue->Signal(fence.p, 1);
    Check(fence->SetEventOnCompletion(1, event));
    if (WaitForSingleObject(event, 5000) != WAIT_OBJECT_0 ||
        SUCCEEDED(device->GetDeviceRemovedReason()) || fence->GetCompletedValue() != UINT64_MAX)
      throw std::runtime_error("asynchronous failure was not published to the waiter");
    if (SUCCEEDED(queue->Signal(fence.p, 2)))
      throw std::runtime_error("removed queue accepted another signal");
    Check(fence->SetEventOnCompletion(2, event));
    if (WaitForSingleObject(event, 5000) != WAIT_OBJECT_0)
      throw std::runtime_error("post-removal listener was not notified");
    SetEnvironmentVariableA("DXMT_TEST_FAIL_QUEUE_TRANSLATION", nullptr);
    std::cout << "ASYNC_FAILURE notification/removal PASS signal=" << std::hex << signaled << '\n';
    return 0;
  }
  unsigned failures = 0;
  const unsigned runs = many_resolves ? 1 : 200;
  for (UINT64 iteration = 1; iteration <= runs; ++iteration) {
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Owned<ID3D12QueryHeap> queries;
    Owned<ID3D12Resource> upload, resolved, readback;
    Check(device->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&allocator.p)));
    Check(device->CreateCommandList(0, qd.Type, allocator.p, nullptr, IID_PPV_ARGS(&list.p)));
    D3D12_QUERY_HEAP_DESC hd = {}; hd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; hd.Count = 4;
    Check(device->CreateQueryHeap(&hd, IID_PPV_ARGS(&queries.p)));
    Buffer(device.p, D3D12_HEAP_TYPE_UPLOAD, &upload.p);
    Buffer(device.p, D3D12_HEAP_TYPE_DEFAULT, &resolved.p);
    Buffer(device.p, D3D12_HEAP_TYPE_READBACK, &readback.p);
    void *mapped = nullptr;
    Check(upload->Map(0, nullptr, &mapped));
    std::memset(mapped, 0xa5, 64); upload->Unmap(0, nullptr);
    list->CopyBufferRegion(resolved.p, 0, upload.p, 0, 64);
    list->EndQuery(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 1);
    list->CopyBufferRegion(resolved.p, 48, upload.p, 48, 8);
    list->EndQuery(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 2);
    list->ResolveQueryData(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 1, 2, resolved.p, 8);
    // An ordinary blit must follow, not append into the resolve encoder.
    list->CopyBufferRegion(resolved.p, 56, upload.p, 56, 8);
    list->ResolveQueryData(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 1, 2, resolved.p, 32);
    if (many_resolves) for (unsigned resolve = 2; resolve < 64; ++resolve)
      list->ResolveQueryData(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 1, 2, resolved.p, 32);
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resolved.p, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    list->CopyBufferRegion(readback.p, 0, resolved.p, 0, 64);
    Check(list->Close());
    HANDLE returned = iteration == 1 ? CreateEventW(nullptr, FALSE, FALSE, nullptr) : nullptr;
    GateRelease release{gate.p, returned};
    HANDLE watchdog = nullptr;
    if (iteration == 1) {
      if (!returned) throw std::runtime_error("gate event allocation failed");
      Check(queue->Wait(gate.p, 1));
      watchdog = CreateThread(nullptr, 0, GateRelease::Run, &release, 0, nullptr);
      if (!watchdog) { gate->Signal(1); CloseHandle(returned); throw std::runtime_error("watchdog allocation failed"); }
    }
    ID3D12CommandList *commands[] = {list.p};
    queue->ExecuteCommandLists(1, commands);
    if (iteration == 1) {
      const bool rejected = FAILED(allocator->Reset());
      // Resetting the list does not change the worker's captured old recording.
      Owned<ID3D12CommandAllocator> next_allocator;
      const HRESULT next_created = device->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&next_allocator.p));
      const HRESULT reset = SUCCEEDED(next_created) ? list->Reset(next_allocator.p, nullptr) : next_created;
      const HRESULT close = SUCCEEDED(reset) ? list->Close() : reset;
      SetEvent(returned);
      WaitForSingleObject(watchdog, INFINITE);
      CloseHandle(watchdog); CloseHandle(returned);
      if (!rejected || release.timed_out || FAILED(close))
        throw std::runtime_error("caller blocked, pending allocator reset succeeded, or recording reset failed");
    }
    Check(queue->Signal(fence.p, iteration));
    Check(fence->SetEventOnCompletion(iteration, event));
    if (WaitForSingleObject(event, 5000) != WAIT_OBJECT_0)
      throw std::runtime_error("GPU completion timed out");
    Check(readback->Map(0, nullptr, &mapped));
    const auto *values = static_cast<const uint64_t *>(mapped);
    const uint64_t sentinel = 0xa5a5a5a5a5a5a5a5ull;
    if (values[0] != sentinel || values[3] != sentinel || values[6] != sentinel || values[7] != sentinel)
      throw std::runtime_error("resolve overwrote a canary or lost an ordinary copy");
    if (!values[1] || values[1] == UINT64_MAX || values[2] == UINT64_MAX || values[2] <= values[1] ||
        values[4] != values[1] || values[5] != values[2]) {
      std::cout << "TIMESTAMP_FAIL iteration=" << iteration << " first=" << values[1] << ',' << values[2]
                << " repeat=" << values[4] << ',' << values[5] << '\n';
      ++failures;
    }
    readback->Unmap(0, nullptr);
    Check(allocator->Reset());
  }
  UINT64 frequency = 0, gpu_clock = 0, cpu_clock = 0;
  Check(queue->GetTimestampFrequency(&frequency));
  Check(queue->GetClockCalibration(&gpu_clock, &cpu_clock));
  if (frequency != 1000000000ull || !gpu_clock || !cpu_clock)
    throw std::runtime_error("clock calibration failed");
  std::cout << "CLOCK_CALIBRATION PASS\n";
  std::cout << "TIMESTAMP_RESOLVE runs=" << runs << " failures=" << failures
            << " resolves_per_list=" << (many_resolves ? 64 : 2) << '\n';
  return failures ? 1 : 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n'; return 2;
}

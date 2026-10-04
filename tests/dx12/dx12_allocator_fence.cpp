#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <memory>

template <typename T> struct ReleaseCOM { void operator()(T *p) const { if (p) p->Release(); } };
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;
static bool Check(HRESULT hr, const char *name) {
  if (FAILED(hr)) std::printf("%s failed %08lx\n", name, (unsigned long)hr);
  return SUCCEEDED(hr);
}

int main() {
  ID3D12Device *raw_device = nullptr;
  if (!Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)), "device")) return 1;
  OwnedCOM<ID3D12Device> device(raw_device);
  D3D12_COMMAND_QUEUE_DESC desc = {};
  ID3D12CommandQueue *raw_queue = nullptr;
  if (!Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&raw_queue)), "queue")) return 1;
  OwnedCOM<ID3D12CommandQueue> queue(raw_queue);
  raw_queue = nullptr;
  if (!Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&raw_queue)), "second queue")) return 1;
  OwnedCOM<ID3D12CommandQueue> second_queue(raw_queue);
  ID3D12Fence *raw_fence = nullptr, *raw_gate = nullptr, *raw_second_fence = nullptr;
  if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_fence)), "fence")) return 1;
  OwnedCOM<ID3D12Fence> fence(raw_fence);
  if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_gate)), "gate")) return 1;
  OwnedCOM<ID3D12Fence> gate(raw_gate);
  if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_second_fence)), "second fence")) return 1;
  OwnedCOM<ID3D12Fence> second_fence(raw_second_fence);
  D3D12_RESOURCE_DESC buffer = {};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = 256;
  buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES properties = {}; properties.Type = D3D12_HEAP_TYPE_UPLOAD;
  ID3D12Resource *raw_upload = nullptr, *raw_readback = nullptr;
  if (!Check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &buffer,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&raw_upload)), "upload")) return 1;
  OwnedCOM<ID3D12Resource> upload(raw_upload);
  properties.Type = D3D12_HEAP_TYPE_READBACK;
  if (!Check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &buffer,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw_readback)), "readback")) return 1;
  OwnedCOM<ID3D12Resource> readback(raw_readback);
  ID3D12CommandAllocator *raw_allocator = nullptr;
  ID3D12GraphicsCommandList *raw_list = nullptr;
  if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "allocator")) return 1;
  OwnedCOM<ID3D12CommandAllocator> allocator(raw_allocator);
  if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
      IID_PPV_ARGS(&raw_list)), "list")) return 1;
  OwnedCOM<ID3D12GraphicsCommandList> list(raw_list);
  if (SUCCEEDED(allocator->Reset())) { std::printf("ALLOCATOR_FENCE open recording Reset succeeded\n"); return 1; }
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) return 1;
  struct GateRelease {
    ID3D12Fence *gate;
    ~GateRelease() { gate->Signal(3); }
  } release_gate{gate.get()};
  OwnedCOM<ID3D12GraphicsCommandList> second_list;
  unsigned passed = 0;
  for (unsigned iteration = 0; iteration < 1024; ++iteration) {
    void *mapped = nullptr;
    if (!Check(upload->Map(0, nullptr, &mapped), "upload map")) break;
    const UINT expected = 0x6d5a0000 | iteration;
    const UINT second_expected = expected ^ 0x00ff0000;
    std::memcpy(mapped, &expected, 4);
    std::memcpy(static_cast<unsigned char *>(mapped) + 4, &second_expected, 4);
    upload->Unmap(0, nullptr);
    list->CopyBufferRegion(readback.get(), 0, upload.get(), 0, 4);
    if (!Check(list->Close(), "close")) break;
    if (iteration < 2) {
      // Two distinct lists, recorded sequentially using the same allocator.
      // Never resubmit a list whose previous execution is still outstanding.
      second_list.reset(); raw_list = nullptr;
      if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
          IID_PPV_ARGS(&raw_list)), "second list")) break;
      second_list.reset(raw_list);
      second_list->CopyBufferRegion(readback.get(), 4, upload.get(), 4, 4);
      if (!Check(second_list->Close(), "second close")) break;
    }
    // First submission cannot finish until the CPU gate is signaled. Reset
    // must still reject actual GPU use; always release the gate on failure.
    if (iteration == 0 && !Check(queue->Wait(gate.get(), 1), "gate wait")) break;
    ID3D12CommandList *lists[] = {list.get()}; queue->ExecuteCommandLists(1, lists);
    ID3D12CommandList *second_lists[] = {second_list.get()};
    if (iteration == 0) {
      if (!Check(second_queue->Wait(gate.get(), 2), "second gate wait")) { gate->Signal(2); break; }
      second_queue->ExecuteCommandLists(1, second_lists);
    } else if (iteration == 1) {
      if (!Check(queue->Signal(second_fence.get(), 2), "first-use signal") ||
          !Check(queue->Wait(gate.get(), 3), "later-use gate")) break;
      queue->ExecuteCommandLists(1, second_lists);
    }
    const bool blocked_rejected = iteration > 1 || FAILED(allocator->Reset());
    if (iteration == 0 && !Check(gate->Signal(1), "gate release")) break;
    bool later_rejected = true;
    if (iteration == 1) {
      if (!Check(second_fence->SetEventOnCompletion(2, event), "first-use event") ||
          WaitForSingleObject(event, 30000) != WAIT_OBJECT_0) break;
      later_rejected = FAILED(allocator->Reset());
      if (!Check(gate->Signal(3), "later-use gate release")) break;
    }
    if (!Check(queue->Signal(fence.get(), iteration + 1), "signal") ||
        !Check(fence->SetEventOnCompletion(iteration + 1, event), "event") ||
        WaitForSingleObject(event, 30000) != WAIT_OBJECT_0) break;
    bool second_rejected = true;
    if (iteration == 0) {
      second_rejected = FAILED(allocator->Reset());
      // Queue A has completed but queue B still owns this same recording.
      if (!Check(gate->Signal(2), "second gate release") ||
          !Check(second_queue->Signal(second_fence.get(), 1), "second signal") ||
          !Check(second_fence->SetEventOnCompletion(1, event), "second event") ||
          WaitForSingleObject(event, 30000) != WAIT_OBJECT_0) break;
    }
    if (!blocked_rejected) { std::printf("ALLOCATOR_FENCE pending Reset incorrectly succeeded\n"); break; }
    if (!second_rejected) { std::printf("ALLOCATOR_FENCE cross-queue Reset incorrectly succeeded\n"); break; }
    if (!later_rejected) { std::printf("ALLOCATOR_FENCE later-use Reset incorrectly succeeded\n"); break; }
    // No retry or sleep: GPU-visible completion must suffice immediately.
    const HRESULT reset = allocator->Reset();
    if (FAILED(reset)) {
      std::printf("ALLOCATOR_FENCE iteration=%u completed=%llu Reset=%08lx FAIL\n",
          iteration, (unsigned long long)fence->GetCompletedValue(), (unsigned long)reset);
      break;
    }
    D3D12_RANGE range = {0, iteration < 2 ? SIZE_T(8) : SIZE_T(4)};
    if (!Check(readback->Map(0, &range, &mapped), "readback map")) break;
    UINT observed[2] = {}; std::memcpy(observed, mapped, range.End);
    D3D12_RANGE empty = {}; readback->Unmap(0, &empty);
    if (observed[0] != expected || (iteration < 2 && observed[1] != second_expected)) {
      std::printf("ALLOCATOR_FENCE GPU copy mismatch\n"); break;
    }
    ++passed;
    if (iteration != 1023 && !Check(list->Reset(allocator.get(), nullptr), "list reset")) break;
  }
  CloseHandle(event);
  if (passed == 1024) std::printf("ALLOCATOR_FENCE open/pending/cross-queue/later-use guards PASS\n");
  std::printf("ALLOCATOR_FENCE completed=%u/1024 %s\n", passed, passed == 1024 ? "PASS" : "FAIL");
  return passed == 1024 ? 0 : 1;
}

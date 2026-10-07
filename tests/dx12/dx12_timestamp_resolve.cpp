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
}

// Diagnostic, not an acceptance workaround: invalid GPU timestamps remain red.
int main() try {
  Owned<ID3D12Device> device;
  Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)));
  Owned<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC qd = {}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue.p)));
  Owned<ID3D12Fence> fence;
  Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.p)));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) throw std::runtime_error("event allocation failed");
  struct EventOwner { HANDLE value; ~EventOwner() { CloseHandle(value); } } event_owner{event};
  unsigned failures = 0;
  for (UINT64 iteration = 1; iteration <= 200; ++iteration) {
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Owned<ID3D12QueryHeap> queries;
    Owned<ID3D12Resource> upload, readback;
    Check(device->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&allocator.p)));
    Check(device->CreateCommandList(0, qd.Type, allocator.p, nullptr, IID_PPV_ARGS(&list.p)));
    D3D12_QUERY_HEAP_DESC hd = {}; hd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; hd.Count = 4;
    Check(device->CreateQueryHeap(&hd, IID_PPV_ARGS(&queries.p)));
    Buffer(device.p, D3D12_HEAP_TYPE_UPLOAD, &upload.p);
    Buffer(device.p, D3D12_HEAP_TYPE_READBACK, &readback.p);
    void *mapped = nullptr;
    Check(upload->Map(0, nullptr, &mapped));
    std::memset(mapped, 0xa5, 64); upload->Unmap(0, nullptr);
    list->CopyBufferRegion(readback.p, 0, upload.p, 0, 64);
    list->EndQuery(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 1);
    list->CopyBufferRegion(readback.p, 48, upload.p, 48, 8);
    list->EndQuery(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 2);
    list->ResolveQueryData(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 1, 2, readback.p, 8);
    // An ordinary blit must follow, not append into the resolve encoder.
    list->CopyBufferRegion(readback.p, 56, upload.p, 56, 8);
    list->ResolveQueryData(queries.p, D3D12_QUERY_TYPE_TIMESTAMP, 1, 2, readback.p, 32);
    Check(list->Close());
    ID3D12CommandList *commands[] = {list.p};
    queue->ExecuteCommandLists(1, commands);
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
  std::cout << "TIMESTAMP_RESOLVE runs=200 failures=" << failures << '\n';
  return failures ? 1 : 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n'; return 2;
}

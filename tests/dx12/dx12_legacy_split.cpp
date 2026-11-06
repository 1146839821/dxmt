#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <vector>
#include "../../src/util/com/com_pointer.hpp"
using dxmt::Com;

class DestructionMarker final : public IUnknown {
  LONG references_ = 1;
  bool *destroyed_;

public:
  explicit DestructionMarker(bool *destroyed) : destroyed_(destroyed) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (iid != __uuidof(IUnknown))
      return E_NOINTERFACE;
    *out = static_cast<IUnknown *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&references_);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    const LONG remaining = InterlockedDecrement(&references_);
    if (!remaining) {
      *destroyed_ = true;
      delete this;
    }
    return remaining;
  }
};

bool SubresourceCases(ID3D12Device *device, const char *mode) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = desc.Height = 8;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 2;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  Com<ID3D12Resource> texture;
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&texture))))
    return false;
  Com<ID3D12CommandAllocator> allocator;
  Com<ID3D12GraphicsCommandList> list;
  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&allocator))) ||
      FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       allocator.ptr(), nullptr,
                                       IID_PPV_ARGS(&list))))
    return false;
  const bool execute =
      strcmp(mode, "--overlap") && strcmp(mode, "--whole-mismatch");
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2]{};
  UINT rows[2]{};
  UINT64 row_sizes[2]{}, total = 0;
  Com<ID3D12Resource> upload, readback;
  if (execute) {
    device->GetCopyableFootprints(&desc, 0, 2, 0, footprints, rows, row_sizes,
                                  &total);
    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = total;
    buffer_desc.Height = buffer_desc.DepthOrArraySize = buffer_desc.MipLevels =
        1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
      return false;
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))))
      return false;
    void *mapped = nullptr;
    const D3D12_RANGE empty{0, 0};
    if (FAILED(upload->Map(0, &empty, &mapped)))
      return false;
    std::memset(mapped, 0, static_cast<size_t>(total));
    for (UINT mip = 0; mip != 2; ++mip) {
      for (UINT row = 0; row < rows[mip]; ++row) {
        auto *bytes = static_cast<unsigned char *>(mapped) +
                      footprints[mip].Offset +
                      row * footprints[mip].Footprint.RowPitch;
        for (UINT64 x = 0; x < row_sizes[mip]; ++x)
          bytes[x] = static_cast<unsigned char>(17 + mip * 61 + row * 7 + x);
      }
    }
    upload->Unmap(0, nullptr);
    for (UINT mip = 0; mip != 2; ++mip) {
      D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
      src.pResource = upload.ptr();
      src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      src.PlacedFootprint = footprints[mip];
      dst.pResource = texture.ptr();
      dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      dst.SubresourceIndex = mip;
      list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
  }
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {texture.ptr(), 0, D3D12_RESOURCE_STATE_COPY_DEST,
                        D3D12_RESOURCE_STATE_COPY_SOURCE};
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
  if (!strcmp(mode, "--whole") || !strcmp(mode, "--whole-mismatch"))
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
  if (!strcmp(mode, "--subresources")) {
    barrier.Transition.Subresource = 1;
    list->ResourceBarrier(1, &barrier);
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
    list->ResourceBarrier(1, &barrier);
    barrier.Transition.Subresource = 0;
  } else if (!strcmp(mode, "--disjoint-transition")) {
    barrier.Transition.Subresource = 1;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    list->ResourceBarrier(1, &barrier);
    barrier.Transition.Subresource = 0;
  } else if (!strcmp(mode, "--overlap")) {
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
    const HRESULT hr = list->Close();
    return hr == E_INVALIDARG;
  } else if (!strcmp(mode, "--whole-mismatch")) {
    barrier.Transition.Subresource = 0;
  }
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
  list->ResourceBarrier(1, &barrier);
  if (execute) {
    for (UINT mip = 0; mip != 2; ++mip) {
      D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
      src.pResource = texture.ptr();
      src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      src.SubresourceIndex = mip;
      dst.pResource = readback.ptr();
      dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      dst.PlacedFootprint = footprints[mip];
      list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
  }
  const HRESULT hr = list->Close();
  const bool should_fail = !strcmp(mode, "--whole-mismatch");
  if (should_fail ? hr != E_INVALIDARG : hr != S_OK) {
    std::cerr << "FAIL subresource mode=" << mode << " close=0x" << std::hex
              << static_cast<unsigned long>(hr) << '\n';
    return false;
  }
  if (execute) {
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    Com<ID3D12CommandQueue> queue;
    Com<ID3D12Fence> fence;
    if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&fence))))
      return false;
    ID3D12CommandList *lists[]{list.ptr()};
    queue->ExecuteCommandLists(1, lists);
    if (FAILED(queue->Signal(fence.ptr(), 1)))
      return false;
    HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!event)
      return false;
    const HRESULT event_result = fence->SetEventOnCompletion(1, event);
    const DWORD wait = SUCCEEDED(event_result)
                           ? WaitForSingleObject(event, 30000)
                           : WAIT_FAILED;
    CloseHandle(event);
    if (FAILED(event_result) || wait != WAIT_OBJECT_0 ||
        FAILED(device->GetDeviceRemovedReason()))
      return false;
    void *mapped = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    if (FAILED(readback->Map(0, &range, &mapped)))
      return false;
    bool equal = true;
    for (UINT mip = 0; mip != 2; ++mip) {
      for (UINT row = 0; row < rows[mip]; ++row) {
        const auto *bytes = static_cast<const unsigned char *>(mapped) +
                            footprints[mip].Offset +
                            row * footprints[mip].Footprint.RowPitch;
        for (UINT64 x = 0; x < row_sizes[mip]; ++x) {
          const auto expected =
              static_cast<unsigned char>(17 + mip * 61 + row * 7 + x);
          if (bytes[x] != expected) {
            std::cerr << "FAIL mip=" << mip << " row=" << row << " byte=" << x
                      << " actual=" << unsigned(bytes[x])
                      << " expected=" << unsigned(expected) << '\n';
            equal = false;
            break;
          }
        }
      }
    }
    readback->Unmap(0, nullptr);
    if (!equal)
      return false;
  }
  std::cout << "PASS subresource mode=" << mode
            << (execute ? " exact GPU readback" : "") << '\n';
  return true;
}

class ReferenceDiagnostics {
  ID3D12Device *device_;
  bool reported_ = false;

public:
  explicit ReferenceDiagnostics(ID3D12Device *device) : device_(device) {}
  ~ReferenceDiagnostics() {
    if (device_ && !reported_)
      Report();
  }
  bool Report() {
    reported_ = true;
    Com<ID3D12InfoQueue> info;
    const HRESULT info_hr = device_->QueryInterface(IID_PPV_ARGS(&info));
    if (FAILED(info_hr) || !info) {
      std::cout << "REFERENCE info queue unavailable hr=0x" << std::hex
                << static_cast<unsigned long>(info_hr) << std::dec << '\n';
    } else {
      const UINT64 count = info->GetNumStoredMessagesAllowedByRetrievalFilter();
      bool errors = false;
      std::cout << "REFERENCE info messages=" << count << '\n';
      for (UINT64 i = 0; i < count; ++i) {
        SIZE_T bytes = 0;
        if (FAILED(info->GetMessage(i, nullptr, &bytes)) || !bytes)
          return false;
        std::vector<unsigned char> storage(bytes);
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        if (FAILED(info->GetMessage(i, message, &bytes)))
          return false;
        std::cout << "REFERENCE message severity=" << message->Severity
                  << " id=" << message->ID << " " << message->pDescription
                  << '\n';
        errors |= message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                  message->Severity == D3D12_MESSAGE_SEVERITY_ERROR;
      }
      if (errors) {
        std::cerr << "FAIL reference debug layer reported errors\n";
        return false;
      }
    }
    return true;
  }
};

int main(int argc, char **argv) {
  const bool reference_cross =
      argc > 1 && !strcmp(argv[1], "--reference-cross-queue");
  const bool reference_reverse =
      argc > 1 && !strcmp(argv[1], "--reference-reverse-queue");
  const bool reference = reference_cross || reference_reverse;
  if (reference) {
    Com<ID3D12Debug> debug;
    using GetDebugInterface = HRESULT(WINAPI *)(REFIID, void **);
    const auto get_debug = reinterpret_cast<GetDebugInterface>(GetProcAddress(
        GetModuleHandleA("d3d12.dll"), "D3D12GetDebugInterface"));
    const HRESULT hr =
        get_debug ? get_debug(IID_PPV_ARGS(&debug)) : E_NOINTERFACE;
    if (SUCCEEDED(hr) && debug) {
      debug->EnableDebugLayer();
      std::cout << "REFERENCE debug layer enabled\n";
    } else {
      std::cout << "REFERENCE debug layer unavailable hr=0x" << std::hex
                << static_cast<unsigned long>(hr) << std::dec << '\n';
    }
  }
  const bool bad_oracle = argc > 1 && !strcmp(argv[1], "--bad-oracle");
  const bool oom_snapshot = argc > 1 && !strcmp(argv[1], "--oom-snapshot");
  const bool oom_signal = argc > 1 && !strcmp(argv[1], "--oom-signal");
  const bool wait_ready =
      argc > 1 && (!strcmp(argv[1], "--mismatch-wait-ready") ||
                   !strcmp(argv[1], "--wait-ready"));
  const bool pending_wait =
      argc > 1 && (!strcmp(argv[1], "--pending-wait") ||
                   !strcmp(argv[1], "--mismatch-pending"));
  const bool mismatch_pending =
      argc > 1 && !strcmp(argv[1], "--mismatch-pending");
  const bool mismatch_wait =
      mismatch_pending ||
      (argc > 1 && (!strcmp(argv[1], "--mismatch-wait") ||
                    !strcmp(argv[1], "--mismatch-wait-ready")));
  const bool mismatch_signal =
      mismatch_wait || (argc > 1 && !strcmp(argv[1], "--mismatch-signal"));
  const bool wait_submissions =
      pending_wait || wait_ready || mismatch_wait ||
      (argc > 1 && !strcmp(argv[1], "--wait-submissions"));
  const bool signal_submissions =
      oom_signal || mismatch_signal || wait_submissions ||
      (argc > 1 && !strcmp(argv[1], "--signal-submissions"));
  const bool mismatch_lists = argc > 1 && !strcmp(argv[1], "--mismatch-lists");
  const bool mismatch_submissions =
      mismatch_signal ||
      (argc > 1 && !strcmp(argv[1], "--mismatch-submissions"));
  const bool reverse_queue =
      reference_reverse || (argc > 1 && !strcmp(argv[1], "--reverse-queue"));
  const bool cross_return = argc > 1 && !strcmp(argv[1], "--cross-return");
  const bool cross_queue = reference_cross || reverse_queue || cross_return ||
                           (argc > 1 && !strcmp(argv[1], "--cross-queue"));
  const bool separate = oom_snapshot || signal_submissions ||
                        mismatch_submissions || mismatch_lists || cross_queue ||
                        (argc > 1 && (!strcmp(argv[1], "--lists") ||
                                      !strcmp(argv[1], "--submissions")));
  const bool submissions = oom_snapshot || signal_submissions ||
                           mismatch_submissions || cross_queue ||
                           (argc > 1 && !strcmp(argv[1], "--submissions"));
  const bool mismatch = argc > 1 && !strcmp(argv[1], "--mismatch");
  auto check = [](HRESULT hr, const char *name) {
    if (SUCCEEDED(hr))
      return true;
    std::cerr << "FAIL " << name << ": 0x" << std::hex
              << static_cast<unsigned long>(hr) << '\n';
    return false;
  };
  Com<ID3D12Device> device;
  if (!check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device)),
             "CreateDevice"))
    return 1;
  ReferenceDiagnostics diagnostics(reference ? device.ptr() : nullptr);
  if (argc > 1 &&
      (!strcmp(argv[1], "--subresources") ||
       !strcmp(argv[1], "--disjoint-transition") ||
       !strcmp(argv[1], "--overlap") || !strcmp(argv[1], "--whole") ||
       !strcmp(argv[1], "--whole-mismatch")))
    return SubresourceCases(device.ptr(), argv[1]) ? 0 : 1;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 65536;
  desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  auto make_buffer = [&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state,
                         Com<ID3D12Resource> &out) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    return check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                                                 &desc, state, nullptr,
                                                 IID_PPV_ARGS(&out)),
                 "CreateBuffer");
  };
  Com<ID3D12Resource> buffer, upload, readback;
  if (!make_buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                   buffer) ||
      !make_buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                   upload) ||
      !make_buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                   readback))
    return 1;
  bool destroyed = false;
  if (cross_queue && !reference) {
    const GUID marker_id{0xb962ba32,
                         0xe7ad,
                         0x4f10,
                         {0x88, 0x90, 0xa4, 0x10, 0x71, 0x12, 0x34, 0x56}};
    auto *marker = new DestructionMarker(&destroyed);
    const HRESULT marker_result =
        buffer->SetPrivateDataInterface(marker_id, marker);
    marker->Release();
    if (!check(marker_result, "Set resource destruction marker"))
      return 1;
  }
  constexpr std::array<UINT, 4> expected{0x12345678, 0xdeadbeef, 0x31415926,
                                         0x76543210};
  void *mapped = nullptr;
  const D3D12_RANGE empty{0, 0};
  if (!check(upload->Map(0, &empty, &mapped), "Map upload"))
    return 1;
  std::memcpy(mapped, expected.data(), sizeof(expected));
  upload->Unmap(0, nullptr);
  Com<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  if (!check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)),
             "CreateQueue"))
    return 1;
  Com<ID3D12CommandAllocator> first_allocator, second_allocator;
  Com<ID3D12GraphicsCommandList> first, second;
  auto record = [&](Com<ID3D12CommandAllocator> &allocator,
                    Com<ID3D12GraphicsCommandList> &list) {
    return check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&allocator)),
                 "CreateAllocator") &&
           check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           allocator.ptr(), nullptr,
                                           IID_PPV_ARGS(&list)),
                 "CreateList");
  };
  if (!record(first_allocator, first))
    return 1;
  first->CopyBufferRegion(buffer.ptr(), 0, upload.ptr(), 0, sizeof(expected));
  D3D12_RESOURCE_BARRIER transition{};
  transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  transition.Transition = {
      buffer.ptr(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
      D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE};
  transition.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
  const bool bad_subresource =
      argc > 1 && !strcmp(argv[1], "--bad-subresource");
  const bool same_state = argc > 1 && !strcmp(argv[1], "--same-state");
  const bool bad_flags = argc > 1 && !strcmp(argv[1], "--bad-flags");
  const bool bad_resource = argc > 1 && !strcmp(argv[1], "--bad-resource");
  const bool bad_state = argc > 1 && !strcmp(argv[1], "--bad-state");
  const bool bad_uav = argc > 1 && !strcmp(argv[1], "--bad-uav");
  const bool reserved_state = argc > 1 && !strcmp(argv[1], "--reserved-state");
  if (bad_subresource)
    transition.Transition.Subresource = 3;
  if (same_state)
    transition.Transition.StateAfter = transition.Transition.StateBefore;
  if (bad_flags)
    transition.Flags = static_cast<D3D12_RESOURCE_BARRIER_FLAGS>(3);
  if (bad_resource)
    transition.Transition.pResource = nullptr;
  if (bad_state)
    transition.Transition.StateAfter = static_cast<D3D12_RESOURCE_STATES>(
        D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE);
  if (bad_uav)
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  if (reserved_state)
    transition.Transition.StateAfter =
        static_cast<D3D12_RESOURCE_STATES>(0x80000000u);
  first->ResourceBarrier(1, &transition);
  if (bad_subresource || same_state || bad_flags || bad_resource || bad_state ||
      bad_uav || reserved_state) {
    const HRESULT result = first->Close();
    if (result != E_INVALIDARG) {
      std::cerr << "FAIL invalid split " << argv[1] << " close=0x" << std::hex
                << static_cast<unsigned long>(result) << '\n';
      return 1;
    }
    std::cout << "PASS invalid split " << argv[1] << " rejected\n";
    return 0;
  }
  ID3D12GraphicsCommandList *last = first.ptr();
  if (separate) {
    if (!check(first->Close(), "Close begin list") ||
        !record(second_allocator, second))
      return 1;
    last = second.ptr();
  }
  transition.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
  if (mismatch || mismatch_lists || mismatch_submissions)
    transition.Transition.StateAfter =
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
  last->ResourceBarrier(1, &transition);
  if (mismatch) {
    const HRESULT close_result = last->Close();
    if (close_result != E_INVALIDARG) {
      std::cerr << "FAIL mismatched split accepted: 0x" << std::hex
                << static_cast<unsigned long>(close_result) << '\n';
      return 1;
    }
    std::cout << "PASS mismatched split rejected\n";
    return 0;
  }
  if (!mismatch_submissions)
    last->CopyBufferRegion(readback.ptr(), 0, buffer.ptr(), 0,
                           sizeof(expected));
  if (!check(last->Close(), "Close end list"))
    return 1;
  ID3D12CommandList *lists[]{first.ptr(), second.ptr()};
  if (mismatch_lists) {
    queue->ExecuteCommandLists(2, lists);
    if (SUCCEEDED(device->GetDeviceRemovedReason())) {
      std::cerr << "FAIL cross-list mismatched split accepted\n";
      return 1;
    }
    std::cout << "PASS cross-list mismatched split rejected\n";
    return 0;
  }
  Com<ID3D12CommandQueue> consumer;
  Com<ID3D12Fence> handoff;
  if (cross_queue &&
      (!check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&consumer)),
              "Create consumer queue") ||
       !check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                  IID_PPV_ARGS(&handoff)),
              "Create handoff fence")))
    return 1;
  if (reverse_queue) {
    if (!check(consumer->Wait(handoff.ptr(), 1),
               "Wait handoff before producer submission"))
      return 1;
    consumer->ExecuteCommandLists(1, lists + 1);
    queue->ExecuteCommandLists(1, lists);
    if (!check(queue->Signal(handoff.ptr(), 1),
               "Signal handoff after consumer submission"))
      return 1;
  } else if (submissions) {
    queue->ExecuteCommandLists(1, lists);
    if (cross_queue) {
      if (!check(queue->Signal(handoff.ptr(), 1), "Signal handoff") ||
          !check(consumer->Wait(handoff.ptr(), 1), "Wait handoff"))
        return 1;
      consumer->ExecuteCommandLists(1, lists + 1);
    } else {
      if (oom_snapshot && !SetEnvironmentVariableA(
                              "DXMT_TEST_LEGACY_DIAGNOSTIC_OOM", "snapshot"))
        return 1;
      if (signal_submissions) {
        HANDLE signal_entered = nullptr, signal_resume = nullptr;
        if (pending_wait) {
          char entered_name[80], resume_name[80];
          std::snprintf(entered_name, sizeof(entered_name),
                        "DXMT.Test.LegacySignalEntered.%lu",
                        GetCurrentProcessId());
          std::snprintf(resume_name, sizeof(resume_name),
                        "DXMT.Test.LegacySignalResume.%lu",
                        GetCurrentProcessId());
          signal_entered = CreateEventA(nullptr, FALSE, FALSE, entered_name);
          signal_resume = CreateEventA(nullptr, FALSE, FALSE, resume_name);
          if (!signal_entered || !signal_resume ||
              !SetEnvironmentVariableA("DXMT_TEST_PAUSE_LEGACY_SIGNAL", "1"))
            return 1;
        }
        Com<ID3D12Fence> intermediate;
        if (oom_signal && !SetEnvironmentVariableA(
                              "DXMT_TEST_LEGACY_DIAGNOSTIC_OOM", "signal"))
          return 1;
        if (!check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&intermediate)),
                   "Create intermediate fence") ||
            !check(queue->Signal(intermediate.ptr(), 1),
                   "Signal between split submissions"))
          return 1;
        if (oom_signal)
          SetEnvironmentVariableA("DXMT_TEST_LEGACY_DIAGNOSTIC_OOM", nullptr);
        if (pending_wait &&
            WaitForSingleObject(signal_entered, 10000) != WAIT_OBJECT_0) {
          SetEvent(signal_resume);
          CloseHandle(signal_entered);
          CloseHandle(signal_resume);
          std::cerr << "FAIL prior signal did not reach deterministic "
                       "completion gate\n";
          return 1;
        }
        if (wait_ready) {
          HANDLE ready = CreateEventA(nullptr, FALSE, FALSE, nullptr);
          if (!ready)
            return 1;
          const HRESULT ready_hr = intermediate->SetEventOnCompletion(1, ready);
          const DWORD ready_wait = SUCCEEDED(ready_hr)
                                       ? WaitForSingleObject(ready, 30000)
                                       : WAIT_FAILED;
          CloseHandle(ready);
          if (!check(ready_hr, "Wait for intermediate completion") ||
              ready_wait != WAIT_OBJECT_0)
            return 1;
        }
        const bool wait_ok =
            !wait_submissions || check(queue->Wait(intermediate.ptr(), 1),
                                       "Wait between split submissions");
        if (pending_wait) {
          SetEvent(signal_resume);
          CloseHandle(signal_entered);
          CloseHandle(signal_resume);
          SetEnvironmentVariableA("DXMT_TEST_PAUSE_LEGACY_SIGNAL", nullptr);
        }
        if (!wait_ok)
          return 1;
      }
      queue->ExecuteCommandLists(1, lists + 1);
      if (oom_snapshot)
        SetEnvironmentVariableA("DXMT_TEST_LEGACY_DIAGNOSTIC_OOM", nullptr);
    }
  } else {
    queue->ExecuteCommandLists(separate ? 2 : 1, lists);
  }
  if (mismatch_submissions) {
    if (SUCCEEDED(device->GetDeviceRemovedReason())) {
      std::cerr << "FAIL cross-submission mismatch was not diagnosed\n";
      return 1;
    }
    std::cout << "PASS cross-submission mismatch diagnosed\n";
    return 0;
  }
  Com<ID3D12Fence> fence;
  if (!check(
          device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
          "CreateFence") ||
      !check(
          (cross_queue ? consumer.ptr() : queue.ptr())->Signal(fence.ptr(), 1),
          "Signal"))
    return 1;
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    return 1;
  HRESULT hr = fence->SetEventOnCompletion(1, event);
  DWORD wait = SUCCEEDED(hr) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event);
  if (!check(hr, "SetEventOnCompletion") || wait != WAIT_OBJECT_0 ||
      !check(device->GetDeviceRemovedReason(), "DeviceRemoved"))
    return 1;
  const D3D12_RANGE range{0, sizeof(expected)};
  if (!check(readback->Map(0, &range, &mapped), "Map readback"))
    return 1;
  auto oracle = expected;
  if (bad_oracle)
    oracle[1] ^= 1u;
  const bool equal = std::memcmp(mapped, oracle.data(), sizeof(oracle)) == 0;
  readback->Unmap(0, nullptr);
  if (!equal) {
    std::cerr << "FAIL split readback mismatch\n";
    return 1;
  }
  if (cross_return) {
    Com<ID3D12CommandAllocator> return_allocator;
    Com<ID3D12GraphicsCommandList> return_list;
    if (!record(return_allocator, return_list))
      return 1;
    D3D12_RESOURCE_BARRIER back{};
    back.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    back.Transition = {buffer.ptr(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                       D3D12_RESOURCE_STATE_COPY_SOURCE,
                       D3D12_RESOURCE_STATE_COPY_DEST};
    return_list->ResourceBarrier(1, &back);
    if (!check(return_list->Close(), "Close return list"))
      return 1;
    ID3D12CommandList *return_lists[]{return_list.ptr()};
    queue->ExecuteCommandLists(1, return_lists);
    if (!check(queue->Signal(fence.ptr(), 2), "Signal return list"))
      return 1;
    HANDLE returned = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!returned)
      return 1;
    const HRESULT event_hr = fence->SetEventOnCompletion(2, returned);
    const DWORD returned_wait = SUCCEEDED(event_hr)
                                    ? WaitForSingleObject(returned, 30000)
                                    : WAIT_FAILED;
    CloseHandle(returned);
    if (!check(event_hr, "Wait return list") ||
        returned_wait != WAIT_OBJECT_0 ||
        !check(device->GetDeviceRemovedReason(), "Return queue device status"))
      return 1;
  }
  if (cross_queue && !reference) {
    first = nullptr;
    second = nullptr;
    first_allocator = nullptr;
    second_allocator = nullptr;
    buffer = nullptr;
    for (unsigned i = 0; i != 1000 && !destroyed; ++i)
      Sleep(1);
    if (!destroyed) {
      std::cerr << "FAIL completed cross-queue split retains resource\n";
      return 1;
    }
  }
  if (reference && !diagnostics.Report())
    return 1;
  std::cout << "PASS legacy split exact readback mode="
            << (signal_submissions ? "signal-submissions"
                : reverse_queue    ? "reverse-queue"
                : cross_queue      ? "cross-queue"
                : submissions      ? "submissions"
                : separate         ? "lists"
                                   : "same-list")
            << '\n';
  return 0;
}

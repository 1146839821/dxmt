#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tlhelp32.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>

#include "d3d12_device.hpp"

#include <iostream>
#include <cstdint>
#include <thread>
#include <atomic>
#include <array>
#include <cstdlib>
#include <cstring>
#include <new>

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

// Scoped IAT replacement affects only this process's driver DLL.
class DriverImportPatch {
  ULONG_PTR *import_ = nullptr;
  void *original_ = nullptr;

public:
  DriverImportPatch() = default;
  DriverImportPatch(const DriverImportPatch &) = delete;
  DriverImportPatch &operator=(const DriverImportPatch &) = delete;
  bool Install(const char *symbol, void *replacement, void *&original) {
    auto *base =
        reinterpret_cast<unsigned char *>(GetModuleHandleA("d3d12.dll"));
    if (!base)
      return false;
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    auto *imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(
        base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
                   .VirtualAddress);
    for (; imports->Name; imports++) {
      if (!imports->OriginalFirstThunk)
        continue;
      auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>(
          base + imports->OriginalFirstThunk);
      auto *iat =
          reinterpret_cast<IMAGE_THUNK_DATA *>(base + imports->FirstThunk);
      for (; names->u1.AddressOfData; names++, iat++) {
        if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
          continue;
        auto *name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(
            base + names->u1.AddressOfData);
        if (std::strcmp(reinterpret_cast<const char *>(name->Name), symbol))
          continue;
        DWORD protection;
        if (!VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function),
                            PAGE_READWRITE, &protection))
          return false;
        original_ = reinterpret_cast<void *>(iat->u1.Function);
        original = original_;
        import_ = &iat->u1.Function;
        InterlockedExchangePointer(reinterpret_cast<void *volatile *>(import_),
                                   replacement);
        VirtualProtect(import_, sizeof(*import_), protection, &protection);
        return true;
      }
    }
    return false;
  }
  ~DriverImportPatch() {
    if (import_) {
      DWORD protection;
      if (VirtualProtect(import_, sizeof(*import_), PAGE_READWRITE,
                         &protection)) {
        InterlockedExchangePointer(reinterpret_cast<void *volatile *>(import_),
                                   original_);
        VirtualProtect(import_, sizeof(*import_), protection, &protection);
      }
    }
  }
};

// Opt-in, process-local fault injection: the driver DLL alone sees one failed
// allocation on this thread. No real memory pressure or production test switch.
class SubmissionAllocationFault {
  inline static void *(__cdecl *malloc_)(size_t) = nullptr;
  inline static void *(__cdecl *calloc_)(size_t, size_t) = nullptr;
  inline static thread_local UINT remaining_ = 0, calls_ = 0;
  inline static thread_local bool hit_ = false;
  DriverImportPatch patch_, calloc_patch_;
  static bool FailAllocation() {
    if (remaining_) {
      calls_++;
      if (!--remaining_) {
        hit_ = true;
        return true;
      }
    }
    return false;
  }
  static void *__cdecl Allocate(size_t size) {
    return FailAllocation() ? nullptr : malloc_(size);
  }
  static void *__cdecl AllocateZeroed(size_t count, size_t size) {
    return FailAllocation() ? nullptr : calloc_(count, size);
  }

public:
  bool Install(bool include_calloc = false) {
    void *original = nullptr;
    if (!patch_.Install("malloc", reinterpret_cast<void *>(&Allocate),
                        original))
      return false;
    malloc_ = reinterpret_cast<decltype(malloc_)>(original);
    if (include_calloc) {
      if (!calloc_patch_.Install(
              "calloc", reinterpret_cast<void *>(&AllocateZeroed), original))
        return false;
      calloc_ = reinterpret_cast<decltype(calloc_)>(original);
    }
    return true;
  }
  void Arm(UINT allocation) {
    calls_ = 0;
    hit_ = false;
    remaining_ = allocation;
  }
  void Stop() { remaining_ = 0; }
  bool Hit() const { return hit_; }
  UINT Calls() const { return calls_; }
  ~SubmissionAllocationFault() { Stop(); }
};

// One-shot native factory failure. Valid command buffers still reach Metal;
// count any commit after the injected failure to catch partial submissions.
class SubmissionCreationFault {
  inline static void *create_ = nullptr, *create_options_ = nullptr;
  inline static void *commit_ = nullptr;
  inline static thread_local bool armed_ = false, hit_ = false;
  inline static thread_local unsigned commits_ = 0;
  DriverImportPatch create_patch_, options_patch_, commit_patch_;
  template <bool Options, typename... Args>
  static obj_handle_t __cdecl Create(obj_handle_t object, Args... args) {
    if (armed_) {
      armed_ = false;
      hit_ = true;
      return 0;
    }
    auto original =
        reinterpret_cast<obj_handle_t(__cdecl *)(obj_handle_t, Args...)>(
            Options ? create_options_ : create_);
    return original(object, args...);
  }
  static void __cdecl Commit(obj_handle_t buffer) {
    if (hit_)
      commits_++;
    // Do not forward nil commits to Metal when testing a broken driver.
    if (buffer)
      reinterpret_cast<void(__cdecl *)(obj_handle_t)>(commit_)(buffer);
  }
public:
  bool Install(const char *encoder = nullptr) {
    const char *symbol = "MTLCommandQueue_commandBuffer";
    void *replacement = reinterpret_cast<void *>(&Create<false>);
    if (encoder) {
      if (!std::strcmp(encoder, "copy"))
        symbol = "MTLCommandBuffer_blitCommandEncoder";
      else if (!std::strcmp(encoder, "compute")) {
        symbol = "MTLCommandBuffer_computeCommandEncoder";
        replacement = reinterpret_cast<void *>(&Create<false, bool>);
      } else if (!std::strcmp(encoder, "clear") ||
                 !std::strcmp(encoder, "late-clear")) {
        symbol = "MTLCommandBuffer_renderCommandEncoder";
        replacement =
            reinterpret_cast<void *>(&Create<false, WMTRenderPassInfo *>);
      } else {
        symbol = "MTLCommandBuffer_blitCommandEncoderWithSampleBuffers";
        replacement = reinterpret_cast<void *>(
            &Create<false, WMTSampleBufferAttachmentInfo *, uint64_t>);
      }
    }
    if (!create_patch_.Install(symbol, replacement, create_))
      return false;
    if (!encoder &&
        !options_patch_.Install(
            "MTLCommandQueue_commandBufferWithErrorOptions",
            reinterpret_cast<void *>(&Create<true, uint64_t>), create_options_))
      return false;
    return commit_patch_.Install("MTLCommandBuffer_commit",
                                 reinterpret_cast<void *>(&Commit), commit_);
  }
  void Arm() {
    hit_ = false;
    commits_ = 0;
    armed_ = true;
  }
  void Stop() { armed_ = false; }
  bool Hit() const { return hit_; }
  unsigned Commits() const { return commits_; }
  ~SubmissionCreationFault() { Stop(); }
};

bool TestCommandBufferFailure(const char *operation,
                              const char *encoder = nullptr) {
  if (std::strcmp(operation, "execute") && std::strcmp(operation, "signal") &&
      std::strcmp(operation, "wait") && std::strcmp(operation, "calibrate"))
    return false;
  struct Event {
    HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Event() {
      if (handle)
        CloseHandle(handle);
    }
  } event; // Outlive all fence references, including pending event
           // registration.
  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Owned<ID3D12Fence> fence;
  Owned<ID3D12Resource> source, destination, texture;
  Owned<ID3D12DescriptorHeap> rtv;
  Owned<ID3D12RootSignature> root;
  Owned<ID3D12PipelineState> pipeline;
  Owned<ID3D12QueryHeap> query;
  D3D12_COMMAND_QUEUE_DESC desc = {};
  if (!event.handle ||
      !CheckHR("buffer device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device.ptr))) ||
      !CheckHR("buffer queue",
               device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR("buffer allocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator.ptr))) ||
      !CheckHR("buffer list",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator.ptr, nullptr,
                                         IID_PPV_ARGS(&list.ptr))) ||
      !CheckHR("buffer fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                   IID_PPV_ARGS(&fence.ptr))) ||
      !CheckHR("buffer event", fence->SetEventOnCompletion(1, event.handle)))
    return false;
  if (encoder) {
    const bool copy =
        !std::strcmp(encoder, "copy") || !std::strcmp(encoder, "late-clear");
    const bool clear =
        !std::strcmp(encoder, "clear") || !std::strcmp(encoder, "late-clear");
    if (copy) {
      D3D12_RESOURCE_DESC buffer = {};
      buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      buffer.Width = 256;
      buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels =
          buffer.SampleDesc.Count = 1;
      buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      D3D12_HEAP_PROPERTIES heap = {};
      heap.Type = D3D12_HEAP_TYPE_UPLOAD;
      heap.CreationNodeMask = heap.VisibleNodeMask = 1;
      if (!CheckHR("encoder upload", device->CreateCommittedResource(
                                         &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                         D3D12_RESOURCE_STATE_GENERIC_READ,
                                         nullptr, IID_PPV_ARGS(&source.ptr))))
        return false;
      heap.Type = D3D12_HEAP_TYPE_READBACK;
      if (!CheckHR("encoder readback",
                   device->CreateCommittedResource(
                       &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                       IID_PPV_ARGS(&destination.ptr))))
        return false;
      list->CopyBufferRegion(destination.ptr, 0, source.ptr, 0, 256);
    }
    if (clear) {
      D3D12_RESOURCE_DESC desc = {};
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Width = desc.Height = 4;
      desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
      desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      D3D12_HEAP_PROPERTIES heap = {};
      heap.Type = D3D12_HEAP_TYPE_DEFAULT;
      heap.CreationNodeMask = heap.VisibleNodeMask = 1;
      D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
      heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
      heap_desc.NumDescriptors = 1;
      if (!CheckHR("encoder texture",
                   device->CreateCommittedResource(
                       &heap, D3D12_HEAP_FLAG_NONE, &desc,
                       D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                       IID_PPV_ARGS(&texture.ptr))) ||
          !CheckHR("encoder RTV", device->CreateDescriptorHeap(
                                      &heap_desc, IID_PPV_ARGS(&rtv.ptr))))
        return false;
      auto handle = rtv->GetCPUDescriptorHandleForHeapStart();
      device->CreateRenderTargetView(texture.ptr, nullptr, handle);
      const float color[] = {1, 0, 0, 1};
      const D3D12_RECT rect = {0, 0, 2, 2};
      list->ClearRenderTargetView(handle, color, 1, &rect);
    } else if (!std::strcmp(encoder, "compute")) {
      struct Compiler {
        HMODULE module = LoadLibraryA(D3DCOMPILER_DLL_A);
        ~Compiler() {
          if (module)
            FreeLibrary(module);
        }
      } compiler;
      auto compile = compiler.module
                         ? reinterpret_cast<pD3DCompile>(
                               GetProcAddress(compiler.module, "D3DCompile"))
                         : nullptr;
      const char shader[] = "[numthreads(1,1,1)] void main() {}";
      Owned<ID3DBlob> code, errors, serialized;
      D3D12_ROOT_SIGNATURE_DESC root_desc = {};
      if (!compile ||
          !CheckHR("encoder shader",
                   compile(shader, sizeof(shader) - 1, nullptr, nullptr,
                           nullptr, "main", "cs_5_0", 0, 0, &code.ptr,
                           &errors.ptr)) ||
          !CheckHR("encoder root serialization",
                   D3D12SerializeRootSignature(&root_desc,
                                               D3D_ROOT_SIGNATURE_VERSION_1,
                                               &serialized.ptr, nullptr)) ||
          !CheckHR("encoder root",
                   device->CreateRootSignature(
                       0, serialized->GetBufferPointer(),
                       serialized->GetBufferSize(), IID_PPV_ARGS(&root.ptr))))
        return false;
      D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
      pso.pRootSignature = root.ptr;
      pso.CS = {code->GetBufferPointer(), code->GetBufferSize()};
      if (!CheckHR("encoder PSO", device->CreateComputePipelineState(
                                      &pso, IID_PPV_ARGS(&pipeline.ptr))))
        return false;
      list->SetPipelineState(pipeline.ptr);
      list->SetComputeRootSignature(root.ptr);
      list->Dispatch(1, 1, 1);
    } else if (!std::strcmp(encoder, "timestamp")) {
      D3D12_QUERY_HEAP_DESC query_desc = {};
      query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
      query_desc.Count = 1;
      if (!CheckHR("encoder query", device->CreateQueryHeap(
                                        &query_desc, IID_PPV_ARGS(&query.ptr))))
        return false;
      list->EndQuery(query.ptr, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    }
  }
  if (!CheckHR("buffer close", list->Close()))
    return false;
  SubmissionCreationFault fault;
  if (!fault.Install(encoder))
    return false;
  HRESULT result = S_OK;
  UINT64 gpu = 0x1234, cpu = 0x5678;
  fault.Arm();
  if (!std::strcmp(operation, "execute")) {
    ID3D12CommandList *batch[] = {list.ptr};
    queue->ExecuteCommandLists(1, batch);
  } else if (!std::strcmp(operation, "signal")) {
    result = queue->Signal(fence.ptr, 1);
  } else if (!std::strcmp(operation, "wait")) {
    result = queue->Wait(fence.ptr, 1);
  } else {
    result = queue->GetClockCalibration(&gpu, &cpu);
  }
  fault.Stop();
  const auto reason = device->GetDeviceRemovedReason();
  const bool busy = static_cast<dxmt::MTLD3D12CommandAllocator *>(allocator.ptr)
                        ->IsInFlight();
  const auto recording =
      static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list.ptr)
          ->GetRecordingState();
  const bool recording_busy = recording && recording->IsInFlight();
  const bool woke = WaitForSingleObject(event.handle, 0) == WAIT_OBJECT_0;
  const auto completed = fence->GetCompletedValue();
  std::cout << "command buffer operation=" << operation
            << " encoder=" << (encoder ? encoder : "none")
            << " hit=" << fault.Hit() << " commits=" << fault.Commits()
            << " allocator_busy=" << busy
            << " recording_busy=" << recording_busy << " woke=" << woke
            << " completed=" << completed << " result=0x" << std::hex
            << static_cast<unsigned long>(result) << " reason=0x"
            << static_cast<unsigned long>(reason) << std::dec << "\n";
  return fault.Hit() && !fault.Commits() && !busy && !recording_busy && woke &&
         completed == UINT64_MAX && reason == DXGI_ERROR_DEVICE_REMOVED &&
         (!std::strcmp(operation, "execute") ||
          result == DXGI_ERROR_DEVICE_REMOVED) &&
         (std::strcmp(operation, "calibrate") ||
          (gpu == 0x1234 && cpu == 0x5678));
}

bool TestSubmissionAllocationFailure(UINT allocation, bool partial_clear,
                                     bool abort_translation = false) {
  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Owned<ID3D12Resource> texture;
  Owned<ID3D12DescriptorHeap> rtv;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  if (!CheckHR("OOM device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                               IID_PPV_ARGS(&device.ptr))) ||
      !CheckHR("OOM queue", device->CreateCommandQueue(
                                &queue_desc, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR("OOM allocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator.ptr))) ||
      !CheckHR("OOM list", device->CreateCommandList(
                               0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr,
                               nullptr, IID_PPV_ARGS(&list.ptr))))
    return false;
  if (partial_clear) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = 4;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors = 1;
    if (!CheckHR("OOM clear texture",
                 device->CreateCommittedResource(
                     &heap, D3D12_HEAP_FLAG_NONE, &desc,
                     D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                     IID_PPV_ARGS(&texture.ptr))) ||
        !CheckHR("OOM RTV heap", device->CreateDescriptorHeap(
                                     &heap_desc, IID_PPV_ARGS(&rtv.ptr))))
      return false;
    auto handle = rtv->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(texture.ptr, nullptr, handle);
    const float color[] = {1, 0, 0, 1};
    const D3D12_RECT rect = {0, 0, 2, 2};
    list->ClearRenderTargetView(handle, color, 1, &rect);
  }
  if (!CheckHR("OOM close", list->Close()))
    return false;
  dxmt::ClearEncoderData *clear = nullptr;
  D3D12_RECT *saved_rects = nullptr;
  if (abort_translation) {
    // Exercise the existing CPU-side translation rejection without sending an
    // invalid command to Metal. Restore the recording before its real replay.
    auto *internal = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list.ptr);
    for (auto *encoder = internal->entry; encoder; encoder = encoder->next)
      if (encoder->type == dxmt::EncoderType::Clear) {
        clear = static_cast<dxmt::ClearEncoderData *>(encoder);
        break;
      }
    if (!clear || !clear->rect_count || !clear->rects)
      return false;
    saved_rects = clear->rects;
    clear->rects = nullptr;
  }
  SubmissionAllocationFault fault;
  if (!fault.Install()) {
    if (clear)
      clear->rects = saved_rects;
    return false;
  }
  ID3D12CommandList *batch[] = {list.ptr};
  bool escaped = false;
  fault.Arm(allocation);
  try {
    queue->ExecuteCommandLists(1, batch);
  } catch (...) {
    escaped = true;
  }
  fault.Stop();
  const auto reason = device->GetDeviceRemovedReason();
  auto *internal_allocator =
      static_cast<dxmt::MTLD3D12CommandAllocator *>(allocator.ptr);
  std::cout << "submission allocation=" << allocation
            << " calls=" << fault.Calls() << " hit=" << fault.Hit()
            << " escaped=" << escaped
            << " allocator_busy=" << internal_allocator->IsInFlight()
            << " reason=0x" << std::hex << static_cast<unsigned long>(reason)
            << std::dec << "\n";
  if (clear) {
    clear->rects = saved_rects;
    if (escaped || internal_allocator->IsInFlight() || FAILED(reason))
      return false;
    queue->ExecuteCommandLists(1, batch);
  }
  if (fault.Hit())
    return !escaped && !internal_allocator->IsInFlight() &&
           reason == E_OUTOFMEMORY;
  // A beyond-last-allocation control must still commit, complete and retire
  // allocator admission. Keep the fault disabled during the fence operation.
  Owned<ID3D12Fence> fence;
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  const bool completed =
      event && !escaped && SUCCEEDED(reason) &&
      CheckHR("OOM control fence",
              device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                  IID_PPV_ARGS(&fence.ptr))) &&
      CheckHR("OOM control signal", queue->Signal(fence.ptr, 1)) &&
      CheckHR("OOM control event", fence->SetEventOnCompletion(1, event)) &&
      WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
  if (event)
    CloseHandle(event);
  return completed && !internal_allocator->IsInFlight();
}

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
  Owned<ID3D12CommandAllocator> busy_allocator;
  Owned<ID3D12GraphicsCommandList> busy_list;
  if (!CheckHR("Busy reset allocator", device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&busy_allocator.ptr))) ||
      !CheckHR("Busy allocator generation 1", busy_allocator->Reset()) ||
      !CheckHR("Busy allocator generation 2", busy_allocator->Reset()) ||
      !CheckHR("Busy reset list", device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, busy_allocator.ptr, nullptr, IID_PPV_ARGS(&busy_list.ptr))))
    return false;
  // A rejected Reset must not replace the allocator owning the closed stream.
  // Different generations make a mistaken owner observable before GPU access.
  if (fresh->Reset(busy_allocator.ptr, nullptr) != E_INVALIDARG) {
    std::cerr << "Reset accepted an allocator used by another recording list\n";
    return false;
  }
  if (!CheckHR("Clear rejected Reset result", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    return false;
  *mapped = 0;
  readback->Unmap(0, nullptr);
  if (!submit("closed recording after rejected Reset", &fresh_list, 1, 0x22222222))
    return false;
  busy_list->CopyBufferRegion(readback.ptr, 0, source.ptr, 0, sizeof(uint32_t));
  ID3D12CommandList *busy = busy_list.ptr;
  if (!CheckHR("Close busy reset list", busy_list->Close()) ||
      !submit("other recording after rejected Reset", &busy, 1, 0x11111111) ||
      !CheckHR("Retry Reset after allocator released", fresh->Reset(busy_allocator.ptr, nullptr)))
    return false;
  fresh->CopyBufferRegion(readback.ptr, 0, source.ptr, sizeof(uint32_t), sizeof(uint32_t));
  if (!CheckHR("Close retried Reset", fresh->Close()) ||
      !submit("retried Reset recording", &fresh_list, 1, 0x22222222))
    return false;
  if (!CheckHR("Reset test rerecord stale", stale->Reset(allocator.ptr, nullptr)))
    return false;
  stale->CopyBufferRegion(readback.ptr, 0, source.ptr, 0, sizeof(uint32_t));
  return CheckHR("Reset test close rerecorded", stale->Close()) &&
         submit("rerecorded command list", &stale_list, 1, 0x11111111);
}

bool TestAllocatorRecordingGrowth(ID3D12Device *device) {
  constexpr UINT max_recordings = 129;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12Resource> source, readback;
  Owned<ID3D12Fence> fence;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  if (!CheckHR("Growth queue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR("Growth allocator", device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr))) ||
      !CheckHR("Growth fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.ptr))))
    return false;
  struct Event {
    HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Event() { if (handle) CloseHandle(handle); }
  } event;
  if (!event.handle) return false;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = max_recordings * sizeof(uint32_t);
  desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  if (!CheckHR("Growth source", device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&source.ptr))))
    return false;
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  if (!CheckHR("Growth readback", device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback.ptr))))
    return false;
  uint32_t *mapped = nullptr;
  if (!CheckHR("Growth map source", source->Map(0, nullptr, reinterpret_cast<void **>(&mapped)))) return false;
  for (UINT i = 0; i < max_recordings; ++i) mapped[i] = 0x5a000000u + i;
  source->Unmap(0, nullptr);
  UINT64 value = 0;
  for (UINT count : {63u, 64u, 65u, max_recordings}) {
    if (!CheckHR("Growth allocator reset", allocator->Reset())) return false;
    std::array<Owned<ID3D12GraphicsCommandList>, max_recordings> lists;
    std::array<ID3D12CommandList *, max_recordings> batch{};
    for (UINT i = 0; i < count; ++i) {
      if (!CheckHR("Growth list", device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, nullptr, IID_PPV_ARGS(&lists[i].ptr)))) return false;
      lists[i]->CopyBufferRegion(readback.ptr, i * sizeof(uint32_t), source.ptr, i * sizeof(uint32_t), sizeof(uint32_t));
      if (!CheckHR("Growth close", lists[i]->Close())) return false;
      batch[i] = lists[i].ptr;
    }
    for (unsigned replay = 0; replay < 2; ++replay) {
      if (!CheckHR("Growth clear readback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)))) return false;
      for (UINT i = 0; i < max_recordings; ++i) mapped[i] = 0;
      readback->Unmap(0, nullptr);
      queue->ExecuteCommandLists(count, batch.data());
      if (!CheckHR("Growth signal", queue->Signal(fence.ptr, ++value)) ||
          !CheckHR("Growth completion event", fence->SetEventOnCompletion(value, event.handle)) ||
          WaitForSingleObject(event.handle, 5000) != WAIT_OBJECT_0 ||
          !CheckHR("Growth healthy device", device->GetDeviceRemovedReason()) ||
          !CheckHR("Growth result", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)))) return false;
      bool correct = true;
      for (UINT i = 0; i < max_recordings; ++i) {
        const uint32_t expected = i < count ? 0x5a000000u + i : 0;
        if (mapped[i] != expected) {
          std::cerr << "growth count " << count << " replay " << replay << " element " << i << " mismatch\n";
          correct = false;
          break;
        }
      }
      readback->Unmap(0, nullptr);
      if (!correct) return false;
      std::cout << "allocator growth count " << count << " replay " << replay << " exact readback passed\n";
    }
  }
  return true;
}

class FenceHandleProbe {
  inline static BOOL(WINAPI *duplicate_)(HANDLE, HANDLE, HANDLE, LPHANDLE,
                                         DWORD, BOOL, DWORD) = nullptr;
  inline static BOOL(WINAPI *close_)(HANDLE) = nullptr;
  inline static BOOL(WINAPI *signal_)(HANDLE) = nullptr;
  inline static std::atomic<unsigned> opened_{0}, closed_{0}, signals_{0};
  DriverImportPatch duplicate_patch_, close_patch_, signal_patch_;
  static BOOL WINAPI Signal(HANDLE handle) {
    signals_++;
    return signal_(handle);
  }
  static BOOL WINAPI Duplicate(HANDLE a, HANDLE b, HANDLE c, LPHANDLE d,
                               DWORD e, BOOL f, DWORD g) {
    BOOL ok = duplicate_(a, b, c, d, e, f, g);
    if (ok)
      opened_++;
    return ok;
  }
  static BOOL WINAPI Close(HANDLE handle) {
    BOOL ok = close_(handle);
    if (ok)
      closed_++;
    return ok;
  }

public:
  bool Install() {
    void *original = nullptr;
    opened_ = closed_ = signals_ = 0;
    if (!duplicate_patch_.Install(
            "DuplicateHandle", reinterpret_cast<void *>(&Duplicate), original))
      return false;
    duplicate_ = reinterpret_cast<decltype(duplicate_)>(original);
    if (!close_patch_.Install("CloseHandle", reinterpret_cast<void *>(&Close),
                              original))
      return false;
    close_ = reinterpret_cast<decltype(close_)>(original);
    if (!signal_patch_.Install("SetEvent", reinterpret_cast<void *>(&Signal),
                               original))
      return false;
    signal_ = reinterpret_cast<decltype(signal_)>(original);
    return true;
  }
  unsigned Signals() const { return signals_.load(); }
  bool Balanced() const {
    std::cout << "Fence handle duplicates=" << opened_ << " closes=" << closed_
              << "\n";
    return opened_ == closed_;
  }
};

DWORD ProcessThreadCount() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return 0;
  THREADENTRY32 entry = {};
  entry.dwSize = sizeof(entry);
  DWORD count = 0;
  if (Thread32First(snapshot, &entry))
    do { if (entry.th32OwnerProcessID == GetCurrentProcessId()) count++; }
    while (Thread32Next(snapshot, &entry));
  CloseHandle(snapshot);
  return count;
}

// Capture debugger dispatch without opening a debugger. Both callbacks reenter
// the queue, so a driver holding its mutex across dispatch will deadlock.
class InfoQueueDebugCapture {
  DriverImportPatch output_patch_, break_patch_;
  inline static ID3D12InfoQueue *queue_ = nullptr;
  static void WINAPI Output(const char *text) {
    outputs++;
    text_matches &= std::strcmp(text, "debug probe") == 0;
    queue_->GetMuteDebugOutput();
    queue_->ClearStoredMessages();
  }
  static void WINAPI Break() {
    breaks++;
    queue_->GetNumStoredMessages();
  }
public:
  inline static unsigned outputs = 0, breaks = 0;
  inline static bool text_matches = true;
  bool Install(ID3D12InfoQueue *queue) {
    queue_ = queue;
    void *original = nullptr;
    return output_patch_.Install("OutputDebugStringA", reinterpret_cast<void *>(&Output), original) &&
           break_patch_.Install("DebugBreak", reinterpret_cast<void *>(&Break), original);
  }
};

bool TestInfoQueueDebugControls() {
  Owned<ID3D12Device> device;
  Owned<ID3D12InfoQueue> queue;
  if (!CheckHR("Debug InfoQueue device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
          IID_PPV_ARGS(&device.ptr))) ||
      !CheckHR("Debug InfoQueue", device->QueryInterface(IID_PPV_ARGS(&queue.ptr)))) return false;
  InfoQueueDebugCapture capture;
  if (!capture.Install(queue.ptr)) {
    std::cerr << "InfoQueue debug imports unavailable\n";
    return false;
  }
  const auto category = D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED;
  const auto severity = D3D12_MESSAGE_SEVERITY_WARNING;
  const auto id = D3D12_MESSAGE_ID_STRING_FROM_APPLICATION;
  auto add = [&](unsigned output, unsigned stop) {
    auto old_output = capture.outputs, old_break = capture.breaks;
    return SUCCEEDED(queue->AddApplicationMessage(severity, "debug probe")) &&
           capture.outputs == old_output + output && capture.breaks == old_break + stop && capture.text_matches;
  };
  queue->SetMuteDebugOutput(FALSE);
  if (!add(1, 0)) return false;
  if (FAILED(queue->SetBreakOnSeverity(severity, TRUE)) || !add(1, 1) ||
      FAILED(queue->SetBreakOnSeverity(severity, FALSE))) return false;
  if (FAILED(queue->SetBreakOnCategory(category, TRUE)) || !add(1, 1) ||
      FAILED(queue->SetBreakOnCategory(category, FALSE))) return false;
  if (FAILED(queue->SetBreakOnID(id, TRUE)) || !add(1, 1)) return false;
  queue->SetMuteDebugOutput(TRUE);
  if (!add(0, 1)) return false;
  D3D12_INFO_QUEUE_FILTER filter = {};
  auto filtered_id = id;
  filter.DenyList.NumIDs = 1;
  filter.DenyList.pIDList = &filtered_id;
  queue->SetMuteDebugOutput(FALSE);
  if (FAILED(queue->PushStorageFilter(&filter)) || !add(0, 0)) return false;
  queue->PopStorageFilter();
  if (FAILED(queue->PushRetrievalFilter(&filter)) || !add(1, 1)) return false;
  queue->PopRetrievalFilter();
  queue->SetMessageCountLimit(0);
  if (!add(1, 1) || queue->GetNumStoredMessages()) return false;
  if (queue->SetBreakOnCategory(static_cast<D3D12_MESSAGE_CATEGORY>(-1), TRUE) != E_INVALIDARG ||
      queue->SetBreakOnSeverity(static_cast<D3D12_MESSAGE_SEVERITY>(-1), TRUE) != E_INVALIDARG ||
      queue->SetBreakOnID(static_cast<D3D12_MESSAGE_ID>(-1), TRUE) != E_INVALIDARG ||
      !queue->GetBreakOnID(id)) return false;
  queue->SetBreakOnID(id, FALSE);
  if (!add(1, 0)) return false;
  for (unsigned mode = 0; mode < 3; mode++) {
    Owned<ID3D12Device> fresh_device;
    Owned<ID3D12InfoQueue> fresh;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&fresh_device.ptr))) ||
        FAILED(fresh_device->QueryInterface(IID_PPV_ARGS(&fresh.ptr)))) return false;
    SubmissionAllocationFault fault;
    if (!fault.Install()) return false;
    fault.Arm(1);
    HRESULT hr = mode == 0 ? fresh->SetBreakOnCategory(category, TRUE) :
                 mode == 1 ? fresh->SetBreakOnSeverity(severity, TRUE) : fresh->SetBreakOnID(id, TRUE);
    fault.Stop();
    if (!fault.Hit() || hr != E_OUTOFMEMORY || fresh->GetBreakOnCategory(category) ||
        fresh->GetBreakOnSeverity(severity) || fresh->GetBreakOnID(id)) return false;
  }
  std::cout << "InfoQueue debug dispatch: output=" << capture.outputs << " break=" << capture.breaks
            << " storage/retrieval/mute/zero-limit/reentry passed\n";
  return true;
}

// The private-data reference survives public Release until the resource object
// is actually destroyed. It exposes leaks without adding production test hooks.
struct SplitLifetimeObserver : IUnknown {
  std::atomic<ULONG> refs{1};
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **result) override {
    if (!result)
      return E_POINTER;
    *result = nullptr;
    if (iid != __uuidof(IUnknown))
      return E_NOINTERFACE;
    *result = this;
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override { return --refs; }
};

bool TestSplitFailure(const char *mode, unsigned allocation) {
  const bool native = !std::strcmp(mode, "native");
  const bool oom = !std::strcmp(mode, "allocation");
  const bool record = !std::strcmp(mode, "record");
  const bool abandon = !std::strcmp(mode, "abandon");
  if (!native && !oom && !record && !abandon)
    return false;
  SplitLifetimeObserver
      observer; // Outlives every resource/queue/list reference.
  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> a, b;
  Owned<ID3D12GraphicsCommandList7> begin, end;
  Owned<ID3D12Resource> resource;
  Owned<ID3D12Fence> fence;
  D3D12_COMMAND_QUEUE_DESC desc = {};
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device.ptr))) ||
      FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue.ptr))) ||
      FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&a.ptr))) ||
      FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&b.ptr))) ||
      FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a.ptr,
                                       nullptr, IID_PPV_ARGS(&begin.ptr))) ||
      FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, b.ptr,
                                       nullptr, IID_PPV_ARGS(&end.ptr))) ||
      FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                 IID_PPV_ARGS(&fence.ptr))))
    return false;
  D3D12_RESOURCE_DESC texture = {};
  texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture.Width = texture.Height = 8;
  texture.DepthOrArraySize = texture.MipLevels = texture.SampleDesc.Count = 1;
  texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  if (FAILED(device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr, IID_PPV_ARGS(&resource.ptr))))
    return false;
  const GUID tag = {0xca2c8db1,
                    0x7c0a,
                    0x466c,
                    {0x93, 0x32, 0x52, 0x92, 0x01, 0x11, 0x01, 0x12}};
  if (FAILED(resource->SetPrivateDataInterface(tag, &observer)) ||
      observer.refs != 2)
    return false;
  D3D12_TEXTURE_BARRIER barrier = {};
  barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
  barrier.SyncAfter = D3D12_BARRIER_SYNC_SPLIT;
  barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
  barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
  barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST;
  barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
  barrier.pResource = resource.ptr;
  barrier.Subresources = {0, 1, 0, 1, 0, 1};
  D3D12_BARRIER_GROUP group = {};
  group.Type = D3D12_BARRIER_TYPE_TEXTURE;
  group.NumBarriers = 1;
  group.pTextureBarriers = &barrier;
  begin->Barrier(1, &group);
  if (FAILED(begin->Close()))
    return false;
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    return false;
  struct EventOwner {
    HANDLE value;
    ~EventOwner() { CloseHandle(value); }
  } event_owner{event};
  auto wait = [&](UINT64 value) {
    return SUCCEEDED(queue->Signal(fence.ptr, value)) &&
           SUCCEEDED(fence->SetEventOnCompletion(value, event)) &&
           WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
  };
  ID3D12CommandList *command = begin.ptr;
  queue->ExecuteCommandLists(1, &command);
  if (!wait(1) || FAILED(a->Reset()) || FAILED(begin->Reset(a.ptr, nullptr)))
    return false;
  SubmissionAllocationFault fault;
  SubmissionCreationFault creation;
  if ((oom || record) && !fault.Install())
    return false;
  if (native && !creation.Install())
    return false;
  barrier.SyncBefore = D3D12_BARRIER_SYNC_SPLIT;
  barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
  if (record)
    fault.Arm(1);
  end->Barrier(1, &group);
  fault.Stop();
  const HRESULT close = end->Close();
  if (record ? (!fault.Hit() || SUCCEEDED(close)) : FAILED(close))
    return false;
  resource.ptr->Release();
  resource.ptr = nullptr;
  if (observer.refs != 2)
    return false;
  if (!record && !abandon) {
    if (oom)
      fault.Arm(allocation);
    if (native)
      creation.Arm();
    command = end.ptr;
    queue->ExecuteCommandLists(1, &command);
    fault.Stop();
    creation.Stop();
    const HRESULT reason = device->GetDeviceRemovedReason();
    if (native && (!creation.Hit() || creation.Commits() || SUCCEEDED(reason)))
      return false;
    if (oom && fault.Hit() && reason != E_OUTOFMEMORY)
      return false;
    if (oom && !fault.Hit() && (!wait(2) || FAILED(reason)))
      return false;
  }
  end.ptr->Release();
  end.ptr = nullptr;
  const ULONG before_teardown = observer.refs;
  if (before_teardown != (record || abandon ? 2u : 1u))
    return false;
  queue.ptr->Release();
  queue.ptr = nullptr;
  std::cout << "split failure mode=" << mode << " allocation=" << allocation
            << " hit=" << (native ? creation.Hit() : fault.Hit())
            << " observer=" << before_teardown << "->" << observer.refs << "\n";
  return observer.refs == 1;
}

bool TestInfoQueueGrowthAllocationFailure() {
  Owned<ID3D12Device> device;
  Owned<ID3D12InfoQueue> queue;
  if (!CheckHR("Growth OOM device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device.ptr))) ||
      !CheckHR("Growth OOM queue",
               device->QueryInterface(IID_PPV_ARGS(&queue.ptr))))
    return false;
  queue->SetMuteDebugOutput(TRUE);
  if (!CheckHR("Growth OOM limit", queue->SetMessageCountLimit(UINT64_MAX)))
    return false;
  auto text_for = [](unsigned index) {
    return std::array<char, 7>{'m',
                               's',
                               'g',
                               char('0' + index / 100),
                               char('0' + index / 10 % 10),
                               char('0' + index % 10),
                               '\0'};
  };
  auto matches = [&](unsigned index) {
    alignas(D3D12_MESSAGE) std::array<unsigned char, sizeof(D3D12_MESSAGE) + 7>
        bytes = {};
    auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
    SIZE_T size = bytes.size();
    const auto text = text_for(index);
    return SUCCEEDED(queue->GetMessage(index, message, &size)) &&
           message->Category == D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED &&
           message->Severity == D3D12_MESSAGE_SEVERITY_INFO &&
           message->ID == D3D12_MESSAGE_ID_STRING_FROM_APPLICATION &&
           message->DescriptionByteLength == text.size() &&
           !std::strcmp(message->pDescription, text.data());
  };
  auto unchanged = [&](unsigned count) {
    return queue->GetNumStoredMessages() == count &&
           queue->GetNumMessagesAllowedByStorageFilter() == count &&
           queue->GetNumMessagesDeniedByStorageFilter() == 0 &&
           queue->GetNumMessagesDiscardedByMessageCountLimit() == 0 &&
           matches(0) && matches(count - 1);
  };
  const auto first = text_for(0);
  if (!CheckHR("Growth OOM initial message",
               queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO,
                                            first.data())))
    return false;
  SubmissionAllocationFault fault;
  if (!fault.Install())
    return false;
  unsigned boundaries = 0, failures = 0, count = 1;
  // Short messages isolate container growth from string allocation. Discover
  // growth through the public API, without assuming a vector/deque block size.
  for (; count < 512 && boundaries < 3; ++count) {
    const auto text = text_for(count);
    unsigned boundary_failures = 0;
    bool appended = false;
    for (unsigned allocation = 1; allocation <= 8; ++allocation) {
      fault.Arm(allocation);
      const HRESULT hr = queue->AddApplicationMessage(
          D3D12_MESSAGE_SEVERITY_INFO, text.data());
      fault.Stop();
      if (!fault.Hit()) {
        appended = hr == S_OK && unchanged(count + 1);
        break;
      }
      ++boundary_failures;
      ++failures;
      if (hr != E_OUTOFMEMORY || !unchanged(count)) {
        std::cerr << "InfoQueue growth rollback failed: count=" << count
                  << " allocation=" << allocation << "\n";
        return false;
      }
    }
    if (!appended)
      return false;
    if (boundary_failures) {
      ++boundaries;
      std::cout << "InfoQueue growth OOM count=" << count
                << " rollback sites=" << boundary_failures << " recovered\n";
    }
  }
  std::cout << "InfoQueue growth OOM boundaries=" << boundaries
            << " rollback sites=" << failures << " stored=" << count << "\n";
  return boundaries >= 3;
}

bool TestRootSerializationAllocationFailure() {
  D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
  D3D12_DESCRIPTOR_RANGE1 range1 = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,  2, 0, 0,
                                    D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0};
  D3D12_ROOT_PARAMETER parameters[3] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable = {1, &range};
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants = {0, 0, 4};
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[2].Descriptor = {1, 0};
  D3D12_ROOT_PARAMETER1 parameters1[3] = {};
  for (UINT i = 0; i < 3; ++i)
    parameters1[i].ParameterType = parameters[i].ParameterType;
  parameters1[0].DescriptorTable = {1, &range1};
  parameters1[1].Constants = parameters[1].Constants;
  parameters1[2].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE};
  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU = sampler.AddressV = sampler.AddressW =
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  sampler.MaxAnisotropy = 1;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
  for (UINT mode = 0; mode < 3; ++mode) {
    desc.Version = mode == 2 ? D3D_ROOT_SIGNATURE_VERSION_1_1
                             : D3D_ROOT_SIGNATURE_VERSION_1_0;
    if (mode == 2)
      desc.Desc_1_1 = {3, parameters1, 1, &sampler,
                       D3D12_ROOT_SIGNATURE_FLAG_NONE};
    else
      desc.Desc_1_0 = {3, parameters, 1, &sampler,
                       D3D12_ROOT_SIGNATURE_FLAG_NONE};
    auto serialize = [&](ID3DBlob **blob, ID3DBlob **errors) {
      return mode == 0
                 ? D3D12SerializeRootSignature(&desc.Desc_1_0, desc.Version,
                                               blob, errors)
                 : D3D12SerializeVersionedRootSignature(&desc, blob, errors);
    };
    Owned<ID3DBlob> reference;
    if (!CheckHR("serialization OOM reference",
                 serialize(&reference.ptr, nullptr)))
      return false;
    auto matches = [&](ID3DBlob *blob) {
      return blob && blob->GetBufferSize() == reference->GetBufferSize() &&
             !std::memcmp(blob->GetBufferPointer(),
                          reference->GetBufferPointer(), blob->GetBufferSize());
    };
    SubmissionAllocationFault fault;
    if (!fault.Install(true))
      return false;
    UINT failures = 0;
    bool completed = false;
    for (UINT allocation = 1; allocation <= 64; ++allocation) {
      // Existing serializer contract preserves the caller's output on failure.
      ID3DBlob *output = reference.ptr, *errors = reference.ptr;
      fault.Arm(allocation);
      HRESULT hr;
      try {
        hr = serialize(&output, &errors);
      } catch (const std::bad_alloc &) {
        fault.Stop();
        std::cerr << "root serialization leaked bad_alloc: mode=" << mode
                  << " allocation=" << allocation << "\n";
        return false;
      }
      fault.Stop();
      if (!fault.Hit()) {
        completed = hr == S_OK && !errors && matches(output);
        if (output != reference.ptr && output)
          output->Release();
        break;
      }
      ++failures;
      if (hr != E_OUTOFMEMORY || output != reference.ptr || errors)
        return false;
      Owned<ID3DBlob> recovered;
      if (!CheckHR("serialization OOM recovery",
                   serialize(&recovered.ptr, nullptr)) ||
          !matches(recovered.ptr))
        return false;
    }
    std::cout << "root serialization OOM mode=" << mode << " sites=" << failures
              << " recovery=" << (completed ? "PASS" : "FAIL") << "\n";
    if (!completed || failures < 7)
      return false;
  }
  return true;
}

bool TestInfoQueueAllocationFailure() {
  for (unsigned mode = 0; mode < 4; mode++) {
    unsigned failures = 0;
    for (unsigned allocation = 1; allocation <= 12; allocation++) {
      Owned<ID3D12Device> device;
      Owned<ID3D12InfoQueue> queue;
      if (!CheckHR("OOM InfoQueue device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
              IID_PPV_ARGS(&device.ptr))) ||
          !CheckHR("OOM InfoQueue", device->QueryInterface(IID_PPV_ARGS(&queue.ptr)))) return false;
      queue->SetMuteDebugOutput(TRUE);
      queue->SetMessageCountLimit(1);
      queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO, "original");
      D3D12_MESSAGE_CATEGORY category = D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED;
      D3D12_INFO_QUEUE_FILTER base = {};
      base.AllowList.NumCategories = 1;
      base.AllowList.pCategoryList = &category;
      if (!CheckHR("OOM initial storage filter", queue->AddStorageFilterEntries(&base)) ||
          !CheckHR("OOM initial retrieval filter", queue->AddRetrievalFilterEntries(&base))) return false;
      D3D12_MESSAGE_SEVERITY severity = D3D12_MESSAGE_SEVERITY_WARNING;
      D3D12_MESSAGE_ID id = D3D12_MESSAGE_ID_UNKNOWN;
      auto update = base;
      update.AllowList.NumSeverities = 1;
      update.AllowList.pSeverityList = &severity;
      update.DenyList.NumIDs = 1;
      update.DenyList.pIDList = &id;
      std::array<char, 1024> text;
      text.fill('x');
      text.back() = 0;
      SubmissionAllocationFault fault;
      if (!fault.Install()) return false;
      fault.Arm(allocation);
      HRESULT hr = mode == 0 ? queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO, text.data()) :
                   mode == 1 ? queue->AddStorageFilterEntries(&update) :
                   mode == 2 ? queue->PushCopyOfStorageFilter() : queue->AddRetrievalFilterEntries(&update);
      fault.Stop();
      if (!fault.Hit()) {
        if (FAILED(hr) || !failures) return false;
        break;
      }
      failures++;
      if (hr != E_OUTOFMEMORY || queue->GetNumStoredMessages() != 1 ||
          queue->GetNumMessagesAllowedByStorageFilter() != 1 ||
          queue->GetNumMessagesDiscardedByMessageCountLimit() != 0 ||
          queue->GetStorageFilterStackSize() != 1 || queue->GetRetrievalFilterStackSize() != 1) return false;
      alignas(D3D12_INFO_QUEUE_FILTER) std::array<unsigned char, 512> bytes = {};
      auto *filter = reinterpret_cast<D3D12_INFO_QUEUE_FILTER *>(bytes.data());
      SIZE_T size = bytes.size();
      hr = mode == 3 ? queue->GetRetrievalFilter(filter, &size) : queue->GetStorageFilter(filter, &size);
      if (FAILED(hr) || filter->AllowList.NumCategories != 1 || filter->AllowList.NumSeverities ||
          filter->DenyList.NumIDs || filter->AllowList.pCategoryList[0] != category) return false;
    }
    std::cout << "InfoQueue OOM mode=" << mode << " rollback sites=" << failures << "\n";
  }
  return TestInfoQueueGrowthAllocationFailure();
}

bool TestMultipleFenceWaits() {
  Owned<ID3D12Device1> device;
  Owned<ID3D12Fence> first, second;
  if (!CheckHR("Multiple wait device", D3D12CreateDevice(
          nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr))) ||
      !CheckHR("Multiple wait first", device->CreateFence(
          1, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&first.ptr))) ||
      !CheckHR("Multiple wait second", device->CreateFence(
          1, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&second.ptr))))
    return false;
  ID3D12Fence *fences[] = {first.ptr, second.ptr};
  UINT64 values[] = {1, 1};
  for (auto flags : {D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY})
    if (!CheckHR("Completed multiple wait with null event",
                 device->SetEventOnMultipleFenceCompletion(fences, values, 2, flags, nullptr)))
      return false;
  for (auto flags : {D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY}) {
    first->Signal(0);
    second->Signal(0);
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!entered || !done) {
      if (entered) CloseHandle(entered);
      if (done) CloseHandle(done);
      return false;
    }
    HRESULT result = E_FAIL;
    std::thread waiter([&] {
      SetEvent(entered);
      result = device->SetEventOnMultipleFenceCompletion(fences, values, 2, flags, nullptr);
      SetEvent(done);
    });
    bool ok = WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0 &&
              WaitForSingleObject(done, 20) == WAIT_TIMEOUT;
    first->Signal(1);
    if (flags == D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE)
      ok &= WaitForSingleObject(done, 20) == WAIT_TIMEOUT;
    else
      ok &= WaitForSingleObject(done, 5000) == WAIT_OBJECT_0;
    second->Signal(1);
    ok &= WaitForSingleObject(done, 5000) == WAIT_OBJECT_0;
    waiter.join();
    CloseHandle(entered);
    CloseHandle(done);
    if (!ok || !CheckHR("Pending null multiple wait", result))
      return false;
  }
  struct EventOwner {
    HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~EventOwner() { if (handle) CloseHandle(handle); }
  } event;
  if (!event.handle)
    return false;
  // Duplicate fences have independent thresholds, and ANY cancels its peers.
  ID3D12Fence *duplicates[] = {first.ptr, first.ptr};
  UINT64 thresholds[] = {2, 3};
  if (!CheckHR("Duplicate ALL", device->SetEventOnMultipleFenceCompletion(
          duplicates, thresholds, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE, event.handle)))
    return false;
  first->Signal(2);
  if (WaitForSingleObject(event.handle, 0) != WAIT_TIMEOUT)
    return false;
  first->Signal(0);
  first->Signal(3);
  if (WaitForSingleObject(event.handle, 5000) != WAIT_OBJECT_0)
    return false;
  ResetEvent(event.handle);
  // More registrations than the Win32 WaitForMultipleObjects limit.
  std::array<ID3D12Fence *, 129> many;
  std::array<UINT64, 129> targets;
  many.fill(first.ptr);
  targets.fill(4);
  if (!CheckHR("Wide fence group", device->SetEventOnMultipleFenceCompletion(
          many.data(), targets.data(), many.size(), D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE, event.handle)))
    return false;
  first->Signal(4);
  if (WaitForSingleObject(event.handle, 5000) != WAIT_OBJECT_0)
    return false;
  ResetEvent(event.handle);
  first->Signal(0);
  second->Signal(0);
  UINT64 pending_values[] = {100, 100};
  // An OOM after one fence has registered must cancel that partial group.
  SubmissionAllocationFault fault;
  if (!fault.Install())
    return false;
  unsigned failures = 0;
  for (UINT allocation = 1; allocation <= 16; allocation++) {
    fault.Arm(allocation);
    HRESULT hr = device->SetEventOnMultipleFenceCompletion(
        fences, pending_values, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE, event.handle);
    fault.Stop();
    if (!fault.Hit()) {
      first->Signal(100);
      second->Signal(100);
      if (FAILED(hr) || WaitForSingleObject(event.handle, 5000) != WAIT_OBJECT_0)
        return false;
      ResetEvent(event.handle);
      break;
    }
    failures++;
    if (hr != E_OUTOFMEMORY)
      return false;
    first->Signal(100);
    second->Signal(100);
    if (WaitForSingleObject(event.handle, 0) != WAIT_TIMEOUT)
      return false;
    first->Signal(0);
    second->Signal(0);
  }
  if (failures < 5)
    return false;
  // An unsatisfied group must not retain either COM child or its device.
  Owned<ID3D12Fence> abandoned;
  if (!CheckHR("Abandoned fence", device->CreateFence(
          0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&abandoned.ptr))))
    return false;
  ID3D12Fence *abandoned_fences[] = {abandoned.ptr, first.ptr};
  UINT64 never[] = {UINT64_MAX, UINT64_MAX};
  FenceHandleProbe handles;
  if (!handles.Install()) return false;
  const DWORD threads_before = ProcessThreadCount();
  for (unsigned i = 0; i < 96; i++)
    if (!CheckHR("Abandoned group", device->SetEventOnMultipleFenceCompletion(
            abandoned_fences, never, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, event.handle)))
      return false;
  const DWORD threads_pending = ProcessThreadCount();
  std::cout << "Fence pending groups=96 threads=" << threads_before << " -> " << threads_pending << "\n";
  if (!threads_before || threads_pending > threads_before + 4) return false;
  const ULONG references = abandoned.ptr->Release();
  abandoned.ptr = nullptr;
  if (references || WaitForSingleObject(event.handle, 0) != WAIT_TIMEOUT)
    return false;
  first->Signal(UINT64_MAX);
  if (WaitForSingleObject(event.handle, 0) != WAIT_TIMEOUT)
    return false;
  if (!handles.Balanced()) return false;
  std::cout << "Multiple fence synchronous/duplicate/wide/OOM/abandonment passed; OOM sites="
            << failures << "\n";
  return true;
}

bool TestConcurrentMultipleFenceWaits() {
  Owned<ID3D12Device1> device;
  std::array<Owned<ID3D12Fence>, 5> owned;
  std::array<ID3D12Fence *, 5> fences;
  if (!CheckHR("Concurrent wait device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device.ptr))))
    return false;
  for (unsigned i = 0; i < fences.size(); ++i) {
    if (!CheckHR("Concurrent wait fence",
                 device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&owned[i].ptr))))
      return false;
    fences[i] = owned[i].ptr;
  }
  struct EventOwner {
    HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~EventOwner() {
      if (handle)
        CloseHandle(handle);
    }
  } start;
  if (!start.handle)
    return false;
  FenceHandleProbe probe;
  if (!probe.Install())
    return false;
  constexpr unsigned groups = 8, rounds = 16;
  for (auto flags : {D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE,
                     D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY}) {
    for (bool racing_registration : {false, true}) {
      for (unsigned round = 0; round < rounds; ++round) {
        std::array<EventOwner, groups> events;
        for (const auto &event : events)
          if (!event.handle)
            return false;
        for (auto *fence : fences)
          if (!CheckHR("Rewind concurrent fence", fence->Signal(0)))
            return false;
        ResetEvent(start.handle);
        const unsigned before = probe.Signals();
        const UINT64 values[] = {1, 1, 1, 1, 100};
        const UINT count = flags == D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY ? 5 : 4;
        std::atomic<bool> good{true};
        auto register_groups = [&] {
          for (const auto &event : events)
            if (FAILED(device->SetEventOnMultipleFenceCompletion(
                    fences.data(), values, count, flags, event.handle)))
              good = false;
        };
        if (!racing_registration) {
          register_groups();
          for (const auto &event : events)
            if (WaitForSingleObject(event.handle, 0) != WAIT_TIMEOUT)
              good = false;
        }
        // Release completion workers and the optional registrar together.
        // This exercises notifications both before and after group Arm.
        std::array<std::thread, 4> workers;
        for (unsigned i = 0; i < workers.size(); ++i)
          workers[i] = std::thread([&, i] {
            if (WaitForSingleObject(start.handle, 5000) != WAIT_OBJECT_0 ||
                FAILED(fences[i]->Signal(1)))
              good = false;
          });
        std::thread registrar;
        if (racing_registration)
          registrar = std::thread([&] {
            if (WaitForSingleObject(start.handle, 5000) != WAIT_OBJECT_0)
              good = false;
            else
              register_groups();
          });
        SetEvent(start.handle);
        for (auto &worker : workers)
          worker.join();
        if (registrar.joinable())
          registrar.join();
        for (const auto &event : events) {
          if (WaitForSingleObject(event.handle, 5000) != WAIT_OBJECT_0)
            good = false;
          ResetEvent(event.handle);
        }
        // ANY must cancel the fifth fence. Rewinding and signaling every
        // source again must not notify completed groups a second time.
        for (auto *fence : fences)
          if (FAILED(fence->Signal(0)) || FAILED(fence->Signal(100)))
            good = false;
        for (const auto &event : events)
          if (WaitForSingleObject(event.handle, 0) != WAIT_TIMEOUT)
            good = false;
        if (!good || probe.Signals() - before != groups) {
          std::cerr << "Concurrent multiple wait failed: flags=" << flags
                    << " racing=" << racing_registration << " round=" << round
                    << " notifications=" << probe.Signals() - before << "\n";
          return false;
        }
      }
      std::cout << "Concurrent multiple wait flags=" << flags
                << " racing=" << racing_registration
                << " groups=" << groups * rounds << " passed\n";
    }
  }
  return probe.Signals() == 4 * groups * rounds && probe.Balanced();
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

bool TestInFlightRecording(unsigned mode) {
  struct Fixture {
    Owned<ID3D12Device> device;
    Owned<ID3D12CommandQueue> first_queue, second_queue;
    Owned<ID3D12CommandAllocator> first_allocator, second_allocator;
    Owned<ID3D12GraphicsCommandList> list, other;
    Owned<ID3D12Fence> gate, done;
    Owned<ID3D12Resource> source, readback;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    void ReleaseQueues() {
      if (first_queue.ptr) { first_queue.ptr->Release(); first_queue.ptr = nullptr; }
      if (second_queue.ptr) { second_queue.ptr->Release(); second_queue.ptr = nullptr; }
    }
    ~Fixture() {
      if (gate.ptr) gate->Signal(UINT64_MAX);
      ReleaseQueues();
      if (event) CloseHandle(event);
    }
  } f;
  if (!f.event || !CheckHR("In-flight device", D3D12CreateDevice(
          nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&f.device.ptr))))
    return false;
  D3D12_COMMAND_QUEUE_DESC qdesc = {};
  if (!CheckHR("In-flight queue A", f.device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&f.first_queue.ptr))) ||
      !CheckHR("In-flight queue B", f.device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&f.second_queue.ptr))) ||
      !CheckHR("In-flight allocator A", f.device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.first_allocator.ptr))) ||
      !CheckHR("In-flight allocator B", f.device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.second_allocator.ptr))) ||
      !CheckHR("In-flight gate", f.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f.gate.ptr))) ||
      !CheckHR("In-flight done", f.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f.done.ptr))))
    return false;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 256; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
  desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_UPLOAD; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  if (!CheckHR("In-flight source", f.device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&f.source.ptr))))
    return false;
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  if (!CheckHR("In-flight readback", f.device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&f.readback.ptr))))
    return false;
  uint32_t *mapped = nullptr;
  if (!CheckHR("In-flight map source", f.source->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    return false;
  mapped[0] = 1234; mapped[1] = 5678;
  f.source->Unmap(0, nullptr);
  if (!CheckHR("In-flight clear result", f.readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    return false;
  mapped[0] = mapped[1] = 0;
  f.readback->Unmap(0, nullptr);
  if (!CheckHR("In-flight list", f.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
          f.first_allocator.ptr, nullptr, IID_PPV_ARGS(&f.list.ptr))))
    return false;
  // Concurrent invalid admission uses no GPU resource accesses on either queue.
  if (mode != 5)
    f.list->CopyBufferRegion(f.readback.ptr, 0, f.source.ptr, 0, sizeof(uint32_t));
  if (!CheckHR("In-flight close", f.list->Close()) ||
      !CheckHR("In-flight other list", f.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
          f.first_allocator.ptr, nullptr, IID_PPV_ARGS(&f.other.ptr))))
    return false;
  f.other->CopyBufferRegion(f.readback.ptr, sizeof(uint32_t), f.source.ptr, sizeof(uint32_t), sizeof(uint32_t));
  if (!CheckHR("In-flight close other", f.other->Close()) ||
      !CheckHR("In-flight GPU gate", f.first_queue->Wait(f.gate.ptr, 1)))
    return false;
  ID3D12CommandList *list = f.list.ptr;
  if (mode == 5) {
    if (!CheckHR("Concurrent GPU gate", f.second_queue->Wait(f.gate.ptr, 1)))
      return false;
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    const auto submit = [&](ID3D12CommandQueue *queue) {
      ready.fetch_add(1);
      while (!start.load()) std::this_thread::yield();
      queue->ExecuteCommandLists(1, &list);
    };
    std::thread first(submit, f.first_queue.ptr), second(submit, f.second_queue.ptr);
    while (ready.load() != 2) std::this_thread::yield();
    start.store(true);
    first.join(); second.join();
  } else {
    f.first_queue->ExecuteCommandLists(1, &list);
    if (mode == 0) {
      f.first_queue->ExecuteCommandLists(1, &list);
    } else if (mode == 1) {
      ID3D12CommandList *batch[] = {f.other.ptr, list};
      f.first_queue->ExecuteCommandLists(2, batch);
    } else if (mode == 2 || mode == 3) {
      if (!CheckHR("In-flight ordered signal", f.first_queue->Signal(f.done.ptr, 1)) ||
          !CheckHR("In-flight cross-queue dependency", f.second_queue->Wait(f.done.ptr, 1)))
        return false;
      if (mode == 3) {
        if (!CheckHR("Reset list while old recording pending", f.list->Reset(f.second_allocator.ptr, nullptr)))
          return false;
        f.list->CopyBufferRegion(f.readback.ptr, sizeof(uint32_t), f.source.ptr, sizeof(uint32_t), sizeof(uint32_t));
        if (!CheckHR("Close new recording", f.list->Close())) return false;
      }
      f.second_queue->ExecuteCommandLists(1, &list);
    } else if (mode == 6) {
      ID3D12CommandList *other = f.other.ptr;
      f.first_queue->ExecuteCommandLists(1, &other);
    }
  }
  if (mode <= 2 || mode == 5) {
    const bool removed = FAILED(f.device->GetDeviceRemovedReason()) && f.gate->GetCompletedValue() == UINT64_MAX;
    f.gate->Signal(UINT64_MAX); // Recovery for the pre-fix runtime.
    f.ReleaseQueues();
    if (!CheckHR("In-flight rejected batch result", f.readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
      return false;
    const bool dropped_other = mapped[1] == 0;
    f.readback->Unmap(0, nullptr);
    if (!removed || !dropped_other) {
      std::cerr << "in-flight replay mode " << mode << " was accepted or mixed batch executed\n";
      return false;
    }
    std::cout << "in-flight replay mode " << mode << " removed device; mixed output remains zero\n";
    return true;
  }
  auto completion_queue = mode == 3 ? f.second_queue.ptr : f.first_queue.ptr;
  if (!CheckHR("In-flight finish signal", completion_queue->Signal(f.done.ptr, 2)) ||
      !CheckHR("In-flight finish event", f.done->SetEventOnCompletion(2, f.event)) ||
      !CheckHR("Release in-flight gate", f.gate->Signal(1)) || WaitForSingleObject(f.event, 5000) != WAIT_OBJECT_0)
    return false;
  if (mode == 4) {
    // Reuse the immutable recording on another queue after completion, with new
    // data.
    if (!CheckHR("Completed replay source",
                 f.source->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
      return false;
    mapped[0] = 4321;
    f.source->Unmap(0, nullptr);
    if (!CheckHR(
            "Completed replay clear",
            f.readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
      return false;
    mapped[0] = 0;
    f.readback->Unmap(0, nullptr);
    f.second_queue->ExecuteCommandLists(1, &list);
    if (!CheckHR("Completed replay signal",
                 f.second_queue->Signal(f.done.ptr, 3)) ||
        !CheckHR("Completed replay event",
                 f.done->SetEventOnCompletion(3, f.event)) ||
        WaitForSingleObject(f.event, 5000) != WAIT_OBJECT_0)
      return false;
  }
  if (!CheckHR("Healthy in-flight device", f.device->GetDeviceRemovedReason()) ||
      !CheckHR("In-flight positive readback", f.readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    return false;
  const bool correct = mapped[0] == (mode == 4 ? 4321u : 1234u) &&
                       mapped[1] == ((mode == 3 || mode == 6) ? 5678u : 0u);
  f.readback->Unmap(0, nullptr);
  if (!correct) { std::cerr << "valid recording mode " << mode << " readback failed\n"; return false; }
  std::cout << "valid recording mode " << mode << " preserved independent recording/readback\n";
  return true;
}

bool TestTransitionChain(unsigned mode) {
  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocators[2];
  Owned<ID3D12GraphicsCommandList> lists[2];
  Owned<ID3D12Resource> resource, source, readback;
  Owned<ID3D12Fence> done;
  struct Event { HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Event() { if (value) CloseHandle(value); } } event;
  if (!event.value || !CheckHR("Transition device", D3D12CreateDevice(
          nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)))) return false;
  D3D12_COMMAND_QUEUE_DESC qdesc = {};
  if (!CheckHR("Transition queue", device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR("Transition done", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&done.ptr)))) return false;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc = {};
  const bool buffer = mode == 7 || mode == 8 || mode == 10 || mode == 17 || mode == 18;
  const bool depth = mode == 12 || mode == 13;
  desc.Dimension = buffer ? D3D12_RESOURCE_DIMENSION_BUFFER : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = buffer ? 256 : 16; desc.Height = buffer ? 1 : 16; desc.DepthOrArraySize = 1;
  desc.MipLevels = buffer ? 1 : 2; desc.SampleDesc.Count = 1;
  desc.Format = buffer ? DXGI_FORMAT_UNKNOWN : depth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.Layout = buffer ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = buffer ? D3D12_RESOURCE_FLAG_NONE : depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (mode == 21) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  if (!CheckHR("Transition resource", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
          depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource.ptr)))) return false;
  desc = {}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = 256; desc.Height = 1;
  desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  if (!CheckHR("Transition marker source", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&source.ptr)))) return false;
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  if (!CheckHR("Transition marker result", device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback.ptr)))) return false;
  uint32_t *mapped = nullptr;
  if (!CheckHR("Transition map marker", source->Map(0, nullptr, reinterpret_cast<void **>(&mapped)))) return false;
  *mapped = 1234; source->Unmap(0, nullptr);
  if (!CheckHR("Transition clear marker", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)))) return false;
  *mapped = 0; readback->Unmap(0, nullptr);
  for (unsigned i = 0; i < 2; i++)
    if (!CheckHR("Transition allocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i].ptr))) ||
        !CheckHR("Transition list", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i].ptr, nullptr, IID_PPV_ARGS(&lists[i].ptr)))) return false;
  D3D12_RESOURCE_BARRIER first = {};
  first.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  first.Transition.pResource = resource.ptr;
  first.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  first.Transition.StateBefore = depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_COPY_DEST;
  first.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  auto second = first;
  second.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  second.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  if (mode == 0) second.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  if (mode == 1) { second.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; second.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE; }
  if (mode == 3 || mode == 4) {
    first.Transition.Subresource = 0;
    second.Transition.Subresource = mode == 3 ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : 1;
    second.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    second.Transition.StateAfter = mode == 3 ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
  if (mode == 5 || mode == 6 || mode == 7 || mode == 11 || mode == 21 || depth) {
    first.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    second.Transition.StateBefore = mode == 5 || mode == 21 ? D3D12_RESOURCE_STATE_RENDER_TARGET :
        mode == 7 ? D3D12_RESOURCE_STATE_GENERIC_READ :
        mode == 11 || mode == 13 ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_COPY_SOURCE;
    second.Transition.StateAfter = depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE :
        mode == 11 ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COPY_DEST;
  }
  if (mode == 8 || mode == 9 || mode == 10) {
    second.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    second.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  }
  if (mode == 9) first.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  if (mode == 15) second.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  if (mode >= 16 && mode <= 20) {
    first.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    second.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  }
  lists[0]->ResourceBarrier(1, &first);
  if (mode >= 16 && mode <= 20) {
    Owned<ID3D12GraphicsCommandList7> enhanced[2];
    for (unsigned i = 0; i < 2; i++)
      if (!CheckHR("Transition enhanced interface", lists[i]->QueryInterface(IID_PPV_ARGS(&enhanced[i].ptr)))) return false;
    const auto emit = [&](unsigned index, D3D12_BARRIER_SYNC before, D3D12_BARRIER_SYNC after) {
      D3D12_BUFFER_BARRIER b = {};
      b.SyncBefore = before; b.SyncAfter = after;
      b.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE; b.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
      b.pResource = resource.ptr; b.Size = UINT64_MAX;
      D3D12_TEXTURE_BARRIER t = {};
      t.SyncBefore = before; t.SyncAfter = after; t.AccessBefore = b.AccessBefore; t.AccessAfter = b.AccessAfter;
      t.LayoutBefore = t.LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON;
      t.pResource = resource.ptr; t.Subresources.IndexOrFirstMipLevel = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      D3D12_BARRIER_GROUP group = {};
      group.Type = buffer ? D3D12_BARRIER_TYPE_BUFFER : D3D12_BARRIER_TYPE_TEXTURE; group.NumBarriers = 1;
      if (buffer) group.pBufferBarriers = &b; else group.pTextureBarriers = &t;
      enhanced[index]->Barrier(1, &group);
    };
    if (mode == 20) {
      D3D12_TEXTURE_BARRIER t = {};
      t.SyncBefore = t.SyncAfter = D3D12_BARRIER_SYNC_ALL;
      t.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE; t.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
      t.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON; t.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
      t.pResource = resource.ptr; t.Subresources.IndexOrFirstMipLevel = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      D3D12_BARRIER_GROUP group = {}; group.Type = D3D12_BARRIER_TYPE_TEXTURE;
      group.NumBarriers = 1; group.pTextureBarriers = &t;
      enhanced[0]->Barrier(1, &group);
      t.LayoutBefore = D3D12_BARRIER_LAYOUT_UNDEFINED; t.LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON;
      t.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS; t.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
      t.Flags = D3D12_TEXTURE_BARRIER_FLAG_DISCARD;
      enhanced[1]->Barrier(1, &group);
    } else if (mode >= 18) {
      emit(0, D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_SPLIT);
      emit(1, D3D12_BARRIER_SYNC_SPLIT, D3D12_BARRIER_SYNC_ALL);
    } else {
      emit(1, D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_ALL);
    }
  }
  lists[mode == 0 || mode == 15 ? 0 : 1]->ResourceBarrier(1, &second);
  lists[0]->CopyBufferRegion(readback.ptr, 0, source.ptr, 0, sizeof(uint32_t));
  const HRESULT close_a = lists[0]->Close();
  if (!CheckHR("Close transition B", lists[1]->Close())) return false;
  if (mode == 0 || mode == 15) {
    if (close_a != E_INVALIDARG || lists[0]->Reset(allocators[0].ptr, nullptr) != E_INVALIDARG) return false;
    ID3D12CommandList *failed = lists[0].ptr;
    queue->ExecuteCommandLists(1, &failed);
    if (FAILED(device->GetDeviceRemovedReason()) ||
        !CheckHR("Failed recording marker", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)))) return false;
    const bool untouched = *mapped == 0;
    readback->Unmap(0, nullptr);
    std::cout << "transition chain mode " << mode << " rejected by Close and Reset; failed recording dropped\n";
    return untouched;
  }
  if (!CheckHR("Close transition A", close_a)) return false;
  UINT64 value = 0;
  const auto drain = [&] {
    return CheckHR("Transition signal", queue->Signal(done.ptr, ++value)) &&
        CheckHR("Transition event", done->SetEventOnCompletion(value, event.value)) &&
        WaitForSingleObject(event.value, 5000) == WAIT_OBJECT_0;
  };
  ID3D12CommandList *batch[] = {lists[0].ptr, lists[1].ptr};
  if (mode == 9) { queue->ExecuteCommandLists(1, &batch[1]); if (!drain()) return false; }
  if (mode == 10) {
    queue->ExecuteCommandLists(1, batch); if (!drain()) return false;
    queue->ExecuteCommandLists(1, &batch[1]);
  } else {
    queue->ExecuteCommandLists(mode == 0 || mode == 15 ? 1 : 2, batch);
  }
  const bool invalid = mode == 0 || mode == 1 || mode == 3 || mode == 5 || mode == 8 || mode == 9 || mode == 12 || mode == 15;
  bool correct = invalid ? FAILED(device->GetDeviceRemovedReason()) : SUCCEEDED(device->GetDeviceRemovedReason()) && drain();
  if (mode == 14 && correct) { queue->ExecuteCommandLists(2, batch); correct = drain(); }
  queue.ptr->Release(); queue.ptr = nullptr; // Wait for accepted native work before observing the marker.
  if (!CheckHR("Transition marker readback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)))) return false;
  correct &= *mapped == (invalid ? 0u : 1234u);
  readback->Unmap(0, nullptr);
  if (!correct) { std::cerr << "transition chain mode " << mode << " failed removal/readback oracle\n"; return false; }
  std::cout << "transition chain mode " << mode << " passed; marker=" << (invalid ? 0 : 1234) << "\n";
  return true;
}

// Invalid query lifecycles are record-only: resolving an incomplete query on
// the GPU is undefined behavior and is never used as a negative test oracle.
bool TestQueryRecording(ID3D12Device *device, unsigned mode) {
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Owned<ID3D12QueryHeap> heap, other;
  Owned<ID3D12Resource> readback;
  if (!CheckHR("Query allocator", device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr))) ||
      !CheckHR("Query list", device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, nullptr, IID_PPV_ARGS(&list.ptr)))) return false;
  D3D12_QUERY_HEAP_DESC hd = {};
  hd.Type = mode == 18 ? D3D12_QUERY_HEAP_TYPE_TIMESTAMP : D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  hd.Count = 2;
  const HRESULT heap_hr = device->CreateQueryHeap(&hd, IID_PPV_ARGS(&heap.ptr));
  if (mode == 18 && heap_hr == E_NOTIMPL) {
    std::cout << "timestamp query UNSUPPORTED on this device (not PASS)\n";
    return true;
  }
  if (!CheckHR("Query heap", heap_hr) ||
      !CheckHR("Other query heap", device->CreateQueryHeap(&hd, IID_PPV_ARGS(&other.ptr)))) return false;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 16; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
  desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES hp = {};
  hp.Type = D3D12_HEAP_TYPE_READBACK;
  if (!CheckHR("Query readback", device->CreateCommittedResource(
          &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&readback.ptr)))) return false;
  const auto count = D3D12_QUERY_TYPE_OCCLUSION;
  const auto binary = D3D12_QUERY_TYPE_BINARY_OCCLUSION;
  if (mode != 2 && mode != 9 && mode != 18) list->BeginQuery(heap.ptr, count, 0);
  switch (mode) {
  case 0: list->BeginQuery(heap.ptr, count, 0); break;
  case 1: break; // Missing End.
  case 2: list->EndQuery(heap.ptr, count, 0); break;
  case 3: list->EndQuery(heap.ptr, binary, 0); break;
  case 4: list->EndQuery(heap.ptr, count, 1); break;
  case 5: list->EndQuery(other.ptr, count, 0); break;
  case 6: list->ResolveQueryData(heap.ptr, count, 0, 1, readback.ptr, 0); break;
  case 7: list->BeginQuery(heap.ptr, binary, 0); break;
  case 8: list->BeginQuery(heap.ptr, count, 2); break;
  case 9: list->BeginQuery(heap.ptr, D3D12_QUERY_TYPE_TIMESTAMP, 0); break;
  case 10:
    list->EndQuery(heap.ptr, count, 0);
    list->EndQuery(heap.ptr, count, 0);
    break;
  case 11: // Empty queries, including overlapping queries and reuse.
    list->BeginQuery(other.ptr, binary, 0);
    list->BeginQuery(heap.ptr, binary, 1);
    list->EndQuery(heap.ptr, count, 0);
    list->EndQuery(other.ptr, binary, 0);
    list->EndQuery(heap.ptr, binary, 1);
    list->BeginQuery(heap.ptr, count, 0);
    list->EndQuery(heap.ptr, count, 0);
    list->ResolveQueryData(heap.ptr, count, 0, 1, readback.ptr, 0);
    list->ResolveQueryData(heap.ptr, binary, 1, 1, readback.ptr, 8);
    break;
  case 12: // Resolving an ended range while another slot is active is legal.
    list->BeginQuery(heap.ptr, count, 1);
    list->EndQuery(heap.ptr, count, 1);
    list->ResolveQueryData(heap.ptr, count, 1, 1, readback.ptr, 0);
    list->EndQuery(heap.ptr, count, 0);
    list->ResolveQueryData(heap.ptr, count, 0, 1, readback.ptr, 8);
    break;
  case 13: {
    Owned<ID3D12CommandAllocator> second_allocator;
    Owned<ID3D12GraphicsCommandList> second;
    if (!CheckHR("Cross-list query allocator", device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&second_allocator.ptr))) ||
        !CheckHR("Cross-list query list", device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, second_allocator.ptr, nullptr, IID_PPV_ARGS(&second.ptr)))) return false;
    second->EndQuery(heap.ptr, count, 0);
    if (SUCCEEDED(second->Close())) { std::cerr << "cross-list query End accepted\n"; return false; }
    break;
  }
  case 14: case 15: case 16: case 17:
    list->EndQuery(heap.ptr, count, 0);
    list->ResolveQueryData(heap.ptr, mode == 17 ? D3D12_QUERY_TYPE_TIMESTAMP : count,
                          mode == 14 ? 2 : 0, mode == 15 ? UINT_MAX : 1,
                          readback.ptr, mode == 16 ? 1 : 0);
    break;
  case 18:
    list->EndQuery(heap.ptr, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    list->EndQuery(heap.ptr, D3D12_QUERY_TYPE_TIMESTAMP, 1);
    list->ResolveQueryData(heap.ptr, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.ptr, 0);
    break;
  }
  HRESULT close = list->Close();
  if (mode < 11 || (mode >= 13 && mode <= 17)) {
    if (SUCCEEDED(close) || list->Reset(allocator.ptr, nullptr) != close) {
      std::cerr << "invalid query mode " << mode << " accepted by Close/Reset\n";
      return false;
    }
    std::cout << "query lifecycle mode " << mode << " rejected before submission\n";
    return true;
  }
  if (!CheckHR("Empty query Close", close)) return false;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12Fence> fence;
  D3D12_COMMAND_QUEUE_DESC qd = {};
  if (!CheckHR("Empty query queue", device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR("Empty query fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.ptr)))) return false;
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) return false;
  bool ok = true;
  for (UINT64 replay = 1; replay <= 2 && ok; ++replay) {
    UINT64 *mapped = nullptr;
    ok = CheckHR("Initialize query readback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
    if (!ok) break;
    mapped[0] = mapped[1] = UINT64_MAX;
    readback->Unmap(0, nullptr);
    ID3D12CommandList *batch[] = {list.ptr};
    queue->ExecuteCommandLists(1, batch);
    ok = CheckHR("Empty query signal", queue->Signal(fence.ptr, replay)) &&
        CheckHR("Empty query event", fence->SetEventOnCompletion(replay, event)) &&
        WaitForSingleObject(event, 5000) == WAIT_OBJECT_0 &&
        CheckHR("Empty query device", device->GetDeviceRemovedReason()) &&
        CheckHR("Empty query map", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
    if (!ok) break;
    ok = mode == 18 ? mapped[0] != 0 && mapped[1] >= mapped[0] : mapped[0] == 0 && mapped[1] == 0;
    readback->Unmap(0, nullptr);
  }
  CloseHandle(event);
  if (!ok) { std::cerr << "empty query/replay readback failed mode " << mode << "\n"; return false; }
  std::cout << "query lifecycle mode " << mode << (mode == 18 ? " timestamp ordered readback/replay passed\n" : " exact zero readback/replay passed\n");
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

// Count only native allocations made while recording the copy fixture. The
// import patch is scoped to this test process and is absent from the driver.
class CopyBufferCounter {
  DriverImportPatch patch_;
  inline static void *original_ = nullptr;
  static obj_handle_t Create(obj_handle_t device, WMTBufferInfo *info) {
    ++count;
    bytes += info->length;
    if (fail)
      return 0;
    return reinterpret_cast<decltype(&Create)>(original_)(device, info);
  }

public:
  inline static size_t count = 0;
  inline static uint64_t bytes = 0;
  inline static bool fail = false;
  bool Install() {
    count = 0;
    bytes = 0;
    fail = false;
    return patch_.Install("MTLDevice_newBuffer",
                          reinterpret_cast<void *>(&Create), original_);
  }
};

bool TestCopyStaging(const char *mode) {
  UINT width = 1, height = 1, mips = 1, slices = 1, repeats = 1;
  if (!std::strcmp(mode, "large")) {
    width = 4096;
    height = 2049;
  } else if (!std::strcmp(mode, "repeat") || !std::strcmp(mode, "split") ||
             !std::strcmp(mode, "split-allocators")) {
    width = height = 1024;
    repeats = 10;
  } else if (!std::strcmp(mode, "small-repeat")) {
    width = height = 64;
    repeats = 2100;
  } else if (!std::strcmp(mode, "growth")) {
    width = height = 1024;
    mips = 3;
  } else if (!std::strcmp(mode, "allocation-failure") ||
             !std::strcmp(mode, "submission-failure")) {
    width = height = 1024;
  } else if (!std::strcmp(mode, "multi")) {
    width = height = 64;
    mips = 3;
    slices = 2;
  } else if (std::strcmp(mode, "tiny") &&
             std::strcmp(mode, "allocation-failure") &&
             std::strcmp(mode, "invalid-box") &&
             std::strcmp(mode, "invalid-destination"))
    return false;
  const bool fail = !std::strcmp(mode, "allocation-failure");
  const bool invalid_box = !std::strcmp(mode, "invalid-box");
  const bool invalid_dst = !std::strcmp(mode, "invalid-destination");
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    return false;
  struct CloseEvent {
    HANDLE value;
    ~CloseEvent() { CloseHandle(value); }
  } close_event{event};
  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Owned<ID3D12Resource> source, target, readback;
  Owned<ID3D12DescriptorHeap> views;
  Owned<ID3D12Fence> gate, done;
  if (!CheckHR("CreateDevice",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device.ptr))))
    return false;
  D3D12_COMMAND_QUEUE_DESC q = {};
  if (!CheckHR("CreateQueue",
               device.ptr->CreateCommandQueue(&q, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR("CreateAllocator", device.ptr->CreateCommandAllocator(
                                      q.Type, IID_PPV_ARGS(&allocator.ptr))) ||
      !CheckHR("CreateList",
               device.ptr->CreateCommandList(0, q.Type, allocator.ptr, nullptr,
                                             IID_PPV_ARGS(&list.ptr))))
    return false;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = slices;
  desc.MipLevels = mips;
  desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  for (auto out : {&source.ptr, &target.ptr})
    if (!CheckHR("CreateTexture", device.ptr->CreateCommittedResource(
                                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                      D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr,
                                      IID_PPV_ARGS(out))))
      return false;
  const UINT subresources = 2 * mips * slices;
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(subresources);
  UINT64 total = 0;
  device.ptr->GetCopyableFootprints(&desc, 0, subresources, 0, fp.data(),
                                    nullptr, nullptr, &total);
  D3D12_RESOURCE_DESC buffer = {};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = ((total + 511) & ~UINT64(511)) * 3;
  buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  if (!CheckHR("CreateReadback", device.ptr->CreateCommittedResource(
                                     &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                     IID_PPV_ARGS(&readback.ptr))))
    return false;
  D3D12_DESCRIPTOR_HEAP_DESC vh = {};
  vh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  vh.NumDescriptors = mips * slices * 2;
  if (!CheckHR("CreateViews",
               device.ptr->CreateDescriptorHeap(&vh, IID_PPV_ARGS(&views.ptr))))
    return false;
  auto base = views.ptr->GetCPUDescriptorHandleForHeapStart();
  const UINT stride = device.ptr->GetDescriptorHandleIncrementSize(vh.Type);
  auto handle = [&](UINT tex, UINT mip, UINT slice) {
    auto h = base;
    h.ptr += (tex * mips * slices + slice * mips + mip) * stride;
    return h;
  };
  for (UINT tex = 0; tex < 2; ++tex)
    for (UINT slice = 0; slice < slices; ++slice)
      for (UINT mip = 0; mip < mips; ++mip) {
        D3D12_DEPTH_STENCIL_VIEW_DESC v = {};
        v.Format = desc.Format;
        if (slices > 1) {
          v.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
          v.Texture2DArray.MipSlice = mip;
          v.Texture2DArray.FirstArraySlice = slice;
          v.Texture2DArray.ArraySize = 1;
        } else {
          v.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
          v.Texture2D.MipSlice = mip;
        }
        device.ptr->CreateDepthStencilView(tex ? target.ptr : source.ptr, &v,
                                           handle(tex, mip, slice));
      }
  if (!CheckHR("CreateGate",
               device.ptr->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&gate.ptr))) ||
      !CheckHR("CreateDone", device.ptr->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                     IID_PPV_ARGS(&done.ptr))))
    return false;

  auto barrier = [&](ID3D12Resource *r, D3D12_RESOURCE_STATES before,
                     D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list.ptr->ResourceBarrier(1, &b);
  };
  for (UINT round = 0; round < 2; ++round) {
    if (round &&
        (!CheckHR("ResetAllocator", allocator.ptr->Reset()) ||
         !CheckHR("ResetList", list.ptr->Reset(allocator.ptr, nullptr))))
      return false;
    for (UINT tex = 0; tex < 2; ++tex)
      for (UINT slice = 0; slice < slices; ++slice)
        for (UINT mip = 0; mip < mips; ++mip)
          list.ptr->ClearDepthStencilView(
              handle(tex, mip, slice),
              D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
              tex ? 0.75f : 0.25f + slice * 0.125f + mip * 0.03125f,
              tex ? 0xa7 : 0x5a + slice + mip, 0, nullptr);
    barrier(source.ptr, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(target.ptr, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_COPY_DEST);
    Owned<ID3D12CommandAllocator> first_allocator;
    Owned<ID3D12GraphicsCommandList> first;
    CopyBufferCounter counter;
    if (!counter.Install())
      return false;
    CopyBufferCounter::fail = fail;
    LARGE_INTEGER frequency, begin, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    // Each phase reads both planes, so a depth-only copy cannot accidentally
    // pass by also copying stencil; the second phase checks stencil
    // preservation.
    for (UINT phase = 0; phase < 3; ++phase) {
      if (phase == 2)
        list.ptr->CopyResource(target.ptr, source.ptr);
      else
        for (UINT iteration = 0; iteration < repeats; ++iteration)
          for (UINT slice = 0; slice < slices; ++slice)
            for (UINT mip = 0; mip < mips; ++mip) {
              D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
              src.pResource = source.ptr;
              dst.pResource = target.ptr;
              src.Type = dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
              const UINT copy_mip =
                  !std::strcmp(mode, "growth") ? mips - 1 - mip : mip;
              src.SubresourceIndex = dst.SubresourceIndex =
                  copy_mip + mips * (slice + slices * phase);
              D3D12_BOX box = {1, 0, 0, 0, 1, 1};
              list.ptr->CopyTextureRegion(&dst, invalid_dst ? width : 0, 0, 0,
                                          &src, invalid_box ? &box : nullptr);
            }
      barrier(target.ptr, D3D12_RESOURCE_STATE_COPY_DEST,
              D3D12_RESOURCE_STATE_COPY_SOURCE);
      for (UINT sub = 0; sub < subresources; ++sub) {
        D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
        src.pResource = target.ptr;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = sub;
        dst.pResource = readback.ptr;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = fp[sub];
        dst.PlacedFootprint.Offset += phase * ((total + 511) & ~UINT64(511));
        list.ptr->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
      }
      if (phase != 2)
        barrier(target.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST);
      if (phase == 0 && (!std::strcmp(mode, "split") ||
                         !std::strcmp(mode, "split-allocators"))) {
        if (!CheckHR("CloseFirst", list.ptr->Close()))
          return false;
        std::swap(first.ptr, list.ptr);
        if (!std::strcmp(mode, "split-allocators")) {
          std::swap(first_allocator.ptr, allocator.ptr);
          if (!CheckHR("CreateSecondAllocator",
                       device.ptr->CreateCommandAllocator(
                           q.Type, IID_PPV_ARGS(&allocator.ptr))))
            return false;
        }
        if (!CheckHR("CreateSecondList", device.ptr->CreateCommandList(
                                             0, q.Type, allocator.ptr, nullptr,
                                             IID_PPV_ARGS(&list.ptr))))
          return false;
      }
    }
    QueryPerformanceCounter(&end);
    barrier(source.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
    barrier(target.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
    HRESULT closed = list.ptr->Close();
    std::cout << "copy staging mode=" << mode << " round=" << round
              << " close=0x" << std::hex << closed << std::dec
              << " native_buffers=" << CopyBufferCounter::count
              << " allocated_bytes=" << CopyBufferCounter::bytes
              << " record_us="
              << (end.QuadPart - begin.QuadPart) * 1e6 / frequency.QuadPart
              << "\n";
    if (fail || invalid_box || invalid_dst)
      return FAILED(closed);
    if (!CheckHR("CloseCopy", closed))
      return false;
    size_t direct = 0, to_buffer = 0, owned = 0;
    for (auto *recorded : {first.ptr, list.ptr}) {
      if (!recorded)
        continue;
      auto *internal =
          static_cast<dxmt::MTLD3D12GraphicsCommandList *>(recorded);
      for (auto *encoder = internal->entry->next; encoder;
           encoder = encoder->next) {
        owned += encoder->resource_refs.size();
        if (encoder->type != dxmt::EncoderType::Blit)
          continue;
        auto *blit = static_cast<dxmt::BlitEncoderData *>(encoder);
        for (auto *cmd = static_cast<wmtcmd_base *>(blit->cmd_head.next.get());
             cmd; cmd = static_cast<wmtcmd_base *>(cmd->next.get())) {
          direct += cmd->type == WMTBlitCommandCopyFromTextureToTexture;
          to_buffer +=
              cmd->type == WMTBlitCommandCopyFromTextureToBufferWithBlitOption;
        }
      }
    }
    std::cout << "copy staging commands direct=" << direct
              << " texture_to_buffer=" << to_buffer
              << " retained_refs=" << owned << "\n";

    if (!std::strcmp(mode, "submission-failure")) {
      SubmissionCreationFault fault;
      if (!fault.Install("copy"))
        return false;
      fault.Arm();
      ID3D12CommandList *batch[] = {list.ptr};
      queue.ptr->ExecuteCommandLists(1, batch);
      fault.Stop();
      const auto busy =
          static_cast<dxmt::MTLD3D12CommandAllocator *>(allocator.ptr)
              ->IsInFlight();
      const bool good =
          fault.Hit() && !fault.Commits() && !busy &&
          device.ptr->GetDeviceRemovedReason() == DXGI_ERROR_DEVICE_REMOVED &&
          SUCCEEDED(allocator.ptr->Reset());
      std::cout << "copy staging submission rollback=" << good << "\n";
      return good;
    }
    // Gate the actual GPU work, then prove allocator Reset cannot free scratch.
    if (!CheckHR("QueueGate", queue.ptr->Wait(gate.ptr, round + 1)))
      return false;
    ID3D12CommandList *batch[] = {first.ptr ? first.ptr : list.ptr, list.ptr};
    queue.ptr->ExecuteCommandLists(first.ptr ? 2 : 1, batch);
    const HRESULT reset = allocator.ptr->Reset();
    const HRESULT first_reset =
        first_allocator.ptr ? first_allocator.ptr->Reset() : E_FAIL;
    // Always release the gate, including a failed assertion, before
    // destructing.
    const HRESULT release = gate.ptr->Signal(round + 1);
    if (SUCCEEDED(reset) || SUCCEEDED(first_reset) ||
        !CheckHR("ReleaseGate", release) ||
        !CheckHR("SignalDone", queue.ptr->Signal(done.ptr, round + 1)) ||
        !CheckHR("DoneEvent",
                 done.ptr->SetEventOnCompletion(round + 1, event)) ||
        WaitForSingleObject(event, 30000) != WAIT_OBJECT_0 ||
        done.ptr->GetCompletedValue() == UINT64_MAX)
      return false;
    const uint8_t *data = nullptr;
    if (!CheckHR("MapCopy",
                 readback.ptr->Map(
                     0, nullptr,
                     reinterpret_cast<void **>(const_cast<uint8_t **>(&data)))))
      return false;
    bool good = true;
    for (UINT phase = 0; phase < 3; ++phase)
      for (UINT sub = 0; sub < subresources; ++sub) {
        const bool stencil = sub >= mips * slices;
        const UINT mip = sub % mips, slice = (sub / mips) % slices;
        const auto &f = fp[sub];
        const auto *start =
            data + phase * ((total + 511) & ~UINT64(511)) + f.Offset;
        for (UINT y = 0; y < f.Footprint.Height; ++y)
          for (UINT x = 0; x < f.Footprint.Width; ++x) {
            const auto *pixel =
                start + y * f.Footprint.RowPitch + x * (stencil ? 1 : 4);
            float value = *pixel;
            if (!stencil)
              std::memcpy(&value, pixel, 4);
            const float expected =
                stencil ? (phase ? 0x5a + slice + mip : 0xa7)
                        : 0.25f + slice * 0.125f + mip * 0.03125f;
            if (good && value != expected)
              std::cerr << "copy mismatch phase=" << phase << " sub=" << sub
                        << " x=" << x << " y=" << y << " actual=" << value
                        << " expected=" << expected << "\n";
            good &= value == expected;
          }
      }
    readback.ptr->Unmap(0, nullptr);
    if (!good || FAILED(device.ptr->GetDeviceRemovedReason())) {
      std::cerr << "copy plane readback failed\n";
      return false;
    }
    std::cout << "copy staging GPU planes/preservation/in-flight passed\n";
  }
  return true;
}

class BlitEncoderCounter {
  DriverImportPatch patch_;
  inline static obj_handle_t (__cdecl *original_)(obj_handle_t) = nullptr;
  static obj_handle_t __cdecl Create(obj_handle_t buffer) {
    count++;
    return original_(buffer);
  }
public:
  inline static unsigned count = 0;
  bool Install() {
    count = 0;
    void *original = nullptr;
    if (!patch_.Install("MTLCommandBuffer_blitCommandEncoder", reinterpret_cast<void *>(&Create), original))
      return false;
    original_ = reinterpret_cast<decltype(original_)>(original);
    return true;
  }
};

// Opt-in private-ABI probe for the actual queue validation call. Derive the
// Itanium member-pointer slot from this build's header, rather than pinning a
// vtable index. The scoped patch always forwards the original implementation.
class SplitValidationTiming {
  using List = dxmt::MTLD3D12GraphicsCommandList;
  using Validation = dxmt::D3D12RecordedStateValidation;
  using Function = HRESULT (*)(const List *, Validation &);
  void **slot_ = nullptr;
  inline static Function original_ = nullptr;
  inline static thread_local bool armed_ = false;
  static HRESULT Validate(const List *list, Validation &state) {
    if (!armed_)
      return original_(list, state);
    ++calls;
    QueryPerformanceCounter(&begin);
    if (delay)
      Sleep(1); // Explicit calibration mode, never a production timing sample.
    const HRESULT hr = original_(list, state);
    QueryPerformanceCounter(&end);
    return hr;
  }

public:
  inline static thread_local UINT calls = 0;
  inline static thread_local bool delay = false;
  inline static thread_local LARGE_INTEGER begin = {}, end = {};
  bool Install(ID3D12GraphicsCommandList *list) {
    const auto method = &List::ValidateRecordedResourceStates;
    std::array<ptrdiff_t, 2> representation;
    static_assert(sizeof(method) == sizeof(representation));
    std::memcpy(representation.data(), &method, sizeof(method));
    if (!(representation[0] & 1) || representation[1] || representation[0] < 1)
      return false;
    auto **table = *reinterpret_cast<void ***>(static_cast<List *>(list));
    slot_ = table + (representation[0] - 1) / sizeof(void *);
    DWORD protection;
    if (!VirtualProtect(slot_, sizeof(*slot_), PAGE_READWRITE, &protection)) {
      slot_ = nullptr;
      return false;
    }
    original_ = reinterpret_cast<Function>(*slot_);
    InterlockedExchangePointer(reinterpret_cast<void *volatile *>(slot_),
                               reinterpret_cast<void *>(&Validate));
    VirtualProtect(slot_, sizeof(*slot_), protection, &protection);
    Validation state;
    state.local_only = true;
    Arm();
    const HRESULT hr =
        static_cast<List *>(list)->ValidateRecordedResourceStates(state);
    Stop();
    return SUCCEEDED(hr) && calls == 1 && state.states.empty() &&
           state.splits.empty();
  }
  void Arm() {
    calls = 0;
    begin = end = {};
    armed_ = true;
  }
  void Stop() { armed_ = false; }
  ~SplitValidationTiming() {
    Stop();
    if (slot_) {
      DWORD protection;
      if (VirtualProtect(slot_, sizeof(*slot_), PAGE_READWRITE, &protection)) {
        InterlockedExchangePointer(reinterpret_cast<void *volatile *>(slot_),
                                   reinterpret_cast<void *>(original_));
        VirtualProtect(slot_, sizeof(*slot_), protection, &protection);
      }
    }
  }
};

// Observe the serialized native dependency between two dispatches, without
// inserting a fence or otherwise changing the commands sent to Metal.
class ComputeBarrierCounter {
  DriverImportPatch patch_;
  inline static void *original_ = nullptr;
  inline static obj_handle_t first_encoder_ = 0;
  static void Encode(obj_handle_t encoder, const wmtcmd_base *head) {
    for (auto *command = head; command;
         command = static_cast<const wmtcmd_base *>(command->next.ptr)) {
      if (command->type == WMTComputeCommandDispatch ||
          command->type == WMTComputeCommandDispatchThreads) {
        if (!dispatches) {
          first_encoder_ = encoder;
          same_encoder = true;
        } else
          same_encoder &= encoder == first_encoder_;
        ++dispatches;
      } else if (command->type == WMTComputeCommandMemoryBarrier &&
                 (dispatches == 1 || dispatches == 2)) {
        ++(dispatches == 1 ? barriers : second_barriers);
        (dispatches == 1 ? scope : second_scope) |=
            reinterpret_cast<const wmtcmd_compute_memory_barrier *>(command)
                ->scope;
      }
    }
    reinterpret_cast<decltype(&Encode)>(original_)(encoder, head);
  }

public:
  inline static UINT dispatches = 0, barriers = 0, scope = 0;
  inline static UINT second_barriers = 0, second_scope = 0;
  inline static bool same_encoder = false;
  bool Install() {
    dispatches = barriers = scope = second_barriers = second_scope = 0;
    same_encoder = false;
    first_encoder_ = 0;
    return patch_.Install("MTLComputeCommandEncoder_encodeCommands",
                          reinterpret_cast<void *>(&Encode), original_);
  }
};

class RenderBarrierCounter {
  DriverImportPatch patch_;
  inline static void *original_ = nullptr;
  inline static obj_handle_t first_encoder_ = 0;
  static void Encode(obj_handle_t encoder, const wmtcmd_base *head) {
    for (auto *command = head; command;
         command = static_cast<const wmtcmd_base *>(command->next.ptr)) {
      if (command->type == WMTRenderCommandDraw) {
        if (!draws) {
          first_encoder_ = encoder;
          same_encoder = true;
        } else
          same_encoder &= encoder == first_encoder_;
        ++draws;
      } else if (command->type == WMTRenderCommandMemoryBarrier &&
                 (draws == 1 || draws == 2)) {
        const auto *barrier =
            reinterpret_cast<const wmtcmd_render_memory_barrier *>(command);
        if (barrier->scope & WMTBarrierScopeBuffers) {
          ++(draws == 1 ? barriers : second_barriers);
          (draws == 1 ? after : second_after) |= barrier->stages_after;
          (draws == 1 ? before : second_before) |= barrier->stages_before;
          (draws == 1 ? scope : second_scope) |= barrier->scope;
        }
      }
    }
    reinterpret_cast<decltype(&Encode)>(original_)(encoder, head);
  }

public:
  inline static UINT draws = 0, barriers = 0, after = 0, before = 0, scope = 0;
  inline static UINT second_barriers = 0, second_after = 0, second_before = 0,
                     second_scope = 0;
  inline static bool same_encoder = false;
  bool Install() {
    draws = barriers = after = before = scope = 0;
    second_barriers = second_after = second_before = second_scope = 0;
    same_encoder = false;
    first_encoder_ = 0;
    return patch_.Install("MTLRenderCommandEncoder_encodeCommands",
                          reinterpret_cast<void *>(&Encode), original_);
  }
};

bool TestConstantBufferBarrierStages(bool adjacent = false) {
  struct Compiler {
    HMODULE value = LoadLibraryA(D3DCOMPILER_DLL_A);
    ~Compiler() {
      if (value)
        FreeLibrary(value);
    }
  } compiler;
  const auto compile = compiler.value
                           ? reinterpret_cast<pD3DCompile>(
                                 GetProcAddress(compiler.value, "D3DCompile"))
                           : nullptr;
  if (!compile)
    return false;
  Owned<ID3D12Device> device;
  Owned<ID3D12RootSignature> root;
  Owned<ID3D12PipelineState> pipeline, writer;
  Owned<ID3DBlob> signature, vs, ps, write_ps, errors;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[1].Descriptor.ShaderRegister = 1;
  for (auto &parameter : parameters)
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {2, parameters, 0, nullptr,
                                         D3D12_ROOT_SIGNATURE_FLAG_NONE};
  const char *vertex = "float4 main(uint i:SV_VertexID):SV_Position {return "
                       "float4(i==2?3:-1,i==1?3:-1,0,1);}";
  const char *pixel = "cbuffer C:register(b0){float4 color;} float4 "
                      "main():SV_Target{return color;}";
  const char *write_pixel =
      "RWByteAddressBuffer data:register(u1); float4 main():SV_Target {"
      "data.Store4(0,asuint(float4(0,0,1,1)));return float4(0,1,0,1);}";
  if (!CheckHR("CBV stages device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device.ptr))) ||
      !CheckHR("CBV stages root serialization",
               D3D12SerializeRootSignature(&root_desc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           &signature.ptr, nullptr)) ||
      !CheckHR("CBV stages root",
               device->CreateRootSignature(0, signature->GetBufferPointer(),
                                           signature->GetBufferSize(),
                                           IID_PPV_ARGS(&root.ptr))) ||
      !CheckHR("CBV stages VS",
               compile(vertex, std::strlen(vertex), nullptr, nullptr, nullptr,
                       "main", "vs_5_0", 0, 0, &vs.ptr, &errors.ptr)) ||
      !CheckHR("CBV stages PS",
               compile(pixel, std::strlen(pixel), nullptr, nullptr, nullptr,
                       "main", "ps_5_0", 0, 0, &ps.ptr, &errors.ptr)))
    return false;
  if (!CheckHR("CBV stages writer PS",
               compile(write_pixel, std::strlen(write_pixel), nullptr, nullptr,
                       nullptr, "main", "ps_5_0", 0, 0, &write_ps.ptr,
                       &errors.ptr))) {
    if (errors.ptr)
      std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
    return false;
  }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
  pso.pRootSignature = root.ptr;
  pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
  pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
  pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso.RasterizerState.DepthClipEnable = TRUE;
  pso.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  pso.SampleMask = UINT_MAX;
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso.NumRenderTargets = 1;
  pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso.SampleDesc.Count = 1;
  if (!CheckHR("CBV stages PSO", device->CreateGraphicsPipelineState(
                                     &pso, IID_PPV_ARGS(&pipeline.ptr))))
    return false;
  pso.PS = {write_ps->GetBufferPointer(), write_ps->GetBufferSize()};
  if (!CheckHR("CBV stages writer PSO", device->CreateGraphicsPipelineState(
                                            &pso, IID_PPV_ARGS(&writer.ptr))))
    return false;
  bool good = true;
  for (UINT mode = 0; mode < 4; ++mode) {
    Owned<ID3D12Resource> constants, upload, target, readback;
    Owned<ID3D12DescriptorHeap> rtvs;
    Owned<ID3D12CommandQueue> queue;
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Owned<ID3D12GraphicsCommandList7> list7;
    Owned<ID3D12Fence> fence;
    struct Event {
      HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      ~Event() {
        if (value)
          CloseHandle(value);
      }
    } event;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = 256;
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels =
        buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!event.value || !CheckHR("CBV stages constants",
                                 device->CreateCommittedResource(
                                     &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                     IID_PPV_ARGS(&constants.ptr))))
      return false;
    buffer.Flags = D3D12_RESOURCE_FLAG_NONE;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (!CheckHR("CBV stages upload", device->CreateCommittedResource(
                                          &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                          D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&upload.ptr))))
      return false;
    void *mapped = nullptr;
    const float color[] = {0, 1, 0, 1};
    const D3D12_RANGE no_read = {};
    if (!CheckHR("CBV stages upload Map", upload->Map(0, &no_read, &mapped)))
      return false;
    std::memset(mapped, 0, 256);
    std::memcpy(mapped, color, sizeof(color));
    upload->Unmap(0, nullptr);
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    buffer.Width = 512;
    if (!CheckHR("CBV stages readback",
                 device->CreateCommittedResource(
                     &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                     IID_PPV_ARGS(&readback.ptr))))
      return false;
    D3D12_RESOURCE_DESC texture = {};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = 2;
    texture.Height = texture.DepthOrArraySize = texture.MipLevels =
        texture.SampleDesc.Count = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = 1;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    if (!CheckHR("CBV stages target",
                 device->CreateCommittedResource(
                     &heap, D3D12_HEAP_FLAG_NONE, &texture,
                     D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                     IID_PPV_ARGS(&target.ptr))) ||
        !CheckHR(
            "CBV stages RTV heap",
            device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtvs.ptr))) ||
        !CheckHR("CBV stages queue",
                 device->CreateCommandQueue(&queue_desc,
                                            IID_PPV_ARGS(&queue.ptr))) ||
        !CheckHR("CBV stages allocator", device->CreateCommandAllocator(
                                             D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&allocator.ptr))) ||
        !CheckHR("CBV stages list",
                 device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           allocator.ptr, pipeline.ptr,
                                           IID_PPV_ARGS(&list.ptr))) ||
        !CheckHR("CBV stages list7",
                 list->QueryInterface(IID_PPV_ARGS(&list7.ptr))) ||
        !CheckHR("CBV stages fence",
                 device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&fence.ptr))))
      return false;
    const auto rtv = rtvs->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target.ptr, nullptr, rtv);
    list->CopyBufferRegion(constants.ptr, 0, upload.ptr, 0, 256);
    const auto initial_state =
        mode == 0   ? D3D12_RESOURCE_STATE_COMMON
        : mode == 2 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        : mode == 3 ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                    : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    D3D12_RESOURCE_BARRIER transition = {};
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition = {constants.ptr,
                             D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                             D3D12_RESOURCE_STATE_COPY_DEST, initial_state};
    list->ResourceBarrier(1, &transition);
    if (mode == 0) {
      D3D12_BUFFER_BARRIER initialize = {};
      initialize.SyncBefore = D3D12_BARRIER_SYNC_COPY;
      initialize.SyncAfter = D3D12_BARRIER_SYNC_PIXEL_SHADING;
      initialize.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
      initialize.AccessAfter = D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
      initialize.pResource = constants.ptr;
      initialize.Size = UINT64_MAX;
      D3D12_BARRIER_GROUP group = {};
      group.Type = D3D12_BARRIER_TYPE_BUFFER;
      group.NumBarriers = 1;
      group.pBufferBarriers = &initialize;
      list7->Barrier(1, &group);
    }
    list->SetGraphicsRootSignature(root.ptr);
    list->SetGraphicsRootConstantBufferView(
        0, (mode < 2 ? constants : upload)->GetGPUVirtualAddress());
    list->SetGraphicsRootUnorderedAccessView(1,
                                             constants->GetGPUVirtualAddress());
    list->SetPipelineState(mode == 2 ? writer.ptr : pipeline.ptr);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport = {0, 0, 1, 1, 0, 1};
    const D3D12_RECT scissor = {0, 0, 2, 1};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    if (mode == 0) {
      D3D12_BUFFER_BARRIER barrier = {};
      barrier.SyncBefore = barrier.SyncAfter = D3D12_BARRIER_SYNC_PIXEL_SHADING;
      barrier.AccessBefore = barrier.AccessAfter =
          D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
      barrier.pResource = constants.ptr;
      barrier.Size = UINT64_MAX;
      D3D12_BARRIER_GROUP group = {};
      group.Type = D3D12_BARRIER_TYPE_BUFFER;
      group.NumBarriers = 1;
      group.pBufferBarriers = &barrier;
      list7->Barrier(1, &group);
    } else {
      transition.Transition.StateBefore = initial_state;
      transition.Transition.StateAfter =
          mode == 2   ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
          : mode == 1 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                      : D3D12_RESOURCE_STATE_COPY_SOURCE;
      list->ResourceBarrier(1, &transition);
    }
    D3D12_GLOBAL_BARRIER dependency = {
        D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_SYNC_PIXEL_SHADING,
        D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_ACCESS_NO_ACCESS};
    D3D12_BARRIER_GROUP dependencies = {};
    dependencies.Type = D3D12_BARRIER_TYPE_GLOBAL;
    dependencies.NumBarriers = 1;
    dependencies.pGlobalBarriers = &dependency;
    if (adjacent)
      for (UINT i = 0; i < 8; ++i)
        list7->Barrier(1, &dependencies);
    if (adjacent && mode == 3) {
      // End with narrower buffer/stage scopes to require a union, not
      // overwrite.
      transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
      transition.Transition.StateAfter =
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      list->ResourceBarrier(1, &transition);
      transition.Transition.StateBefore =
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      transition.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
      list->ResourceBarrier(1, &transition);
    }
    list->SetGraphicsRootConstantBufferView(
        0,
        (mode == 0 || mode == 2 ? constants : upload)->GetGPUVirtualAddress());
    list->SetPipelineState(mode == 1 ? writer.ptr : pipeline.ptr);
    viewport.TopLeftX = 1;
    list->RSSetViewports(1, &viewport);
    list->DrawInstanced(3, 1, 0, 0);
    if (adjacent) {
      for (UINT i = 0; i < 8; ++i)
        list7->Barrier(1, &dependencies);
      if (mode == 3) {
        transition.Transition.StateBefore = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        transition.Transition.StateAfter =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1, &transition);
        transition.Transition.StateBefore =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        transition.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        list->ResourceBarrier(1, &transition);
      }
      list->DrawInstanced(3, 1, 0, 0);
    }
    if (mode == 0) {
      D3D12_BUFFER_BARRIER finish = {};
      finish.SyncBefore = D3D12_BARRIER_SYNC_PIXEL_SHADING;
      finish.SyncAfter = D3D12_BARRIER_SYNC_COPY;
      finish.AccessBefore = D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
      finish.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
      finish.pResource = constants.ptr;
      finish.Size = UINT64_MAX;
      D3D12_BARRIER_GROUP group = {};
      group.Type = D3D12_BARRIER_TYPE_BUFFER;
      group.NumBarriers = 1;
      group.pBufferBarriers = &finish;
      list7->Barrier(1, &group);
    } else if (mode != 3) {
      transition.Transition.StateBefore =
          mode == 1 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                    : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
      transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      list->ResourceBarrier(1, &transition);
    }
    if (adjacent && mode == 3) {
      transition.Transition.StateBefore = D3D12_RESOURCE_STATE_INDEX_BUFFER;
      transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      list->ResourceBarrier(1, &transition);
    }
    list->CopyBufferRegion(readback.ptr, 256, constants.ptr, 0, 16);
    transition.Transition = {
        target.ptr, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &transition);
    D3D12_TEXTURE_COPY_LOCATION source = {}, destination = {};
    source.pResource = target.ptr;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = readback.ptr;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 2, 1,
                                             1, 256};
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    if (!CheckHR("CBV stages Close", list->Close()))
      return false;
    RenderBarrierCounter counter;
    if (!counter.Install())
      return false;
    ID3D12CommandList *batch[] = {list.ptr};
    queue->ExecuteCommandLists(1, batch);
    if (!CheckHR("CBV stages Signal", queue->Signal(fence.ptr, 1)) ||
        !CheckHR("CBV stages wait",
                 fence->SetEventOnCompletion(1, event.value)) ||
        WaitForSingleObject(event.value, 5000) != WAIT_OBJECT_0 ||
        !CheckHR("CBV stages device status", device->GetDeviceRemovedReason()))
      return false;
    if (!CheckHR("CBV stages pixel Map", readback->Map(0, nullptr, &mapped)))
      return false;
    UINT pixel_value = 0, second_pixel = 0;
    float buffer_color[4] = {};
    std::memcpy(&pixel_value, mapped, sizeof(pixel_value));
    std::memcpy(&second_pixel, static_cast<const BYTE *>(mapped) + 4,
                sizeof(second_pixel));
    std::memcpy(buffer_color, static_cast<const BYTE *>(mapped) + 256,
                sizeof(buffer_color));
    readback->Unmap(0, &no_read);
    const UINT all = WMTRenderStagePreRaster | WMTRenderStageFragment;
    const bool native =
        counter.draws == (adjacent ? 3u : 2u) && counter.same_encoder &&
        counter.barriers == 1 &&
        counter.after ==
            (!adjacent && mode == 3 ? UINT(WMTRenderStagePreRaster) : all) &&
        counter.before == all &&
        (!adjacent ||
         (counter.scope == (WMTBarrierScopeBuffers | WMTBarrierScopeTextures |
                            WMTBarrierScopeRenderTargets) &&
          counter.second_barriers == 1 &&
          counter.second_scope == counter.scope &&
          counter.second_after == all && counter.second_before == all));
    const bool written = mode == 1 || mode == 2;
    const bool pixels =
        pixel_value == 0xff00ff00u &&
        second_pixel == (mode == 2 ? 0xffff0000u : 0xff00ff00u) &&
        buffer_color[0] == 0 && buffer_color[1] == (written ? 0 : 1) &&
        buffer_color[2] == (written ? 1 : 0) && buffer_color[3] == 1;
    std::cout << "constant-stages mode=" << mode << " draws=" << counter.draws
              << " same_encoder=" << counter.same_encoder
              << " barriers=" << counter.barriers << " after=" << counter.after
              << " before=" << counter.before << " adjacent=" << adjacent
              << " scope=" << counter.scope
              << " second_barriers=" << counter.second_barriers
              << " second_scope=" << counter.second_scope
              << " native=" << native << " pixel=0x" << std::hex << pixel_value
              << "/0x" << second_pixel << std::dec
              << " buffer=" << buffer_color[0] << "," << buffer_color[1] << ","
              << buffer_color[2] << "," << buffer_color[3]
              << " readback=" << (pixels ? "PASS" : "FAIL") << "\n";
    good &= native && pixels;
  }
  return good;
}

bool TestCommonAccessBarriers(bool adjacent = false, bool access_only = false) {
  struct Compiler {
    HMODULE module = LoadLibraryA(D3DCOMPILER_DLL_A);
    ~Compiler() {
      if (module)
        FreeLibrary(module);
    }
  } compiler;
  auto compile = compiler.module ? reinterpret_cast<pD3DCompile>(GetProcAddress(
                                       compiler.module, "D3DCompile"))
                                 : nullptr;
  Owned<ID3D12Device> device;
  Owned<ID3D12RootSignature> root;
  Owned<ID3D12PipelineState> pipelines[3];
  Owned<ID3DBlob> serialized;
  D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable = {1, &range};
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants = {0, 0, 1};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {2, parameters, 0, nullptr,
                                         D3D12_ROOT_SIGNATURE_FLAG_NONE};
  if (!compile ||
      !CheckHR("common access device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device.ptr))) ||
      !CheckHR("common access serialize",
               D3D12SerializeRootSignature(&root_desc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           &serialized.ptr, nullptr)) ||
      !CheckHR("common access root",
               device->CreateRootSignature(0, serialized->GetBufferPointer(),
                                           serialized->GetBufferSize(),
                                           IID_PPV_ARGS(&root.ptr))))
    return false;
  const char *shaders[] = {
      "RWStructuredBuffer<uint> data:register(u0); cbuffer C:register(b0){uint "
      "phase;}"
      "[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID){uint "
      "i=p.x+8*p.y;"
      "if(!phase)data[i]=0x12345600u^i;else data[i]=3u*data[i]+7u;}",
      "RWTexture2D<uint> data:register(u0); cbuffer C:register(b0){uint phase;}"
      "[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID){uint "
      "i=p.x+8*p.y;"
      "if(!phase)data[p.xy]=0x12345600u^i;else data[p.xy]=3u*data[p.xy]+7u;}",
      "[numthreads(8,8,1)] void main() {}"};
  for (UINT i = 0; i < 3; ++i) {
    Owned<ID3DBlob> code, errors;
    if (!CheckHR("common access shader",
                 compile(shaders[i], std::strlen(shaders[i]), nullptr, nullptr,
                         nullptr, "main", "cs_5_0", 0, 0, &code.ptr,
                         &errors.ptr)))
      return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root.ptr;
    desc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    if (!CheckHR("common access PSO",
                 device->CreateComputePipelineState(
                     &desc, IID_PPV_ARGS(&pipelines[i].ptr))))
      return false;
  }
  bool good = true;
  for (UINT type = access_only ? 2u : 0u; type < 3; ++type) {
    for (UINT mode = 0; mode < 4; ++mode) {
      const bool texture = type == 2;
      // NONE/NO_ACCESS is empty; COMPUTE/NO_ACCESS still orders dispatches.
      const bool no_access = mode >= 2;
      Owned<ID3D12Resource> resource, readback;
      Owned<ID3D12DescriptorHeap> descriptors;
      Owned<ID3D12CommandQueue> queue;
      Owned<ID3D12CommandAllocator> allocator;
      Owned<ID3D12GraphicsCommandList> list;
      Owned<ID3D12GraphicsCommandList7> list7;
      Owned<ID3D12Fence> fence;
      struct Event {
        HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ~Event() {
          if (value)
            CloseHandle(value);
        }
      } event;
      D3D12_HEAP_PROPERTIES heap = {};
      heap.Type = D3D12_HEAP_TYPE_DEFAULT;
      heap.CreationNodeMask = heap.VisibleNodeMask = 1;
      D3D12_RESOURCE_DESC desc = {};
      desc.Dimension = texture ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
                               : D3D12_RESOURCE_DIMENSION_BUFFER;
      desc.Width = texture ? 8 : 64 * sizeof(UINT);
      desc.Height = texture ? 8 : 1;
      desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
      desc.Layout = texture ? D3D12_TEXTURE_LAYOUT_UNKNOWN
                            : D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      desc.Format = texture ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_UNKNOWN;
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      D3D12_COMMAND_QUEUE_DESC queue_desc = {};
      D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc = {};
      descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      descriptor_desc.NumDescriptors = 1;
      descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      if (!event.value ||
          !CheckHR("common access resource",
                   device->CreateCommittedResource(
                       &heap, D3D12_HEAP_FLAG_NONE, &desc,
                       D3D12_RESOURCE_STATE_COMMON, nullptr,
                       IID_PPV_ARGS(&resource.ptr))) ||
          !CheckHR("common access descriptors",
                   device->CreateDescriptorHeap(
                       &descriptor_desc, IID_PPV_ARGS(&descriptors.ptr))) ||
          !CheckHR("common access queue",
                   device->CreateCommandQueue(&queue_desc,
                                              IID_PPV_ARGS(&queue.ptr))) ||
          !CheckHR(
              "common access allocator",
              device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&allocator.ptr))) ||
          !CheckHR("common access list",
                   device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             allocator.ptr, nullptr,
                                             IID_PPV_ARGS(&list.ptr))) ||
          !CheckHR("common access list7",
                   list->QueryInterface(IID_PPV_ARGS(&list7.ptr))) ||
          !CheckHR("common access fence",
                   device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&fence.ptr))))
        return false;
      D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
      view.Format = desc.Format;
      view.ViewDimension =
          texture ? D3D12_UAV_DIMENSION_TEXTURE2D : D3D12_UAV_DIMENSION_BUFFER;
      if (!texture) {
        view.Buffer.NumElements = 64;
        view.Buffer.StructureByteStride = sizeof(UINT);
      }
      device->CreateUnorderedAccessView(
          resource.ptr, nullptr, &view,
          descriptors->GetCPUDescriptorHandleForHeapStart());
      D3D12_TEXTURE_BARRIER texture_barrier = {};
      texture_barrier.pResource = resource.ptr;
      texture_barrier.Subresources.IndexOrFirstMipLevel =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      D3D12_BARRIER_GROUP group = {};
      group.NumBarriers = 1;
      if (texture && !no_access) {
        texture_barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
        texture_barrier.SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
        texture_barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
        texture_barrier.AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
        texture_barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
        texture_barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
        group.Type = D3D12_BARRIER_TYPE_TEXTURE;
        group.pTextureBarriers = &texture_barrier;
        list7->Barrier(1, &group);
      }
      list->SetPipelineState(pipelines[no_access ? 2 : texture ? 1 : 0].ptr);
      list->SetComputeRootSignature(root.ptr);
      ID3D12DescriptorHeap *heaps[] = {descriptors.ptr};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootDescriptorTable(
          0, descriptors->GetGPUDescriptorHandleForHeapStart());
      list->SetComputeRoot32BitConstant(1, 0, 0);
      list->Dispatch(1, 1, 1);
      const auto access = no_access   ? D3D12_BARRIER_ACCESS_NO_ACCESS
                          : mode == 0 ? D3D12_BARRIER_ACCESS_COMMON
                                      : D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
      const auto sync = mode == 2 ? D3D12_BARRIER_SYNC_NONE
                                  : D3D12_BARRIER_SYNC_COMPUTE_SHADING;
      D3D12_GLOBAL_BARRIER global = {sync, sync, access, access};
      D3D12_BUFFER_BARRIER buffer = {sync,         sync, access,    access,
                                     resource.ptr, 0,    UINT64_MAX};
      if (type == 0) {
        group.Type = D3D12_BARRIER_TYPE_GLOBAL;
        group.pGlobalBarriers = &global;
      } else if (type == 1) {
        group.Type = D3D12_BARRIER_TYPE_BUFFER;
        group.pBufferBarriers = &buffer;
      } else {
        texture_barrier.SyncBefore = texture_barrier.SyncAfter = sync;
        texture_barrier.AccessBefore = texture_barrier.AccessAfter = access;
        texture_barrier.LayoutBefore = texture_barrier.LayoutAfter =
            access_only ? D3D12_BARRIER_LAYOUT_UNDEFINED
            : no_access ? D3D12_BARRIER_LAYOUT_COMMON
                        : D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
        group.Type = D3D12_BARRIER_TYPE_TEXTURE;
        group.pTextureBarriers = &texture_barrier;
      }
      for (UINT i = 0; i < (adjacent ? 8u : 1u); ++i)
        list7->Barrier(1, &group);
      D3D12_GLOBAL_BARRIER dependency = {sync, sync,
                                         D3D12_BARRIER_ACCESS_NO_ACCESS,
                                         D3D12_BARRIER_ACCESS_NO_ACCESS};
      D3D12_BARRIER_GROUP dependencies = {};
      dependencies.Type = D3D12_BARRIER_TYPE_GLOBAL;
      dependencies.NumBarriers = 1;
      dependencies.pGlobalBarriers = &dependency;
      if (adjacent) {
        list7->Barrier(1, &dependencies);
        // Buffer/texture scope after a global scope must not narrow the union.
        list7->Barrier(1, &group);
      }
      list->SetComputeRoot32BitConstant(1, 1, 0);
      list->Dispatch(1, 1, 1);
      if (adjacent) {
        for (UINT i = 0; i < 8; ++i)
          list7->Barrier(1, &group);
        list7->Barrier(1, &dependencies);
        list7->Barrier(1, &group);
        list->Dispatch(1, 1, 1);
      }
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
      UINT64 total = desc.Width;
      if (!no_access) {
        if (texture)
          device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr,
                                        nullptr, &total);
        D3D12_RESOURCE_DESC readback_desc = {};
        readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readback_desc.Width = total;
        readback_desc.Height = readback_desc.DepthOrArraySize =
            readback_desc.MipLevels = readback_desc.SampleDesc.Count = 1;
        readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        if (!CheckHR("common access readback",
                     device->CreateCommittedResource(
                         &heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                         IID_PPV_ARGS(&readback.ptr))))
          return false;
        if (texture) {
          texture_barrier.SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
          texture_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
          texture_barrier.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
          texture_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
          texture_barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
          texture_barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
          list7->Barrier(1, &group);
          D3D12_TEXTURE_COPY_LOCATION source = {}, target = {};
          source.pResource = resource.ptr;
          source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
          target.pResource = readback.ptr;
          target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
          target.PlacedFootprint = footprint;
          list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
        } else {
          buffer.SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
          buffer.SyncAfter = D3D12_BARRIER_SYNC_COPY;
          buffer.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
          buffer.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
          group.Type = D3D12_BARRIER_TYPE_BUFFER;
          group.pBufferBarriers = &buffer;
          list7->Barrier(1, &group);
          list->CopyBufferRegion(readback.ptr, 0, resource.ptr, 0, total);
        }
      }
      if (!CheckHR("common access Close", list->Close()))
        return false;
      ComputeBarrierCounter counter;
      if (!counter.Install())
        return false;
      ID3D12CommandList *batch[] = {list.ptr};
      queue->ExecuteCommandLists(1, batch);
      if (!CheckHR("common access Signal", queue->Signal(fence.ptr, 1)) ||
          !CheckHR("common access completion",
                   fence->SetEventOnCompletion(1, event.value)) ||
          WaitForSingleObject(event.value, 5000) != WAIT_OBJECT_0 ||
          !CheckHR("common access device status",
                   device->GetDeviceRemovedReason()))
        return false;
      bool pixels = true;
      if (!no_access) {
        void *mapped = nullptr;
        if (!CheckHR("common access Map", readback->Map(0, nullptr, &mapped)))
          return false;
        for (UINT i = 0; i < 64; ++i) {
          const auto offset =
              texture ? footprint.Offset +
                            (i / 8) * footprint.Footprint.RowPitch + (i % 8) * 4
                      : i * 4;
          UINT actual;
          std::memcpy(&actual, static_cast<const BYTE *>(mapped) + offset,
                      sizeof(actual));
          const UINT first = 3u * (0x12345600u ^ i) + 7u;
          pixels &= actual == (adjacent ? 3u * first + 7u : first);
        }
        const D3D12_RANGE no_write = {};
        readback->Unmap(0, &no_write);
      }
      const UINT expected_scope =
          texture ? WMTBarrierScopeTextures : WMTBarrierScopeBuffers;
      const bool native =
          counter.dispatches == (adjacent ? 3u : 2u) && counter.same_encoder &&
          counter.barriers == (mode == 2 ? 0u : 1u) &&
          (mode == 2 || (counter.scope & expected_scope)) &&
          (!adjacent ||
           (counter.second_barriers == (mode == 2 ? 0u : 1u) &&
            (mode == 2 || (counter.scope == (WMTBarrierScopeBuffers |
                                             WMTBarrierScopeTextures) &&
                           counter.second_scope == counter.scope))));
      std::cout << "common-access type=" << type << " mode=" << mode
                << " access_only=" << access_only
                << " dispatches=" << counter.dispatches
                << " same_encoder=" << counter.same_encoder
                << " barriers=" << counter.barriers
                << " scope=" << counter.scope << " adjacent=" << adjacent
                << " second_barriers=" << counter.second_barriers
                << " second_scope=" << counter.second_scope
                << " native=" << native << " readback="
                << (no_access ? "NOT_RUN"
                    : pixels  ? "PASS"
                              : "FAIL")
                << "\n";
      good &= native && pixels;
    }
  }
  return good;
}

bool TestSmallBlitCost(bool late_event = false, bool long_batch = false,
                       bool split_cost = false,
                       bool omit_second_submission = false,
                       bool alternating_resources = false,
                       UINT pending_width = 0, bool reverse_pending = false,
                       bool validation_timing = false,
                       bool validation_delay = false) {
  HANDLE event =
      late_event ? nullptr : CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!late_event && !event)
    return false;
  struct CloseEvent {
    HANDLE value;
    ~CloseEvent() {
      if (value)
        CloseHandle(value);
    }
  } close_event{event};
  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Owned<ID3D12GraphicsCommandList7> list7;
  Owned<ID3D12Resource> upload, readback, alternate_upload;
  std::vector<Owned<ID3D12Resource>> pending_uploads(
      pending_width ? pending_width - 1 : 0);
  Owned<ID3D12Fence> fence;
  if (!CheckHR("CreateDevice",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device.ptr))))
    return false;
  D3D12_COMMAND_QUEUE_DESC q = {};
  if (!CheckHR("CreateQueue",
               device.ptr->CreateCommandQueue(&q, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR("CreateAllocator", device.ptr->CreateCommandAllocator(
                                      q.Type, IID_PPV_ARGS(&allocator.ptr))) ||
      !CheckHR("CreateList",
               device.ptr->CreateCommandList(0, q.Type, allocator.ptr, nullptr,
                                             IID_PPV_ARGS(&list.ptr))) ||
      !CheckHR("CreateFence",
               device.ptr->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&fence.ptr))))
    return false;
  D3D12_RESOURCE_DESC b = {};
  b.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  b.Width = 256;
  b.Height = b.DepthOrArraySize = b.MipLevels = 1;
  b.SampleDesc.Count = 1;
  b.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES h = {};
  h.CreationNodeMask = h.VisibleNodeMask = 1;
  h.Type = D3D12_HEAP_TYPE_UPLOAD;
  if (!CheckHR("CreateUpload", device.ptr->CreateCommittedResource(
                                   &h, D3D12_HEAP_FLAG_NONE, &b,
                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                   IID_PPV_ARGS(&upload.ptr))))
    return false;
  if (alternating_resources &&
      !CheckHR("CreateAlternateUpload",
               device.ptr->CreateCommittedResource(
                   &h, D3D12_HEAP_FLAG_NONE, &b,
                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                   IID_PPV_ARGS(&alternate_upload.ptr))))
    return false;
  for (auto &resource : pending_uploads)
    if (!CheckHR("CreatePendingUpload",
                 device->CreateCommittedResource(
                     &h, D3D12_HEAP_FLAG_NONE, &b,
                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                     IID_PPV_ARGS(&resource.ptr))))
      return false;
  const auto pending_resource = [&](UINT index) {
    return index ? pending_uploads[index - 1].ptr : upload.ptr;
  };
  h.Type = D3D12_HEAP_TYPE_READBACK;
  b.Width = pending_width
                ? std::max<UINT>(256, pending_width * sizeof(uint32_t))
                : 256;
  if (!CheckHR("CreateReadback",
               device.ptr->CreateCommittedResource(
                   &h, D3D12_HEAP_FLAG_NONE, &b, D3D12_RESOURCE_STATE_COPY_DEST,
                   nullptr, IID_PPV_ARGS(&readback.ptr))))
    return false;
  uint32_t *mapped = nullptr;

  // Keep ownership before the device so either setup order has safe teardown.
  if (late_event) {
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    close_event.value = event;
    if (!event)
      return false;
  }
  if (split_cost && FAILED(list->QueryInterface(IID_PPV_ARGS(&list7.ptr))))
    return false;
  SplitValidationTiming timing;
  if (validation_timing)
    timing.delay = validation_delay;
  if (validation_timing && !timing.Install(list.ptr))
    return false;
  D3D12_BUFFER_BARRIER begin = {};
  begin.SyncBefore = D3D12_BARRIER_SYNC_COPY;
  begin.SyncAfter = D3D12_BARRIER_SYNC_SPLIT;
  begin.AccessBefore = begin.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
  begin.pResource = upload.ptr;
  begin.Size = UINT64_MAX;
  auto end_barrier = begin;
  end_barrier.SyncBefore = D3D12_BARRIER_SYNC_SPLIT;
  end_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
  auto alternate_begin = begin;
  auto alternate_end = end_barrier;
  alternate_begin.pResource = alternate_end.pResource = alternate_upload.ptr;
  D3D12_BARRIER_GROUP group = {};
  group.Type = D3D12_BARRIER_TYPE_BUFFER;
  group.NumBarriers = 1;
  // Both batches fit the existing 4MiB CPU arena (64 bytes per copy).
  const UINT copies = pending_width ? 2048
                      : split_cost  ? 2000
                      : long_batch  ? 50000
                                    : 10000;
  const UINT rounds = omit_second_submission ? 2 : long_batch ? 25 : 9;

  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  for (UINT round = 0; round < rounds; ++round) {
    if (round &&
        (!CheckHR("ResetAllocator", allocator.ptr->Reset()) ||
         !CheckHR("ResetList", list.ptr->Reset(allocator.ptr, nullptr))))
      return false;
    // The previous round's fence completed before reset. Update outside the
    // timed interval so stale readback cannot validate a missing submission.
    const uint32_t payload = 0x51a61234u ^ (round * 0x9e3779b9u);
    const D3D12_RANGE no_read = {0, 0};
    if (!CheckHR(
            "MapUpload",
            upload.ptr->Map(0, &no_read, reinterpret_cast<void **>(&mapped))))
      return false;
    *mapped = payload;
    const D3D12_RANGE written = {0, sizeof(payload)};
    upload.ptr->Unmap(0, &written);
    for (UINT index = 1; index < pending_width; ++index) {
      auto *resource = pending_resource(index);
      if (!CheckHR(
              "MapPendingUpload",
              resource->Map(0, &no_read, reinterpret_cast<void **>(&mapped))))
        return false;
      *mapped = payload ^ (index * 0x85ebca6bu);
      resource->Unmap(0, &written);
    }
    CopyBufferCounter counter;
    if (!counter.Install())
      return false;
    BlitEncoderCounter encoders;
    if (split_cost && !encoders.Install())
      return false;
    LARGE_INTEGER start, end, closed, submitted;
    QueryPerformanceCounter(&start);
    if (split_cost) {
      list->CopyBufferRegion(readback.ptr, 0, upload.ptr, 0, 4);
      if (pending_width) {
        for (UINT batch = 0; batch < copies / pending_width; ++batch) {
          for (UINT index = 0; index < pending_width; ++index) {
            begin.pResource = pending_resource(index);
            group.pBufferBarriers = &begin;
            list7->Barrier(1, &group);
          }
          for (UINT index = 0; index < pending_width; ++index) {
            end_barrier.pResource = pending_resource(
                reverse_pending ? pending_width - index - 1 : index);
            group.pBufferBarriers = &end_barrier;
            list7->Barrier(1, &group);
          }
        }
        for (UINT index = 0; index < pending_width; ++index)
          list->CopyBufferRegion(readback.ptr, index * sizeof(uint32_t),
                                 pending_resource(index), 0, sizeof(uint32_t));
      } else {
        for (UINT i = 0; i < copies; ++i) {
          group.pBufferBarriers = &begin;
          list7->Barrier(1, &group);
          if (alternating_resources) {
            group.pBufferBarriers = &alternate_begin;
            list7->Barrier(1, &group);
          }
          group.pBufferBarriers = &end_barrier;
          list7->Barrier(1, &group);
          if (alternating_resources) {
            group.pBufferBarriers = &alternate_end;
            list7->Barrier(1, &group);
            ++i; // Two pairs in A-BEGIN/B-BEGIN/A-END/B-END order.
          }
        }
        list->CopyBufferRegion(readback.ptr, 0, upload.ptr, 0, 4);
      }
    } else {
      for (UINT i = 0; i < copies; ++i)
        list.ptr->CopyBufferRegion(readback.ptr, 0, upload.ptr, 0, 4);
    }
    QueryPerformanceCounter(&end);
    if (!CheckHR("CloseBlit", list.ptr->Close()) || CopyBufferCounter::count)
      return false;
    QueryPerformanceCounter(&closed);
    ID3D12CommandList *batch[] = {list.ptr};
    // Test-only fault: exercise the readback oracle without issuing this
    // round's copy.
    if (validation_timing)
      timing.Arm();
    if (!omit_second_submission || round != 1)
      queue.ptr->ExecuteCommandLists(1, batch);
    QueryPerformanceCounter(&submitted);
    if (validation_timing)
      timing.Stop();
    if (validation_timing && SplitValidationTiming::calls != 1)
      return false;
    if (split_cost && BlitEncoderCounter::count != 2)
      return false;
    if (!CheckHR("SignalBlit", queue.ptr->Signal(fence.ptr, round + 1)) ||
        !CheckHR("EventBlit",
                 fence.ptr->SetEventOnCompletion(round + 1, event)) ||
        WaitForSingleObject(event, 30000) != WAIT_OBJECT_0 ||
        fence.ptr->GetCompletedValue() == UINT64_MAX)
      return false;
    if (!CheckHR(
            "MapBlit",
            readback.ptr->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
      return false;
    bool pending_matches = true;
    for (UINT index = 0; index < pending_width; ++index) {
      const uint32_t expected = payload ^ (index * 0x85ebca6bu);
      if (mapped[index] != expected) {
        std::cerr << "pending split readback mismatch resource=" << index
                  << " round=" << round << " actual=" << mapped[index]
                  << " expected=" << expected << "\n";
        pending_matches = false;
      }
    }
    const uint32_t actual = *mapped;
    readback.ptr->Unmap(0, nullptr);
    if (!pending_matches || actual != payload) {
      std::cerr << "small blit readback mismatch round=" << round
                << " actual=" << actual << " expected=" << payload
                << " device_reason=" << device.ptr->GetDeviceRemovedReason()
                << "\n";
      return false;
    }
    std::cout << (split_cost ? "split_cost round=" : "small_blit round=")
              << round << " alternating=" << alternating_resources
              << " pending_width=" << pending_width
              << " reverse=" << reverse_pending
              << " pairs=" << (split_cost ? copies : 0) << " copies="
              << (pending_width ? pending_width + 1
                  : split_cost  ? 2
                                : copies)
              << " late_event=" << late_event << " payload=" << payload
              << " native_buffers=" << CopyBufferCounter::count << " record_us="
              << (end.QuadPart - start.QuadPart) * 1e6 / frequency.QuadPart
              << " close_us="
              << (closed.QuadPart - end.QuadPart) * 1e6 / frequency.QuadPart
              << " submit_us="
              << (submitted.QuadPart - closed.QuadPart) * 1e6 /
                     frequency.QuadPart
              << " native_encoders="
              << (split_cost ? BlitEncoderCounter::count : 0) << "\n";
    if (validation_timing) {
      const auto &a = SplitValidationTiming::begin;
      const auto &b = SplitValidationTiming::end;
      std::cout << "split_phase round=" << round
                << " calls=" << SplitValidationTiming::calls
                << " prefix_us=" << (a.QuadPart - closed.QuadPart) * 1e6 / frequency.QuadPart
                << " validate_us=" << (b.QuadPart - a.QuadPart) * 1e6 / frequency.QuadPart
                << " suffix_us=" << (submitted.QuadPart - b.QuadPart) * 1e6 / frequency.QuadPart
                << " total_us=" << (submitted.QuadPart - closed.QuadPart) * 1e6 / frequency.QuadPart << "\n";
    }
  }
  return true;
}

int main(int argc, char **argv) {
  if (argc == 2 && !std::strcmp(argv[1], "--texture-access-only-barriers"))
    return TestCommonAccessBarriers(true, true) ? 0 : 1;
  if (argc == 2 && !std::strcmp(argv[1], "--adjacent-memory-barriers")) {
    const bool render = TestConstantBufferBarrierStages(true);
    const bool compute = TestCommonAccessBarriers(true);
    return render && compute ? 0 : 1;
  }
  if (argc == 2 && !std::strcmp(argv[1], "--constant-buffer-barrier-stages"))
    return TestConstantBufferBarrierStages() ? 0 : 1;
  if (argc == 2 && !std::strcmp(argv[1], "--common-access-barriers"))
    return TestCommonAccessBarriers() ? 0 : 1;
  if (argc == 3 && !std::strcmp(argv[1], "--split-cost")) {
    if (!std::strcmp(argv[2], "phases") || !std::strcmp(argv[2], "phases-delay"))
      return TestSmallBlitCost(false, true, true, false, false, 0, false, true,
                              !std::strcmp(argv[2], "phases-delay")) ? 0 : 1;
    if (!std::strcmp(argv[2], "pending32"))
      return TestSmallBlitCost(false, true, true, false, false, 32) ? 0 : 1;
    if (!std::strcmp(argv[2], "pending512"))
      return TestSmallBlitCost(false, true, true, false, false, 512) ? 0 : 1;
    if (!std::strcmp(argv[2], "reverse512"))
      return TestSmallBlitCost(false, true, true, false, false, 512, true) ? 0
                                                                           : 1;
  }
  if (argc == 3 && !std::strcmp(argv[1], "--small-blit-cost") &&
      !std::strcmp(argv[2], "stale"))
    return TestSmallBlitCost(false, false, false, true) ? 0 : 1;
  if (argc == 3 && !std::strcmp(argv[1], "--split-cost") &&
      !std::strcmp(argv[2], "alternating"))
    return TestSmallBlitCost(false, true, true, false, true) ? 0 : 1;
  if (argc == 2 && !std::strcmp(argv[1], "--split-cost"))
    return TestSmallBlitCost(false, true, true) ? 0 : 1;
  if (argc == 2 && std::strcmp(argv[1], "--small-blit-cost") == 0)
    return TestSmallBlitCost() ? 0 : 1;
  if (argc == 3 && std::strcmp(argv[1], "--small-blit-cost") == 0) {
    const bool late =
        !std::strcmp(argv[2], "late") || !std::strcmp(argv[2], "late-long");
    const bool extended = !std::strcmp(argv[2], "early-long") ||
                          !std::strcmp(argv[2], "late-long");
    if (!late && !extended && std::strcmp(argv[2], "early"))
      return 1;
    return TestSmallBlitCost(late, extended) ? 0 : 1;
  }
  if (argc == 3 && std::strcmp(argv[1], "--copy-staging") == 0)
    return TestCopyStaging(argv[2]) ? 0 : 1;
  if (argc == 2 && std::strcmp(argv[1], "--multiple-fences-concurrent") == 0)
    return TestConcurrentMultipleFenceWaits() ? 0 : 1;
  if (argc == 2 && std::strcmp(argv[1], "--multiple-fences") == 0)
    return TestMultipleFenceWaits() ? 0 : 1;
  if ((argc == 3 || argc == 4) && !std::strcmp(argv[1], "--split-failure")) {
    unsigned allocation = argc == 4 ? std::strtoul(argv[3], nullptr, 10) : 1;
    if (!allocation || allocation > 64)
      return 2;
    return TestSplitFailure(argv[2], allocation) ? 0 : 1;
  }
  if (argc == 2 && std::strcmp(argv[1], "--infoqueue-debug-controls") == 0)
    return TestInfoQueueDebugControls() ? 0 : 1;
  if (argc == 2 && std::strcmp(argv[1], "--root-serialization-allocation-failure") == 0)
    return TestRootSerializationAllocationFailure() ? 0 : 1;
  if (argc == 2 && std::strcmp(argv[1], "--infoqueue-allocation-failure") == 0)
    return TestInfoQueueAllocationFailure() ? 0 : 1;
  if (argc == 3 && std::strcmp(argv[1], "--encoder-creation-failure") == 0) {
    const char *mode = argv[2];
    if (std::strcmp(mode, "copy") && std::strcmp(mode, "clear") &&
        std::strcmp(mode, "late-clear") && std::strcmp(mode, "compute") &&
        std::strcmp(mode, "timestamp") && std::strcmp(mode, "calibrate"))
      return 2;
    return TestCommandBufferFailure(
               !std::strcmp(mode, "calibrate") ? "calibrate" : "execute", mode)
               ? 0
               : 1;
  }
  if (argc == 3 && std::strcmp(argv[1], "--command-buffer-failure") == 0)
    return TestCommandBufferFailure(argv[2]) ? 0 : 1;
  if (argc == 2 && std::strcmp(argv[1], "--submission-translation-abort") == 0)
    return TestSubmissionAllocationFailure(0, true, true) ? 0 : 1;
  if (argc >= 3 && std::strcmp(argv[1], "--submission-allocation-failure") == 0) {
    char *end = nullptr;
    const auto allocation = std::strtoull(argv[2], &end, 10);
    if (end == argv[2] || *end || argv[2][0] == '-' || allocation > UINT_MAX || (argc != 3 && argc != 4) ||
        (argc == 4 && std::strcmp(argv[3], "clear") != 0))
      return 2;
    return TestSubmissionAllocationFailure(static_cast<UINT>(allocation), argc == 4) ? 0 : 1;
  }
  ID3D12Device *device = nullptr;
  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return 1;

  if (!TestResetAllocatorSubmission(device) || !TestAllocatorRecordingGrowth(device)) {
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
  if (!TestConstantBufferBarrierStages() || !TestCommonAccessBarriers() ||
      !TestConstantBufferBarrierStages(true) ||
      !TestCommonAccessBarriers(true) ||
      !TestCommonAccessBarriers(true, true) || !TestMultipleFenceWaits() ||
      !TestDeviceRemovalWaits() || !TestConcurrentRemovalListeners()) {
    device->Release();
    return 1;
  }

  for (unsigned mode = 0; mode <= 6; mode++) {
    if (!TestInFlightRecording(mode)) {
      device->Release();
      return 1;
    }
  }

  for (unsigned mode = 0; mode < 22; mode++) {
    if (!TestTransitionChain(mode)) {
      device->Release();
      return 1;
    }
  }

  for (unsigned mode = 0; mode <= 18; ++mode) {
    if (!TestQueryRecording(device, mode)) {
      device->Release();
      return 1;
    }
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

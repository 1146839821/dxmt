#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <utility>

namespace {
constexpr UINT TileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT A = 0x12345678, B = 0x76543210, Written = 0xabcdef01;

template <typename T> struct Owned {
  T *ptr = nullptr;
  ~Owned() { if (ptr) ptr->Release(); }
  T *operator->() const { return ptr; }
  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
};
void Check(const char *operation, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << operation << ": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    throw std::runtime_error(operation);
  }
}
D3D12_HEAP_PROPERTIES Properties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES p = {};
  p.Type = type; p.CreationNodeMask = p.VisibleNodeMask = 1;
  return p;
}
D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC d = {};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes;
  d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
  return d;
}
void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b = {};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list->ResourceBarrier(1, &b);
}
std::vector<char> ReadShader(const char *path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f || f.tellg() <= 0) throw std::runtime_error("cannot open shader");
  std::vector<char> data(static_cast<size_t>(f.tellg()));
  f.seekg(0); f.read(data.data(), data.size());
  if (!f) throw std::runtime_error("cannot read shader");
  return data;
}
void Wait(ID3D12Fence *fence, UINT64 value) {
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event) throw std::runtime_error("CreateEvent");
  HRESULT hr = fence->SetEventOnCompletion(value, event);
  DWORD status =
      SUCCEEDED(hr) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event);
  if (status != WAIT_OBJECT_0) {
    // Do not destroy allocations still referenced by GPU work after a timeout.
    std::cerr << "GPU completion failed or timed out at " << value << std::endl;
    ExitProcess(1);
  }
}
void CheckSparseQueueAdmission(ID3D12Device *device) {
  if (!GetModuleHandleA("winemetal.dll"))
    throw std::runtime_error("fault probe requires DXMT");
  constexpr const char *variable = "DXMT_TEST_SPARSE_QUEUE_INIT_FAILURE";
  struct RestoreEnvironment {
    const char *name;
    char saved[128] = {};
    DWORD length;
    RestoreEnvironment(const char *n)
        : name(n), length(GetEnvironmentVariableA(n, saved, sizeof(saved))) {
      if (length >= sizeof(saved))
        throw std::runtime_error("fault setting too long");
    }
    ~RestoreEnvironment() {
      SetEnvironmentVariableA(name, length ? saved : nullptr);
    }
  } restore(variable);
  Owned<ID3D12Device9> device9;
  Check("device9", device->QueryInterface(IID_PPV_ARGS(&device9.ptr)));
  for (auto type :
       {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE,
        D3D12_COMMAND_LIST_TYPE_COPY}) {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    for (const char *missing : {"queue", "event", "both"}) {
      for (bool version1 : {false, true}) {
        if (!SetEnvironmentVariableA(variable, missing))
          throw std::runtime_error("set failure hook");
        auto *queue = reinterpret_cast<ID3D12CommandQueue *>(uintptr_t(1));
        HRESULT hr = version1 ? device9->CreateCommandQueue1(
                                    &desc, __uuidof(ID3D12Device),
                                    __uuidof(ID3D12CommandQueue),
                                    reinterpret_cast<void **>(&queue))
                              : device->CreateCommandQueue(
                                    &desc, __uuidof(ID3D12CommandQueue),
                                    reinterpret_cast<void **>(&queue));
        if (!SetEnvironmentVariableA(variable, nullptr))
          throw std::runtime_error("clear failure hook");
        if (hr != E_OUTOFMEMORY || queue) {
          if (queue &&
              queue != reinterpret_cast<ID3D12CommandQueue *>(uintptr_t(1)))
            queue->Release();
          throw std::runtime_error(
              "missing sparse prerequisite was not rejected");
        }
        Check("device healthy after failed creation",
              device->GetDeviceRemovedReason());
        Owned<ID3D12CommandQueue> recovered;
        Check("queue creation recovery",
              version1
                  ? device9->CreateCommandQueue1(&desc, __uuidof(ID3D12Device),
                                                 IID_PPV_ARGS(&recovered.ptr))
                  : device->CreateCommandQueue(&desc,
                                               IID_PPV_ARGS(&recovered.ptr)));
        Owned<ID3D12Fence> completed;
        Check("recovery fence",
              device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                  IID_PPV_ARGS(&completed.ptr)));
        Check("recovered queue signal", recovered->Signal(completed.ptr, 1));
        Wait(completed.ptr, 1);
        std::cout << "SPARSE_QUEUE_ADMISSION type=" << type
                  << " missing=" << missing << " api=" << (version1 ? 1 : 0)
                  << " rejected=E_OUTOFMEMORY output=NULL recovery=PASS\n";
      }
    }
  }
}
UINT Pattern(UINT seed, UINT page, UINT index) {
  const UINT base = page ? B : A;
  if (index == 0 || index == TileBytes / 4 - 1)
    return base ^ seed;
  if (index == TileBytes / 8)
    return 0;
  return base ^ (seed * 0x9e3779b9u) ^ (index * 0x85ebca6bu);
}
void LogRuntimeIdentity() {
  for (auto [dll, symbol] :
       {std::pair{"d3d12.dll", "D3D12CreateDevice"},
        std::pair{"winemetal.dll", "DXMTMSCGetCapabilities"},
        std::pair{"winemetal.dll",
                  "SparseMappingQueue_tryBarrierBeforeResourceState"},
        std::pair{"winemetal.dll",
                  "SparseMappingQueue_tryUpdateBufferMappings"},
        std::pair{"winemetal.dll",
                  "SparseMappingQueue_tryCopyBufferMappings"}}) {
    auto module = GetModuleHandleA(dll);
    auto code =
        module
            ? reinterpret_cast<const uint8_t *>(GetProcAddress(module, symbol))
            : nullptr;
    if (!module && !std::strcmp(dll, "winemetal.dll"))
      continue; // Windows native reference.
    if (!code)
      throw std::runtime_error("identity export missing");
    char path[32768] = {};
    if (!GetModuleFileNameA(module, path, sizeof(path)))
      throw std::runtime_error("module path missing");
    std::cout << "LOADED_MODULE " << dll << " " << path << "\n";
    std::printf("LOADED_CODE %s %s ", dll, symbol);
    for (UINT i = 0; i < 16; ++i)
      std::printf("%02x", code[i]);
    std::printf("\n");
  }
}
int CheckSparseMappingFailure(const char *mode) {
  const bool self_copy = !std::strcmp(mode, "buffer-self") ||
                         !std::strcmp(mode, "buffer-self-null") ||
                         !std::strcmp(mode, "buffer-self-mixed") ||
                         !std::strcmp(mode, "buffer-self-multiheap");
  const bool self_null = !std::strcmp(mode, "buffer-self-null");
  const bool self_mixed = !std::strcmp(mode, "buffer-self-mixed");
  const bool self_multiheap = !std::strcmp(mode, "buffer-self-multiheap");
  const UINT setup_calls = self_multiheap ? 2 : 1;
  const bool buffer_update =
      !std::strcmp(mode, "buffer-update") || !std::strcmp(mode, "buffer-unmap");
  const bool buffer_copy = !std::strcmp(mode, "buffer-copy") ||
                           !std::strcmp(mode, "buffer-copy-null");
  const bool copy_null = !std::strcmp(mode, "buffer-copy-null");
  const bool unmap = !std::strcmp(mode, "buffer-unmap");
  const bool copy =
      !std::strcmp(mode, "barrier-copy") || buffer_copy || self_copy;
  Owned<ID3D12Device> device;
  Check("device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&device.ptr)));
  LogRuntimeIdentity();
  if (!GetModuleHandleA("winemetal.dll"))
    throw std::runtime_error("barrier fault requires DXMT");
  char at[16] = {};
  DWORD length = GetEnvironmentVariableA(
      buffer_copy                  ? "DXMT_TEST_SPARSE_BUFFER_COPY_FAIL_AT"
      : buffer_update || self_copy ? "DXMT_TEST_SPARSE_BUFFER_UPDATE_FAIL_AT"
                                   : "DXMT_TEST_SPARSE_BARRIER_FAIL_AT",
      at, sizeof(at));
  if (length >= sizeof(at))
    throw std::runtime_error("invalid failure call");
  const UINT native_call = length ? std::stoul(at) : 1;
  if (self_copy
          ? native_call != setup_calls + 1 && native_call != setup_calls + 2
          : native_call != 1 && native_call != 2)
    throw std::runtime_error("failure call outside tested operation");
  const bool prior = !self_copy && native_call == 2;
  Owned<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC q = {};
  Check("fault queue",
        device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue.ptr)));
  Owned<ID3D12Heap> heap, other_heap;
  D3D12_HEAP_DESC hd = {};
  hd.SizeInBytes = (buffer_copy || self_copy ? 4 : 2) * TileBytes;
  hd.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  Check("fault heap", device->CreateHeap(&hd, IID_PPV_ARGS(&heap.ptr)));
  if (self_multiheap)
    Check("second fault heap",
          device->CreateHeap(&hd, IID_PPV_ARGS(&other_heap.ptr)));
  Owned<ID3D12Resource> source, destination;
  auto desc = BufferDesc((self_copy ? 3 : 2) * TileBytes);
  Check("fault source",
        device->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_COMMON,
                                       nullptr, IID_PPV_ARGS(&source.ptr)));
  Check("fault destination", device->CreateReservedResource(
                                 &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                 IID_PPV_ARGS(&destination.ptr)));
  if (!source->GetGPUVirtualAddress() || !destination->GetGPUVirtualAddress())
    return 77;
  Owned<ID3D12Fence> fence;
  Check("fault fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS(&fence.ptr)));
  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  D3D12_TILE_REGION_SIZE region = {};
  region.NumTiles = 1;
  UINT offset = 0;
  const UINT64 base_value = buffer_update || buffer_copy || self_copy ? 1 : 0;
  if (buffer_update) {
    // Neither a rejected flag nor an all-SKIP request may consume the first
    // nonempty update failure or remove an otherwise healthy device.
    queue->UpdateTileMappings(
        source.ptr, 1, &coordinate, &region, heap.ptr, 1, nullptr, &offset,
        nullptr, static_cast<D3D12_TILE_MAPPING_FLAGS>(0x80000000u));
    Check("invalid update stayed healthy", device->GetDeviceRemovedReason());
    D3D12_TILE_RANGE_FLAGS skip = D3D12_TILE_RANGE_FLAG_SKIP;
    queue->UpdateTileMappings(source.ptr, 1, &coordinate, &region, nullptr, 1,
                              &skip, nullptr, nullptr,
                              D3D12_TILE_MAPPING_FLAG_NONE);
    Check("SKIP update stayed healthy", device->GetDeviceRemovedReason());
    Check("preflight signal", queue->Signal(fence.ptr, base_value));
    Wait(fence.ptr, base_value);
    std::cout << "BUFFER_UPDATE_PREFLIGHT invalid=healthy skip=healthy\n";
    region.NumTiles = 2;
  }
  if (buffer_copy) {
    region.NumTiles = 2;
    queue->CopyTileMappings(destination.ptr, &coordinate, source.ptr,
                            &coordinate, &region,
                            static_cast<D3D12_TILE_MAPPING_FLAGS>(0x80000000u));
    Check("invalid copy stayed healthy", device->GetDeviceRemovedReason());
    if (!copy_null) {
      queue->UpdateTileMappings(source.ptr, 1, &coordinate, &region, heap.ptr,
                                1, nullptr, &offset, nullptr,
                                D3D12_TILE_MAPPING_FLAG_NONE);
    }
    UINT destination_offset = 2;
    queue->UpdateTileMappings(destination.ptr, 1, &coordinate, &region,
                              heap.ptr, 1, nullptr, &destination_offset,
                              nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
    Check("copy setup stayed healthy", device->GetDeviceRemovedReason());
    Check("copy preflight signal", queue->Signal(fence.ptr, base_value));
    Wait(fence.ptr, base_value);
    std::cout << "BUFFER_COPY_PREFLIGHT invalid=healthy source="
              << (copy_null ? "NULL" : "mapped") << "\n";
  }
  if (self_copy) {
    region.NumTiles = 2;
    queue->CopyTileMappings(source.ptr, &coordinate, source.ptr, &coordinate,
                            &region,
                            static_cast<D3D12_TILE_MAPPING_FLAGS>(0x80000000u));
    Check("invalid self-copy stayed healthy", device->GetDeviceRemovedReason());
    D3D12_TILE_REGION_SIZE all = {};
    all.NumTiles = 3;
    D3D12_TILE_RANGE_FLAGS flags[3] = {D3D12_TILE_RANGE_FLAG_NONE,
                                       D3D12_TILE_RANGE_FLAG_NULL,
                                       D3D12_TILE_RANGE_FLAG_NONE};
    UINT offsets[3] = {0, 1, 2}, counts[3] = {1, 1, 1};
    D3D12_TILE_RANGE_FLAGS null_flag = D3D12_TILE_RANGE_FLAG_NULL;
    queue->UpdateTileMappings(
        source.ptr, 1, &coordinate, &all, self_null ? nullptr : heap.ptr,
        self_mixed ? 3 : 1,
        self_null    ? &null_flag
        : self_mixed ? flags
                     : nullptr,
        offsets, self_mixed ? counts : nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
    if (self_multiheap) {
      auto one = coordinate;
      one.X = 1;
      D3D12_TILE_REGION_SIZE single = {};
      single.NumTiles = 1;
      queue->UpdateTileMappings(source.ptr, 1, &one, &single, other_heap.ptr, 1,
                                nullptr, &offset, nullptr,
                                D3D12_TILE_MAPPING_FLAG_NONE);
    }
    Check("self-copy setup healthy", device->GetDeviceRemovedReason());
    Check("self-copy setup signal", queue->Signal(fence.ptr, base_value));
    Wait(fence.ptr, base_value);
    std::cout << "SELF_COPY_PREFLIGHT invalid=healthy setup_calls="
              << setup_calls << "\n";
  }
  if (prior) {
    if (buffer_copy)
      queue->CopyTileMappings(destination.ptr, &coordinate, source.ptr,
                              &coordinate, &region,
                              D3D12_TILE_MAPPING_FLAG_NONE);
    else
      queue->UpdateTileMappings(source.ptr, 1, &coordinate, &region, heap.ptr,
                                1, nullptr, &offset, nullptr,
                                D3D12_TILE_MAPPING_FLAG_NONE);
    Check("healthy prior mapping", device->GetDeviceRemovedReason());
    Check("prior signal", queue->Signal(fence.ptr, base_value + 1));
    Wait(fence.ptr, base_value + 1);
  }
  struct PendingEvent {
    HANDLE handle = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    ~PendingEvent() {
      if (handle)
        CloseHandle(handle);
    }
  } event;
  if (!event.handle)
    throw std::runtime_error("pending removal event");
  Check("pending removal fence",
        fence->SetEventOnCompletion(base_value + 2, event.handle));
  if (copy) {
    auto target = coordinate;
    if (self_copy)
      target.X = 1;
    queue->CopyTileMappings(self_copy ? source.ptr : destination.ptr, &target,
                            source.ptr, &coordinate, &region,
                            D3D12_TILE_MAPPING_FLAG_NONE);
  } else {
    offset = 1;
    D3D12_TILE_RANGE_FLAGS flags =
        unmap ? D3D12_TILE_RANGE_FLAG_NULL
              : D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
    queue->UpdateTileMappings(source.ptr, 1, &coordinate, &region,
                              unmap ? nullptr : heap.ptr, 1,
                              buffer_update ? &flags : nullptr, &offset,
                              nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
  }
  char phase[16] = {};
  if (buffer_copy || buffer_update)
    GetEnvironmentVariableA(buffer_copy
                                ? "DXMT_TEST_SPARSE_BUFFER_COPY_FAILURE"
                                : "DXMT_TEST_SPARSE_BUFFER_UPDATE_FAILURE",
                            phase, sizeof(phase));
  const HRESULT expected_reason =
      (buffer_copy || buffer_update) && (!std::strcmp(phase, "prepare") ||
                                         !std::strcmp(phase, "snapshot"))
          ? E_OUTOFMEMORY
          : DXGI_ERROR_DEVICE_REMOVED;
  if (device->GetDeviceRemovedReason() != expected_reason) {
    // A control may have issued a valid mapping after a real barrier. Drain
    // it before reporting failure and releasing resources used by the GPU.
    if (SUCCEEDED(device->GetDeviceRemovedReason())) {
      Check("unexpected healthy queue drain",
            queue->Signal(fence.ptr, base_value + 3));
      Wait(fence.ptr, base_value + 3);
    }
    throw std::runtime_error(
        "sparse mapping failure was not reported as device removal");
  }
  if (WaitForSingleObject(event.handle, 5000) != WAIT_OBJECT_0 ||
      fence->GetCompletedValue() != UINT64_MAX)
    throw std::runtime_error("barrier failure stranded pending fence");
  if (queue->Signal(fence.ptr, base_value + 2) != DXGI_ERROR_DEVICE_REMOVED)
    throw std::runtime_error("barrier failure allowed later submission");
  if (self_copy) {
    std::cout << "SPARSE_BUFFER_SELF_COPY_FAILURE mode=" << mode
              << " tile=" << native_call - setup_calls
              << " native_call=" << native_call
              << " removed=PASS fence=PASS signal_rejected=PASS\n";
    return 0;
  }
  std::cout << (buffer_copy     ? "SPARSE_BUFFER_COPY_FAILURE api="
                : buffer_update ? "SPARSE_BUFFER_UPDATE_FAILURE api="
                                : "SPARSE_BARRIER_FAILURE api=")
            << (copy_null ? "null"
                : unmap   ? "unmap"
                : copy    ? "copy"
                          : "update")
            << " call=" << (prior ? 2 : 1)
            << " removed=PASS fence=PASS signal_rejected=PASS\n";
  return 0;
}
int Run(const char *read_path, const char *write_path, UINT seed,
        bool wrong_oracle, bool queue_failure) {
  auto read_shader = ReadShader(read_path),
       write_shader = ReadShader(write_path);
  Owned<ID3D12Device> device;
  Check("device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&device.ptr)));
  LogRuntimeIdentity();
  Owned<ID3D12CommandQueue> direct, compute;
  D3D12_COMMAND_QUEUE_DESC q = {};
  q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Check("direct queue",
        device->CreateCommandQueue(&q, IID_PPV_ARGS(&direct.ptr)));
  q.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  Check("compute queue",
        device->CreateCommandQueue(&q, IID_PPV_ARGS(&compute.ptr)));
  Owned<ID3D12Fence> mapping_done, compute_done, gate;
  Check("mapping fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(&mapping_done.ptr)));
  Check("compute fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(&compute_done.ptr)));
  Check("gate fence",
        device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate.ptr)));
  Owned<ID3D12Heap> heap;
  D3D12_HEAP_DESC hd = {};
  hd.SizeInBytes = 2 * TileBytes;
  hd.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  Check("tile heap", device->CreateHeap(&hd, IID_PPV_ARGS(&heap.ptr)));
  const ULONG heap_public_ref_after_create = heap->AddRef();
  heap->Release();
  Owned<ID3D12Resource> sparse, upload, output, readback;
  auto d =
      BufferDesc(2 * TileBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  Check("reserved buffer",
        device->CreateReservedResource(&d, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                       IID_PPV_ARGS(&sparse.ptr)));
  if (!sparse->GetGPUVirtualAddress()) {
    std::cout << "SKIP: reserved buffer has no shader-visible backing\n";
    return 77;
  }
  const auto stable_va = sparse->GetGPUVirtualAddress();
  if (queue_failure) CheckSparseQueueAdmission(device.ptr);
  auto create = [&](Owned<ID3D12Resource> &r, UINT bytes, D3D12_HEAP_TYPE type,
                    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    auto props = Properties(type);
    auto desc = BufferDesc(bytes, flags);
    Check("committed buffer", device->CreateCommittedResource(
                                  &props, D3D12_HEAP_FLAG_NONE, &desc, state,
                                  nullptr, IID_PPV_ARGS(&r.ptr)));
  };
  create(upload, 2 * TileBytes, D3D12_HEAP_TYPE_UPLOAD,
         D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
  create(output, TileBytes, D3D12_HEAP_TYPE_DEFAULT,
         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
         D3D12_RESOURCE_STATE_COMMON);
  create(readback, 2 * TileBytes, D3D12_HEAP_TYPE_READBACK,
         D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  UINT *data = nullptr;
  Check("upload map", upload->Map(0, nullptr, reinterpret_cast<void **>(&data)));
  for (UINT i = 0; i < 2 * TileBytes / sizeof(UINT); ++i) data[i] = Pattern(seed, i / (TileBytes / 4), i % (TileBytes / 4));
  upload->Unmap(0, nullptr);

  // Create all descriptors before mapping. Never rewrite them during any phase.
  Owned<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC dh = {};
  dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; dh.NumDescriptors = 3;
  dh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check("descriptor heap", device->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&descriptors.ptr)));
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
  const UINT stride = device->GetDescriptorHandleIncrementSize(dh.Type);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = TileBytes / 4; srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device->CreateShaderResourceView(sparse.ptr, &srv, cpu);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = TileBytes / 4; uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  cpu.ptr += stride; device->CreateUnorderedAccessView(output.ptr, nullptr, &uav, cpu);
  uav.Buffer.NumElements = TileBytes / 4;
  cpu.ptr += stride; device->CreateUnorderedAccessView(sparse.ptr, nullptr, &uav, cpu);
  D3D12_DESCRIPTOR_RANGE ranges[2] = {
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 1}};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  D3D12_ROOT_SIGNATURE_DESC rs = {}; rs.NumParameters = 1; rs.pParameters = &parameter;
  Owned<ID3DBlob> blob, errors;
  Check("serialize root signature", D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob.ptr, &errors.ptr));
  Owned<ID3D12RootSignature> root;
  Check("root signature", device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root.ptr)));
  Owned<ID3D12PipelineState> read_pso, write_pso;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = root.ptr;
  pd.CS = {read_shader.data(), read_shader.size()};
  Check("read PSO", device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&read_pso.ptr)));
  pd.CS = {write_shader.data(), write_shader.size()};
  Check("write PSO", device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&write_pso.ptr)));
  Owned<ID3D12CommandAllocator> init_allocator;
  Owned<ID3D12GraphicsCommandList> init;
  Check("init allocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&init_allocator.ptr)));
  Check("init list", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, init_allocator.ptr, nullptr, IID_PPV_ARGS(&init.ptr)));
  D3D12_TILED_RESOURCE_COORDINATE coord = {};
  D3D12_TILE_REGION_SIZE region = {}; region.NumTiles = 2;
  UINT offset = 0;
  direct->UpdateTileMappings(sparse.ptr, 1, &coord, &region, heap.ptr, 1, nullptr, &offset, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
  const ULONG heap_public_ref_after_mapping = heap->AddRef();
  heap->Release();
  if (heap_public_ref_after_mapping != heap_public_ref_after_create) {
    std::cerr << "reserved buffer mapping changed the heap public ref count from "
              << heap_public_ref_after_create - 1 << " to " << heap_public_ref_after_mapping - 1 << "\n";
    throw std::runtime_error("reserved buffer mapping retained a public heap reference");
  }
  std::cout << "reserved buffer mapping kept heap public ref count at "
            << heap_public_ref_after_mapping - 1 << "\n";
  Transition(init.ptr, sparse.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
  init->CopyTiles(sparse.ptr, &coord, &region, upload.ptr, 0, D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  Transition(init.ptr, sparse.ptr, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
  Check("init close", init->Close());
  ID3D12CommandList *initial_lists[] = {init.ptr};
  direct->ExecuteCommandLists(1, initial_lists);
  Check("init signal", direct->Signal(mapping_done.ptr, 1));
  UINT64 serial = 1;
  region.NumTiles = 1;
  bool written[2] = {};
  UINT64 words_checked = 0;
  auto phase = [&](const char *name, int tile, bool write, UINT expected,
                   bool delayed = false) {
    if (serial > 1)
      Check("direct waits previous compute",
            direct->Wait(compute_done.ptr, serial - 1));
    if (delayed)
      Check("hold mapping queue", direct->Wait(gate.ptr, 1));
    D3D12_TILE_RANGE_FLAGS flags = tile < 0 ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE;
    UINT heap_tile = tile < 0 ? 0 : static_cast<UINT>(tile);
    direct->UpdateTileMappings(sparse.ptr, 1, &coord, &region, tile < 0 ? nullptr : heap.ptr,
                              1, &flags, &heap_tile, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
    Check("mapping signal", direct->Signal(mapping_done.ptr, serial + 1));
    Check("compute waits mapping", compute->Wait(mapping_done.ptr, serial + 1));
    // Use a fresh allocator/list for every phase.  The queue fence signals
    // submission ordering, while DXMT retires command allocators on the
    // Metal command-buffer completion callback; reusing one allocator here
    // can race that callback and produce a spurious E_FAIL from Reset().
    Owned<ID3D12CommandAllocator> phase_allocator;
    Owned<ID3D12GraphicsCommandList> phase_list;
    Check("phase allocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                                              IID_PPV_ARGS(&phase_allocator.ptr)));
    Check("phase list", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                                    phase_allocator.ptr,
                                                    write ? write_pso.ptr : read_pso.ptr,
                                                    IID_PPV_ARGS(&phase_list.ptr)));
    phase_list->SetComputeRootSignature(root.ptr);
    ID3D12DescriptorHeap *heaps[] = {descriptors.ptr};
    phase_list->SetDescriptorHeaps(1, heaps);
    phase_list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
    const auto access = write ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Transition(phase_list.ptr, sparse.ptr, D3D12_RESOURCE_STATE_COMMON, access);
    if (!write)
      Transition(phase_list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON,
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    phase_list->Dispatch(write ? 1 : TileBytes / (4 * 64), 1, 1);
    if (!write) {
      Transition(phase_list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      phase_list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, TileBytes);
      Transition(phase_list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    }
    if (tile >= 0) {
      Transition(phase_list.ptr, sparse.ptr, access, D3D12_RESOURCE_STATE_COPY_SOURCE);
      phase_list->CopyTiles(sparse.ptr, &coord, &region, readback.ptr, TileBytes, D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
      if (write)
        written[tile] = true;
    }
    Transition(phase_list.ptr, sparse.ptr,
               tile >= 0 ? D3D12_RESOURCE_STATE_COPY_SOURCE : access,
               D3D12_RESOURCE_STATE_COMMON);
    Check("phase close", phase_list->Close());
    ID3D12CommandList *lists[] = {phase_list.ptr};
    compute->ExecuteCommandLists(1, lists);
    Check("compute signal", compute->Signal(compute_done.ptr, serial));
    if (delayed)
      Check("release mapping queue", gate->Signal(1));
    Wait(compute_done.ptr, serial++);
    if (!write || tile >= 0) {
      Check("readback map",
            readback->Map(0, nullptr, reinterpret_cast<void **>(&data)));
      bool mismatch = false;
      if (!write) {
        for (UINT i = 0; i < TileBytes / 4; ++i) {
          UINT wanted = tile < 0 ? 0
                        : written[tile] && (i == 0 || i == TileBytes / 4 - 1)
                            ? Written
                            : Pattern(seed, tile, i);
          ++wanted; // Shader deliberately adds one, including for NULL and
                    // mapped zero.
          if (i == 0 && wanted != expected)
            throw std::runtime_error("invalid test expectation");
          if (data[i] != wanted) {
            if (!mismatch)
              std::cerr << name << " shader word=" << i
                        << " expected=" << wanted << " got=" << data[i] << "\n";
            mismatch = true;
          }
          ++words_checked;
        }
      }
      if (tile >= 0) {
        for (UINT i = 0; i < TileBytes / 4; ++i) {
          UINT wanted = written[tile] && (i == 0 || i == TileBytes / 4 - 1)
                            ? Written
                            : Pattern(seed, tile, i);
          if (wrong_oracle && serial == 2 && i == 0)
            wanted ^= 1;
          if (data[TileBytes / 4 + i] != wanted) {
            if (!mismatch)
              std::cerr << name << " word=" << i << " expected=" << wanted
                        << " got=" << data[TileBytes / 4 + i] << "\n";
            mismatch = true;
          }
          ++words_checked;
        }
      }
      readback->Unmap(0, nullptr);
      if (mismatch)
        throw std::runtime_error("shader/full-page result mismatch");
    }
    // Check the first result before replay so a later successful submission
    // cannot hide an earlier failure. Keep the closed list and allocator alive.
    if (serial == 2) {
      Owned<ID3D12Fence> replay_done;
      Check("replay fence",
            device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                IID_PPV_ARGS(&replay_done.ptr)));
      compute->ExecuteCommandLists(1, lists);
      Check("replay signal", compute->Signal(replay_done.ptr, 1));
      Wait(replay_done.ptr, 1);
      Check("replay map",
            readback->Map(0, nullptr, reinterpret_cast<void **>(&data)));
      bool mismatch = false;
      for (UINT i = 0; i < TileBytes / 4; ++i) {
        UINT wanted = Pattern(seed, 0, i);
        mismatch |= data[i] != wanted + 1 || data[TileBytes / 4 + i] != wanted;
        words_checked += 2;
      }
      readback->Unmap(0, nullptr);
      if (mismatch)
        throw std::runtime_error("closed-list replay mismatch");
      std::cout << "closed-list replay passed\n";
    }
    if (sparse->GetGPUVirtualAddress() != stable_va)
      throw std::runtime_error("VA changed on remap");
    std::cout << name << " passed\n";
  };
  phase("nonzero backing A / descriptors before map", 0, false, (A ^ seed) + 1);
  phase("remap A to B / delayed direct-to-compute wait", 1, false,
        (B ^ seed) + 1, true);
  phase("remap back to A preserves contents", 0, false, (A ^ seed) + 1);
  phase("NULL SRV reads zero", -1, false, 1);
  phase("NULL UAV write completes", -1, true, 0);
  phase("NULL SRV remains zero after write", -1, false, 1);
  phase("NULL write leaves backing A intact", 0, false, (A ^ seed) + 1);
  phase("NULL write leaves backing B intact", 1, false, (B ^ seed) + 1);
  phase("mapped UAV write and CopyTiles readback", 1, true, Written);
  phase("SRV sees mapped UAV write", 1, false, Written + 1);
  phase("mapped write leaves other backing intact", 0, false, (A ^ seed) + 1);
  std::cout << "Reserved buffer shader, remap, NULL and cross-queue tests "
               "passed seed="
            << seed << " words=" << words_checked << "\n";
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 3 && argc != 5 && argc != 6) {
    std::cerr << "usage: dx12_reserved_buffer <probe_read.cso> "
                 "<probe_write.cso> [seed rounds "
                 "[wrong-oracle|queue-init-failure|barrier-update|barrier-copy|"
                 "buffer-update|buffer-unmap|buffer-copy|buffer-copy-null|"
                 "buffer-self|buffer-self-null|buffer-self-mixed|buffer-self-"
                 "multiheap]]\n";
    return 2;
  }
  try {
    const UINT seed = argc >= 5 ? std::stoul(argv[3]) : 0;
    const UINT rounds = argc >= 5 ? std::stoul(argv[4]) : 1;
    if (rounds == 0 || rounds > 64 ||
        (argc == 6 && std::strcmp(argv[5], "wrong-oracle") &&
         std::strcmp(argv[5], "queue-init-failure") &&
         std::strcmp(argv[5], "barrier-update") &&
         std::strcmp(argv[5], "barrier-copy") &&
         std::strcmp(argv[5], "buffer-update") &&
         std::strcmp(argv[5], "buffer-unmap") &&
         std::strcmp(argv[5], "buffer-copy") &&
         std::strcmp(argv[5], "buffer-copy-null") &&
         std::strcmp(argv[5], "buffer-self") &&
         std::strcmp(argv[5], "buffer-self-null") &&
         std::strcmp(argv[5], "buffer-self-mixed") &&
         std::strcmp(argv[5], "buffer-self-multiheap")))
      return 2;
    if (argc == 6 && (!std::strcmp(argv[5], "barrier-update") ||
                      !std::strcmp(argv[5], "barrier-copy") ||
                      !std::strcmp(argv[5], "buffer-update") ||
                      !std::strcmp(argv[5], "buffer-unmap") ||
                      !std::strcmp(argv[5], "buffer-copy") ||
                      !std::strcmp(argv[5], "buffer-copy-null") ||
                      !std::strcmp(argv[5], "buffer-self") ||
                      !std::strcmp(argv[5], "buffer-self-null") ||
                      !std::strcmp(argv[5], "buffer-self-mixed") ||
                      !std::strcmp(argv[5], "buffer-self-multiheap"))) {
      if (rounds != 1)
        return 2; // Fault counters and removal are process-local.
      return CheckSparseMappingFailure(argv[5]);
    }
    for (UINT round = 0; round < rounds; ++round) {
      int result =
          Run(argv[1], argv[2], seed + round,
              argc == 6 && !std::strcmp(argv[5], "wrong-oracle"),
              argc == 6 && !std::strcmp(argv[5], "queue-init-failure"));
      if (result)
        return result;
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}

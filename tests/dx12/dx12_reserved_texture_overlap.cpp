#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstring>
#include <cstdio>
#include <functional>
#include <fstream>
#include <vector>
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <memory>
#include <cstdlib>
#include "../../src/d3d12/d3d12_device.hpp"

namespace {

constexpr UINT kTileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT kValue = 0x12345678;

UINT TileWord(UINT value, UINT word) { return value ^ (word * 0x9e3779b9u); }

template <typename T> struct Owned {
  T *ptr = nullptr;

  ~Owned() {
    if (ptr)
      ptr->Release();
  }

  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;

  T *operator->() const { return ptr; }
};

void Check(const char *operation, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << operation << ": 0x" << std::hex
              << static_cast<unsigned long>(hr) << std::dec << "\n";
    throw std::runtime_error(operation);
  }
}

D3D12_HEAP_PROPERTIES Properties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = type;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC BufferDescription(UINT64 bytes,
                                      D3D12_RESOURCE_FLAGS flags) {
  D3D12_RESOURCE_DESC description = {};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Width = bytes;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  description.Flags = flags;
  return description;
}

void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  list->ResourceBarrier(1, &barrier);
}

void Wait(ID3D12Fence *fence, UINT64 value) {
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    throw std::runtime_error("CreateEvent");
  Check("SetEventOnCompletion", fence->SetEventOnCompletion(value, event));
  const DWORD status = WaitForSingleObject(event, 30000);
  CloseHandle(event);
  if (status != WAIT_OBJECT_0)
    throw std::runtime_error("GPU completion timeout");
}

void CreateBuffer(ID3D12Device *device, Owned<ID3D12Resource> &resource,
                  UINT64 bytes, D3D12_HEAP_TYPE type,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
  const auto properties = Properties(type);
  const auto description = BufferDescription(bytes, flags);
  Check("CreateBuffer", device->CreateCommittedResource(
                            &properties, D3D12_HEAP_FLAG_NONE, &description,
                            state, nullptr, IID_PPV_ARGS(&resource.ptr)));
}

// Internal matched-build probe; ordinary modes retain delay-loaded WineMetal.
class TextureIntentSentinel final : public IUnknown {
  std::atomic<ULONG> refs_{1};
  std::atomic<UINT> &deaths_;

public:
  explicit TextureIntentSentinel(std::atomic<UINT> &deaths) : deaths_(deaths) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (iid != __uuidof(IUnknown))
      return E_NOINTERFACE;
    *out = this;
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto n = --refs_;
    if (!n) {
      ++deaths_;
      delete this;
    }
    return n;
  }
};

bool CheckTextureIntentOwnership(ID3D12Device *device, bool packed = false,
                                 bool credential = false) {
  using namespace dxmt;
  auto require = [](bool ok) {
    if (!ok)
      throw std::runtime_error("texture intent ownership");
  };
  std::atomic<UINT> deaths{0};
  const GUID key = {0x92143401,
                    0xe31e,
                    0x475a,
                    {0x88, 0x14, 0x29, 0x03, 0x77, 0x40, 0x19, 0x12}};
  auto attach = [&](ID3D12Object *obj) {
    auto sentinel = new TextureIntentSentinel(deaths);
    Check("attach intent sentinel",
          obj->SetPrivateDataInterface(key, sentinel));
    sentinel->Release();
  };
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 384;
  desc.Height = 128;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  if (packed) {
    desc.Width = 64;
    desc.Height = 64;
    desc.MipLevels = 4;
  }
  Owned<ID3D12Resource> source, target;
  Check("intent source",
        device->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_COMMON,
                                       nullptr, IID_PPV_ARGS(&source.ptr)));
  Check("intent target",
        device->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_COMMON,
                                       nullptr, IID_PPV_ARGS(&target.ptr)));
  UINT total = 0;
  D3D12_PACKED_MIP_INFO packed_info = {};
  device->GetResourceTiling(source.ptr, &total, &packed_info, nullptr, nullptr,
                            0, nullptr);
  require(total == (packed ? packed_info.NumTilesForPackedMips : 3) && total &&
          (!packed || packed_info.NumPackedMips));
  D3D12_HEAP_DESC hd = {};
  hd.SizeInBytes = 3ull * kTileBytes;
  hd.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  Owned<ID3D12Heap> heap;
  Check("intent heap", device->CreateHeap(&hd, IID_PPV_ARGS(&heap.ptr)));
  attach(source.ptr);
  attach(target.ptr);
  attach(heap.ptr);
  auto src = static_cast<MTLD3D12Resource *>(source.ptr),
       dst = static_cast<MTLD3D12Resource *>(target.ptr);
  auto expected = static_cast<MTLD3D12Heap *>(heap.ptr)->GetTileBackingBuffer();
  std::unique_ptr<D3D12SparseTextureMappingPlan> update, copy;
  std::atomic<UINT> old_deaths{0};
  bool old_target_mapping = false;
  WMT::Reference<WMT::SparseMappingQueue> native_queue;
  if (credential) {
    auto owner = static_cast<MTLD3D12Device *>(device);
    native_queue = owner->GetMTLDevice().newSparseMappingQueue();
    auto residency = owner->GetGlobalResidencySet();
    residency.commit();
    native_queue.addResidencySet(residency);
    static_assert(sizeof(WMTSparseTextureBatchOperation) == 144);
    static_assert(offsetof(WMTSparseTextureBatchOperation, region) == 32);
    static_assert(offsetof(WMTSparseTextureBatchOperation, heap_offset) == 136);
    WMTSparseTextureBatchOperation invalid = {};
    require(!SparseMappingQueue_prepareTextureBatch(0, nullptr, 0) &&
            !SparseMappingQueue_prepareTextureBatch(native_queue.handle,
                                                    nullptr, 1) &&
            !native_queue.prepareTextureBatch(&invalid, 1));
    invalid.destination = src->texture->current()->texture().handle;
    invalid.region.size = {1, 1, 1};
    invalid.mode = WMTSparseTextureMappingModeMap;
    require(!native_queue.prepareTextureBatch(&invalid, 1)); // map without heap
    invalid.mode = WMTSparseTextureMappingModeUnmap;
    invalid.heap_offset = 1;
    require(!native_queue.prepareTextureBatch(&invalid, 1));
    invalid.heap_offset = 0;
    invalid.region.origin.x = UINT64_MAX;
    require(!native_queue.prepareTextureBatch(&invalid, 1));
    invalid.region.origin.x = 0;
    invalid.kind = 1;
    invalid.mode = 0;
    invalid.source = invalid.destination;
    require(!native_queue.prepareTextureBatch(&invalid, 1)); // unsafe self copy
    std::printf("TEXTURE_CREDENTIAL_ABI invalid=7 calls=0 PASS\n");
    char scenario[32] = {};
    if (!GetEnvironmentVariableA("DXMT_TEST_TEXTURE_CREDENTIAL_CASE", scenario,
                                 sizeof(scenario))) {
      Owned<ID3D12Heap> previous_heap;
      Check("previous target heap",
            device->CreateHeap(&hd, IID_PPV_ARGS(&previous_heap.ptr)));
      auto sentinel = new TextureIntentSentinel(old_deaths);
      Check("previous heap sentinel",
            previous_heap->SetPrivateDataInterface(key, sentinel));
      sentinel->Release();
      D3D12_TILED_RESOURCE_COORDINATE start = {};
      D3D12_TILE_REGION_SIZE size = {};
      size.NumTiles = total;
      const UINT heap_offset = 0;
      std::unique_ptr<D3D12SparseTextureMappingPlan> previous;
      Check("previous map intent",
            dst->PrepareTextureTileUpdate(
                1, &start, &size, previous_heap.ptr, 1, nullptr, &heap_offset,
                nullptr, D3D12_TILE_MAPPING_FLAG_NONE, previous));
      residency.commit();
      Check("previous map native", previous->PrepareNative(native_queue));
      require(previous->PrepareTerminal(1));
      auto before = native_queue.prepareBarrierBeforeResourceState();
      require(bool(before) && before.submit());
      for (size_t i = 0; i < previous->OperationCount(); ++i)
        require(previous->IssueNext());
      WMTSparseMappingResult result = {};
      require(previous->Finish(WMTSparseMappingHostAccepted) &&
              previous->Wait(10000, result) &&
              result.state == WMTSparseMappingGPUComplete &&
              previous->Retire(result));
      previous_heap.ptr->Release();
      previous_heap.ptr = nullptr;
      require(old_deaths == 0);
      old_target_mapping = true;
    }
  }
  auto check_failure = [&](HRESULT hr, const char *name) {
    char phase[16] = {};
    if (!GetEnvironmentVariableA(name, phase, sizeof(phase)) ||
        std::strcmp(phase, "intent"))
      return false;
    require(hr == E_OUTOFMEMORY);
    update.reset();
    copy.reset();
    require(!src->IsTileMapped(0) && !dst->IsTileMapped(0));
    source.ptr->Release();
    source.ptr = nullptr;
    target.ptr->Release();
    target.ptr = nullptr;
    heap.ptr->Release();
    heap.ptr = nullptr;
    require(deaths == 3);
    std::printf("TEXTURE_INTENT_PREPARE_END unchanged=1 oom=1 destroyed=3\n");
    return true;
  };
  {
    std::vector<D3D12_TILED_RESOURCE_COORDINATE> starts(1);
    std::vector<D3D12_TILE_REGION_SIZE> sizes(1);
    sizes[0].NumTiles = total;
    std::vector<UINT> offsets{0}, counts{total};
    std::printf("TEXTURE_INTENT_PREPARE_BEGIN ownership=1\n");
    HRESULT hr = src->PrepareTextureTileUpdate(
        1, starts.data(), sizes.data(), heap.ptr, 1, nullptr, offsets.data(),
        counts.data(), D3D12_TILE_MAPPING_FLAG_NONE, update);
    if (check_failure(hr, "DXMT_TEST_SPARSE_TEXTURE_UPDATE_FAILURE"))
      return true;
    Check("prepare update", hr);
    hr = dst->PrepareTextureTileCopy(src, starts.data(), starts.data(),
                                     sizes.data(), D3D12_TILE_MAPPING_FLAG_NONE,
                                     copy);
    if (check_failure(hr, "DXMT_TEST_SPARSE_TEXTURE_COPY_FAILURE"))
      return true;
    Check("prepare copy", hr);
    starts[0].X = 999;
    sizes[0].NumTiles = 0;
    offsets[0] = 999;
    counts[0] = 0;
  }
  for (UINT i = 0; i < total; ++i)
    require(!src->IsTileMapped(i) &&
            dst->IsTileMapped(i) == old_target_mapping);
  std::printf(
      "TEXTURE_INTENT_PREPARE_END unchanged=1 caller_arrays_destroyed=1\n");
  source.ptr->Release();
  source.ptr = nullptr;
  target.ptr->Release();
  target.ptr = nullptr;
  heap.ptr->Release();
  heap.ptr = nullptr;
  require(deaths == 0);
  if (credential) {
    char failure[32] = {};
    GetEnvironmentVariableA("DXMT_TEST_TEXTURE_CREDENTIAL_CASE", failure,
                            sizeof(failure));
    const bool reject = !std::strcmp(failure, "reject"),
               partial = !std::strcmp(failure, "partial"),
               abandon = !std::strcmp(failure, "abandon"),
               terminal_failure = !std::strcmp(failure, "terminal"),
               prepare_failure = !std::strcmp(failure, "prepare");
    const HRESULT native_hr = update->PrepareNative(native_queue);
    WMTSparseMappingResult result = {};
    if (prepare_failure) {
      require(native_hr == E_OUTOFMEMORY && !update->Wait(0, result));
      update.reset();
      require(deaths == 1);
      copy.reset();
      require(deaths == 3);
      std::printf(
          "TEXTURE_CREDENTIAL_FAILURE prepare=1 calls=0 destroyed=3 PASS\n");
      return true;
    }
    Check("owned native update", native_hr);
    if (reject || terminal_failure) {
      if (terminal_failure)
        require(!update->PrepareTerminal(1));
      require(update->Finish(WMTSparseMappingHostRejected) &&
              update->Wait(0, result) &&
              result.state == WMTSparseMappingNotIssued &&
              !result.possibly_issued_calls && !src->IsTileMapped(0) &&
              update->Retire(result));
      require(deaths == 1);
      copy.reset();
      require(deaths == 3);
      std::printf("TEXTURE_CREDENTIAL_FAILURE rejected=1 terminal_failure=%u "
                  "calls=0 destroyed=3 PASS\n",
                  terminal_failure);
      return true;
    }
    require(update->PrepareTerminal(3));
    auto before = native_queue.prepareBarrierBeforeResourceState();
    require(bool(before) && before.submit());
    if (partial || abandon) {
      require(update->IssueNext() && !src->IsTileMapped(0) &&
              !update->Retire(result));
      if (partial) {
        require(!update->Finish(WMTSparseMappingHostAccepted) &&
                update->Finish(WMTSparseMappingHostFailedAfterIssue));
        require(update->Wait(0, result) &&
                result.state == WMTSparseMappingSealedPending &&
                !update->Retire(result) && deaths == 0);
        require(update->Wait(10000, result) &&
                result.state == WMTSparseMappingGPUComplete &&
                result.host_outcome == WMTSparseMappingHostFailedAfterIssue &&
                result.possibly_issued_calls == 1 && !result.event_value &&
                !src->IsTileMapped(0) && update->Retire(result));
      }
      // Unsealed issued abandonment must drain actual completion.
      update.reset();
      require(deaths == 1);
      WMTSparseBarrierResult feedback = {};
      require(before.wait(10000, feedback) &&
              feedback.state == WMTSparseBarrierCompleted);
      copy.reset();
      require(deaths == 3);
      std::printf("TEXTURE_CREDENTIAL_FAILURE partial=%u abandon=%u calls=1 "
                  "destroyed=3 drained=1 PASS\n",
                  partial, abandon);
      return true;
    }
    for (size_t i = 0; i < update->OperationCount(); ++i)
      require(update->IssueNext());
    require(update->Finish(WMTSparseMappingHostAccepted) &&
            update->Wait(0, result) &&
            result.state == WMTSparseMappingSealedPending &&
            !update->Retire(result) && deaths == 0);
    require(update->Wait(10000, result) &&
            result.state == WMTSparseMappingGPUComplete && !result.event_value);
    // Exercise real texel contents for both standard and packed native batches,
    // separately from the logical ownership checks below.
    Owned<ID3D12CommandQueue> io_queue;
    D3D12_COMMAND_QUEUE_DESC io_desc = {};
    Check("owned IO queue",
          device->CreateCommandQueue(&io_desc, IID_PPV_ARGS(&io_queue.ptr)));
    Owned<ID3D12Fence> io_fence;
    Check("owned IO fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                IID_PPV_ARGS(&io_fence.ptr)));
    const UINT row_pitch = UINT(desc.Width) * sizeof(UINT);
    const UINT words = UINT(desc.Width) * desc.Height;
    Owned<ID3D12Resource> upload, readback;
    CreateBuffer(device, upload, UINT64(row_pitch) * desc.Height,
                 D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                 D3D12_RESOURCE_STATE_GENERIC_READ);
    CreateBuffer(device, readback, UINT64(row_pitch) * desc.Height,
                 D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                 D3D12_RESOURCE_STATE_COPY_DEST);
    UINT *data = nullptr;
    Check("owned upload map",
          upload->Map(0, nullptr, reinterpret_cast<void **>(&data)));
    char seed_text[16] = {};
    GetEnvironmentVariableA("DXMT_TEST_TEXTURE_INTENT_SEED", seed_text,
                            sizeof(seed_text));
    const UINT seed = UINT(std::strtoul(seed_text, nullptr, 0)) ^ 0x6a473921;
    for (UINT i = 0; i < words; ++i)
      data[i] = TileWord(seed, i);
    upload->Unmap(0, nullptr);
    auto transfer = [&](bool write) {
      Owned<ID3D12CommandAllocator> allocator;
      Owned<ID3D12GraphicsCommandList> list;
      Check("owned IO allocator",
            device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&allocator.ptr)));
      Check("owned IO list",
            device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      allocator.ptr, nullptr,
                                      IID_PPV_ARGS(&list.ptr)));
      auto resource = write ? src : dst;
      D3D12_RESOURCE_BARRIER alias = {};
      alias.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
      list->ResourceBarrier(1, &alias);
      const auto state = write ? D3D12_RESOURCE_STATE_COPY_DEST
                               : D3D12_RESOURCE_STATE_COPY_SOURCE;
      Transition(list.ptr, resource, D3D12_RESOURCE_STATE_COMMON, state);
      D3D12_TEXTURE_COPY_LOCATION tex = {}, linear = {};
      tex.pResource = resource;
      tex.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      linear.pResource = write ? upload.ptr : readback.ptr;
      linear.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      linear.PlacedFootprint.Footprint = {desc.Format, UINT(desc.Width),
                                          desc.Height, 1, row_pitch};
      if (write)
        list->CopyTextureRegion(&tex, 0, 0, 0, &linear, nullptr);
      else
        list->CopyTextureRegion(&linear, 0, 0, 0, &tex, nullptr);
      Transition(list.ptr, resource, state, D3D12_RESOURCE_STATE_COMMON);
      Check("owned IO close", list->Close());
      ID3D12CommandList *lists[] = {list.ptr};
      io_queue->ExecuteCommandLists(1, lists);
      Check("owned IO signal", io_queue->Signal(io_fence.ptr, write ? 1 : 2));
      Wait(io_fence.ptr, write ? 1 : 2);
    };
    transfer(true);
    Check("owned native copy", copy->PrepareNative(native_queue));
    require(copy->PrepareTerminal(3));
    for (size_t i = 0; i < copy->OperationCount(); ++i)
      require(copy->IssueNext());
    require(copy->Finish(WMTSparseMappingHostAccepted) && old_deaths == 0 &&
            copy->Wait(10000, result) &&
            result.state == WMTSparseMappingGPUComplete &&
            !result.event_value && deaths == 0);
    transfer(false);
    Check("owned readback map",
          readback->Map(0, nullptr, reinterpret_cast<void **>(&data)));
    UINT bad_words = 0;
    for (UINT i = 0; i < words; ++i)
      bad_words += data[i] != TileWord(seed, i);
    readback->Unmap(0, nullptr);
    require(!bad_words);
    std::printf(
        "TEXTURE_CREDENTIAL_TEXELS packed=%u words=%u mismatches=0 PASS\n",
        packed, words);
    UINT mismatches = 0;
    for (UINT i = 0; i < total; ++i) {
      WMT::Buffer actual;
      UINT64 offset = 0;
      Check("owned native logical mapping",
            dst->GetTileMapping(i, actual, offset));
      mismatches +=
          actual.handle != expected.handle || offset != UINT64(i) * kTileBytes;
    }
    require(!mismatches && update->Retire(result) && deaths == 0 &&
            old_deaths == 0 && copy->Retire(result) && deaths == 3 &&
            old_deaths == 1);
    std::printf("TEXTURE_CREDENTIAL_OWNERSHIP packed=%u tiles=%u callbacks=2 "
                "destroyed=3 PASS\n",
                packed, total);
    return false;
  }
  Check("execute owned update", update->IssueLegacy({}));
  require(update->IssueLegacy({}) == E_INVALIDARG);
  Check("execute live copy", copy->IssueLegacy({}));
  require(copy->IssueLegacy({}) == E_INVALIDARG);
  UINT mismatches = 0;
  for (UINT i = 0; i < total; ++i) {
    WMT::Buffer actual;
    UINT64 offset = 0;
    Check("owned copy mapping", dst->GetTileMapping(i, actual, offset));
    mismatches +=
        actual.handle != expected.handle || offset != UINT64(i) * kTileBytes;
  }
  update.reset();
  require(deaths == 0);
  copy.reset();
  require(deaths == 3);
  std::printf("TEXTURE_INTENT_OWNERSHIP packed=%u tiles=%u "
              "changed_source_observed=1 mismatches=%u destroyed=%u\n",
              packed, total, mismatches, deaths.load());
  require(!mismatches);
  return false;
}

// Use the existing SM6 fixture through the normal descriptor/root/MSC path.
// Nonzero output canaries make a skipped dispatch fail the NULL-read oracle.
void CheckHandoffShader(ID3D12Device *device, ID3D12CommandQueue *queue,
                        ID3D12Resource *texture, ID3D12Resource *upload,
                        ID3D12Resource *readback, ID3D12Fence *fence,
                        UINT64 &serial, const char *phase,
                        std::array<UINT, 2> expected, bool bad_oracle) {
  std::ifstream file("reserved_texture.read.cs.cso",
                     std::ios::binary | std::ios::ate);
  if (!file || file.tellg() <= 0)
    throw std::runtime_error("cannot open handoff shader");
  std::vector<char> shader(static_cast<size_t>(file.tellg()));
  file.seekg(0);
  file.read(shader.data(), shader.size());
  if (!file)
    throw std::runtime_error("cannot read handoff shader");

  D3D12_DESCRIPTOR_RANGE ranges[2] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
      {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1}};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  Owned<ID3DBlob> blob, errors;
  Check("handoff serialize root",
        D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                    &blob.ptr, &errors.ptr));
  Owned<ID3D12RootSignature> root;
  Check("handoff root", device->CreateRootSignature(0, blob->GetBufferPointer(),
                                                    blob->GetBufferSize(),
                                                    IID_PPV_ARGS(&root.ptr)));
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.ptr;
  pipeline_desc.CS = {shader.data(), shader.size()};
  Owned<ID3D12PipelineState> pipeline;
  Check("handoff pipeline", device->CreateComputePipelineState(
                                &pipeline_desc, IID_PPV_ARGS(&pipeline.ptr)));
  Owned<ID3D12Resource> output;
  CreateBuffer(device, output, 2 * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc = {};
  descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_desc.NumDescriptors = 2;
  descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Owned<ID3D12DescriptorHeap> descriptors;
  Check("handoff descriptors",
        device->CreateDescriptorHeap(&descriptor_desc,
                                     IID_PPV_ARGS(&descriptors.ptr)));
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(texture, &srv, cpu);
  cpu.ptr += device->GetDescriptorHandleIncrementSize(descriptor_desc.Type);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 2;
  uav.Buffer.StructureByteStride = sizeof(UINT);
  device->CreateUnorderedAccessView(output.ptr, nullptr, &uav, cpu);
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check("handoff shader allocator",
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       IID_PPV_ARGS(&allocator.ptr)));
  Check("handoff shader list",
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  allocator.ptr, pipeline.ptr,
                                  IID_PPV_ARGS(&list.ptr)));
  list->CopyBufferRegion(output.ptr, 0, upload, 0, 2 * sizeof(UINT));
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  Transition(list.ptr, texture, D3D12_RESOURCE_STATE_COMMON,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  list->SetComputeRootSignature(root.ptr);
  ID3D12DescriptorHeap *heaps[] = {descriptors.ptr};
  list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootDescriptorTable(
      0, descriptors->GetGPUDescriptorHandleForHeapStart());
  list->Dispatch(1, 1, 1);
  Transition(list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback, 0, output.ptr, 0, 2 * sizeof(UINT));
  Transition(list.ptr, texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_COMMON);
  Check("handoff shader close", list->Close());
  ID3D12CommandList *lists[] = {list.ptr};
  queue->ExecuteCommandLists(1, lists);
  Check("handoff shader signal", queue->Signal(fence, ++serial));
  Wait(fence, serial);
  Check("handoff shader device", device->GetDeviceRemovedReason());
  UINT *data = nullptr;
  Check("handoff shader map",
        readback->Map(0, nullptr, reinterpret_cast<void **>(&data)));
  const std::array<UINT, 2> actual{data[0], data[1]};
  readback->Unmap(0, nullptr);
  if (bad_oracle)
    expected[0] ^= 1u;
  std::cout << "HANDOFF_SHADER phase=" << phase
            << " words=2 actual=" << actual[0] << "," << actual[1]
            << " expected=" << expected[0] << "," << expected[1]
            << " canary=NONZERO\n";
  if (actual != expected)
    throw std::runtime_error("handoff shader mismatch");
}

struct GateWatchdog {
  ID3D12Fence *gate;
  HANDLE released = nullptr;
  std::atomic<bool> timed_out{false};
  std::thread worker;

  explicit GateWatchdog(ID3D12Fence *fence) : gate(fence) {
    released = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!released)
      throw std::runtime_error("gate watchdog event");
    try {
      worker = std::thread([this] {
        if (WaitForSingleObject(released, 15000) != WAIT_OBJECT_0) {
          timed_out.store(true);
          gate->Signal(1);
        }
      });
    } catch (...) {
      CloseHandle(released);
      throw;
    }
  }

  HRESULT Release() {
    const HRESULT hr = gate->Signal(1);
    SetEvent(released);
    if (worker.joinable())
      worker.join();
    return hr;
  }

  ~GateWatchdog() {
    Release();
    CloseHandle(released);
  }
};

int Run(bool bad_oracle, bool cross_resource, bool bad_cross, bool cross_queue,
        bool reverse_queue, bool early_work, DXGI_FORMAT format, bool bad_edge,
        bool bad_position, bool multi_heap, bool retirement,
        bool bad_retirement, bool bad_shader, bool intent, bool bad_intent,
        bool credential) {
  Owned<ID3D12Device> device;
  Check("device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&device.ptr)));
  Owned<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC q = {};
  q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Check("queue", device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue.ptr)));
  Owned<ID3D12CommandQueue> consumer;
  Owned<ID3D12Fence> handoff;
  if (cross_queue) {
    Check("consumer queue",
          device->CreateCommandQueue(&q, IID_PPV_ARGS(&consumer.ptr)));
    Check("handoff fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                               IID_PPV_ARGS(&handoff.ptr)));
  }
  ID3D12CommandQueue *active_queue = queue.ptr;
  std::function<void()> after_submission;
  std::function<void()> before_completion, drain_on_failure;
  bool reset_in_flight = false;
  Owned<ID3D12Fence> fence;
  Check("fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&fence.ptr)));
  D3D12_RESOURCE_DESC d = {};
  d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  const UINT bytes_per_texel = format == DXGI_FORMAT_R8_UINT             ? 1
                               : format == DXGI_FORMAT_R16_UINT          ? 2
                               : format == DXGI_FORMAT_R32G32_UINT       ? 8
                               : format == DXGI_FORMAT_R32G32B32A32_UINT ? 16
                                                                         : 4;
  const UINT tile_width = bytes_per_texel <= 2    ? 256
                          : bytes_per_texel == 16 ? 64
                                                  : 128;
  const UINT tile_height = bytes_per_texel == 1 ? 256
                           : bytes_per_texel == 8 || bytes_per_texel == 16
                               ? 64
                               : 128;
  // The last tile is half-width, so readback padding is observable.
  d.Width = tile_width * 5 / 2;
  d.Height = tile_height;
  d.DepthOrArraySize = 1;
  d.MipLevels = 1;
  d.Format = format;
  std::cout << "texture format=" << static_cast<unsigned>(format) << "\n";
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  Owned<ID3D12Resource> texture;
  Check("reserved texture",
        device->CreateReservedResource(&d, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                       IID_PPV_ARGS(&texture.ptr)));
  UINT total = 0, count = 1;
  D3D12_TILE_SHAPE shape = {};
  D3D12_SUBRESOURCE_TILING tiling = {};
  device->GetResourceTiling(texture.ptr, &total, nullptr, &shape, &count, 0,
                            &tiling);
  if (total != 3 || count != 1 || shape.WidthInTexels != tile_width ||
      shape.HeightInTexels != tile_height || tiling.WidthInTiles != 3 ||
      tiling.HeightInTiles != 1)
    throw std::runtime_error("unexpected texture tile shape");
  D3D12_HEAP_DESC hd = {};
  hd.SizeInBytes = 3ull * kTileBytes;
  hd.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  Owned<ID3D12Heap> heap, other_heap;
  Check("heap", device->CreateHeap(&hd, IID_PPV_ARGS(&heap.ptr)));
  if (multi_heap)
    Check("other heap", device->CreateHeap(&hd, IID_PPV_ARGS(&other_heap.ptr)));
  Owned<ID3D12Resource> upload, readback;
  CreateBuffer(device.ptr, upload, 3ull * kTileBytes, D3D12_HEAP_TYPE_UPLOAD,
               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
  CreateBuffer(device.ptr, readback, 3ull * kTileBytes,
               D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
               D3D12_RESOURCE_STATE_COPY_DEST);
  UINT *data = nullptr;
  Check("upload map",
        upload->Map(0, nullptr, reinterpret_cast<void **>(&data)));
  std::array<UINT, 3> values{0x11112222, 0x33334444, 0x55556666};
  if (intent) {
    char seed[16] = {};
    GetEnvironmentVariableA("DXMT_TEST_TEXTURE_INTENT_SEED", seed,
                            sizeof(seed));
    const auto value = static_cast<UINT>(std::strtoul(seed, nullptr, 0));
    for (auto &word : values)
      word ^= value;
    std::printf("TEXTURE_INTENT_SEED value=%u\n", value);
  }
  for (UINT i = 0; i < 3; i++)
    for (UINT j = 0; j < kTileBytes / 4; j++)
      data[i * (kTileBytes / 4) + j] = TileWord(values[i], j);
  upload->Unmap(0, nullptr);
  D3D12_TILED_RESOURCE_COORDINATE zero = {};
  D3D12_TILE_REGION_SIZE all = {};
  all.NumTiles = 3;
  UINT offset = 0;
  auto map_all = [&] {
    queue->UpdateTileMappings(texture.ptr, 1, &zero, &all, heap.ptr, 1, nullptr,
                              &offset, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
    if (multi_heap) {
      D3D12_TILED_RESOURCE_COORDINATE middle = {};
      middle.X = 1;
      D3D12_TILE_REGION_SIZE single = {};
      single.NumTiles = 1;
      queue->UpdateTileMappings(texture.ptr, 1, &middle, &single,
                                other_heap.ptr, 1, nullptr, &offset, nullptr,
                                D3D12_TILE_MAPPING_FLAG_NONE);
    }
  };
  UINT64 serial = 0;
  auto execute = [&](bool write, bool seed_edge = false) {
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Check("allocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&allocator.ptr)));
    Check("list", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            allocator.ptr, nullptr,
                                            IID_PPV_ARGS(&list.ptr)));
    D3D12_RESOURCE_BARRIER alias = {};
    alias.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    alias.Aliasing = {nullptr, nullptr};
    list->ResourceBarrier(1, &alias);
    if (!write)
      list->CopyBufferRegion(readback.ptr, 0, upload.ptr, 0, 3ull * kTileBytes);
    Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON,
               write ? D3D12_RESOURCE_STATE_COPY_DEST
                     : D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TILE_REGION_SIZE region = all;
    if (seed_edge)
      region.NumTiles = 1;
    list->CopyTiles(
        texture.ptr, &zero, &region, write ? upload.ptr : readback.ptr,
        seed_edge ? 2ull * kTileBytes : 0,
        write ? D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE
              : D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
    Transition(list.ptr, texture.ptr,
               write ? D3D12_RESOURCE_STATE_COPY_DEST
                     : D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_COMMON);
    Check("close", list->Close());
    ID3D12CommandList *lists[] = {list.ptr};
    active_queue->ExecuteCommandLists(1, lists);
    try {
      if (reset_in_flight) {
        Owned<ID3D12CommandAllocator> fresh;
        Check("fresh allocator",
              device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&fresh.ptr)));
        Check("in-flight list reset", list->Reset(fresh.ptr, nullptr));
        Check("fresh list close", list->Close());
        list.ptr->Release();
        list.ptr = nullptr;
        std::cout << "HANDOFF_RECORDING reset=PASS released=PASS "
                     "original_allocator=LIVE\n";
      }
      if (after_submission) {
        auto action = std::move(after_submission);
        after_submission = {};
        action();
      }
      Check("signal", active_queue->Signal(fence.ptr, ++serial));
      if (before_completion) {
        auto action = std::move(before_completion);
        before_completion = {};
        action();
      }
      Wait(fence.ptr, serial);
      Check("device status", device->GetDeviceRemovedReason());
    } catch (...) {
      if (drain_on_failure)
        drain_on_failure();
      throw;
    }
  };
  auto verify = [&](const char *name, std::array<UINT, 3> expected) {
    execute(false);
    Check("readback map",
          readback->Map(0, nullptr, reinterpret_cast<void **>(&data)));
    bool ok = true;
    for (UINT i = 0; i < 3; i++) {
      UINT actual = data[i * (kTileBytes / 4)];
      std::cout << name << " tile " << i << " actual=0x" << std::hex << actual
                << " expected=0x" << expected[i] << std::dec << "\n";
      ok &= actual == expected[i];
    }
    for (UINT tile = 0; tile < 2 && ok; ++tile) {
      for (UINT word = 0; word < kTileBytes / sizeof(UINT); ++word) {
        UINT wanted = expected[tile] ? TileWord(expected[tile], word) : 0;
        if (bad_intent && std::strcmp(name, "intent live source") == 0 &&
            tile == 1 && word == kTileBytes / sizeof(UINT) - 1)
          wanted ^= 1;
        if (bad_retirement && std::strcmp(name, "handoff queued") == 0 &&
            tile == 0 && word == kTileBytes / sizeof(UINT) - 1)
          wanted ^= 1u;
        if (data[tile * (kTileBytes / sizeof(UINT)) + word] != wanted) {
          std::cerr << name << " full tile mismatch tile=" << tile
                    << " word=" << word << "\n";
          ok = false;
          break;
        }
      }
    }
    const UINT row_words = tile_width * bytes_per_texel / sizeof(UINT);
    for (UINT y = 0; y < tile_height; ++y) {
      for (UINT x = 0; x < row_words; ++x) {
        UINT word = y * row_words + x;
        if (bad_position && std::strcmp(name, "forward") == 0 &&
            y == tile_height - 1 && x == row_words / 2 - 1)
          word -= row_words;
        UINT wanted =
            x < row_words / 2 && expected[2] ? TileWord(expected[2], word) : 0;
        if (bad_edge && std::strcmp(name, "forward") == 0 &&
            y == tile_height - 1 && x == row_words / 2 - 1)
          wanted ^= 1u;
        const UINT actual = data[2 * (kTileBytes / 4) + y * row_words + x];
        if (actual != wanted) {
          std::cerr << name << " edge mismatch row=" << y << " word=" << x
                    << " actual=0x" << std::hex << actual << " expected=0x"
                    << wanted << std::dec << "\n";
          ok = false;
          break;
        }
      }
      if (!ok)
        break;
    }
    if (ok) {
      std::cout << name << " full edge and padding passed\n";
      std::cout << "TEXTURE_FULL_READBACK phase=" << name
                << " words=" << 3 * (kTileBytes / sizeof(UINT)) << "\n";
    }
    readback->Unmap(0, nullptr);
    if (!ok)
      throw std::runtime_error(name);
  };
  // Seed the entire backing page through a full virtual tile before exposing
  // it at the half-width edge. Reverse overlap later exposes the whole page.
  D3D12_TILE_REGION_SIZE seed_region = {};
  seed_region.NumTiles = 1;
  const UINT edge_heap_page = 2;
  queue->UpdateTileMappings(texture.ptr, 1, &zero, &seed_region, heap.ptr, 1,
                            nullptr, &edge_heap_page, nullptr,
                            D3D12_TILE_MAPPING_FLAG_NONE);
  execute(true, true);
  map_all();
  execute(true);
  if (retirement) {
    for (const char *name : {"d3d12.dll", "winemetal.dll"}) {
      const char *symbol = !std::strcmp(name, "d3d12.dll")
                               ? "D3D12CreateDevice"
                               : "SparseMappingQueue_tryUpdateTextureMappings";
      const auto module = GetModuleHandleA(name);
      const auto code = reinterpret_cast<const unsigned char *>(
          GetProcAddress(module, symbol));
      char path[2048] = {};
      if (!module || !code || !GetModuleFileNameA(module, path, sizeof(path)))
        throw std::runtime_error("missing handoff runtime identity");
      std::cout << "LOADED_MODULE " << name << " " << path << "\n";
      std::cout << "LOADED_CODE " << name << " " << symbol << " ";
      for (UINT i = 0; i < 16; ++i)
        std::printf("%02x", code[i]);
      std::cout << "\n";
    }
    CheckHandoffShader(device.ptr, queue.ptr, texture.ptr, upload.ptr,
                       readback.ptr, fence.ptr, serial, "mapped",
                       {values[0], values[1]}, false);
    Owned<ID3D12Fence> gate, retired;
    Check("retirement gate", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                 IID_PPV_ARGS(&gate.ptr)));
    Check("retirement fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                  IID_PPV_ARGS(&retired.ptr)));
    GateWatchdog watchdog(gate.ptr);
    drain_on_failure = [&] {
      watchdog.Release();
      if (SUCCEEDED(queue->Signal(retired.ptr, 2)))
        Wait(retired.ptr, 2);
    };
    try {
      Check("retirement GPU wait", queue->Wait(gate.ptr, 1));
      D3D12_TILED_RESOURCE_COORDINATE destination = {};
      destination.X = 1;
      D3D12_TILE_REGION_SIZE pair = {};
      pair.NumTiles = 2;
      queue->CopyTileMappings(texture.ptr, &destination, texture.ptr, &zero,
                              &pair, D3D12_TILE_MAPPING_FLAG_NONE);
      Check("retirement copy status", device->GetDeviceRemovedReason());
      reset_in_flight = true;
      before_completion = [&] {
        D3D12_TILE_RANGE_FLAGS null_range = D3D12_TILE_RANGE_FLAG_NULL;
        queue->UpdateTileMappings(texture.ptr, 1, &zero, &all, nullptr, 1,
                                  &null_range, &offset, nullptr,
                                  D3D12_TILE_MAPPING_FLAG_NONE);
        Check("retirement unmap status", device->GetDeviceRemovedReason());
        Check("retirement signal", queue->Signal(retired.ptr, 1));
        if (gate->GetCompletedValue() != 0 ||
            fence->GetCompletedValue() >= serial ||
            retired->GetCompletedValue() != 0 || watchdog.timed_out.load())
          throw std::runtime_error("handoff completed behind closed GPU gate");
        std::cout << "HANDOFF_PENDING readback=1 unmap=1 heap=LIVE\n";
        Check("release retirement gate", watchdog.Release());
        Wait(retired.ptr, 1);
        if (watchdog.timed_out.load())
          throw std::runtime_error("handoff gate watchdog released");
      };
      verify("handoff queued", {values[0], values[0], values[1]});
    } catch (...) {
      drain_on_failure();
      throw;
    }
    reset_in_flight = false;
    drain_on_failure = {};
    // Caller memory stays alive through its final GPU use and unmap. Do not
    // use a premature last Release as a purported D3D12 conformance test.
    heap.ptr->Release();
    heap.ptr = nullptr;
    std::cout << "HANDOFF_RETIRE heap_app_release=AFTER_FENCE\n";
    verify("handoff retired", {0, 0, 0});
    CheckHandoffShader(device.ptr, queue.ptr, texture.ptr, upload.ptr,
                       readback.ptr, fence.ptr, serial, "retired", {0, 0},
                       bad_shader);
    std::cout << "Sparse handoff retirement readback passed\n";
    return 0;
  }
  if (intent) {
    using namespace dxmt;
    for (const auto &entry :
         {std::pair{"d3d12.dll", "D3D12CreateDevice"},
          std::pair{"winemetal.dll",
                    "SparseMappingQueue_tryUpdateTextureMappings"}}) {
      const auto module = GetModuleHandleA(entry.first);
      const auto code = reinterpret_cast<const unsigned char *>(
          GetProcAddress(module, entry.second));
      char path[2048] = {};
      if (!module || !code || !GetModuleFileNameA(module, path, sizeof(path)))
        throw std::runtime_error("intent runtime identity");
      std::printf("LOADED_MODULE %s %s\nLOADED_CODE %s %s ", entry.first, path,
                  entry.first, entry.second);
      for (UINT i = 0; i < 16; ++i)
        std::printf("%02x", code[i]);
      std::printf("\n");
    }
    if (CheckTextureIntentOwnership(device.ptr, false, credential))
      return 0;
    CheckTextureIntentOwnership(device.ptr, true, credential);
    auto require = [](bool ok) {
      if (!ok)
        throw std::runtime_error("texture intent GPU contract");
    };
    auto native_device = static_cast<MTLD3D12Device *>(device.ptr);
    auto metal = native_device->GetMTLDevice();
    require(metal.supportsPlacementSparse() &&
            metal.supportsFamily(WMTGPUFamilyMetal4));
    auto sparse = metal.newSparseMappingQueue();
    require(bool(sparse));
    sparse.addResidencySet(native_device->GetGlobalResidencySet());
    Owned<ID3D12Resource> marker;
    auto marker_desc = BufferDescription(kTileBytes, D3D12_RESOURCE_FLAG_NONE);
    Check("terminal marker", device->CreateReservedResource(
                                 &marker_desc, D3D12_RESOURCE_STATE_COMMON,
                                 nullptr, IID_PPV_ARGS(&marker.ptr)));
    // A real unmap of an unused buffer page supplies the existing terminal
    // observer. It carries no texture operation or texture ownership.
    auto execute_plan = [&](D3D12SparseTextureMappingPlan &plan) {
      if (credential) {
        Check("prepare native texture", plan.PrepareNative(sparse));
        require(plan.PrepareTerminal(
            3)); // actual delayed callback, no private signal
        WMTSparseMappingResult result = {};
        require(plan.Wait(0, result) &&
                result.state == WMTSparseMappingPrepared &&
                !plan.Retire(result));
        auto barrier = sparse.prepareBarrierBeforeResourceState();
        require(bool(barrier) && barrier.submit());
        for (size_t i = 0; i < plan.OperationCount(); ++i)
          require(plan.IssueNext());
        require(!plan.IssueNext() &&
                plan.Finish(WMTSparseMappingHostAccepted) &&
                !plan.Finish(WMTSparseMappingHostAccepted));
        require(plan.Wait(0, result) &&
                result.state == WMTSparseMappingSealedPending &&
                !plan.Retire(result));
        require(plan.Wait(10000, result) &&
                result.state == WMTSparseMappingGPUComplete &&
                result.event_value == 0 &&
                result.possibly_issued_calls == plan.OperationCount());
        WMTSparseBarrierResult before = {};
        require(barrier.wait(10000, before) &&
                before.state == WMTSparseBarrierCompleted &&
                before.native_feedback_received);
        std::printf(
            "TEXTURE_CREDENTIAL_COMPLETE operations=%zu event=%llu retired=0\n",
            plan.OperationCount(), (unsigned long long)result.event_value);
        require(plan.Retire(result) && !plan.Retire(result));
        return;
      }
      WMTSparseBufferBatchOperation op = {};
      op.mode = WMTSparseTextureMappingModeUnmap;
      op.destination =
          static_cast<MTLD3D12Resource *>(marker.ptr)->GetMetalBuffer().handle;
      op.length = 1;
      auto terminal = sparse.prepareBufferBatch(&op, 1);
      require(bool(terminal) &&
              SparseMappingCompletion_prepareTerminal(terminal.handle, 1));
      auto barrier = sparse.prepareBarrierBeforeResourceState();
      require(bool(barrier) && barrier.submit());
      const HRESULT hr = plan.IssueLegacy(sparse);
      require(terminal.issueNext());
      require(terminal.seal(SUCCEEDED(hr) ? WMTSparseMappingHostAccepted
                                          : WMTSparseMappingHostRejected));
      WMTSparseMappingResult result = {};
      require(terminal.wait(10000, result) &&
              result.state == WMTSparseMappingGPUComplete &&
              terminal.retireInputs());
      WMTSparseBarrierResult before = {};
      require(barrier.wait(10000, before) &&
              before.state == WMTSparseBarrierCompleted &&
              before.native_feedback_received);
      Check("execute texture intent", hr);
      require(plan.IssueLegacy(sparse) == E_INVALIDARG);
    };
    Owned<ID3D12Resource> destination;
    Check("intent GPU destination",
          device->CreateReservedResource(&d, D3D12_RESOURCE_STATE_COMMON,
                                         nullptr,
                                         IID_PPV_ARGS(&destination.ptr)));
    auto source = static_cast<MTLD3D12Resource *>(texture.ptr);
    auto target = static_cast<MTLD3D12Resource *>(destination.ptr);
    std::unique_ptr<D3D12SparseTextureMappingPlan> plan;
    {
      auto start = zero;
      auto size = all;
      std::printf("TEXTURE_INTENT_PREPARE_BEGIN gpu_copy=1\n");
      Check("prepare GPU copy",
            target->PrepareTextureTileCopy(source, &start, &start, &size,
                                           D3D12_TILE_MAPPING_FLAG_NONE, plan));
      start.X = 999;
      size.NumTiles = 0;
    }
    for (UINT i = 0; i < 3; ++i)
      require(!target->IsTileMapped(i));
    std::printf(
        "TEXTURE_INTENT_PREPARE_END unchanged=1 caller_arrays_destroyed=1\n");
    D3D12_TILED_RESOURCE_COORDINATE a = {}, b = {};
    b.X = 1;
    D3D12_TILE_REGION_SIZE pair = {};
    pair.NumTiles = 2;
    queue->CopyTileMappings(texture.ptr, &b, texture.ptr, &a, &pair,
                            D3D12_TILE_MAPPING_FLAG_NONE);
    Check("source remap drain", queue->Signal(fence.ptr, ++serial));
    Wait(fence.ptr, serial);
    execute_plan(*plan);
    std::swap(texture.ptr, destination.ptr);
    verify("intent live source", {values[0], values[0], values[1]});
    plan.reset();
    target = static_cast<MTLD3D12Resource *>(texture.ptr);
    std::vector<D3D12_TILED_RESOURCE_COORDINATE> starts(3);
    starts[1].X = 1;
    starts[2].X = 1;
    std::vector<D3D12_TILE_REGION_SIZE> sizes(3);
    sizes[0].NumTiles = 1;
    sizes[1].NumTiles = 1;
    sizes[2].NumTiles = 2;
    std::vector<D3D12_TILE_RANGE_FLAGS> flags{
        D3D12_TILE_RANGE_FLAG_SKIP, D3D12_TILE_RANGE_FLAG_NONE,
        D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE};
    std::vector<UINT> offsets{0, 2, 1}, counts{1, 1, 2};
    Check("prepare ranges", target->PrepareTextureTileUpdate(
                                3, starts.data(), sizes.data(), heap.ptr, 3,
                                flags.data(), offsets.data(), counts.data(),
                                D3D12_TILE_MAPPING_FLAG_NONE, plan));
    starts.clear();
    sizes.clear();
    flags.clear();
    offsets.clear();
    counts.clear();
    execute_plan(*plan);
    verify("intent ranges", {values[0], values[1], values[1]});
    plan.reset();
    Check("prepare self",
          target->PrepareTextureTileCopy(target, &a, &b, &pair,
                                         D3D12_TILE_MAPPING_FLAG_NONE, plan));
    map_all();
    Check("self source drain", queue->Signal(fence.ptr, ++serial));
    Wait(fence.ptr, serial);
    execute_plan(*plan);
    verify("intent self live", {values[1], values[2], values[2]});
    plan.reset();
    D3D12_TILE_RANGE_FLAGS null_range = D3D12_TILE_RANGE_FLAG_NULL;
    Check("prepare null", target->PrepareTextureTileUpdate(
                              1, &zero, &all, nullptr, 1, &null_range, nullptr,
                              nullptr, D3D12_TILE_MAPPING_FLAG_NONE, plan));
    execute_plan(*plan);
    verify("intent null", {0, 0, 0});
    plan.reset();
    require(target->PrepareTextureTileCopy(target, &b, &b, &all,
                                           D3D12_TILE_MAPPING_FLAG_NONE,
                                           plan) == E_INVALIDARG &&
            !plan);
    require(target->PrepareTextureTileUpdate(
                1, &zero, &all, nullptr, 1, nullptr, nullptr, nullptr,
                D3D12_TILE_MAPPING_FLAG_NONE, plan) == E_INVALIDARG &&
            !plan);
    std::cout << "TEXTURE_INTENT_RESULT gpu_readbacks=4 invalid=2 "
                 "repeat_rejected=4 PASS\n";
    if (credential) {
      map_all();
      Check("partial setup drain", queue->Signal(fence.ptr, ++serial));
      Wait(fence.ptr, serial);
      Check("prepare partial null",
            target->PrepareTextureTileUpdate(
                1, &zero, &all, nullptr, 1, &null_range, nullptr, nullptr,
                D3D12_TILE_MAPPING_FLAG_NONE, plan));
      Check("native partial null", plan->PrepareNative(sparse));
      require(plan->PrepareTerminal(3) && plan->OperationCount() == 3);
      auto before = sparse.prepareBarrierBeforeResourceState();
      require(bool(before) && before.submit() && plan->IssueNext() &&
              !plan->Finish(WMTSparseMappingHostAccepted) &&
              plan->Finish(WMTSparseMappingHostFailedAfterIssue));
      WMTSparseMappingResult result = {};
      require(plan->Wait(10000, result) &&
              result.state == WMTSparseMappingGPUComplete &&
              result.possibly_issued_calls == 1 && !result.event_value &&
              target->IsTileMapped(0) && target->IsTileMapped(1) &&
              target->IsTileMapped(2));
      verify("credential partial prefix", {0, values[1], values[2]});
      require(plan->Retire(result));
      plan.reset();
      std::cout << "TEXTURE_CREDENTIAL_RESULT gpu_readbacks=5 callbacks=5 "
                   "partial_prefix=1 PASS\n";
      // Failed batches leave a native prefix with unpublished logical state.
      // Discard these resources; no normal submission follows this diagnostic.
    }
    return 0;
  }
  D3D12_TILED_RESOURCE_COORDINATE src = {}, dst = {};
  D3D12_TILE_REGION_SIZE pair = {};
  pair.NumTiles = 2;
  dst.X = 1;
  queue->CopyTileMappings(texture.ptr, &dst, texture.ptr, &src, &pair,
                          D3D12_TILE_MAPPING_FLAG_NONE);
  verify("forward", {values[0], bad_oracle ? values[1] : values[0], values[1]});
  map_all();
  src.X = 1;
  dst.X = 0;
  queue->CopyTileMappings(texture.ptr, &dst, texture.ptr, &src, &pair,
                          D3D12_TILE_MAPPING_FLAG_NONE);
  verify("reverse", {values[1], values[2], values[2]});
  map_all();
  D3D12_TILED_RESOURCE_COORDINATE one = {};
  one.X = 1;
  D3D12_TILE_REGION_SIZE single = {};
  single.NumTiles = 1;
  D3D12_TILE_RANGE_FLAGS null_flag = D3D12_TILE_RANGE_FLAG_NULL;
  queue->UpdateTileMappings(texture.ptr, 1, &one, &single, nullptr, 1,
                            &null_flag, &offset, nullptr,
                            D3D12_TILE_MAPPING_FLAG_NONE);
  src.X = 0;
  dst.X = 1;
  queue->CopyTileMappings(texture.ptr, &dst, texture.ptr, &src, &pair,
                          D3D12_TILE_MAPPING_FLAG_NONE);
  verify("null", {values[0], values[0], 0});
  // Writing an unbacked sparse tile must be discarded, not affect the heap.
  {
    Owned<ID3D12CommandAllocator> allocator;
    Owned<ID3D12GraphicsCommandList> list;
    Check("null write allocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&allocator.ptr)));
    Check("null write list",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    allocator.ptr, nullptr,
                                    IID_PPV_ARGS(&list.ptr)));
    D3D12_TILED_RESOURCE_COORDINATE null_tile = {};
    null_tile.X = 2;
    Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyTiles(
        texture.ptr, &null_tile, &single, upload.ptr, 0,
        D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
    Transition(list.ptr, texture.ptr, D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_COMMON);
    Check("null write close", list->Close());
    ID3D12CommandList *lists[] = {list.ptr};
    queue->ExecuteCommandLists(1, lists);
    Check("null write signal", queue->Signal(fence.ptr, ++serial));
    Wait(fence.ptr, serial);
  }
  map_all();
  verify("null write discarded", values);
  if (cross_resource) {
    map_all();
    Owned<ID3D12Resource> destination;
    Check("cross-resource destination",
          device->CreateReservedResource(&d, D3D12_RESOURCE_STATE_COMMON,
                                         nullptr,
                                         IID_PPV_ARGS(&destination.ptr)));
    if (reverse_queue || early_work)
      Check("early consumer wait", consumer->Wait(handoff.ptr, 1));
    if (!early_work)
      queue->CopyTileMappings(destination.ptr, &zero, texture.ptr, &zero, &all,
                              D3D12_TILE_MAPPING_FLAG_NONE);
    UINT64 handoff_value = 0;
    const auto synchronize = [&] {
      if (!cross_queue)
        return;
      ++handoff_value;
      Check("producer handoff signal",
            queue->Signal(handoff.ptr, handoff_value));
      if (!((reverse_queue || early_work) && handoff_value == 1))
        Check("consumer handoff wait",
              consumer->Wait(handoff.ptr, handoff_value));
    };
    if (!early_work)
      synchronize();
    if (cross_queue)
      active_queue = consumer.ptr;
    std::swap(texture.ptr, destination.ptr);
    if (early_work) {
      after_submission = [&] {
        queue->CopyTileMappings(texture.ptr, &zero, destination.ptr, &zero,
                                &all, D3D12_TILE_MAPPING_FLAG_NONE);
        synchronize();
      };
    }
    verify("cross copied",
           {values[0], bad_cross ? values[0] : values[1], values[2]});
    queue->UpdateTileMappings(destination.ptr, 1, &one, &single, nullptr, 1,
                              &null_flag, &offset, nullptr,
                              D3D12_TILE_MAPPING_FLAG_NONE);
    synchronize();
    verify("cross independent", {values[0], values[1], values[2]});
    queue->CopyTileMappings(texture.ptr, &one, destination.ptr, &one, &single,
                            D3D12_TILE_MAPPING_FLAG_NONE);
    synchronize();
    verify("cross null", {values[0], 0, values[2]});
    std::cout << "cross-resource texture mapping readback passed\n";
  }
  std::cout << "texture overlap mapping readback passed\n";
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 2)
    return 2;
  const char *arg = argc == 2 ? argv[1] : "";
  const auto is = [&](const char *name) { return std::strcmp(arg, name) == 0; };
  const bool credential = is("--credential") || is("--bad-credential");
  const bool intent = is("--intent") || is("--bad-intent") || credential;
  if (intent)
    std::setvbuf(stdout, nullptr, _IONBF, 0);
  const bool rgba8 = is("--rgba8") || is("--bad-rgba8");
  const bool rg16 = is("--rg16");
  const bool rgb10a2 = is("--rgb10a2");
  const bool bgrx = is("--bgrx");
  const bool bad_edge = is("--bad-edge-r8");
  const bool bad_position = is("--bad-position-r8");
  const bool r8 = is("--r8") || is("--bad-r8") || bad_edge || bad_position;
  const bool r16 = is("--r16");
  const bool rg32 = is("--rg32");
  const bool rgba32 = is("--rgba32");
  const bool format_case =
      rgba8 || rg16 || rgb10a2 || bgrx || r8 || r16 || rg32 || rgba32;
  const bool multi_heap =
      is("--multiheap") || is("--bad-multiheap") || is("--multiheap-queue");
  const bool retirement = is("--handoff-retirement") ||
                          is("--bad-handoff-retirement") ||
                          is("--bad-handoff-shader");
  const bool bad = is("--bad-oracle") || is("--bad-multiheap");
  const bool cross = is("--cross-resource");
  const bool bad_cross = is("--bad-cross-oracle");
  const bool reverse_queue = is("--reverse-queue");
  const bool early_work =
      format_case || is("--early-work") || is("--bad-early-work");
  const bool cross_queue = early_work || reverse_queue || is("--cross-queue") ||
                           is("--multiheap-queue") || is("--bad-queue-oracle");
  const bool bad_queue = is("--bad-rgba8") || is("--bad-r8") ||
                         is("--bad-queue-oracle") || is("--bad-early-work");
  if (argc == 2 && !bad && !cross && !bad_cross && !cross_queue &&
      !multi_heap && !retirement && !intent)
    return 2;
  const DXGI_FORMAT format = rgba8     ? DXGI_FORMAT_R8G8B8A8_UNORM
                             : rg16    ? DXGI_FORMAT_R16G16_UNORM
                             : rgb10a2 ? DXGI_FORMAT_R10G10B10A2_UNORM
                             : bgrx    ? DXGI_FORMAT_B8G8R8X8_UNORM
                             : r8      ? DXGI_FORMAT_R8_UINT
                             : r16     ? DXGI_FORMAT_R16_UINT
                             : rg32    ? DXGI_FORMAT_R32G32_UINT
                             : rgba32  ? DXGI_FORMAT_R32G32B32A32_UINT
                                       : DXGI_FORMAT_R32_UINT;
  try {
    return Run(bad, cross || bad_cross || cross_queue, bad_cross || bad_queue,
               cross_queue, reverse_queue, early_work, format, bad_edge,
               bad_position, multi_heap, retirement,
               is("--bad-handoff-retirement"), is("--bad-handoff-shader"),
               intent, is("--bad-intent") || is("--bad-credential"),
               credential);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}

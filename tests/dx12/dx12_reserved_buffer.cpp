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
#include <future>
#include <chrono>
#include <cstddef>
#include <memory>
#include <algorithm>
#include <cstdlib>
#include "../../src/winemetal/winemetal.h"
#include "../../src/d3d12/d3d12_device.hpp"
#include <array>

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
class SparsePayloadSentinel : public IUnknown {
  std::atomic<ULONG> refs_{1};
  std::atomic<unsigned> &destroyed_;
  unsigned id_;

public:
  SparsePayloadSentinel(std::atomic<unsigned> &destroyed, unsigned id)
      : destroyed_(destroyed), id_(id) {}
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
    auto refs = --refs_;
    if (!refs) {
      ++destroyed_;
      std::printf("PAYLOAD_COM_DESTROY id=%u\n", id_);
      std::fflush(stdout);
      delete this;
    }
    return refs;
  }
};

int CheckSparseMappingPayload(const char *mode, uint32_t seed) {
  using namespace dxmt;
  auto require = [](bool ok, const char *why) {
    if (!ok) {
      std::fprintf(stderr, "PAYLOAD_FATAL %s\n", why);
      std::fflush(nullptr);
      std::_Exit(3);
    }
  };
  const bool plan_mode = !std::strncmp(mode, "plan-", 5);
  const bool native_only = !std::strcmp(mode, "payload-native-only"),
             release_pending = !std::strcmp(mode, "payload-release"),
             wrong_word = !std::strcmp(mode, "payload-wrong-word");
  auto pool = WMT::MakeAutoreleasePool();
  ID3D12Device *public_device = nullptr;
  require(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                      IID_PPV_ARGS(&public_device))),
          "device");
  auto device = static_cast<MTLD3D12Device *>(public_device);
  auto metal = device->GetMTLDevice();
  require(metal.supportsPlacementSparse() &&
              metal.supportsFamily(WMTGPUFamilyMetal4),
          "actual sparse device");
  std::printf("PAYLOAD_DEVICE apple7=%u apple8=%u apple9=%u metal4=%u "
              "placement=%u seed=%u\n",
              metal.supportsFamily(WMTGPUFamilyApple7),
              metal.supportsFamily(WMTGPUFamilyApple8),
              metal.supportsFamily(WMTGPUFamilyApple9),
              metal.supportsFamily(WMTGPUFamilyMetal4),
              metal.supportsPlacementSparse(), seed);
  auto bridge = GetModuleHandleA("winemetal.dll");
  require(bridge != nullptr, "bridge");
  for (auto pair :
       {std::pair{"d3d12.dll", "D3D12CreateDevice"},
        std::pair{"winemetal.dll", "SparseMappingQueue_prepareBufferBatch"},
        std::pair{"winemetal.dll", "SparseMappingCompletion_wait"}}) {
    auto module = GetModuleHandleA(pair.first);
    auto code = reinterpret_cast<const unsigned char *>(
        GetProcAddress(module, pair.second));
    require(code != nullptr, "export");
    char path[32768] = {};
    GetModuleFileNameA(module, path, sizeof(path));
    std::printf("PAYLOAD_MODULE %s %s\n", pair.first, path);
    std::printf("LOADED_CODE %s %s ", pair.first, pair.second);
    for (unsigned i = 0; i < 16; ++i)
      std::printf("%02x", code[i]);
    std::printf("\n");
  }
  const uint64_t bytes = plan_mode ? 196608 : 131072, words = bytes / 4,
                 page_words = 65536 / 4, guards = 32;
  std::array<std::atomic<unsigned>, 5> destroyed{};
  const GUID sentinel_guid = {0xaed22c31,
                              0xcaba,
                              0x4421,
                              {0x98, 0x11, 0x72, 0x58, 0x03, 0x64, 0x05, 0x12}};
  auto sentinel = [&](ID3D12Object *obj, unsigned id) {
    auto s = new SparsePayloadSentinel(destroyed[id], id);
    require(SUCCEEDED(obj->SetPrivateDataInterface(sentinel_guid, s)),
            "sentinel attach");
    s->Release();
  };
  auto deaths = [&] {
    unsigned n = 0;
    for (auto &v : destroyed)
      n += v.load();
    return n;
  };
  ID3D12Resource *public_resources[2] = {};
  MTLD3D12Resource *resources[2] = {};
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = bytes;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  for (unsigned i = 0; i < 2; ++i) {
    require(SUCCEEDED(public_device->CreateReservedResource(
                &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(&public_resources[i]))),
            "reserved buffer");
    resources[i] = static_cast<MTLD3D12Resource *>(public_resources[i]);
    sentinel(public_resources[i], i);
  }
  ID3D12Heap *public_heaps[3] = {};
  MTLD3D12Heap *heaps[3] = {};
  D3D12_HEAP_DESC hd = {};
  hd.SizeInBytes = bytes;
  hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  for (unsigned i = 0; i < 3; ++i) {
    require(SUCCEEDED(
                public_device->CreateHeap(&hd, IID_PPV_ARGS(&public_heaps[i]))),
            "heap");
    heaps[i] = static_cast<MTLD3D12Heap *>(public_heaps[i]);
    sentinel(public_heaps[i], i + 2);
    std::printf("PAYLOAD_HEAP id=%u allocation=%llu\n", i + 2,
                (unsigned long long)heaps[i]->GetMetalHeap().handle);
  }
  WMT::Buffer source = resources[0]->GetMetalBuffer(),
              dest = resources[1]->GetMetalBuffer();
  for (unsigned i = 0; i < 2; ++i)
    std::printf("PAYLOAD_RESOURCE id=%u resource=%llu allocation=%llu\n", i,
                (unsigned long long)reinterpret_cast<uintptr_t>(resources[i]),
                (unsigned long long)resources[i]->GetMetalBuffer().handle);
  D3D12_TILED_RESOURCE_COORDINATE start = {};
  D3D12_TILE_REGION_SIZE region = {};
  region.NumTiles = plan_mode ? 3 : 2;
  UINT offset = 0, count = region.NumTiles;
  for (unsigned i = 0; i < 2; ++i)
    require(SUCCEEDED(resources[i]->UpdateTileMappings(
                1, &start, &region, public_heaps[i], 1, nullptr, &offset,
                &count, D3D12_TILE_MAPPING_FLAG_NONE, {})),
            "logical fixture map");
  auto payload = std::make_unique<D3D12SparseMappingPayload>();
  require(resources[0]->CaptureTileMappingOwners({plan_mode ? 3u : 2u},
                                                 *payload) == E_INVALIDARG &&
              payload->resources.empty() && payload->heaps.empty(),
          "invalid range changed payload");
  if (!native_only && !plan_mode) {
    for (auto r : resources)
      require(SUCCEEDED(r->CaptureTileMappingOwners({0, 1}, *payload)),
              "capture owners");
    payload->heaps.emplace_back(heaps[2]);
  }
  require(payload->resources.size() == ((native_only || plan_mode) ? 0 : 2) &&
              payload->heaps.size() == ((native_only || plan_mode) ? 0 : 5),
          "owner snapshot size");
  WMT::Reference<WMT::CommandQueue> classic{
      MTLDevice_newCommandQueue(metal.handle, 32)};
  auto sparse = metal.newSparseMappingQueue();
  require(classic && sparse, "queues");
  classic.addResidencySet(device->GetGlobalResidencySet());
  sparse.addResidencySet(device->GetGlobalResidencySet());
  WMT::Reference<WMT::Buffer> placed[2], upload[2];
  auto pattern = [&](unsigned h, uint64_t i) {
    return (seed ^ (h ? 0x13579bdfu : 0x2468ace0u)) +
           uint32_t(i / page_words) * 0x27101983u +
           uint32_t(i % page_words) * 0x9e3779b9u;
  };
  for (unsigned i = 0; i < 2; ++i) {
    WMTBufferInfo pi = {};
    pi.length = bytes;
    pi.options = static_cast<WMTResourceOptions>(
        WMTResourceStorageModePrivate | WMTResourceHazardTrackingModeUntracked);
    placed[i] = WMT::Reference<WMT::Buffer>{
        MTLHeap_newBuffer(heaps[i ? 2 : 0]->GetMetalHeap().handle, &pi, 0)};
    WMTBufferInfo ui = {};
    ui.length = bytes;
    ui.options = WMTResourceStorageModeShared;
    upload[i] =
        WMT::Reference<WMT::Buffer>{MTLDevice_newBuffer(metal.handle, &ui)};
    require(placed[i] && upload[i] && ui.memory.ptr, "producer allocations");
    auto data = static_cast<uint32_t *>(ui.memory.ptr);
    for (uint64_t w = 0; w < words; ++w)
      data[w] = pattern(i, w);
  }
  WMTBufferInfo oi = {};
  oi.length = (2 * words + guards) * 4;
  oi.options = WMTResourceStorageModeShared;
  WMT::Reference<WMT::Buffer> output{MTLDevice_newBuffer(metal.handle, &oi)};
  auto out = static_cast<uint32_t *>(oi.memory.ptr);
  require(output && out, "output");
  obj_handle_t error = 0;
  WMT::Reference<WMT::ResidencySet> fixture{
      MTLDevice_newResidencySet(metal.handle, 5, &error)};
  require(fixture && !error, "fixture residency");
  obj_handle_t allocations[] = {placed[0].handle, placed[1].handle,
                                upload[0].handle, upload[1].handle,
                                output.handle};
  MTLResidencySet_addAllocations(fixture.handle, allocations, 5);
  fixture.commit();
  classic.addResidencySet(fixture);
  sparse.addResidencySet(fixture);
  auto blit = [&](obj_handle_t enc, obj_handle_t src, obj_handle_t dst,
                  uint64_t dst_offset) {
    wmtcmd_blit_copy_from_buffer_to_buffer c = {};
    c.type = WMTBlitCommandCopyFromBufferToBuffer;
    c.src = src;
    c.dst = dst;
    c.dst_offset = dst_offset;
    c.copy_length = bytes;
    MTLBlitCommandEncoder_encodeCommands(enc,
                                         reinterpret_cast<wmtcmd_base *>(&c));
  };
  auto init = MTLCommandQueue_commandBuffer(classic.handle),
       enc = MTLCommandBuffer_blitCommandEncoder(init);
  require(init && enc, "init objects");
  for (unsigned i = 0; i < 2; ++i)
    blit(enc, upload[i].handle, placed[i].handle, 0);
  MTLCommandEncoder_endEncoding(enc);
  MTLCommandBuffer_commit(init);
  MTLCommandBuffer_waitUntilCompleted(init);
  require(MTLCommandBuffer_status(init) == WMTCommandBufferStatusCompleted,
          "init failed");
  if (!std::strncmp(mode, "plan-queue", 10)) {
    const bool partial = !std::strcmp(mode, "plan-queue-partial"),
               rejected = !std::strcmp(mode, "plan-queue-rejected"),
               capacity_removal =
                   !std::strcmp(mode, "plan-queue-capacity-removal"),
               removal = !std::strcmp(mode, "plan-queue-removal"),
               release = !std::strcmp(mode, "plan-queue-release"),
               capacity = !std::strcmp(mode, "plan-queue-capacity"),
               empty = !std::strcmp(mode, "plan-queue-empty"),
               negative = !std::strcmp(mode, "plan-queue-wrong-word"),
               prepare_fail = !std::strcmp(mode, "plan-queue-prepare-failure");
    const UINT terminal = !std::strcmp(mode, "plan-queue-event")   ? 0
                          : !std::strcmp(mode, "plan-queue-delay") ? 3
                                                                   : 1;
    ID3D12CommandQueue *public_queue = nullptr;
    D3D12_COMMAND_QUEUE_DESC qd = {};
    require(SUCCEEDED(public_device->CreateCommandQueue(
                &qd, IID_PPV_ARGS(&public_queue))),
            "owned queue");
    auto queue = static_cast<MTLD3D12CommandQueue *>(public_queue);
    Owned<ID3D12Fence> fence, unblock;
    require(SUCCEEDED(public_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                 IID_PPV_ARGS(&fence.ptr))) &&
                SUCCEEDED(public_device->CreateFence(
                    0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&unblock.ptr))),
            "queue fences");
    auto gate = metal.newSharedEvent();
    require(gate != nullptr, "queue gate");
    std::array<std::unique_ptr<D3D12SparseBufferMappingPlan>, 3> plans;
    for (unsigned i = 0; i < 2; ++i)
      require(SUCCEEDED(resources[i]->PrepareBufferTileUpdate(
                  1, &start, &region, public_heaps[i ? 2 : 0], 1, nullptr,
                  &offset, &count, D3D12_TILE_MAPPING_FLAG_NONE, plans[i])),
              "queue update plan");
    auto copy_size = region;
    copy_size.NumTiles = 2;
    require(SUCCEEDED(resources[1]->PrepareBufferTileCopy(
                resources[0], &start, &start, &copy_size,
                D3D12_TILE_MAPPING_FLAG_NONE, plans[2])),
            "queue copy plan");
    SetEnvironmentVariableA("DXMT_TEST_SPARSE_SUBMISSION", nullptr);
    require(queue->SubmitSparsePlanForTesting(plans[0], gate, terminal,
                                              UINT_MAX, nullptr,
                                              nullptr) == E_ACCESSDENIED &&
                plans[0],
            "queue opt-in");
    SetEnvironmentVariableA("DXMT_TEST_SPARSE_SUBMISSION", "1");
    if (prepare_fail) {
      require(queue->SubmitSparsePlanForTesting(plans[0], gate, terminal,
                                                UINT_MAX, nullptr,
                                                nullptr) == E_OUTOFMEMORY &&
                  plans[0] &&
                  SUCCEEDED(public_device->GetDeviceRemovedReason()),
              "queue prepare failure");
      plans = {};
      public_queue->Release();
      for (auto r : public_resources)
        r->Release();
      for (auto h : public_heaps)
        h->Release();
      require(deaths() == 5, "queue prepare cleanup");
      std::printf("QUEUE_PLAN_PREPARE_FAILURE calls=0 destroyed=5 removed=0\n");
      public_device->Release();
      return 0;
    }
    if (capacity || capacity_removal) {
      require(SUCCEEDED(queue->Wait(unblock.ptr, 1)), "capacity wait");
      for (unsigned i = 0; i < 31; ++i)
        require(SUCCEEDED(queue->Signal(fence.ptr, i + 1)), "capacity fill");
      std::promise<void> entered;
      auto started = entered.get_future();
      auto next = std::async(std::launch::async, [&] {
        entered.set_value();
        return queue->SubmitSparsePlanForTesting(plans[0], gate, terminal,
                                                 UINT_MAX, nullptr, nullptr);
      });
      require(started.wait_for(std::chrono::seconds(10)) ==
                      std::future_status::ready &&
                  next.wait_for(std::chrono::milliseconds(40)) ==
                      std::future_status::timeout &&
                  plans[0] && fence->GetCompletedValue() == 0,
              "capacity 33 admitted early");
      std::printf(
          "QUEUE_PLAN_CAPACITY pending=32 next=blocked plan_retained=1\n");
      if (capacity_removal) {
        device->MarkDeviceRemoved(DXGI_ERROR_DEVICE_REMOVED);
        require(next.wait_for(std::chrono::seconds(10)) ==
                        std::future_status::ready &&
                    next.get() == DXGI_ERROR_DEVICE_REMOVED && plans[0],
                "capacity removal admitted work");
        public_queue->Release();
        plans = {};
        for (auto r : public_resources)
          r->Release();
        for (auto h : public_heaps)
          h->Release();
        require(deaths() == 5 && fence->GetCompletedValue() == UINT64_MAX,
                "capacity removal cleanup");
        std::printf("QUEUE_PLAN_CAPACITY_REMOVAL calls=0 plan_retained=1 "
                    "destroyed=5 drained=1\n");
        public_device->Release();
        return 0;
      }
      require(SUCCEEDED(unblock->Signal(1)), "capacity unblock");
      require(next.wait_for(std::chrono::seconds(10)) ==
                      std::future_status::ready &&
                  SUCCEEDED(next.get()) && !plans[0],
              "capacity admission");
    } else {
      require(SUCCEEDED(queue->SubmitSparsePlanForTesting(
                  plans[0], gate, terminal, UINT_MAX, nullptr, nullptr)) &&
                  !plans[0],
              "queue source submit");
    }
    require(SUCCEEDED(queue->SubmitSparsePlanForTesting(
                plans[1], gate, terminal, UINT_MAX, nullptr, nullptr)) &&
                !plans[1],
            "queue destination submit");
    HRESULT last = S_OK;
    HANDLE issued = nullptr, resume = nullptr;
    if (removal) {
      issued = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      require(issued && resume, "removal events");
      auto next = std::async(std::launch::async, [&] {
        return queue->SubmitSparsePlanForTesting(plans[2], gate, terminal, 1,
                                                 issued, resume);
      });
      require(WaitForSingleObject(issued, 10000) == WAIT_OBJECT_0 && !deaths(),
              "removal issue boundary");
      device->MarkDeviceRemoved(DXGI_ERROR_DEVICE_REMOVED);
      require(fence->GetCompletedValue() == UINT64_MAX, "removed fence");
      SetEvent(resume);
      require(next.wait_for(std::chrono::seconds(10)) ==
                  std::future_status::ready,
              "removal publication");
      last = next.get();
      CloseHandle(issued);
      CloseHandle(resume);
    } else {
      last = queue->SubmitSparsePlanForTesting(plans[2], gate, terminal,
                                               rejected  ? 0
                                               : partial ? 1
                                                         : UINT_MAX,
                                               nullptr, nullptr);
    }
    require(!plans[2] && (partial || removal || rejected
                              ? last == DXGI_ERROR_DEVICE_REMOVED
                              : SUCCEEDED(last)),
            "sealed record transfer");
    WMT::Buffer backing;
    UINT64 at = UINT64_MAX;
    require(SUCCEEDED(resources[1]->GetTileMapping(0, backing, at)) &&
                at == 0 &&
                backing.handle == heaps[partial || removal || rejected ? 2 : 0]
                                      ->GetTileBackingBuffer()
                                      .handle,
            "logical prefix publication");
    if (empty) {
      std::unique_ptr<D3D12SparseBufferMappingPlan> skip;
      D3D12_TILE_RANGE_FLAGS flag = D3D12_TILE_RANGE_FLAG_SKIP;
      require(SUCCEEDED(resources[0]->PrepareBufferTileUpdate(
                  1, &start, &region, nullptr, 1, &flag, nullptr, &count,
                  D3D12_TILE_MAPPING_FLAG_NONE, skip)) &&
                  skip->OperationCount() == 0 &&
                  SUCCEEDED(queue->SubmitSparsePlanForTesting(
                      skip, gate, terminal, UINT_MAX, nullptr, nullptr)) &&
                  !skip,
              "empty queue record");
    }
    if (partial || removal || rejected) {
      require(queue->Signal(fence.ptr, 77) == DXGI_ERROR_DEVICE_REMOVED,
              "failure admitted success");
    } else {
      require(SUCCEEDED(queue->Signal(fence.ptr, 77)) &&
                  fence->GetCompletedValue() < 77,
              "FIFO signal premature");
    }
    if (release) {
      for (auto r : public_resources)
        r->Release();
      for (auto h : public_heaps)
        h->Release();
      require(!deaths(), "pending public-ref release");
    }
    std::promise<void> entered;
    auto started = entered.get_future();
    auto drain = std::async(std::launch::async, [&] {
      entered.set_value();
      public_queue->Release();
    });
    require(started.wait_for(std::chrono::seconds(10)) ==
                    std::future_status::ready &&
                drain.wait_for(std::chrono::milliseconds(40)) ==
                    std::future_status::timeout &&
                !deaths(),
            "queue teardown freed pending prefix");
    std::printf("QUEUE_PLAN_PENDING partial=%u removal=%u release=%u "
                "logical=%s destroyed=0 drain=blocked\n",
                partial, removal, release,
                partial || removal || rejected ? "prefix-unpublished"
                                               : "accepted");
    std::fflush(stdout);
    gate.signalValue(1);
    if (terminal == 3)
      require(drain.wait_for(std::chrono::milliseconds(40)) ==
                      std::future_status::timeout &&
                  fence->GetCompletedValue() < 77,
              "delayed report retired FIFO early");
    require(drain.wait_for(std::chrono::seconds(10)) ==
                std::future_status::ready,
            "queue terminal drain");
    drain.get();
    require(fence->GetCompletedValue() ==
                (partial || removal || rejected ? UINT64_MAX : 77),
            "queue FIFO final fence");
    unsigned bad = 0, guard = 0;
    if (!release) {
      std::fill(out, out + 2 * words + guards, 0xa5a55a5au);
      auto read = MTLCommandQueue_commandBuffer(classic.handle);
      enc = MTLCommandBuffer_blitCommandEncoder(read);
      require(read && enc, "queue read objects");
      blit(enc, source.handle, output.handle, 0);
      blit(enc, dest.handle, output.handle, bytes);
      MTLCommandEncoder_endEncoding(enc);
      MTLCommandBuffer_commit(read);
      MTLCommandBuffer_waitUntilCompleted(read);
      require(MTLCommandBuffer_status(read) == WMTCommandBufferStatusCompleted,
              "queue read completion");
      for (unsigned n = 0; n < 2; ++n)
        for (uint64_t w = 0; w < words; ++w) {
          unsigned heap = n && w / page_words >= (rejected             ? 0u
                                                  : partial || removal ? 1u
                                                                       : 2u);
          auto want = pattern(heap, w);
          if (negative && !n && w + 1 == words)
            want ^= 1;
          bad += out[n * words + w] != want;
        }
      for (uint64_t w = 2 * words; w < 2 * words + guards; ++w)
        guard += out[w] != 0xa5a55a5au;
      require(bad == unsigned(negative) && !guard, "queue GPU oracle");
      std::printf("QUEUE_PLAN_READBACK words=%llu bad=%u guard=%u\n",
                  (unsigned long long)(2 * words), bad, guard);
      for (auto r : public_resources)
        r->Release();
      for (auto h : public_heaps)
        h->Release();
    }
    require(deaths() == 5, "queue COM final destruction");
    std::printf("QUEUE_PLAN_RESULT seed=%u mode=%s terminal=%u destroyed=5 "
                "failures=%u drained=1\n",
                seed, mode, terminal, negative);
    public_device->Release();
    return negative ? 1 : 0;
  }
  if (plan_mode) {
    const bool partial = !std::strcmp(mode, "plan-partial"),
               self = !std::strcmp(mode, "plan-self"),
               rejected = !std::strcmp(mode, "plan-rejected"),
               prepare_fail = !std::strcmp(mode, "plan-prepare-failure"),
               release = !std::strcmp(mode, "plan-release"),
               negative = !std::strcmp(mode, "plan-wrong-word");
    if (!std::strcmp(mode, "plan-cost")) {
      LARGE_INTEGER frequency, begin, end;
      QueryPerformanceFrequency(&frequency);
      constexpr unsigned iterations = 20000;
      for (unsigned sample = 0; sample < 5; ++sample) {
        for (unsigned copy = 0; copy < 2; ++copy) {
          QueryPerformanceCounter(&begin);
          for (unsigned i = 0; i < iterations; ++i) {
            const auto hr =
                copy ? resources[1]->CopyTileMappingsFrom(
                           resources[0], &start, &start, &region,
                           D3D12_TILE_MAPPING_FLAG_NONE, {})
                     : resources[0]->UpdateTileMappings(
                           1, &start, &region, public_heaps[0], 1, nullptr,
                           &offset, &count, D3D12_TILE_MAPPING_FLAG_NONE, {});
            require(SUCCEEDED(hr), "cost mapping");
          }
          QueryPerformanceCounter(&end);
          std::printf(
              "PLAN_COST sample=%u copy=%u iterations=%u ns_per_call=%.3f\n",
              sample, copy, iterations,
              (end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart /
                  iterations);
        }
      }
      for (auto r : public_resources)
        r->Release();
      for (auto h : public_heaps)
        h->Release();
      require(deaths() == 5, "cost cleanup");
      public_device->Release();
      return 0;
    }
    std::vector<std::unique_ptr<D3D12SparseBufferMappingPlan>> plans;
    auto logical = [&](unsigned resource, unsigned tile, unsigned heap,
                       unsigned page) {
      WMT::Buffer backing;
      UINT64 at = UINT64_MAX;
      return SUCCEEDED(
                 resources[resource]->GetTileMapping(tile, backing, at)) &&
             backing.handle == heaps[heap]->GetTileBackingBuffer().handle &&
             at == uint64_t(page) * 65536;
    };
    std::unique_ptr<D3D12SparseBufferMappingPlan> invalid;
    auto bad_start = start;
    bad_start.X = 3;
    require(resources[0]->PrepareBufferTileUpdate(
                1, &bad_start, &region, public_heaps[0], 1, nullptr, &offset,
                &count, D3D12_TILE_MAPPING_FLAG_NONE,
                invalid) == E_INVALIDARG &&
                !invalid,
            "invalid preparation published output");
    auto dst = start;
    dst.X = self ? 1 : 0;
    auto copy_size = region;
    copy_size.NumTiles = 2;
    if (self) {
      plans.emplace_back();
      require(SUCCEEDED(resources[0]->PrepareBufferTileUpdate(
                  1, &start, &region, public_heaps[0], 1, nullptr, &offset,
                  &count, D3D12_TILE_MAPPING_FLAG_NONE, plans.back())),
              "self source plan");
      plans.emplace_back();
      require(SUCCEEDED(resources[0]->PrepareBufferTileCopy(
                  resources[0], &dst, &start, &copy_size,
                  D3D12_TILE_MAPPING_FLAG_NONE, plans.back())),
              "self plan");
    } else {
      // Intermediate NULL and REUSE mappings are retained in order; SKIP is
      // elided. Final source remains A0/A1/A2 for the later copied snapshot.
      D3D12_TILED_RESOURCE_COORDINATE starts[7] = {};
      D3D12_TILE_REGION_SIZE sizes[7] = {};
      D3D12_TILE_RANGE_FLAGS flags[7] = {
          D3D12_TILE_RANGE_FLAG_NONE, D3D12_TILE_RANGE_FLAG_NULL,
          D3D12_TILE_RANGE_FLAG_SKIP, D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE,
          D3D12_TILE_RANGE_FLAG_NONE, D3D12_TILE_RANGE_FLAG_NONE,
          D3D12_TILE_RANGE_FLAG_NONE};
      UINT offsets[7] = {0, 0, 0, 1, 0, 1, 2},
           counts[7] = {1, 1, 1, 1, 1, 1, 1};
      for (unsigned i = 0; i < 7; ++i) {
        starts[i].X = i < 3 ? i : i - 4;
        sizes[i].NumTiles = 1;
      }
      starts[3].X = 0;
      plans.emplace_back();
      require(SUCCEEDED(resources[0]->PrepareBufferTileUpdate(
                  7, starts, sizes, public_heaps[0], 7, flags, offsets, counts,
                  D3D12_TILE_MAPPING_FLAG_NONE, plans.back())) &&
                  plans.back()->OperationCount() == 6,
              "range plan");
      std::memset(starts, 0xff, sizeof(starts));
      std::memset(sizes, 0xff, sizeof(sizes));
      std::memset(flags, 0xff, sizeof(flags));
      std::memset(offsets, 0xff, sizeof(offsets));
      std::memset(counts, 0xff, sizeof(counts));
    }
    plans.emplace_back();
    require(SUCCEEDED(resources[1]->PrepareBufferTileUpdate(
                1, &start, &region, public_heaps[2], 1, nullptr, &offset,
                &count, D3D12_TILE_MAPPING_FLAG_NONE, plans.back())),
            "destination plan");
    if (!self) {
      plans.emplace_back();
      require(SUCCEEDED(resources[1]->PrepareBufferTileCopy(
                  resources[0], &dst, &start, &copy_size,
                  D3D12_TILE_MAPPING_FLAG_NONE, plans.back())),
              "copy plan");
    }
    copy_size.NumTiles = UINT_MAX;
    dst.X = UINT_MAX;
    // Zero native operations are a legal all-SKIP plan; acceptance must keep
    // the logical map unchanged and retire with no GPU event requirement.
    if (!prepare_fail) {
      std::unique_ptr<D3D12SparseBufferMappingPlan> skip;
      D3D12_TILE_RANGE_FLAGS flag = D3D12_TILE_RANGE_FLAG_SKIP;
      require(SUCCEEDED(resources[0]->PrepareBufferTileUpdate(
                  1, &start, &region, nullptr, 1, &flag, nullptr, &count,
                  D3D12_TILE_MAPPING_FLAG_NONE, skip)) &&
                  skip->OperationCount() == 0 &&
                  SUCCEEDED(skip->PrepareNative(sparse)) &&
                  !skip->IssueNext() &&
                  skip->Finish(WMTSparseMappingHostAccepted),
              "empty plan");
      WMTSparseMappingResult empty = {};
      require(skip->Wait(0, empty) &&
                  empty.state == WMTSparseMappingNotIssued &&
                  skip->Retire(empty) &&
                  !skip->Finish(WMTSparseMappingHostAccepted) &&
                  !skip->Retire(empty),
              "empty retirement");
    }
    for (auto &plan : plans) {
      const auto hr = plan->PrepareNative(sparse);
      require(prepare_fail ? hr == E_OUTOFMEMORY : SUCCEEDED(hr),
              "native plan preparation");
    }
    require(logical(0, 1, 0, 1) && logical(1, 0, 1, 0) && logical(1, 2, 1, 2),
            "preparation changed logical map");
    for (auto r : public_resources)
      require(r->Release() == 0, "public resource count");
    for (auto h : public_heaps)
      require(h->Release() == 0, "public heap count");
    require(!deaths(), "plan failed to retain COM owners");
    std::printf(
        "PLAN_PREPARED self=%u partial=%u plans=%zu mutated=1 destroyed=%u\n",
        self, partial, plans.size(), deaths());
    if (prepare_fail || rejected) {
      for (auto &plan : plans) {
        WMTSparseMappingResult result = {};
        if (!prepare_fail)
          require(plan->Finish(WMTSparseMappingHostRejected) &&
                      plan->Retire(result),
                  "rejected plan");
        else
          require(!plan->IssueNext() &&
                      !plan->Finish(WMTSparseMappingHostAccepted) &&
                      !plan->Retire(result),
                  "failed native preparation");
      }
      plans.clear();
      require(deaths() == 5, "zero-issue plan cleanup");
      std::printf("PLAN_ZERO prepare_failure=%u rejected=%u issued=0 "
                  "destroyed=5 drained=1\n",
                  prepare_fail, rejected);
      public_device->Release();
      return 0;
    }
    auto gate = metal.newSharedEvent(), done = metal.newSharedEvent();
    auto barrier = sparse.prepareBarrierBeforeResourceState();
    require(gate && done && barrier, "plan events");
    sparse.waitForEvent(gate, 1);
    require(barrier.submit(), "plan barrier");
    unsigned issued = 0;
    for (size_t i = 0; i < plans.size(); ++i) {
      auto &plan = plans[i];
      require(!plan->Finish(WMTSparseMappingHostAccepted), "early acceptance");
      const auto n =
          partial && i + 1 == plans.size() ? 1 : plan->OperationCount();
      for (size_t j = 0; j < n; ++j)
        require(plan->IssueNext(), "plan issue");
      issued += n;
      const auto host = partial && i + 1 == plans.size()
                            ? WMTSparseMappingHostFailedAfterIssue
                            : WMTSparseMappingHostAccepted;
      require(plan->Finish(host) && !plan->Finish(host) && !plan->IssueNext(),
              "plan finish once");
      WMTSparseMappingResult result = {};
      require(plan->Wait(0, result) &&
                  result.state == WMTSparseMappingSealedPending &&
                  !plan->Retire(result) && !deaths(),
              "pending plan retired");
    }
    sparse.signalEvent(done, 1);
    require(logical(0, 1, 0, self ? 0 : 1) && logical(0, 2, 0, self ? 1 : 2) &&
                logical(1, 0, partial || self ? 2 : 0, 0) &&
                logical(1, 1, partial || self ? 2 : 0, 1) &&
                logical(1, 2, 2, 2),
            "logical publication / partial rejection");
    std::printf("PLAN_PENDING issued=%u logical=%s destroyed=%u\n", issued,
                partial ? "prefix-unpublished" : "accepted", deaths());
    std::fflush(stdout);
    if (release) {
      std::promise<void> entered;
      auto started = entered.get_future();
      auto worker = std::async(std::launch::async,
                               [owned = std::move(plans), &entered]() mutable {
                                 auto pool = WMT::MakeAutoreleasePool();
                                 entered.set_value();
                                 owned.clear();
                               });
      require(started.wait_for(std::chrono::seconds(10)) ==
                      std::future_status::ready &&
                  worker.wait_for(std::chrono::milliseconds(40)) ==
                      std::future_status::timeout &&
                  !deaths(),
              "plan drain ownership");
      gate.signalValue(1);
      require(worker.wait_for(std::chrono::seconds(10)) ==
                  std::future_status::ready,
              "plan release drain");
      worker.get();
    } else {
      gate.signalValue(1);
      for (auto &plan : plans) {
        WMTSparseMappingResult result = {};
        require(plan->Wait(10000, result) &&
                    result.state == WMTSparseMappingGPUComplete && !deaths(),
                "plan GPU boundary");
      }
      std::fill(out, out + 2 * words + guards, 0xa5a55a5au);
      auto read = MTLCommandQueue_commandBuffer(classic.handle);
      MTLCommandBuffer_encodeWaitForEvent(read, done.handle, 1);
      enc = MTLCommandBuffer_blitCommandEncoder(read);
      require(read && enc, "plan read objects");
      blit(enc, source.handle, output.handle, 0);
      blit(enc, dest.handle, output.handle, bytes);
      MTLCommandEncoder_endEncoding(enc);
      MTLCommandBuffer_commit(read);
      MTLCommandBuffer_waitUntilCompleted(read);
      require(MTLCommandBuffer_status(read) == WMTCommandBufferStatusCompleted,
              "plan read completed");
      unsigned bad = 0, guard = 0;
      for (unsigned n = 0; n < 2; ++n)
        for (uint64_t w = 0; w < words; ++w) {
          auto page = w / page_words;
          unsigned heap = n && (self || page >= (partial ? 1u : 2u));
          uint64_t index = self && !n && page ? w - page_words : w;
          auto want = pattern(heap, index);
          if (negative && !n && w + 1 == words)
            want ^= 1;
          bad += out[n * words + w] != want;
        }
      for (uint64_t w = 2 * words; w < 2 * words + guards; ++w)
        guard += out[w] != 0xa5a55a5au;
      std::printf("PLAN_READBACK words=%llu bad=%u guard=%u\n",
                  (unsigned long long)(2 * words), bad, guard);
      require(bad == unsigned(negative) && !guard, "plan read oracle");
      for (auto &plan : plans) {
        WMTSparseMappingResult result = {};
        require(plan->Retire(result) && !plan->Retire(result),
                "plan retirement once");
      }
      plans.clear();
    }
    require(deaths() == 5, "plan COM destruction");
    WMTSparseBarrierResult feedback = {};
    require(barrier.wait(10000, feedback) &&
                feedback.state == WMTSparseBarrierCompleted,
            "plan feedback");
    std::printf("PLAN_RESULT seed=%u self=%u partial=%u release=%u issued=%u "
                "destroyed=5 failures=%u drained=1\n",
                seed, self, partial, release, issued, negative);
    public_device->Release();
    return negative ? 1 : 0;
  }
  WMTSparseBufferBatchOperation ops[3] = {};
  ops[0].destination = source.handle;
  ops[0].heap = heaps[0]->GetMetalHeap().handle;
  ops[0].length = 2;
  ops[1].destination = dest.handle;
  ops[1].heap = heaps[2]->GetMetalHeap().handle;
  ops[1].length = 2;
  ops[2].kind = 1;
  ops[2].source = source.handle;
  ops[2].destination = dest.handle;
  ops[2].length = 1;
  payload->completion = sparse.prepareBufferBatch(ops, 3);
  const bool prepare_failure = !std::strcmp(mode, "payload-prepare-failure");
  require(prepare_failure ? payload->completion == nullptr
                          : payload->completion != nullptr,
          "prepare native");
  if (!native_only && !prepare_failure)
    require(resources[0]->CaptureTileMappingOwners({0}, *payload) ==
                    E_INVALIDARG &&
                payload->resources.size() == 2 && payload->heaps.size() == 5,
            "sealed capture");
  // Remove all logical/public ownership; only the private payload may keep COM
  // destructors from unregistering the global residency allocations.
  D3D12_TILE_RANGE_FLAGS nullflag = D3D12_TILE_RANGE_FLAG_NULL;
  for (auto r : resources)
    require(SUCCEEDED(r->UpdateTileMappings(1, &start, &region, nullptr, 1,
                                            &nullflag, nullptr, &count,
                                            D3D12_TILE_MAPPING_FLAG_NONE, {})),
            "logical fixture clear");
  bool public_zero = true;
  for (auto r : public_resources)
    public_zero &= r->Release() == 0;
  for (auto h : public_heaps)
    public_zero &= h->Release() == 0;
  require(public_zero, "private retains changed public count");
  unsigned failures = 0;
  std::printf("PAYLOAD_DROPPED public_zero=1 destroyed=%u resources=%zu "
              "heaps=%zu native_only=%u\n",
              deaths(), payload->resources.size(), payload->heaps.size(),
              native_only);
  std::fflush(stdout);
  if (native_only) {
    require(deaths() == 5, "native-only counter oracle");
    payload.reset();
    public_device->Release();
    std::printf("PAYLOAD_RESULT negative=native-only missing_owner=1 issued=0 "
                "failures=1 drained=1\n");
    return 1;
  }
  require(!deaths(), "premature COM destruction");
  if (prepare_failure || !std::strcmp(mode, "payload-rejected")) {
    WMTSparseMappingResult result = {};
    if (prepare_failure)
      require(!payload->Retire(result),
              "failed preparation retired as complete");
    else
      require(payload->completion.seal(WMTSparseMappingHostRejected) &&
                  payload->Retire(result) &&
                  result.state == WMTSparseMappingNotIssued &&
                  !result.possibly_issued_calls,
              "rejected payload retirement");
    payload.reset();
    require(deaths() == 5, "zero-issue cleanup");
    std::printf("PAYLOAD_ZERO prepare_failure=%u rejected=%u issued=0 "
                "destroyed=%u drained=1\n",
                prepare_failure, !prepare_failure, deaths());
    public_device->Release();
    return 0;
  }
  auto gate = metal.newSharedEvent(), cancel = metal.newSharedEvent(),
       done = metal.newSharedEvent();
  require(gate && cancel && done, "events");
  auto barrier = sparse.prepareBarrierBeforeResourceState();
  require(barrier != nullptr, "barrier");
  sparse.waitForEvent(gate, 1);
  require(barrier.submit(), "barrier submit");
  for (unsigned i = 0; i < 3; ++i)
    require(payload->completion.issueNext(), "issue");
  require(payload->completion.seal(WMTSparseMappingHostAccepted), "seal");
  sparse.signalEvent(done, 1);
  cancel.signalValue(UINT64_MAX);
  WMTSparseMappingResult result = {};
  require(payload->completion.wait(40, result) &&
              result.state == WMTSparseMappingSealedPending &&
              result.event_value == 0 && result.possibly_issued_calls == 3 &&
              !payload->Retire(result) && !deaths(),
          "pending ownership");
  std::printf(
      "PAYLOAD_PENDING state=%u private=%llu cancel=%llu destroyed=%u\n",
      result.state, (unsigned long long)result.event_value,
      (unsigned long long)cancel.signaledValue(), deaths());
  std::fflush(stdout);
  if (release_pending) {
    std::promise<void> entered;
    auto started = entered.get_future();
    auto release = std::async(std::launch::async,
                              [owned = std::move(payload), &entered]() mutable {
                                auto worker_pool = WMT::MakeAutoreleasePool();
                                entered.set_value();
                                owned.reset();
                              });
    require(started.wait_for(std::chrono::seconds(10)) ==
                std::future_status::ready,
            "release worker not started");
    require(release.wait_for(std::chrono::milliseconds(40)) ==
                    std::future_status::timeout &&
                !deaths(),
            "early payload final release");
    gate.signalValue(1);
    require(release.wait_for(std::chrono::seconds(10)) ==
                std::future_status::ready,
            "payload release drain");
    release.get();
    require(deaths() == 5, "payload destructor missed COM refs");
  } else {
    gate.signalValue(1);
    require(payload->completion.wait(10000, result) &&
                result.state == WMTSparseMappingGPUComplete &&
                result.event_value == 1 && !deaths(),
            "GPU boundary");
    std::fill(out, out + 2 * words + guards, 0xa5a55a5au);
    auto read = MTLCommandQueue_commandBuffer(classic.handle);
    MTLCommandBuffer_encodeWaitForEvent(read, done.handle, 1);
    enc = MTLCommandBuffer_blitCommandEncoder(read);
    require(read && enc, "read objects");
    blit(enc, source.handle, output.handle, 0);
    blit(enc, dest.handle, output.handle, bytes);
    MTLCommandEncoder_endEncoding(enc);
    MTLCommandBuffer_commit(read);
    MTLCommandBuffer_waitUntilCompleted(read);
    require(MTLCommandBuffer_status(read) == WMTCommandBufferStatusCompleted,
            "read failed");
    unsigned bad = 0, guard = 0;
    for (unsigned n = 0; n < 2; ++n)
      for (uint64_t w = 0; w < words; ++w) {
        unsigned h = n && w >= page_words;
        auto want = pattern(h, w);
        if (wrong_word && !n && w == words - 1)
          want ^= 1;
        bad += out[n * words + w] != want;
      }
    for (uint64_t w = 2 * words; w < 2 * words + guards; ++w)
      guard += out[w] != 0xa5a55a5au;
    failures = bad || guard;
    std::printf("PAYLOAD_READBACK words=%llu bad=%u guard=%u destroyed=%u\n",
                (unsigned long long)(2 * words), bad, guard, deaths());
    std::fflush(stdout);
    require(payload->Retire(result) && !payload->Retire(result) &&
                payload->resources.empty() && payload->heaps.empty() &&
                deaths() == 5,
            "exact payload retirement");
    payload.reset();
  }
  WMTSparseBarrierResult feedback = {};
  require(barrier.wait(10000, feedback) &&
              feedback.state == WMTSparseBarrierCompleted,
          "feedback");
  for (auto &n : destroyed)
    require(n.load() == 1, "duplicate COM destruction");
  std::printf("PAYLOAD_RESULT seed=%u release=%u words=%llu destroyed=%u "
              "failures=%u drained=1\n",
              seed, release_pending,
              (unsigned long long)(release_pending ? 0 : 2 * words), deaths(),
              failures);
  std::fflush(stdout);
  public_device->Release();
  return failures ? 1 : 0;
}

int CheckSparseMappingCredential(const char *mode, uint32_t seed) {
  const bool terminal = !std::strncmp(mode, "mapping-terminal", 16);
  const bool terminal_event =
      !std::strcmp(mode, "mapping-terminal-event") ||
      !std::strcmp(mode, "mapping-terminal-event-delay");
  const bool terminal_partial = !std::strcmp(mode, "mapping-terminal-partial"),
             terminal_delay =
                 !std::strcmp(mode, "mapping-terminal-delay") ||
                 !std::strcmp(mode, "mapping-terminal-event-delay"),
             terminal_wrong =
                 !std::strcmp(mode, "mapping-terminal-wrong-event"),
             terminal_word = !std::strcmp(mode, "mapping-terminal-wrong-word"),
             terminal_failure =
                 !std::strcmp(mode, "mapping-terminal-prepare-failure");
  static_assert(sizeof(WMTSparseBufferBatchOperation) == 64);
  static_assert(offsetof(WMTSparseBufferBatchOperation, source) == 8);
  static_assert(offsetof(WMTSparseBufferBatchOperation, offset) == 48);
  static_assert(sizeof(WMTSparseMappingResult) == 32);
  static_assert(offsetof(WMTSparseMappingResult, event_value) == 16);
  auto require = [](bool ok, const char *message) {
    if (!ok) {
      std::fprintf(stderr, "MAPPING_FATAL %s\n", message);
      std::fflush(nullptr);
      std::_Exit(3);
    }
  };
  HMODULE bridge = GetModuleHandleA("winemetal.dll");
  require(bridge != nullptr, "WineMetal not loaded");
#define MAP_BIND(name)                                                         \
  auto name =                                                                  \
      reinterpret_cast<decltype(&::name)>(GetProcAddress(bridge, #name));      \
  require(name != nullptr, "missing " #name)
  MAP_BIND(NSObject_release);
  MAP_BIND(NSAutoreleasePool_alloc_init);
  MAP_BIND(WMTCopyAllDevices);
  MAP_BIND(NSArray_count);
  MAP_BIND(NSArray_object);
  MAP_BIND(MTLDevice_supportsFamily);
  MAP_BIND(MTLDevice_supportsPlacementSparse);
  MAP_BIND(MTLDevice_newSparseMappingQueue);
  MAP_BIND(MTLDevice_newCommandQueue);
  MAP_BIND(MTLDevice_newBuffer);
  MAP_BIND(MTLDevice_newPlacementSparseBuffer);
  MAP_BIND(MTLDevice_newHeap);
  MAP_BIND(MTLHeap_newBuffer);
  MAP_BIND(MTLDevice_newResidencySet);
  MAP_BIND(MTLResidencySet_addAllocations);
  MAP_BIND(MTLResidencySet_commit);
  MAP_BIND(MTLCommandQueue_addResidencySet);
  MAP_BIND(SparseMappingQueue_addResidencySet);
  MAP_BIND(MTLDevice_newSharedEvent);
  MAP_BIND(MTLSharedEvent_signalValue);
  MAP_BIND(MTLSharedEvent_signaledValue);
  MAP_BIND(MTLSharedEvent_waitUntilSignaledValue);
  MAP_BIND(SparseMappingQueue_waitForEvent);
  MAP_BIND(SparseMappingQueue_signalEvent);
  MAP_BIND(SparseMappingQueue_prepareBarrierBeforeResourceState);
  MAP_BIND(SparseBarrierCompletion_submit);
  MAP_BIND(SparseBarrierCompletion_wait);
  MAP_BIND(SparseMappingQueue_prepareBufferBatch);
  MAP_BIND(SparseMappingCompletion_prepareTerminal);
  MAP_BIND(SparseMappingCompletion_issueNext);
  MAP_BIND(SparseMappingCompletion_seal);
  MAP_BIND(SparseMappingCompletion_wait);
  MAP_BIND(SparseMappingCompletion_retireInputs);
  MAP_BIND(MTLCommandQueue_commandBuffer);
  MAP_BIND(MTLCommandBuffer_blitCommandEncoder);
  MAP_BIND(MTLBlitCommandEncoder_encodeCommands);
  MAP_BIND(MTLCommandEncoder_endEncoding);
  MAP_BIND(MTLCommandBuffer_commit);
  MAP_BIND(MTLCommandBuffer_waitUntilCompleted);
  MAP_BIND(MTLCommandBuffer_status);
  MAP_BIND(MTLCommandBuffer_encodeWaitForEvent);
  MAP_BIND(MTLCommandBuffer_encodeSignalEvent);
#undef MAP_BIND
  for (const char *symbol :
       {"SparseMappingQueue_prepareBufferBatch",
        "SparseMappingCompletion_prepareTerminal",
        "SparseMappingCompletion_issueNext", "SparseMappingCompletion_seal",
        "SparseMappingCompletion_wait",
        "SparseMappingCompletion_retireInputs"}) {
    auto code =
        reinterpret_cast<const unsigned char *>(GetProcAddress(bridge, symbol));
    std::printf("LOADED_CODE winemetal.dll %s ", symbol);
    for (unsigned i = 0; i < 16; ++i)
      std::printf("%02x", code[i]);
    std::printf("\n");
  }
  char module[MAX_PATH] = {};
  GetModuleFileNameA(bridge, module, MAX_PATH);
  std::printf("MAPPING_MODULE %s\n", module);
  std::vector<obj_handle_t> owned;
  auto own = [&](obj_handle_t h) {
    require(h != 0, "native creation failed");
    owned.push_back(h);
    return h;
  };
  own(NSAutoreleasePool_alloc_init());
  obj_handle_t devices = own(WMTCopyAllDevices());
  require(NSArray_count(devices) > 0, "no device");
  obj_handle_t device = NSArray_object(devices, 0);
  require(MTLDevice_supportsFamily(device, WMTGPUFamilyMetal4) &&
              MTLDevice_supportsPlacementSparse(device),
          "real sparse unavailable");
  std::printf("MAPPING_DEVICE apple7=%u apple8=%u apple9=%u metal4=%u "
              "placement=%u seed=%u\n",
              MTLDevice_supportsFamily(device, WMTGPUFamilyApple7),
              MTLDevice_supportsFamily(device, WMTGPUFamilyApple8),
              MTLDevice_supportsFamily(device, WMTGPUFamilyApple9),
              MTLDevice_supportsFamily(device, WMTGPUFamilyMetal4),
              MTLDevice_supportsPlacementSparse(device), seed);
  obj_handle_t sparse = own(MTLDevice_newSparseMappingQueue(device)),
               classic = own(MTLDevice_newCommandQueue(device, 32));
  constexpr uint64_t bytes = 131072, words = bytes / 4, page_words = 65536 / 4,
                     guards = 32;
  const auto options = static_cast<WMTResourceOptions>(
      WMTResourceStorageModePrivate | WMTResourceHazardTrackingModeUntracked);
  WMTHeapInfo hi = {};
  hi.size = bytes;
  hi.options = options;
  hi.type = WMTHeapTypePlacement;
  hi.sparse_page_size = WMTSparsePageSize64;
  obj_handle_t heaps[2], placed[2], upload[2];
  WMTBufferInfo ui[2] = {};
  auto pattern = [&](unsigned h, uint64_t i) {
    return (seed ^ (h ? 0x13579bdfu : 0x2468ace0u)) +
           uint32_t(i / page_words) * 0x27101983u +
           uint32_t(i % page_words) * 0x9e3779b9u;
  };
  for (unsigned h = 0; h < 2; ++h) {
    heaps[h] = own(MTLDevice_newHeap(device, &hi));
    WMTBufferInfo pi = {};
    pi.length = bytes;
    pi.options = options;
    placed[h] = own(MTLHeap_newBuffer(heaps[h], &pi, 0));
    ui[h].length = bytes;
    ui[h].options = WMTResourceStorageModeShared;
    upload[h] = own(MTLDevice_newBuffer(device, &ui[h]));
    require(ui[h].memory.ptr != nullptr, "upload memory");
    auto data = static_cast<uint32_t *>(ui[h].memory.ptr);
    for (uint64_t i = 0; i < words; ++i)
      data[i] = pattern(h, i);
  }
  WMTBufferInfo info = {};
  info.length = bytes;
  info.options = options;
  obj_handle_t source = own(
      MTLDevice_newPlacementSparseBuffer(device, &info, WMTSparsePageSize64));
  obj_handle_t dest = own(
      MTLDevice_newPlacementSparseBuffer(device, &info, WMTSparsePageSize64));
  WMTBufferInfo oi = {};
  oi.length = (2 * words + guards) * 4;
  oi.options = WMTResourceStorageModeShared;
  obj_handle_t output = own(MTLDevice_newBuffer(device, &oi));
  auto out = static_cast<uint32_t *>(oi.memory.ptr);
  require(out != nullptr, "readback memory");
  obj_handle_t error = 0,
               resident = own(MTLDevice_newResidencySet(device, 9, &error));
  require(!error, "residency error");
  obj_handle_t allocations[] = {heaps[0],  heaps[1],  placed[0],
                                placed[1], upload[0], upload[1],
                                source,    dest,      output};
  MTLResidencySet_addAllocations(resident, allocations, 9);
  MTLResidencySet_commit(resident);
  MTLCommandQueue_addResidencySet(classic, resident);
  SparseMappingQueue_addResidencySet(sparse, resident);
  auto blitcopy = [&](obj_handle_t encoder, obj_handle_t src, obj_handle_t dst,
                      uint64_t offset) {
    wmtcmd_blit_copy_from_buffer_to_buffer cmd = {};
    cmd.type = WMTBlitCommandCopyFromBufferToBuffer;
    cmd.src = src;
    cmd.dst = dst;
    cmd.dst_offset = offset;
    cmd.copy_length = bytes;
    MTLBlitCommandEncoder_encodeCommands(
        encoder, reinterpret_cast<const wmtcmd_base *>(&cmd));
  };
  obj_handle_t init = MTLCommandQueue_commandBuffer(classic),
               enc = MTLCommandBuffer_blitCommandEncoder(init);
  require(init && enc, "producer objects");
  for (unsigned h = 0; h < 2; ++h)
    blitcopy(enc, upload[h], placed[h], 0);
  MTLCommandEncoder_endEncoding(enc);
  obj_handle_t produced = own(MTLDevice_newSharedEvent(device));
  MTLCommandBuffer_encodeSignalEvent(init, produced, 1);
  MTLCommandBuffer_commit(init);
  MTLCommandBuffer_waitUntilCompleted(init);
  SparseMappingQueue_waitForEvent(sparse, produced, 1);
  require(MTLCommandBuffer_status(init) == WMTCommandBufferStatusCompleted,
          "producer failed");
  const bool partial =
                 terminal_partial || !std::strcmp(mode, "mapping-partial"),
             wrong_event =
                 terminal_wrong || !std::strcmp(mode, "mapping-wrong-event"),
             wrong_word =
                 terminal_word || !std::strcmp(mode, "mapping-wrong-word");
  WMTSparseMappingResult result = {};
  require(!SparseMappingQueue_prepareBufferBatch(0, nullptr, 0) &&
              !SparseMappingQueue_prepareBufferBatch(sparse, nullptr, 1) &&
              !SparseMappingCompletion_prepareTerminal(0, 0) &&
              !SparseMappingCompletion_issueNext(0) &&
              !SparseMappingCompletion_seal(0, WMTSparseMappingHostAccepted) &&
              !SparseMappingCompletion_wait(0, 0, &result) &&
              !SparseMappingCompletion_retireInputs(0),
          "null transport accepted");
  if (!std::strcmp(mode, "mapping-prepare-failure")) {
    require(!SparseMappingQueue_prepareBufferBatch(sparse, nullptr, 0),
            "prepare injection did not reject");
    for (auto i = owned.rbegin(); i != owned.rend(); ++i)
      NSObject_release(*i);
    std::printf("MAPPING_PREPARE_FAILURE PASS calls=0 drained=1\n");
    return 0;
  }
  if (terminal_failure) {
    auto c = SparseMappingQueue_prepareBufferBatch(sparse, nullptr, 0);
    require(c && !SparseMappingCompletion_prepareTerminal(c, 1),
            "terminal prepare failure");
    require(SparseMappingCompletion_seal(c, WMTSparseMappingHostRejected) &&
                SparseMappingCompletion_retireInputs(c),
            "prepare failure unissued");
    NSObject_release(c);
    for (auto i = owned.rbegin(); i != owned.rend(); ++i)
      NSObject_release(*i);
    std::printf("MAPPING_TERMINAL_PREPARE_FAILURE PASS calls=0 drained=1\n");
    return 0;
  }
  auto zero = own(SparseMappingQueue_prepareBufferBatch(sparse, nullptr, 0));
  require(!SparseMappingCompletion_prepareTerminal(zero, 4),
          "invalid terminal flags");
  if (terminal)
    require(SparseMappingCompletion_prepareTerminal(zero, 1),
            "empty terminal prepare");
  require(
      !SparseMappingCompletion_wait(zero, 0, nullptr) &&
          !SparseMappingCompletion_issueNext(zero) &&
          !SparseMappingCompletion_seal(zero,
                                        WMTSparseMappingHostFailedAfterIssue) &&
          SparseMappingCompletion_seal(zero, WMTSparseMappingHostAccepted) &&
          SparseMappingCompletion_wait(zero, 0, &result) &&
          result.state == WMTSparseMappingNotIssued &&
          !result.possibly_issued_calls &&
          SparseMappingCompletion_retireInputs(zero),
      "empty contract");
  WMTSparseBufferBatchOperation valid = {};
  valid.destination = dest;
  valid.heap = heaps[1];
  valid.length = 2;
  for (unsigned invalid_case = 0; invalid_case < 8; ++invalid_case) {
    auto invalid = valid;
    switch (invalid_case) {
    case 0:
      invalid.kind = 2;
      break;
    case 1:
      invalid.length = 0;
      break;
    case 2:
      invalid.location = UINT64_MAX;
      break;
    case 3:
      invalid.offset = UINT64_MAX;
      break;
    case 4:
      invalid.mode = 1;
      break; // Unmap cannot carry a heap.
    case 5:
      invalid.kind = 1;
      invalid.source = source;
      break; // Copy cannot carry a heap.
    case 6:
      invalid.destination = 0;
      break;
    case 7:
      invalid.mode = 2;
      break;
    }
    require(!SparseMappingQueue_prepareBufferBatch(sparse, &invalid, 1),
            "invalid shape accepted");
  }
  require(!SparseMappingQueue_prepareBufferBatch(sparse, &valid, UINT64_MAX),
          "overflow count accepted");
  auto rejected = SparseMappingQueue_prepareBufferBatch(sparse, &valid, 1);
  require(rejected != 0, "rejected prepare");
  if (terminal)
    require(SparseMappingCompletion_prepareTerminal(rejected, 1),
            "rejected terminal prepare");
  require(
      !SparseMappingCompletion_seal(rejected, WMTSparseMappingHostUnset) &&
          !SparseMappingCompletion_seal(
              rejected, static_cast<WMTSparseMappingHostOutcome>(4)) &&
          SparseMappingCompletion_seal(rejected,
                                       WMTSparseMappingHostRejected) &&
          SparseMappingCompletion_wait(rejected, UINT64_MAX, &result) &&
          result.state == WMTSparseMappingNotIssued &&
          result.host_outcome == WMTSparseMappingHostRejected &&
          !result.possibly_issued_calls &&
          !SparseMappingCompletion_issueNext(rejected) &&
          SparseMappingCompletion_retireInputs(rejected) &&
          !SparseMappingCompletion_seal(rejected, WMTSparseMappingHostRejected),
      "rejected contract");
  NSObject_release(rejected);
  auto abandoned = SparseMappingQueue_prepareBufferBatch(sparse, &valid, 1);
  require(abandoned != 0, "unissued release prepare");
  if (terminal)
    require(SparseMappingCompletion_prepareTerminal(abandoned, 1),
            "abandoned terminal prepare");
  NSObject_release(abandoned);
  std::printf(
      "MAPPING_HOST_CONTRACT invalid=9 rejected=1 abandoned=1 calls=0\n");
  unsigned failures = 0, readbacks = 0;
  uint64_t checked = 0;
  for (unsigned step = 0; step < 5; ++step) {
    WMTSparseBufferBatchOperation ops[2] = {};
    ops[0].kind = step == 1 ? 1 : 0;
    ops[0].mode = step == 3 ? 1 : 0;
    ops[0].source = step == 1 ? source : 0;
    ops[0].destination = step == 0 ? source : dest;
    ops[0].heap = step == 1 || step == 3 ? 0 : heaps[step == 0 ? 0 : 1];
    ops[0].location = step == 2 ? 1 : 0;
    ops[0].length = step == 2 ? 1 : 2;
    ops[0].offset = step == 2 ? 1 : 0;
    ops[1] = ops[0];
    ops[1].kind = 0;
    ops[1].mode = 0;
    ops[1].source = 0;
    ops[1].heap = heaps[step == 0 || step == 1 ? 1 : 0];
    ops[1].location = 0;
    ops[1].length = 2;
    ops[1].offset = 0;
    auto invalid = ops[0];
    invalid.reserved = 1;
    require(!SparseMappingQueue_prepareBufferBatch(sparse, &invalid, 1),
            "reserved input accepted");
    const bool paired = !partial && step == 0;
    if (paired) {
      ops[0].length = 1;
      ops[1] = ops[0];
      ops[1].location = 1;
      ops[1].offset = 1;
    }
    obj_handle_t c = SparseMappingQueue_prepareBufferBatch(
        sparse, ops, partial || paired ? 2 : 1);
    require(c != 0, "prepare batch");
    if (terminal)
      require(SparseMappingCompletion_prepareTerminal(
                  c, (terminal_event ? 0 : 1) | (terminal_delay ? 2 : 0)) &&
                  !SparseMappingCompletion_prepareTerminal(c, 1),
              "terminal prepare once");
    ops[0] = ops[1];
    ops[0].heap = heaps[step == 0 || step == 1 ? 1 : 0];
    require(SparseMappingCompletion_wait(c, UINT64_MAX, &result) &&
                result.state == WMTSparseMappingPrepared &&
                !result.possibly_issued_calls &&
                !SparseMappingCompletion_retireInputs(c) &&
                !SparseMappingCompletion_seal(c, WMTSparseMappingHostAccepted),
            "prepared contract");
    obj_handle_t gate = own(MTLDevice_newSharedEvent(device)),
                 cancel = own(MTLDevice_newSharedEvent(device)),
                 done = own(MTLDevice_newSharedEvent(device));
    obj_handle_t barrier =
        own(SparseMappingQueue_prepareBarrierBeforeResourceState(sparse));
    // Settle the preceding callback before the gate, matching E13's controlled
    // boundary. This callback alone must not authorize mapping retirement.
    if (terminal) {
      WMTSparseBarrierResult pre = {};
      require(SparseBarrierCompletion_submit(barrier) &&
                  SparseBarrierCompletion_wait(barrier, 10000, &pre) &&
                  pre.state == WMTSparseBarrierCompleted,
              "terminal pre settled");
    }
    SparseMappingQueue_waitForEvent(sparse, gate, 1);
    if (!terminal)
      require(SparseBarrierCompletion_submit(barrier), "barrier submit");
    require(SparseMappingCompletion_issueNext(c) &&
                SparseMappingCompletion_wait(c, 0, &result) &&
                result.state == WMTSparseMappingIssuing &&
                result.possibly_issued_calls == 1 &&
                !SparseMappingCompletion_retireInputs(c),
            "issue contract");
    require(
        !SparseMappingCompletion_prepareTerminal(c, 1) &&
            !SparseMappingCompletion_seal(c, WMTSparseMappingHostRejected) &&
            !SparseMappingCompletion_seal(c, WMTSparseMappingHostUnset),
        "issuing outcome accepted");
    if (paired)
      require(!SparseMappingCompletion_seal(c, WMTSparseMappingHostAccepted) &&
                  SparseMappingCompletion_issueNext(c),
              "full batch issue");
    const uint64_t issued = paired ? 2 : 1;
    auto host = partial ? WMTSparseMappingHostFailedAfterIssue
                        : WMTSparseMappingHostAccepted;
    require(SparseMappingCompletion_seal(c, host) &&
                !SparseMappingCompletion_seal(c, host) &&
                !SparseMappingCompletion_issueNext(c),
            "single close");
    if (!terminal)
      SparseMappingQueue_signalEvent(sparse, done, 1);
    MTLSharedEvent_signalValue(cancel, UINT64_MAX);
    std::fill(out, out + 2 * words + guards, 0xa5a55a5au);
    require(SparseMappingCompletion_wait(c, 40, &result), "pending transport");
    uint64_t observed = wrong_event && step == 0
                            ? MTLSharedEvent_signaledValue(cancel)
                            : result.event_value;
    bool pending = result.state == WMTSparseMappingSealedPending &&
                   result.event_value == 0 && observed == 0 &&
                   !SparseMappingCompletion_retireInputs(c) &&
                   MTLSharedEvent_signaledValue(done) == 0;
    if (!pending)
      ++failures;
    std::printf("MAPPING_PENDING step=%u state=%u private=%llu observed=%llu "
                "host=%u pass=%u\n",
                step, result.state, (unsigned long long)result.event_value,
                (unsigned long long)observed, result.host_outcome, pending);
    MTLSharedEvent_signalValue(gate, 1);
    if (terminal_delay && !terminal_event) {
      require(SparseMappingCompletion_wait(c, 40, &result) &&
                  result.state == WMTSparseMappingSealedPending &&
                  !result.event_value &&
                  !SparseMappingCompletion_retireInputs(c),
              "terminal report delay");
      std::printf("MAPPING_TERMINAL_DELAY step=%u pending=1 retired=0\n", step);
    }
    require(SparseMappingCompletion_wait(c, 10000, &result) &&
                result.state == WMTSparseMappingGPUComplete &&
                result.event_value == (terminal && !terminal_event ? 0u : 1u) &&
                result.host_outcome == host &&
                result.possibly_issued_calls == issued && result.seals == 1,
            "mapping completion");
    require(terminal ? MTLSharedEvent_signaledValue(done) == 0
                     : MTLSharedEvent_waitUntilSignaledValue(done, 1, 10000),
            "ordered completion");
    WMTSparseBarrierResult feedback = {};
    require(SparseBarrierCompletion_wait(barrier, 10000, &feedback) &&
                feedback.state == WMTSparseBarrierCompleted &&
                feedback.native_feedback_received,
            "barrier feedback");
    unsigned reads = step == 0 ? 1 : (step == 3 ? 0 : 2);
    obj_handle_t read = MTLCommandQueue_commandBuffer(classic);
    if (!terminal)
      MTLCommandBuffer_encodeWaitForEvent(read, done, 1);
    enc = MTLCommandBuffer_blitCommandEncoder(read);
    require(read && enc, "readback objects");
    for (unsigned n = 0; n < reads; ++n)
      blitcopy(enc, n ? dest : source, output, n * bytes);
    MTLCommandEncoder_endEncoding(enc);
    MTLCommandBuffer_commit(read);
    MTLCommandBuffer_waitUntilCompleted(read);
    require(MTLCommandBuffer_status(read) == WMTCommandBufferStatusCompleted,
            "readback failure");
    unsigned bad = 0, guard = 0;
    for (unsigned n = 0; n < reads; ++n) {
      for (uint64_t i = 0; i < words; ++i) {
        unsigned h =
            n ? (step == 1 ? 0 : (step == 2 ? (i >= page_words) : 1)) : 0;
        uint32_t want = pattern(h, i);
        if (wrong_word && step == 0 && i == words - 1)
          want ^= 1;
        bad += out[n * words + i] != want;
      }
      ++readbacks;
      checked += words;
    }
    for (uint64_t i = reads * words; i < 2 * words + guards; ++i)
      guard += out[i] != 0xa5a55a5au;
    if (bad || guard)
      ++failures;
    require(SparseMappingCompletion_retireInputs(c) &&
                !SparseMappingCompletion_retireInputs(c) &&
                SparseMappingCompletion_wait(c, 0, &result) &&
                result.inputs_retired == 1,
            "retire contract");
    NSObject_release(c);
    std::printf("MAPPING_COMPLETE step=%u reads=%u bad=%u guard=%u host=%u "
                "partial=%u state=%u issued=%llu seals=%u retired=%u "
                "terminal=%u private=%llu\n",
                step, reads, bad, guard, result.host_outcome, partial,
                result.state, (unsigned long long)result.possibly_issued_calls,
                result.seals, result.inputs_retired, terminal,
                (unsigned long long)result.event_value);
  }
  // Final native release cannot free inputs while the gate still holds work.
  // Exercise both explicitly sealed and destructor-sealed valid prefixes.
  for (unsigned sealed = 0; sealed < 2; ++sealed) {
    WMTSparseBufferBatchOperation op = {};
    op.destination = dest;
    op.heap = heaps[1];
    op.length = 2;
    auto c = SparseMappingQueue_prepareBufferBatch(sparse, &op, 1);
    require(c != 0, "release prepare");
    if (terminal)
      require(SparseMappingCompletion_prepareTerminal(
                  c, (terminal_event ? 0 : 1) | (terminal_delay ? 2 : 0)),
              "release terminal prepare");
    auto gate = own(MTLDevice_newSharedEvent(device));
    auto barrier =
        own(SparseMappingQueue_prepareBarrierBeforeResourceState(sparse));
    SparseMappingQueue_waitForEvent(sparse, gate, 1);
    require(SparseBarrierCompletion_submit(barrier) &&
                SparseMappingCompletion_issueNext(c),
            "release issue");
    if (sealed)
      require(SparseMappingCompletion_seal(c, WMTSparseMappingHostAccepted),
              "release seal");
    auto release = std::async(std::launch::async, [=] {
      auto pool = NSAutoreleasePool_alloc_init();
      NSObject_release(c);
      NSObject_release(pool);
    });
    require(release.wait_for(std::chrono::milliseconds(40)) ==
                std::future_status::timeout,
            "early final release returned");
    MTLSharedEvent_signalValue(gate, 1);
    require(release.wait_for(std::chrono::seconds(10)) ==
                std::future_status::ready,
            "release drain timeout");
    release.get();
    WMTSparseBarrierResult feedback = {};
    require(SparseBarrierCompletion_wait(barrier, 10000, &feedback) &&
                feedback.state == WMTSparseBarrierCompleted,
            "release barrier feedback");
    std::printf("MAPPING_RELEASE sealed=%u waited=1 actual_drained=1\n",
                sealed);
  }
  // All actual mapping and readback work drained before native refs/residency
  // die.
  for (auto i = owned.rbegin(); i != owned.rend(); ++i)
    NSObject_release(*i);
  std::printf(
      "MAPPING_RESULT seed=%u readbacks=%u words=%llu failures=%u drained=1\n",
      seed, readbacks, (unsigned long long)checked, failures);
  return failures ? 1 : 0;
}

void CheckSparseFeedback(const char *mode) {
  using namespace std::chrono_literals;
  static_assert(sizeof(WMTSparseBarrierResult) == 56);
  static_assert(offsetof(WMTSparseBarrierResult, native_error_code) == 16);
  static_assert(offsetof(WMTSparseBarrierResult, native_error) == 24);
  static_assert(offsetof(WMTSparseBarrierResult, gpu_start_time) == 32);
  static_assert(offsetof(WMTSparseBarrierResult, callback_count) == 48);
  auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  // Resolve only in feedback modes, preserving native Windows use of the
  // existing probe.
  HMODULE bridge = GetModuleHandleA("winemetal.dll");
  require(bridge != nullptr, "feedback probe requires loaded DXMT WineMetal");
  auto NSObject_release = reinterpret_cast<decltype(&::NSObject_release)>(
      GetProcAddress(bridge, "NSObject_release"));
  require(NSObject_release != nullptr,
          "missing WineMetal export NSObject_release");
  auto NSAutoreleasePool_alloc_init =
      reinterpret_cast<decltype(&::NSAutoreleasePool_alloc_init)>(
          GetProcAddress(bridge, "NSAutoreleasePool_alloc_init"));
  require(NSAutoreleasePool_alloc_init != nullptr,
          "missing WineMetal export NSAutoreleasePool_alloc_init");
  auto WMTCopyAllDevices = reinterpret_cast<decltype(&::WMTCopyAllDevices)>(
      GetProcAddress(bridge, "WMTCopyAllDevices"));
  require(WMTCopyAllDevices != nullptr,
          "missing WineMetal export WMTCopyAllDevices");
  auto NSArray_count = reinterpret_cast<decltype(&::NSArray_count)>(
      GetProcAddress(bridge, "NSArray_count"));
  require(NSArray_count != nullptr, "missing WineMetal export NSArray_count");
  auto NSArray_object = reinterpret_cast<decltype(&::NSArray_object)>(
      GetProcAddress(bridge, "NSArray_object"));
  require(NSArray_object != nullptr, "missing WineMetal export NSArray_object");
  auto MTLDevice_supportsFamily =
      reinterpret_cast<decltype(&::MTLDevice_supportsFamily)>(
          GetProcAddress(bridge, "MTLDevice_supportsFamily"));
  require(MTLDevice_supportsFamily != nullptr,
          "missing WineMetal export MTLDevice_supportsFamily");
  auto MTLDevice_supportsPlacementSparse =
      reinterpret_cast<decltype(&::MTLDevice_supportsPlacementSparse)>(
          GetProcAddress(bridge, "MTLDevice_supportsPlacementSparse"));
  require(MTLDevice_supportsPlacementSparse != nullptr,
          "missing WineMetal export MTLDevice_supportsPlacementSparse");
  auto MTLDevice_newSparseMappingQueue =
      reinterpret_cast<decltype(&::MTLDevice_newSparseMappingQueue)>(
          GetProcAddress(bridge, "MTLDevice_newSparseMappingQueue"));
  require(MTLDevice_newSparseMappingQueue != nullptr,
          "missing WineMetal export MTLDevice_newSparseMappingQueue");
  auto MTLDevice_newSharedEvent =
      reinterpret_cast<decltype(&::MTLDevice_newSharedEvent)>(
          GetProcAddress(bridge, "MTLDevice_newSharedEvent"));
  require(MTLDevice_newSharedEvent != nullptr,
          "missing WineMetal export MTLDevice_newSharedEvent");
  auto SparseMappingQueue_prepareBarrierBeforeResourceState = reinterpret_cast<
      decltype(&::SparseMappingQueue_prepareBarrierBeforeResourceState)>(
      GetProcAddress(bridge,
                     "SparseMappingQueue_prepareBarrierBeforeResourceState"));
  require(SparseMappingQueue_prepareBarrierBeforeResourceState != nullptr,
          "missing WineMetal export "
          "SparseMappingQueue_prepareBarrierBeforeResourceState");
  auto SparseBarrierCompletion_submit =
      reinterpret_cast<decltype(&::SparseBarrierCompletion_submit)>(
          GetProcAddress(bridge, "SparseBarrierCompletion_submit"));
  require(SparseBarrierCompletion_submit != nullptr,
          "missing WineMetal export SparseBarrierCompletion_submit");
  auto SparseBarrierCompletion_wait =
      reinterpret_cast<decltype(&::SparseBarrierCompletion_wait)>(
          GetProcAddress(bridge, "SparseBarrierCompletion_wait"));
  require(SparseBarrierCompletion_wait != nullptr,
          "missing WineMetal export SparseBarrierCompletion_wait");
  auto SparseMappingQueue_waitForEvent =
      reinterpret_cast<decltype(&::SparseMappingQueue_waitForEvent)>(
          GetProcAddress(bridge, "SparseMappingQueue_waitForEvent"));
  require(SparseMappingQueue_waitForEvent != nullptr,
          "missing WineMetal export SparseMappingQueue_waitForEvent");
  auto SparseMappingQueue_signalEvent =
      reinterpret_cast<decltype(&::SparseMappingQueue_signalEvent)>(
          GetProcAddress(bridge, "SparseMappingQueue_signalEvent"));
  require(SparseMappingQueue_signalEvent != nullptr,
          "missing WineMetal export SparseMappingQueue_signalEvent");
  auto MTLSharedEvent_signalValue =
      reinterpret_cast<decltype(&::MTLSharedEvent_signalValue)>(
          GetProcAddress(bridge, "MTLSharedEvent_signalValue"));
  require(MTLSharedEvent_signalValue != nullptr,
          "missing WineMetal export MTLSharedEvent_signalValue");
  auto MTLSharedEvent_waitUntilSignaledValue =
      reinterpret_cast<decltype(&::MTLSharedEvent_waitUntilSignaledValue)>(
          GetProcAddress(bridge, "MTLSharedEvent_waitUntilSignaledValue"));
  require(MTLSharedEvent_waitUntilSignaledValue != nullptr,
          "missing WineMetal export MTLSharedEvent_waitUntilSignaledValue");
  for (const char *symbol :
       {"SparseMappingQueue_prepareBarrierBeforeResourceState",
        "SparseBarrierCompletion_submit", "SparseBarrierCompletion_wait"}) {
    auto code =
        reinterpret_cast<const unsigned char *>(GetProcAddress(bridge, symbol));
    std::printf("LOADED_CODE winemetal.dll %s ", symbol);
    for (unsigned i = 0; i < 16; ++i)
      std::printf("%02x", code[i]);
    std::printf("\n");
  }
  LARGE_INTEGER frequency, begin, prepared_at, submitted_at;
  QueryPerformanceFrequency(&frequency);
  struct OpenGateOnExit {
    obj_handle_t event;
    decltype(MTLSharedEvent_signalValue) signal;
    ~OpenGateOnExit() { signal(event, 1); }
  };
  auto own = [NSObject_release](obj_handle_t handle) {
    return std::shared_ptr<obj_handle_t>(
        handle ? new obj_handle_t(handle) : nullptr,
        [NSObject_release](obj_handle_t *value) {
          if (value)
            NSObject_release(*value);
          delete value;
        });
  };
  auto prepare = [&](const auto &queue) {
    return own(SparseMappingQueue_prepareBarrierBeforeResourceState(*queue));
  };
  auto submit = [&](const auto &completion) {
    return SparseBarrierCompletion_submit(*completion) != 0;
  };
  auto wait = [&](const auto &completion, uint64_t timeout,
                  WMTSparseBarrierResult &result) {
    return SparseBarrierCompletion_wait(*completion, timeout, &result) != 0;
  };
  auto pool = own(NSAutoreleasePool_alloc_init());
  auto devices = own(WMTCopyAllDevices());
  require(devices && NSArray_count(*devices),
          "native Metal device unavailable");
  obj_handle_t device = NSArray_object(*devices, 0);
  require(MTLDevice_supportsFamily(device, WMTGPUFamilyApple7) &&
              MTLDevice_supportsFamily(device, WMTGPUFamilyMetal4) &&
              MTLDevice_supportsPlacementSparse(device),
          "actual placement sparse capability unavailable");
  auto queue = own(MTLDevice_newSparseMappingQueue(device));
  require(bool(queue), "native sparse queue unavailable");
  const bool delay = !std::strcmp(mode, "feedback-delay");
  const bool reverse = !std::strcmp(mode, "feedback-reverse");
  const bool synthetic = !std::strcmp(mode, "feedback-synthetic");
  const bool submitted = !std::strcmp(mode, "feedback-submitted");
  const bool abandoned = !std::strcmp(mode, "feedback-release");
  const bool prepare_failure = !std::strncmp(mode, "feedback-prepare-", 17);
  QueryPerformanceCounter(&begin);
  auto completion = prepare(queue);
  QueryPerformanceCounter(&prepared_at);
  if (prepare_failure) {
    require(!completion, "expected prepare failure was not observed");
    std::cout << "SPARSE_FEEDBACK prepare_failure=PASS mode=" << mode << "\n";
    return;
  }
  require(bool(completion), "barrier preparation failed");
  WMTSparseBarrierResult result = {};
  require(!SparseMappingQueue_prepareBarrierBeforeResourceState(0) &&
              !SparseBarrierCompletion_submit(0) &&
              !SparseBarrierCompletion_wait(0, 0, &result) &&
              !SparseBarrierCompletion_wait(*completion, 0, nullptr),
          "invalid transport arguments accepted");
  require(wait(completion, UINT64_MAX, result) &&
              result.state == WMTSparseBarrierPrepared &&
              !result.callback_count,
          "prepare unexpectedly submitted work");
  // Abandoned prepared objects must be releasable without waiting on a
  // callback.
  if (!reverse) {
    auto prepared = prepare(queue);
    require(bool(prepared), "prepare-release failed");
  }
  auto gate = own(MTLDevice_newSharedEvent(device));
  auto done = own(MTLDevice_newSharedEvent(device));
  require(gate && done, "feedback event creation failed");
  OpenGateOnExit open_gate{*gate, MTLSharedEvent_signalValue};
  SparseMappingQueue_waitForEvent(*queue, *gate, 1);
  LARGE_INTEGER submit_begin;
  QueryPerformanceCounter(&submit_begin);
  require(submit(completion) != submitted,
          "unexpected synchronous submit status");
  QueryPerformanceCounter(&submitted_at);
  std::printf("SPARSE_FEEDBACK_COST prepare_us=%.3f submit_us=%.3f "
              "validation_sensitive=1\n",
              (prepared_at.QuadPart - begin.QuadPart) * 1e6 /
                  frequency.QuadPart,
              (submitted_at.QuadPart - submit_begin.QuadPart) * 1e6 /
                  frequency.QuadPart);
  require(!submit(completion), "duplicate submit accepted");
  require(wait(completion, 10, result) &&
              result.state == WMTSparseBarrierPending &&
              !result.native_feedback_received,
          "closed GPU gate did not keep completion pending");
  auto second = reverse ? prepare(queue) : decltype(completion)();
  if (reverse)
    require(second && submit(second), "second reverse credential failed");
  SparseMappingQueue_signalEvent(*queue, *done, 1);
  MTLSharedEvent_signalValue(*gate, 1);
  require(MTLSharedEvent_waitUntilSignaledValue(*done, 1, 10000),
          "feedback GPU event timeout");
  if (abandoned) {
    // Keep an explicit independent owner for inspection; drop the caller's
    // handles.
    auto observer = completion;
    completion = nullptr;
    queue = nullptr;
    require(wait(observer, 10000, result) &&
                result.state == WMTSparseBarrierCompleted,
            "caller release lost native completion");
    require(result.native_feedback_received == 1 && result.callback_count == 1,
            "release callback evidence invalid");
    // Drop the final caller credential reference while another benign barrier
    // is genuinely pending. Its copied native callback must keep the payload
    // alive.
    auto owner = own(MTLDevice_newSparseMappingQueue(device));
    auto orphan_gate = own(MTLDevice_newSharedEvent(device));
    auto orphan_done = own(MTLDevice_newSharedEvent(device));
    require(owner && orphan_gate && orphan_done, "orphan setup failed");
    OpenGateOnExit open_orphan_gate{*orphan_gate, MTLSharedEvent_signalValue};
    SparseMappingQueue_waitForEvent(*owner, *orphan_gate, 1);
    auto orphan = prepare(owner);
    require(orphan && submit(orphan), "orphan barrier submit failed");
    require(wait(orphan, 0, result) && result.state == WMTSparseBarrierPending,
            "orphan not pending");
    SparseMappingQueue_signalEvent(*owner, *orphan_done, 1);
    std::printf("SPARSE_FEEDBACK orphan=%llx pending=1 last_caller_release=1\n",
                static_cast<unsigned long long>(*orphan));
    orphan = nullptr;
    MTLSharedEvent_signalValue(*orphan_gate, 1);
    require(MTLSharedEvent_waitUntilSignaledValue(*orphan_done, 1, 10000),
            "orphan native event timeout");
    // This mode deliberately drops every credential owner. Allow its bounded
    // 200 ms test report to drain before process exit; external trace validation
    // must still observe the orphan's actual feedback and terminal destruction.
    Sleep(1000);
    std::cout << "SPARSE_FEEDBACK caller_release=PASS observer=LIVE "
                 "callback_count=1\n";
    return;
  }
  if (delay || reverse) {
    // This only observes feedback; it never turns GPU event progress into a
    // report.
    ULONGLONG deadline = GetTickCount64() + 10000;
    do {
      require(wait(completion, 0, result), "feedback polling failed");
      if (result.native_feedback_received)
        break;
      Sleep(1);
    } while (GetTickCount64() < deadline);
    require(result.native_feedback_received &&
                result.state == WMTSparseBarrierPending,
            "delayed report did not remain pending after actual feedback");
    if (reverse) {
      WMTSparseBarrierResult later = {};
      require(wait(second, 10000, later) &&
                  later.state == WMTSparseBarrierCompleted,
              "higher report missing");
      require(wait(completion, 0, result) &&
                  result.state == WMTSparseBarrierPending,
              "higher report incorrectly completed lower ticket");
      std::cout
          << "SPARSE_FEEDBACK reverse=PASS higher_terminal=1 lower_pending=1\n";
    }
    auto waiter = std::async(std::launch::async, [&] {
      auto waiter_pool = own(NSAutoreleasePool_alloc_init());
      WMTSparseBarrierResult waited = {};
      return wait(completion, 10000, waited) &&
             waited.state == WMTSparseBarrierCompleted;
    });
    require(waiter.wait_for(25ms) == std::future_status::timeout,
            "report waiter completed prematurely");
    require(waiter.get(), "delayed report waiter failed");
    std::cout
        << "SPARSE_FEEDBACK report_delay=PASS actual_feedback=1 pending=1\n";
  }
  require(wait(completion, 10000, result), "native feedback wait failed");
  require(result.state ==
              (synthetic ? WMTSparseBarrierError : WMTSparseBarrierCompleted),
          "wrong terminal report");
  require(result.native_feedback_received == 1 && result.callback_count == 1 &&
              result.synthetic_error == unsigned(synthetic) &&
              !result.native_error && !result.native_error_code &&
              result.gpu_end_time >= result.gpu_start_time &&
              result.gpu_start_time > 0,
          "native/synthetic feedback evidence mismatch");
  // Early/repeated consumption must preserve exactly the same terminal
  // evidence.
  WMTSparseBarrierResult repeated = {};
  require(wait(completion, 0, repeated) &&
              !std::memcmp(&result, &repeated, sizeof(result)),
          "terminal report not retained for later waiter");
  queue = nullptr; // The credential retains its native queue through terminal
                   // cleanup.
  std::cout << "SPARSE_FEEDBACK mode=" << mode << " terminal=" << result.state
            << " callback_count=" << result.callback_count
            << " native_error=0 synthetic=" << synthetic
            << " early_repeat=PASS ABI=56\n";
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
int CheckSparseFIFO(const char *mode) {
  const bool synthetic =
      !std::strcmp(mode, "fifo-error") || !std::strcmp(mode, "fifo-cancel");
  const bool reverse = !std::strcmp(mode, "fifo-reverse");
  const bool perf = !std::strcmp(mode, "fifo-perf");
  const bool wrong = !std::strcmp(mode, "fifo-wrong-oracle");
  const bool submitted = !std::strcmp(mode, "fifo-submitted");
  const bool prepare_failure = !std::strncmp(mode, "fifo-prepare-", 13);
  Owned<ID3D12Device> device;
  Check("FIFO device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                         IID_PPV_ARGS(&device.ptr)));
  LogRuntimeIdentity();
  if (!GetModuleHandleA("winemetal.dll"))
    throw std::runtime_error("FIFO probe requires DXMT");
  Owned<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC q = {};
  Check("FIFO queue", device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue.ptr)));
  Owned<ID3D12Fence> fence;
  Check("FIFO fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                          IID_PPV_ARGS(&fence.ptr)));
  // Establish an independently observed healthy prefix before enabling hooks.
  Check("FIFO prefix", queue->Signal(fence.ptr, 1));
  Wait(fence.ptr, 1);
  if (fence->GetCompletedValue() != 1)
    throw std::runtime_error("healthy prefix missing");
  Owned<ID3D12Heap> heap;
  D3D12_HEAP_DESC hd = {};
  hd.SizeInBytes = TileBytes;
  hd.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  Check("FIFO heap", device->CreateHeap(&hd, IID_PPV_ARGS(&heap.ptr)));
  Owned<ID3D12Resource> sparse, upload, readback;
  auto desc = BufferDesc(TileBytes);
  Check("FIFO sparse",
        device->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                       nullptr, IID_PPV_ARGS(&sparse.ptr)));
  auto make = [&](Owned<ID3D12Resource> &r, D3D12_HEAP_TYPE type,
                  D3D12_RESOURCE_STATES state) {
    auto props = Properties(type);
    Check("FIFO committed", device->CreateCommittedResource(
                                &props, D3D12_HEAP_FLAG_NONE, &desc, state,
                                nullptr, IID_PPV_ARGS(&r.ptr)));
  };
  make(upload, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
  make(readback, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
  struct DrainQueueBeforeResources {
    ID3D12CommandQueue *&queue;
    ~DrainQueueBeforeResources() {
      if (queue) {
        queue->Release();
        queue = nullptr;
      }
    }
  } drain{queue.ptr};
  UINT *mapped = nullptr;
  Check("FIFO upload",
        upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
  for (UINT i = 0; i < TileBytes / 4; ++i)
    mapped[i] = 0xbeef0000u ^ (i * 0x9e3779b9u);
  upload->Unmap(0, nullptr);
  struct Events {
    HANDLE main = nullptr;
    HANDLE completed = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    ~Events() {
      if (main)
        CloseHandle(main);
      if (completed)
        CloseHandle(completed);
    }
  } events;
  char event_name[80];
  std::snprintf(event_name, sizeof(event_name),
                "DXMT.Test.SparseMainComplete.%lu", GetCurrentProcessId());
  events.main = CreateEventA(nullptr, TRUE, FALSE, event_name);
  if (!events.main || !events.completed)
    throw std::runtime_error("FIFO events");
  Check("FIFO register", fence->SetEventOnCompletion(2, events.completed));
  // Native Unix controls must be inherited at process launch; changing the PE
  // environment here does not update Unix getenv(). The runner records them.
  D3D12_TILED_RESOURCE_COORDINATE coordinate = {};
  D3D12_TILE_REGION_SIZE region = {};
  region.NumTiles = 1;
  UINT offset = 0;
  LARGE_INTEGER frequency, start, finish;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&start);
  for (UINT i = 0; i < (perf ? 128u : 1u); ++i)
    queue->UpdateTileMappings(sparse.ptr, 1, &coordinate, &region, heap.ptr, 1,
                              nullptr, &offset, nullptr,
                              D3D12_TILE_MAPPING_FLAG_NONE);
  QueryPerformanceCounter(&finish);
  if (perf)
    std::printf("SPARSE_FIFO_COST mappings=128 cpu_us_per_mapping=%.6f "
                "validation=external\n",
                (finish.QuadPart - start.QuadPart) * 1e6 / frequency.QuadPart /
                    128.0);
  if (reverse)
    queue->UpdateTileMappings(sparse.ptr, 1, &coordinate, &region, heap.ptr, 1,
                              nullptr, &offset, nullptr,
                              D3D12_TILE_MAPPING_FLAG_NONE);
  if (submitted || prepare_failure) {
    if (device->GetDeviceRemovedReason() != DXGI_ERROR_DEVICE_REMOVED ||
        WaitForSingleObject(events.completed, 5000) != WAIT_OBJECT_0 ||
        fence->GetCompletedValue() != UINT64_MAX ||
        queue->Signal(fence.ptr, 2) != DXGI_ERROR_DEVICE_REMOVED)
      throw std::runtime_error("FIFO synchronous failure contract");
    queue.ptr->Release();
    queue.ptr = nullptr; // Drain submitted work before resources retire.
    std::cout << "SPARSE_FIFO_TEST mode=" << mode
              << " prefix=1 removal=PASS signal_rejected=PASS drained=PASS\n";
    return 0;
  }
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  Check("FIFO allocator",
        device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&allocator.ptr)));
  Check("FIFO list",
        device->CreateCommandList(0, q.Type, allocator.ptr, nullptr,
                                  IID_PPV_ARGS(&list.ptr)));
  list->CopyBufferRegion(sparse.ptr, 0, upload.ptr, 0, TileBytes);
  Transition(list.ptr, sparse.ptr, D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.ptr, 0, sparse.ptr, 0, TileBytes);
  Check("FIFO close", list->Close());
  ID3D12CommandList *lists[] = {list.ptr};
  queue->ExecuteCommandLists(1, lists);
  Check("FIFO later signal", queue->Signal(fence.ptr, 2));
  if (!perf && WaitForSingleObject(events.main, 10000) != WAIT_OBJECT_0)
    throw std::runtime_error("FIFO main completion missing");
  if (!perf && (WaitForSingleObject(events.completed, 25) != WAIT_TIMEOUT ||
                fence->GetCompletedValue() != 1 ||
                FAILED(device->GetDeviceRemovedReason())))
    throw std::runtime_error("FIFO success published before report");
  if (WaitForSingleObject(events.completed, 10000) != WAIT_OBJECT_0)
    throw std::runtime_error("FIFO report timeout");
  if (synthetic) {
    if (device->GetDeviceRemovedReason() != DXGI_ERROR_DEVICE_REMOVED ||
        fence->GetCompletedValue() != UINT64_MAX ||
        queue->Signal(fence.ptr, 3) != DXGI_ERROR_DEVICE_REMOVED)
      throw std::runtime_error("FIFO asynchronous removal contract");
    // Queue destruction drains submitted native and main work before fixture
    // resources retire.
    queue.ptr->Release();
    queue.ptr = nullptr;
    std::cout << "SPARSE_FIFO_TEST mode=" << mode
              << " prefix=1 pending=PASS removal=PASS signal_rejected=PASS "
                 "drained=PASS\n";
    return 0;
  }
  if (fence->GetCompletedValue() != 2 ||
      FAILED(device->GetDeviceRemovedReason()))
    throw std::runtime_error("FIFO success contract");
  Check("FIFO readback",
        readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)));
  bool ok = true;
  for (UINT i = 0; i < TileBytes / 4; ++i)
    if (mapped[i] != ((0xbeef0000u ^ (i * 0x9e3779b9u)) ^
                      (wrong && i == TileBytes / 4 - 1 ? 1u : 0u))) {
      ok = false;
      break;
    }
  readback->Unmap(0, nullptr);
  if (!ok)
    throw std::runtime_error("FIFO readback mismatch");
  std::cout << "SPARSE_FIFO_TEST mode=" << mode
            << " prefix=1 pending=PASS terminal=PASS words=16384\n";
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
  if (argc == 3 && (!std::strncmp(argv[1], "plan-queue", 10) ||
                    !std::strcmp(argv[1], "plan-cost") ||
                    !std::strcmp(argv[1], "plan-normal") ||
                    !std::strcmp(argv[1], "plan-partial") ||
                    !std::strcmp(argv[1], "plan-self") ||
                    !std::strcmp(argv[1], "plan-rejected") ||
                    !std::strcmp(argv[1], "plan-prepare-failure") ||
                    !std::strcmp(argv[1], "plan-release") ||
                    !std::strcmp(argv[1], "plan-wrong-word") ||
                    !std::strcmp(argv[1], "payload-normal") ||
                    !std::strcmp(argv[1], "payload-release") ||
                    !std::strcmp(argv[1], "payload-native-only") ||
                    !std::strcmp(argv[1], "payload-prepare-failure") ||
                    !std::strcmp(argv[1], "payload-rejected") ||
                    !std::strcmp(argv[1], "payload-wrong-word")))
    return CheckSparseMappingPayload(
        argv[1], static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 0)));

  if (argc == 3 && (!std::strncmp(argv[1], "mapping-terminal", 16) ||
                    !std::strcmp(argv[1], "mapping-normal") ||
                    !std::strcmp(argv[1], "mapping-partial") ||
                    !std::strcmp(argv[1], "mapping-wrong-event") ||
                    !std::strcmp(argv[1], "mapping-wrong-word") ||
                    !std::strcmp(argv[1], "mapping-prepare-failure")))
    return CheckSparseMappingCredential(
        argv[1], static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 0)));

  if (argc != 3 && argc != 5 && argc != 6) {
    std::cerr
        << "usage: dx12_reserved_buffer <probe_read.cso> "
           "<probe_write.cso> [seed rounds "
           "[wrong-oracle|queue-init-failure|barrier-update|barrier-copy|"
           "buffer-update|buffer-unmap|buffer-copy|buffer-copy-null|"
           "buffer-self|buffer-self-null|buffer-self-mixed|buffer-self-"
           "multiheap|fifo-delay|fifo-reverse|fifo-error|fifo-cancel|fifo-perf|"
           "fifo-wrong-oracle|fifo-submitted|fifo-prepare-command_buffer|"
           "fifo-prepare-allocator|fifo-prepare-encoder|"
           "feedback-normal|feedback-delay|feedback-reverse|"
           "feedback-synthetic|feedback-submitted|feedback-release|"
           "feedback-prepare-command_buffer|feedback-prepare-allocator|"
           "feedback-prepare-encoder]]\n";
    return 2;
  }
  try {
    if (argc == 6 && (!std::strcmp(argv[5], "fifo-delay") ||
                      !std::strcmp(argv[5], "fifo-reverse") ||
                      !std::strcmp(argv[5], "fifo-perf") ||
                      !std::strcmp(argv[5], "fifo-error") ||
                      !std::strcmp(argv[5], "fifo-cancel") ||
                      !std::strcmp(argv[5], "fifo-wrong-oracle") ||
                      !std::strcmp(argv[5], "fifo-submitted") ||
                      !std::strcmp(argv[5], "fifo-prepare-command_buffer") ||
                      !std::strcmp(argv[5], "fifo-prepare-allocator") ||
                      !std::strcmp(argv[5], "fifo-prepare-encoder")))
      return CheckSparseFIFO(argv[5]);
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
         std::strcmp(argv[5], "buffer-self-multiheap") &&
         std::strcmp(argv[5], "feedback-normal") &&
         std::strcmp(argv[5], "feedback-delay") &&
         std::strcmp(argv[5], "feedback-reverse") &&
         std::strcmp(argv[5], "feedback-synthetic") &&
         std::strcmp(argv[5], "feedback-submitted") &&
         std::strcmp(argv[5], "feedback-release") &&
         std::strcmp(argv[5], "feedback-prepare-command_buffer") &&
         std::strcmp(argv[5], "feedback-prepare-allocator") &&
         std::strcmp(argv[5], "feedback-prepare-encoder")))
      return 2;
    if (argc == 6 && !std::strncmp(argv[5], "feedback-", 9))
      CheckSparseFeedback(argv[5]);
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

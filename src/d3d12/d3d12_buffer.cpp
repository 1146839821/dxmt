/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "d3d12_device.hpp"
#include "d3d12_pageable.hpp"
#include "com/com_pointer.hpp"
#include "dxmt_format.hpp"
#include <limits>
#include <atomic>
#include <cstring>
#include <new>
#include <mutex>

namespace dxmt {

namespace {

constexpr uint64_t kD3D12TileSize = 64ull * 1024;

static void
FailBufferCopyPreparationForTesting(const char *point) {
  char phase[16] = {}, at[16] = {};
  const DWORD length = GetEnvironmentVariableA("DXMT_TEST_SPARSE_BUFFER_COPY_FAILURE", phase, sizeof(phase));
  if (!length || length >= sizeof(phase) || std::strcmp(phase, point))
    return;
  const DWORD at_length = GetEnvironmentVariableA("DXMT_TEST_SPARSE_BUFFER_COPY_FAIL_AT", at, sizeof(at));
  if (at_length >= sizeof(at) || (at_length && std::strcmp(at, "1") && std::strcmp(at, "2")))
    return;
  const uint64_t target = at_length && !std::strcmp(at, "2") ? 2 : 1;
  static std::atomic<uint64_t> calls{0};
  const auto call = calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (call == target) {
    ERR("DXMT_TEST_SPARSE_BUFFER_COPY_FAILURE phase=", point, " call=", call);
    throw std::bad_alloc();
  }
}

static void
FailBufferUpdatePreparationForTesting() {
  char phase[16] = {}, at[16] = {};
  const DWORD length = GetEnvironmentVariableA("DXMT_TEST_SPARSE_BUFFER_UPDATE_FAILURE", phase, sizeof(phase));
  if (!length || length >= sizeof(phase) || std::strcmp(phase, "prepare"))
    return;
  const DWORD at_length = GetEnvironmentVariableA("DXMT_TEST_SPARSE_BUFFER_UPDATE_FAIL_AT", at, sizeof(at));
  if (at_length >= sizeof(at) || (at_length && std::strcmp(at, "1") && std::strcmp(at, "2")))
    return;
  const uint64_t target = at_length && !std::strcmp(at, "2") ? 2 : 1;
  static std::atomic<uint64_t> calls{0};
  const auto call = calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (call == target) {
    ERR("DXMT_TEST_SPARSE_BUFFER_UPDATE_FAILURE phase=prepare call=", call);
    throw std::bad_alloc();
  }
}

void
ClearResourceTiling(
    UINT *TotalTileCount, D3D12_PACKED_MIP_INFO *PackedMipInfo, D3D12_TILE_SHAPE *StandardTileShape,
    UINT *SubresourceTilingCount
) {
  if (TotalTileCount)
    *TotalTileCount = 0;
  if (PackedMipInfo)
    *PackedMipInfo = {};
  if (StandardTileShape)
    *StandardTileShape = {};
  if (SubresourceTilingCount)
    *SubresourceTilingCount = 0;
}

bool
MakeBufferSlice(
    UINT64 first_element, UINT element_count, uint64_t element_stride, uint64_t buffer_size, BufferSlice &slice
) {
  if (!element_stride || first_element > UINT64_MAX / element_stride ||
      element_count > UINT64_MAX / element_stride)
    return false;

  const uint64_t byte_offset = uint64_t(first_element) * element_stride;
  const uint64_t byte_length = uint64_t(element_count) * element_stride;
  if (byte_offset > buffer_size || byte_length > buffer_size - byte_offset || byte_offset > UINT32_MAX ||
      byte_length > UINT32_MAX)
    return false;

  // A nonzero stride and the checked byte range also bound the element index.
  slice.firstElement = static_cast<uint32_t>(first_element);
  slice.elementCount = element_count;
  slice.byteOffset = static_cast<uint32_t>(byte_offset);
  slice.byteLength = static_cast<uint32_t>(byte_length);
  return true;
}

} // namespace

class MTLD3D12Buffer : public MTLD3D12Pageable<MTLD3D12Resource> {
  D3D12_RESOURCE_DESC desc_;
  D3D12_HEAP_PROPERTIES heap_props_;
  D3D12_HEAP_FLAGS heap_flags_;
  Com<MTLD3D12Heap, false> heap_;
  bool reserved_ = false;
  UINT tile_count_ = 0;

  struct TileMapping {
    // Mapping bookkeeping needs the heap object after the application's
    // public reference is released, but a reserved resource must not add one
    // public COM reference per mapped tile.
    Com<MTLD3D12Heap, false> heap;
    UINT heap_tile = 0;
  };

  struct TileUpdate {
    UINT resource_tile = 0;
    Com<MTLD3D12Heap, false> heap;
    UINT heap_tile = 0;
  };

  // Pending changes only. tile_mappings_ remains the sole logical authority.
  struct MappingPlan final : D3D12SparseBufferMappingPlan {
    MTLD3D12Buffer *destination = nullptr;
    MTLD3D12Buffer *source = nullptr;
    UINT source_start = 0;
    // Synchronous legacy argument only. Valid new heaps are privately retained
    // by updates; do not query/retain an unvalidated optional heap argument.
    MTLD3D12Heap *update_heap = nullptr;
    std::vector<TileUpdate> updates;
    std::vector<WMTSparseBufferBatchOperation> operations;
    bool finished = false;
    // Must die before updates/source/resource owners: native final release drains.
    D3D12SparseMappingPayload payload;

    void
    Publish() {
      std::unique_lock<dxmt::mutex> lock(destination->tile_mapping_mutex_);
      for (const auto &update : updates)
        destination->tile_mappings_[update.resource_tile] = {update.heap, update.heap_tile};
    }

    HRESULT
    Capture() {
      std::vector<UINT> tiles;
      tiles.reserve(updates.size());
      for (const auto &u : updates)
        tiles.push_back(u.resource_tile);
      HRESULT hr = destination->CaptureTileMappingOwners(tiles, payload);
      if (FAILED(hr))
        return hr;
      if (source && source != destination) {
        tiles.clear();
        for (UINT i = 0; i < updates.size(); ++i)
          tiles.push_back(source_start + i);
        if (FAILED(hr = source->CaptureTileMappingOwners(tiles, payload)))
          return hr;
      }
      // New/snapshot heaps include transient intermediate mappings even when a
      // later update in this plan overwrites the same logical destination tile.
      for (const auto &u : updates)
        if (u.heap)
          payload.heaps.push_back(u.heap);
      return S_OK;
    }

    HRESULT
    ResolveAdmission() {
      // Caller owns serialized mapping admission. Capture source contents and
      // overwritten destination owners together, after any capacity wait.
      // Locals outlive the locks so old private refs never die under them.
      std::vector<TileUpdate> resolved;
      std::vector<Com<MTLD3D12Heap, false>> heaps;
      if (source)
        FailBufferCopyPreparationForTesting("admission");
      if (updates.size() > (heaps.max_size() - payload.heaps.size()) / 2)
        return E_OUTOFMEMORY;
      heaps.reserve(payload.heaps.size() + updates.size() * 2);
      // Preserve prior captures until normal retirement outside queue locks,
      // including references from a failed native preparation that is retried.
      heaps.insert(heaps.end(), payload.heaps.begin(), payload.heaps.end());
      if (source)
        resolved.reserve(updates.size());
      {
        std::unique_lock<dxmt::mutex> dst_lock(destination->tile_mapping_mutex_, std::defer_lock);
        std::unique_lock<dxmt::mutex> src_lock;
        if (source && source != destination) {
          src_lock = std::unique_lock<dxmt::mutex>(source->tile_mapping_mutex_, std::defer_lock);
          std::lock(dst_lock, src_lock);
        } else {
          dst_lock.lock();
        }
        for (size_t i = 0; i < updates.size(); ++i) {
          const auto &old = destination->tile_mappings_[updates[i].resource_tile];
          if (old.heap)
            heaps.push_back(old.heap);
          if (source) {
            const auto &saved = source->tile_mappings_[source_start + i];
            resolved.push_back({updates[i].resource_tile, saved.heap, saved.heap_tile});
            if (saved.heap)
              heaps.push_back(saved.heap);
          } else if (updates[i].heap) {
            // Includes transient updates overwritten by a later duplicate tile.
            heaps.push_back(updates[i].heap);
          }
        }
      }
      if (source)
        FailBufferCopyPreparationForTesting("admission");
      if (source)
        updates.swap(resolved);
      payload.heaps.swap(heaps);
      return S_OK;
    }

    HRESULT
    PrepareNative(WMT::SparseMappingQueue queue) override try {
      if (!queue || payload.completion || finished)
        return E_INVALIDARG;
      if (!destination->GetMetalBuffer() || (source && !source->GetMetalBuffer()))
        return E_OUTOFMEMORY;
      HRESULT hr = ResolveAdmission();
      if (FAILED(hr))
        return hr;
      operations.clear();
      operations.reserve(updates.size());
      for (size_t i = 0; i < updates.size(); ++i) {
        const auto &u = updates[i];
        WMTSparseBufferBatchOperation op = {};
        op.destination = destination->GetMetalBuffer().handle;
        op.length = 1;
        if (source && source != destination) {
          op.kind = 1;
          op.source = source->GetMetalBuffer().handle;
          op.location = source_start + i;
          op.offset = u.resource_tile;
        } else {
          op.mode = u.heap ? WMTSparseTextureMappingModeMap : WMTSparseTextureMappingModeUnmap;
          op.heap = u.heap ? u.heap->GetMetalHeap().handle : 0;
          op.location = u.resource_tile;
          op.offset = u.heap_tile;
        }
        operations.push_back(op);
      }
      payload.completion = queue.prepareBufferBatch(operations.data(), operations.size());
      return payload.completion ? S_OK : E_OUTOFMEMORY;
    } catch (const std::bad_alloc &) {
      return E_OUTOFMEMORY;
    }

    bool
    PrepareTerminal(UINT flags) override {
      return !finished && payload.completion &&
             SparseMappingCompletion_prepareTerminal(payload.completion.handle, flags);
    }
    bool
    IssueNext() override {
      return !finished && payload.completion && payload.completion.issueNext();
    }
    bool
    Finish(WMTSparseMappingHostOutcome outcome) override {
      if (finished || !payload.completion || !payload.completion.seal(outcome))
        return false;
      finished = true;
      if (outcome == WMTSparseMappingHostAccepted)
        Publish();
      return true;
    }
    bool
    Wait(uint64_t timeout, WMTSparseMappingResult &result) override {
      return payload.completion && payload.completion.wait(timeout, result);
    }
    bool
    Retire(WMTSparseMappingResult &result) override {
      if (!payload.Retire(result))
        return false;
      updates.clear();
      update_heap = nullptr;
      source = destination = nullptr;
      return true;
    }
    size_t
    OperationCount() const override {
      return updates.size();
    }

    HRESULT
    IssueLegacy(WMT::SparseMappingQueue queue) {
      if (queue) {
        if (!destination->GetMetalBuffer() || (source && !source->GetMetalBuffer()))
          return E_OUTOFMEMORY;
        if (source && source != destination) {
          FailBufferCopyPreparationForTesting("prepare");
          std::vector<WMTCopySparseBufferMappingOperation> ops;
          ops.reserve(updates.size());
          for (size_t i = 0; i < updates.size(); ++i) {
            auto &op = ops.emplace_back();
            op.source_range.location = source_start + i;
            op.source_range.length = 1;
            op.destination_offset = updates[i].resource_tile;
          }
          if (!queue.tryCopyBufferMappings(
                  source->GetMetalBuffer(), destination->GetMetalBuffer(), ops.data(), ops.size()
              ))
            return DXGI_ERROR_DEVICE_REMOVED;
        } else if (source) {
          // Self-overlap is a snapshot; sequential native copies are unsafe.
          for (const auto &u : updates) {
            WMTUpdateSparseBufferMappingOperation op = {};
            op.mode = u.heap ? WMTSparseTextureMappingModeMap : WMTSparseTextureMappingModeUnmap;
            op.buffer_range = {u.resource_tile, 1};
            op.heap_offset = u.heap_tile;
            if (!queue.tryUpdateBufferMappings(
                    destination->GetMetalBuffer(), u.heap ? u.heap->GetMetalHeap() : WMT::Heap{}, &op, 1
                ))
              return DXGI_ERROR_DEVICE_REMOVED;
          }
        } else {
          if (!updates.empty())
            FailBufferUpdatePreparationForTesting();
          std::vector<WMTUpdateSparseBufferMappingOperation> ops;
          ops.reserve(updates.size());
          for (const auto &u : updates) {
            auto &op = ops.emplace_back();
            op.mode = u.heap ? WMTSparseTextureMappingModeMap : WMTSparseTextureMappingModeUnmap;
            op.buffer_range = {u.resource_tile, 1};
            op.heap_offset = u.heap_tile;
          }
          if (!queue.tryUpdateBufferMappings(
                  destination->GetMetalBuffer(), update_heap ? update_heap->GetMetalHeap() : WMT::Heap{}, ops.data(),
                  ops.size()
              ))
            return DXGI_ERROR_DEVICE_REMOVED;
        }
      }
      Publish();
      return S_OK;
    }
  };

  std::vector<TileMapping> tile_mappings_;
  mutable dxmt::mutex tile_mapping_mutex_;

public:
  MTLD3D12Buffer(MTLD3D12Device *pDevice) : MTLD3D12Pageable<MTLD3D12Resource>(pDevice) {}

  HRESULT
  Initialize(
      const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc,
      D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *OptimizedClearValue, MTLD3D12Heap *pHeap,
      UINT64 HeapOffset
  ) {
    if (OptimizedClearValue)
      return E_INVALIDARG;

    // TODO: validate and normalize
    desc_ = *pDesc;
    heap_props_ = *pHeapProps;
    heap_flags_ = HeapFlags;
    state = InitialState;
    InitializeStateTracking(desc_, device_->GetMTLDevice());

    if (desc_.Alignment) {
      if (desc_.Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
        return E_INVALIDARG;
    } else {
      desc_.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    }

    buffer = new Buffer(desc_.Width, device_->GetMTLDevice());

    Flags<BufferAllocationFlag> flags;
    if (pHeapProps->Type == D3D12_HEAP_TYPE_DEFAULT ||
        (pHeapProps->Type == D3D12_HEAP_TYPE_CUSTOM &&
         pHeapProps->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE))
      flags.set(BufferAllocationFlag::CpuInvisible);
#ifdef __i386__
    if (pHeapProps->Type == D3D12_HEAP_TYPE_UPLOAD || pHeapProps->Type == D3D12_HEAP_TYPE_READBACK)
      flags.set(BufferAllocationFlag::CpuPlaced);
#endif
    bool use_heap = pHeap != nullptr;
#ifdef __i386__
    // A shared heap may return a contents pointer above the 32-bit address space.
    if (flags.test(BufferAllocationFlag::CpuPlaced))
      use_heap = false;
#endif
    if (pHeap) {
      auto size_and_align = device_->GetMTLDevice().heapBufferSizeAndAlign(desc_.Width, {});
      auto heap_size = pHeap->GetDesc().SizeInBytes;
      if (HeapOffset > heap_size || size_and_align.size > heap_size - HeapOffset)
        return E_INVALIDARG;
    }

    auto allocation = use_heap ? buffer->allocate(pHeap->GetMetalHeap(), HeapOffset, flags) : buffer->allocate(flags);
    if (!allocation || !allocation->buffer())
      return E_OUTOFMEMORY;
    buffer->rename(std::move(allocation));
    if (pHeap)
      heap_ = pHeap;
    device_->RegisterResidencyAndVA(buffer->current(), this);

    return S_OK;
  };

  HRESULT
  InitializeReserved(const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState) {
    if (!pDesc || pDesc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
      return pDesc ? E_NOTIMPL : E_INVALIDARG;
    if (!IsValidBufferResourceDesc(*pDesc))
      return E_INVALIDARG;

    D3D12_HEAP_PROPERTIES default_heap = {};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    default_heap.CreationNodeMask = 1;
    default_heap.VisibleNodeMask = 1;
    if (FAILED(ValidateResourceDescs(pDesc, &default_heap)) ||
        FAILED(ValidateResourceStates(InitialState, &default_heap, pDesc)))
      return E_INVALIDARG;

    const uint64_t tile_count = (pDesc->Width - 1) / kD3D12TileSize + 1;
    if (tile_count > std::numeric_limits<UINT>::max())
      return E_INVALIDARG;

    desc_ = *pDesc;
    heap_props_ = default_heap;
    heap_flags_ = D3D12_HEAP_FLAG_NONE;
    state = InitialState;
    InitializeStateTracking(desc_, device_->GetMTLDevice());
    reserved_ = true;
    tile_count_ = static_cast<UINT>(tile_count);

    if (device_->GetMTLDevice().supportsPlacementSparse()) {
      const uint64_t sparse_length = tile_count * uint64_t(kD3D12TileSize);
      buffer = new Buffer(sparse_length, device_->GetMTLDevice());
      Flags<BufferAllocationFlag> flags;
      flags.set(BufferAllocationFlag::CpuInvisible);
      auto allocation = buffer->allocatePlacementSparse(flags);
      if (!allocation || !allocation->buffer())
        return E_OUTOFMEMORY;
      buffer->rename(std::move(allocation));
      device_->RegisterResidencyAndVA(buffer->current(), this);
    }

    tile_mappings_.resize(tile_count_);
    return S_OK;
  }

  ~MTLD3D12Buffer() {
    if (buffer && buffer->current())
      device_->UnregisterResidencyAndVA(buffer->current(), this);
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12Resource)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12Resource), riid)) {
      WARN("D3D12Buffer: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  virtual HRESULT STDMETHODCALLTYPE
  Map(UINT Subresource, const D3D12_RANGE *pReadRange, void **ppData) {
    if (reserved_ || !buffer || Subresource || !ppData)
      return E_INVALIDARG;
    if (heap_props_.Type == D3D12_HEAP_TYPE_DEFAULT)
      return E_INVALIDARG;
    *ppData = buffer->current()->mappedMemory(0);
    return S_OK;
  };

  virtual void STDMETHODCALLTYPE Unmap(UINT Subresource, const D3D12_RANGE *pWrittenRange) {
    // no-op
  };

  virtual D3D12_RESOURCE_DESC *STDMETHODCALLTYPE
  GetDesc(D3D12_RESOURCE_DESC *__ret) {
    if (!__ret)
      return nullptr;
    *__ret = desc_;
    return __ret;
  };

  virtual D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE
  GetGPUVirtualAddress() {
    return buffer && buffer->current() ? buffer->current()->gpuAddress() : 0;
  };

  virtual HRESULT STDMETHODCALLTYPE
  WriteToSubresource(
      UINT DstSubresource, const D3D12_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcSlicePitch
  ) {
    return E_INVALIDARG;
  };

  virtual HRESULT STDMETHODCALLTYPE
  ReadFromSubresource(
      void *pDstData, UINT DstRowPitch, UINT DstSlicePitch, UINT SrcSubresource, const D3D12_BOX *pSrcBox
  ) {
    return E_INVALIDARG;
  };

  const D3D12_HEAP_PROPERTIES *
  GetHeapPropertiesView() const override {
    return reserved_ ? nullptr : &heap_props_;
  }

  virtual HRESULT STDMETHODCALLTYPE
  GetHeapProperties(D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS *pFlags) {
    if (reserved_)
      return E_INVALIDARG;
    if (pHeapProps)
      *pHeapProps = heap_props_;
    if (pFlags)
      *pFlags = heap_flags_;
    return S_OK;
  };

  virtual HRESULT STDMETHODCALLTYPE
  CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) {
    HRESULT hr;
    D3D12_SHADER_RESOURCE_VIEW_DESC ViewDesc;
    if (!pDesc) {
      hr = ExtractEntireResourceViewDescription(desc_, &ViewDesc);
      if (FAILED(hr))
        return hr;
    } else {
      ViewDesc = *pDesc;
    }

    if (ViewDesc.ViewDimension != D3D12_SRV_DIMENSION_BUFFER)
      return E_INVALIDARG;

    // Reserved buffers are shader-visible only when the native placement-sparse backing exists.
    // Keep the unsupported fallback explicit rather than passing a null Metal buffer to the descriptor heap.
    if (reserved_ && (!buffer || !buffer->current()))
      return E_NOTIMPL;
    auto [Heap, Index] = GetShaderVisibleDescriptorHeap(device_, Descriptor);
    if (!Heap)
      return E_INVALIDARG;
    BufferSlice Slice;

    if (ViewDesc.Format == DXGI_FORMAT_UNKNOWN || ViewDesc.Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW) {
      if ((ViewDesc.Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW) && ViewDesc.Format != DXGI_FORMAT_R32_TYPELESS)
        return E_INVALIDARG;
      UINT Stride = (ViewDesc.Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW) ? 4 : ViewDesc.Buffer.StructureByteStride;
      if (!MakeBufferSlice(ViewDesc.Buffer.FirstElement, ViewDesc.Buffer.NumElements, Stride, desc_.Width, Slice))
        return E_INVALIDARG;
      return Heap->AddShaderResourceView(Index, buffer.ptr(), Slice);
    }

    MTL_DXGI_FORMAT_DESC Format;
    if (FAILED(MTLQueryDXGIFormat(device_->GetMTLDevice(), ViewDesc.Format, Format))) {
      ERR("D3D12Buffer::CreateShaderResourceView: not an ordinary or packed format: ", ViewDesc.Format);
      return E_FAIL;
    }
    if (IsFullyTypelessFormat(ViewDesc.Format, Format) ||
        (Format.Flag & (MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER)))
      return E_INVALIDARG;
    const auto capabilities = device_->GetMTLPixelFormatCapability(Format.PixelFormat);
    if (!any_bit_set(capabilities & (FormatCapability::TextureBufferRead | FormatCapability::TextureBufferReadWrite)))
      return E_INVALIDARG;
    // Keep writable formats shared with UAVs; a read-only native format must
    // not inherit ShaderWrite merely because its backing allocation is private.
    BufferViewDescriptor view_descriptor{
        Format.PixelFormat,
        !any_bit_set(capabilities & (FormatCapability::TextureBufferWrite | FormatCapability::TextureBufferReadWrite))
    };
    if (!MakeBufferSlice(ViewDesc.Buffer.FirstElement, ViewDesc.Buffer.NumElements, Format.BytesPerTexel, desc_.Width, Slice))
      return E_INVALIDARG;

    auto view = buffer->createView(view_descriptor);
    return Heap->AddShaderResourceView(Index, buffer.ptr(), view, Slice);
  };

  virtual HRESULT STDMETHODCALLTYPE
  CreateUnorderedAccessView(
      ID3D12Resource *pCounter, const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor
  ) {
    if (pCounter && !IsSameDevice(device_, pCounter))
      return E_INVALIDARG;
    HRESULT hr;
    D3D12_UNORDERED_ACCESS_VIEW_DESC ViewDesc;
    if (!pDesc) {
      hr = ExtractEntireResourceViewDescription(desc_, &ViewDesc);
      if (FAILED(hr))
        return hr;
    } else {
      ViewDesc = *pDesc;
    }

    if (ViewDesc.ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
      return E_INVALIDARG;
    if (pCounter) {
      if (ViewDesc.Format != DXGI_FORMAT_UNKNOWN || !ViewDesc.Buffer.StructureByteStride ||
          ViewDesc.Buffer.Flags != D3D12_BUFFER_UAV_FLAG_NONE)
        return E_INVALIDARG;
    } else if (ViewDesc.Buffer.CounterOffsetInBytes) {
      return E_INVALIDARG;
    }

    // Reserved buffers are shader-visible only when the native placement-sparse backing exists.
    // Keep the unsupported fallback explicit rather than passing a null Metal buffer to the descriptor heap.
    if (reserved_ && (!buffer || !buffer->current()))
      return E_NOTIMPL;
    auto [Heap, Index] = GetShaderVisibleDescriptorHeap(device_, Descriptor);
    if (!Heap)
      return E_INVALIDARG;
    BufferSlice Slice;

    if (ViewDesc.Format == DXGI_FORMAT_UNKNOWN || ViewDesc.Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW) {
      if ((ViewDesc.Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW) && ViewDesc.Format != DXGI_FORMAT_R32_TYPELESS)
        return E_INVALIDARG;
      UINT Stride = (ViewDesc.Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW) ? 4 : ViewDesc.Buffer.StructureByteStride;
      if (!MakeBufferSlice(ViewDesc.Buffer.FirstElement, ViewDesc.Buffer.NumElements, Stride, desc_.Width, Slice))
        return E_INVALIDARG;
      if (!pCounter)
        return Heap->AddUnorderedAccessView(Index, buffer.ptr(), Slice, nullptr, 0);

      auto Counter = static_cast<MTLD3D12Resource *>(pCounter);
      auto counter_desc = Counter->GetDesc();
      if (!Counter->buffer || counter_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
          (ViewDesc.Buffer.CounterOffsetInBytes & (D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT - 1)) ||
          ViewDesc.Buffer.CounterOffsetInBytes > counter_desc.Width ||
          sizeof(UINT) > counter_desc.Width - ViewDesc.Buffer.CounterOffsetInBytes)
        return E_INVALIDARG;
      return Heap->AddUnorderedAccessView(
          Index, buffer.ptr(), Slice, Counter->buffer.ptr(), ViewDesc.Buffer.CounterOffsetInBytes
      );
    }

    MTL_DXGI_FORMAT_DESC Format;
    if (FAILED(MTLQueryDXGIFormat(device_->GetMTLDevice(), ViewDesc.Format, Format))) {
      ERR("D3D12Buffer::CreateUnorderedAccessView: not an ordinary or packed format: ", ViewDesc.Format);
      return E_FAIL;
    }
    if (pCounter)
      return E_INVALIDARG;
    if (IsFullyTypelessFormat(ViewDesc.Format, Format) ||
        (Format.Flag & (MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER)))
      return E_INVALIDARG;
    const auto capabilities = device_->GetMTLPixelFormatCapability(Format.PixelFormat);
    if (!any_bit_set(capabilities & (FormatCapability::TextureBufferWrite | FormatCapability::TextureBufferReadWrite)))
      return E_INVALIDARG;
    BufferViewDescriptor view_descriptor{Format.PixelFormat};
    if (!MakeBufferSlice(ViewDesc.Buffer.FirstElement, ViewDesc.Buffer.NumElements, Format.BytesPerTexel, desc_.Width, Slice))
      return E_INVALIDARG;

    auto view = buffer->createView(view_descriptor);
    return Heap->AddUnorderedAccessView(Index, buffer.ptr(), view, Slice);
  };

  virtual HRESULT STDMETHODCALLTYPE
  CreateRenderTargetView(const D3D12_RENDER_TARGET_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) {
    return E_INVALIDARG;
  };

  virtual HRESULT STDMETHODCALLTYPE
  CreateDepthStencilView(const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) {
    return E_INVALIDARG;
  };

  virtual void STDMETHODCALLTYPE GetResourceTiling(
      UINT *TotalTileCount, D3D12_PACKED_MIP_INFO *PackedMipInfo, D3D12_TILE_SHAPE *StandardTitleShape,
      UINT *SubresourceTilingCount, UINT FirstSubresourceTiling, D3D12_SUBRESOURCE_TILING *SubresourceTilings
  ) {
    if (!reserved_) {
      WARN("D3D12 buffer GetResourceTiling is not implemented");
      ClearResourceTiling(TotalTileCount, PackedMipInfo, StandardTitleShape, SubresourceTilingCount);
      return;
    }

    if (TotalTileCount)
      *TotalTileCount = tile_count_;
    if (PackedMipInfo)
      *PackedMipInfo = {};
    if (StandardTitleShape)
      *StandardTitleShape = {};
    if (!SubresourceTilingCount)
      return;

    const UINT requested = *SubresourceTilingCount;
    const UINT retrieved = requested && FirstSubresourceTiling == 0 ? 1 : 0;
    *SubresourceTilingCount = retrieved;
    if (retrieved && SubresourceTilings) {
      *SubresourceTilings = {};
      SubresourceTilings->WidthInTiles = tile_count_;
      SubresourceTilings->HeightInTiles = 1;
      SubresourceTilings->DepthInTiles = 1;
      SubresourceTilings->StartTileIndexInOverallResource = 0;
    }
  };

  bool
  IsReservedResource() const override {
    return reserved_;
  }

  bool
  IsReservedBuffer() const override {
    return reserved_;
  }

  HRESULT
  GetTileIndices(
      const D3D12_TILED_RESOURCE_COORDINATE *pRegionStartCoordinate, const D3D12_TILE_REGION_SIZE *pRegionSize,
      std::vector<UINT> &tile_indices
  ) const override {
    const auto coordinate = pRegionStartCoordinate ? *pRegionStartCoordinate : D3D12_TILED_RESOURCE_COORDINATE{};
    if (!reserved_ || coordinate.X > tile_count_ || coordinate.Y || coordinate.Z || coordinate.Subresource)
      return E_INVALIDARG;

    UINT tile_count = pRegionSize ? pRegionSize->NumTiles : (pRegionStartCoordinate ? 1 : tile_count_);
    if (pRegionSize && pRegionSize->UseBox) {
      if (!pRegionSize->Width || pRegionSize->Height != 1 || pRegionSize->Depth != 1 ||
          pRegionSize->NumTiles != pRegionSize->Width)
        return E_INVALIDARG;
      tile_count = pRegionSize->Width;
    }
    if (!tile_count || tile_count > tile_count_ - coordinate.X)
      return E_INVALIDARG;
    tile_indices.reserve(tile_indices.size() + tile_count);
    for (UINT tile = 0; tile < tile_count; tile++)
      tile_indices.push_back(coordinate.X + tile);
    return S_OK;
  }

  HRESULT
  GetTileMapping(UINT tile_index, WMT::Buffer &backing_buffer, UINT64 &backing_offset) const override {
    backing_buffer = {};
    backing_offset = 0;
    if (!reserved_ || tile_index >= tile_mappings_.size())
      return E_INVALIDARG;

    std::unique_lock<dxmt::mutex> lock(tile_mapping_mutex_);
    const auto &mapping = tile_mappings_[tile_index];
    if (!mapping.heap)
      return S_OK;

    auto *heap = static_cast<MTLD3D12Heap *>(mapping.heap.ptr());
    backing_buffer = heap->GetTileBackingBuffer();
    if (!backing_buffer)
      return E_OUTOFMEMORY;
    backing_offset = uint64_t(mapping.heap_tile) * kD3D12TileSize;
    return S_OK;
  }

  WMT::Buffer
  GetMetalBuffer() const override {
    return buffer && buffer->current() ? buffer->current()->buffer() : WMT::Buffer{};
  }

  bool
  IsTileMapped(UINT tile_index) const override {
    if (!reserved_ || tile_index >= tile_mappings_.size())
      return false;
    std::unique_lock<dxmt::mutex> lock(tile_mapping_mutex_);
    return tile_mappings_[tile_index].heap != nullptr;
  }

  HRESULT
  CaptureTileMappingOwners(const std::vector<UINT> &tiles, D3D12SparseMappingPayload &payload) const override try {
    if (!reserved_ || payload.completion || payload.resources.size() == payload.resources.max_size() ||
        tiles.size() > payload.heaps.max_size() - payload.heaps.size())
      return E_INVALIDARG;
    for (const auto tile : tiles)
      if (tile >= tile_count_)
        return E_INVALIDARG;
    // Collect before the native credential exists or work can be issued. Keep
    // overwritten/source heaps even after tile_mappings_ publishes newer refs.
    std::unique_lock<dxmt::mutex> lock(tile_mapping_mutex_);
    payload.resources.reserve(payload.resources.size() + 1);
    payload.heaps.reserve(payload.heaps.size() + tiles.size());
    payload.resources.emplace_back(const_cast<MTLD3D12Buffer *>(this));
    for (const auto tile : tiles)
      if (tile_mappings_[tile].heap)
        payload.heaps.push_back(tile_mappings_[tile].heap);
    return S_OK;
  } catch (const std::bad_alloc &) {
    return E_OUTOFMEMORY;
  }

private:
  HRESULT
  BuildUpdatePlan(
      UINT NumResourceRegions, const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates,
      const D3D12_TILE_REGION_SIZE *pResourceRegionSizes, ID3D12Heap *pHeap, UINT NumRanges,
      const D3D12_TILE_RANGE_FLAGS *pRangeFlags, const UINT *pHeapRangeStartOffsets, const UINT *pRangeTileCounts,
      D3D12_TILE_MAPPING_FLAGS Flags, MappingPlan &plan
  ) {
    if (!reserved_ || (Flags & ~D3D12_TILE_MAPPING_FLAG_NO_HAZARD))
      return E_INVALIDARG;
    if (!NumResourceRegions || !NumRanges)
      return E_INVALIDARG;
    if (NumResourceRegions > 1 && !pResourceRegionStartCoordinates)
      return E_INVALIDARG;

    std::vector<UINT> resource_tiles;
    resource_tiles.reserve(tile_count_);
    for (UINT region = 0; region < NumResourceRegions; region++) {
      if (FAILED(GetTileIndices(
              pResourceRegionStartCoordinates ? &pResourceRegionStartCoordinates[region] : nullptr,
              pResourceRegionSizes ? &pResourceRegionSizes[region] : nullptr, resource_tiles
          )))
        return E_INVALIDARG;
    }

    if (resource_tiles.empty())
      return E_INVALIDARG;

    plan.destination = this;
    plan.update_heap = static_cast<MTLD3D12Heap *>(pHeap);
    auto &updates = plan.updates;
    updates.reserve(resource_tiles.size());
    uint64_t resource_tile = 0;
    for (UINT range = 0; range < NumRanges; range++) {
      const auto range_flag = pRangeFlags ? pRangeFlags[range] : D3D12_TILE_RANGE_FLAG_NONE;
      if (range_flag & ~(D3D12_TILE_RANGE_FLAG_NULL | D3D12_TILE_RANGE_FLAG_SKIP |
                         D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE))
        return E_INVALIDARG;
      if (bit::popcnt(static_cast<unsigned>(range_flag)) > 1)
        return E_INVALIDARG;

      const UINT tile_count =
          pRangeTileCounts ? pRangeTileCounts[range] : (NumRanges == 1 ? static_cast<UINT>(resource_tiles.size()) : 0);
      if (!tile_count || resource_tile > resource_tiles.size() || tile_count > resource_tiles.size() - resource_tile)
        return E_INVALIDARG;

      const bool needs_heap = range_flag == D3D12_TILE_RANGE_FLAG_NONE ||
                              range_flag == D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
      if (needs_heap && (!pHeap || !pHeapRangeStartOffsets || !IsSameDevice(device_, pHeap)))
        return E_INVALIDARG;

      UINT heap_tile = pHeapRangeStartOffsets ? pHeapRangeStartOffsets[range] : 0;
      if (needs_heap) {
        auto *heap = static_cast<MTLD3D12Heap *>(pHeap);
        const auto heap_desc = heap->GetDesc();
        if (heap_desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS)
          return E_INVALIDARG;
        const uint64_t heap_tile_count = (heap_desc.SizeInBytes - 1) / kD3D12TileSize + 1;
        if (range_flag == D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE) {
          if (heap_tile >= heap_tile_count)
            return E_INVALIDARG;
        } else if (heap_tile > heap_tile_count || tile_count > heap_tile_count - heap_tile) {
          return E_INVALIDARG;
        }
      }

      if (range_flag != D3D12_TILE_RANGE_FLAG_SKIP) {
        for (UINT tile = 0; tile < tile_count; tile++) {
          auto &update = updates.emplace_back();
          update.resource_tile = resource_tiles[resource_tile + tile];
          if (range_flag == D3D12_TILE_RANGE_FLAG_NONE || range_flag == D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE) {
            update.heap = static_cast<MTLD3D12Heap *>(pHeap);
            update.heap_tile = range_flag == D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE ? heap_tile : heap_tile + tile;
          }
        }
      }
      resource_tile += tile_count;
    }
    if (resource_tile != resource_tiles.size())
      return E_INVALIDARG;

    return S_OK;
  }

  HRESULT
  BuildCopyPlan(
      MTLD3D12Resource *pSourceResource, const D3D12_TILED_RESOURCE_COORDINATE *pDstRegionStartCoordinate,
      const D3D12_TILED_RESOURCE_COORDINATE *pSrcRegionStartCoordinate, const D3D12_TILE_REGION_SIZE *pRegionSize,
      D3D12_TILE_MAPPING_FLAGS Flags, MappingPlan &plan
  ) {
    if (!reserved_ || !pSourceResource || !pDstRegionStartCoordinate || !pSrcRegionStartCoordinate || !pRegionSize ||
        !pSourceResource->IsReservedResource() || !IsSameDevice(device_, pSourceResource) ||
        pSourceResource->IsReservedTexture() || (Flags & ~D3D12_TILE_MAPPING_FLAG_NO_HAZARD))
      return E_INVALIDARG;

    auto *source = static_cast<MTLD3D12Buffer *>(pSourceResource);
    const auto dst_coordinate =
        pDstRegionStartCoordinate ? *pDstRegionStartCoordinate : D3D12_TILED_RESOURCE_COORDINATE{};
    const auto src_coordinate =
        pSrcRegionStartCoordinate ? *pSrcRegionStartCoordinate : D3D12_TILED_RESOURCE_COORDINATE{};
    if (dst_coordinate.Y || dst_coordinate.Z || dst_coordinate.Subresource >= 1 || src_coordinate.Y ||
        src_coordinate.Z || src_coordinate.Subresource >= 1)
      return E_INVALIDARG;

    UINT tile_count = pRegionSize ? pRegionSize->NumTiles : tile_count_;
    if (pRegionSize && pRegionSize->UseBox) {
      if (!pRegionSize->Width || pRegionSize->Height != 1 || pRegionSize->Depth != 1 ||
          pRegionSize->NumTiles != pRegionSize->Width)
        return E_INVALIDARG;
      tile_count = pRegionSize->Width;
    }
    if (!tile_count || dst_coordinate.X > tile_count_ || tile_count > tile_count_ - dst_coordinate.X ||
        src_coordinate.X > source->tile_count_ || tile_count > source->tile_count_ - src_coordinate.X || !pRegionSize)
      return E_INVALIDARG;
    if (source != this)
      FailBufferCopyPreparationForTesting("snapshot");
    plan.destination = this;
    plan.source = source;
    plan.source_start = src_coordinate.X;
    plan.updates.reserve(tile_count);
    {
      std::unique_lock<dxmt::mutex> lock(source->tile_mapping_mutex_);
      for (UINT i = 0; i < tile_count; ++i) {
        const auto &saved = source->tile_mappings_[src_coordinate.X + i];
        plan.updates.push_back({dst_coordinate.X + i, saved.heap, saved.heap_tile});
      }
    }
    return S_OK;
  }

public:
  HRESULT
  UpdateTileMappings(
      UINT regions, const D3D12_TILED_RESOURCE_COORDINATE *starts, const D3D12_TILE_REGION_SIZE *sizes,
      ID3D12Heap *heap, UINT ranges, const D3D12_TILE_RANGE_FLAGS *flags, const UINT *offsets, const UINT *counts,
      D3D12_TILE_MAPPING_FLAGS mapping_flags, WMT::SparseMappingQueue queue
  ) override try {
    MappingPlan plan;
    HRESULT hr = BuildUpdatePlan(regions, starts, sizes, heap, ranges, flags, offsets, counts, mapping_flags, plan);
    return FAILED(hr) ? hr : plan.IssueLegacy(queue);
  } catch (const std::bad_alloc &) {
    return E_OUTOFMEMORY;
  }

  HRESULT
  CopyTileMappingsFrom(
      MTLD3D12Resource *source, const D3D12_TILED_RESOURCE_COORDINATE *dst, const D3D12_TILED_RESOURCE_COORDINATE *src,
      const D3D12_TILE_REGION_SIZE *size, D3D12_TILE_MAPPING_FLAGS flags, WMT::SparseMappingQueue queue
  ) override try {
    MappingPlan plan;
    HRESULT hr = BuildCopyPlan(source, dst, src, size, flags, plan);
    return FAILED(hr) ? hr : plan.IssueLegacy(queue);
  } catch (const std::bad_alloc &) {
    return E_OUTOFMEMORY;
  }

  HRESULT
  PrepareBufferTileUpdate(
      UINT regions, const D3D12_TILED_RESOURCE_COORDINATE *starts, const D3D12_TILE_REGION_SIZE *sizes,
      ID3D12Heap *heap, UINT ranges, const D3D12_TILE_RANGE_FLAGS *flags, const UINT *offsets, const UINT *counts,
      D3D12_TILE_MAPPING_FLAGS mapping_flags, std::unique_ptr<D3D12SparseBufferMappingPlan> &out
  ) override try {
    if (out)
      return E_INVALIDARG;
    auto plan = std::make_unique<MappingPlan>();
    HRESULT hr = BuildUpdatePlan(regions, starts, sizes, heap, ranges, flags, offsets, counts, mapping_flags, *plan);
    if (FAILED(hr) || FAILED(hr = plan->Capture()))
      return hr;
    out = std::move(plan);
    return S_OK;
  } catch (const std::bad_alloc &) {
    return E_OUTOFMEMORY;
  }

  HRESULT
  PrepareBufferTileCopy(
      MTLD3D12Resource *source, const D3D12_TILED_RESOURCE_COORDINATE *dst, const D3D12_TILED_RESOURCE_COORDINATE *src,
      const D3D12_TILE_REGION_SIZE *size, D3D12_TILE_MAPPING_FLAGS flags,
      std::unique_ptr<D3D12SparseBufferMappingPlan> &out
  ) override try {
    if (out)
      return E_INVALIDARG;
    auto plan = std::make_unique<MappingPlan>();
    HRESULT hr = BuildCopyPlan(source, dst, src, size, flags, *plan);
    if (FAILED(hr) || FAILED(hr = plan->Capture()))
      return hr;
    out = std::move(plan);
    return S_OK;
  } catch (const std::bad_alloc &) {
    return E_OUTOFMEMORY;
  }
};

HRESULT
CreateCommittedBuffer(
    MTLD3D12Device *pDevice, const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS HeapFlags,
    const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *OptimizedClearValue,
    REFIID riid, void **ppResource
) {
  auto buffer = Com(new MTLD3D12Buffer(pDevice));
  HRESULT hr = buffer->Initialize(pHeapProps, HeapFlags, pDesc, InitialState, OptimizedClearValue, nullptr, ~0ull);
  if (FAILED(hr))
    return hr;
  if (!ppResource)
    return S_FALSE;
  return buffer->QueryInterface(riid, ppResource);
}

HRESULT
CreatePlacedBuffer(
    MTLD3D12Device *pDevice, MTLD3D12Heap *pHeap, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
    UINT64 HeapOffset, const D3D12_CLEAR_VALUE *OptimizedClearValue, REFIID riid, void **ppResource
) {
  auto buffer = Com(new MTLD3D12Buffer(pDevice));
  D3D12_HEAP_DESC heap_desc = pHeap->GetDesc();
  if (heap_desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS)
    return E_INVALIDARG;

  HRESULT hr = buffer->Initialize(
      &heap_desc.Properties, heap_desc.Flags, pDesc, InitialState, OptimizedClearValue, pHeap, HeapOffset
  );
  if (FAILED(hr)) {
    ERR("CreatePlacedBuffer: initialization failed: 0x", std::hex, hr, std::dec);
    return hr;
  }
  if (!ppResource)
    return S_FALSE;
  return buffer->QueryInterface(riid, ppResource);
}

HRESULT
CreateReservedBuffer(
    MTLD3D12Device *pDevice, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
    const D3D12_CLEAR_VALUE *OptimizedClearValue, REFIID riid, void **ppResource
) {
  InitReturnPtr(ppResource);
  if (OptimizedClearValue)
    return E_INVALIDARG;

  auto buffer = Com(new MTLD3D12Buffer(pDevice));
  HRESULT hr = buffer->InitializeReserved(pDesc, InitialState);
  if (FAILED(hr))
    return hr;
  if (!ppResource)
    return S_FALSE;
  return buffer->QueryInterface(riid, ppResource);
}

} // namespace dxmt

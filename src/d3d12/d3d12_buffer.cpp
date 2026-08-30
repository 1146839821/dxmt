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

namespace dxmt {

namespace {

bool
MakeBufferSlice(UINT first_element, UINT element_count, uint64_t element_stride, uint64_t buffer_size,
                BufferSlice &slice) {
  if (!element_stride || first_element > UINT64_MAX / element_stride ||
      element_count > UINT64_MAX / element_stride)
    return false;

  const uint64_t byte_offset = uint64_t(first_element) * element_stride;
  const uint64_t byte_length = uint64_t(element_count) * element_stride;
  if (byte_offset > buffer_size || byte_length > buffer_size - byte_offset || byte_offset > UINT32_MAX ||
      byte_length > UINT32_MAX)
    return false;

  slice.firstElement = first_element;
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
    device_->RegisterResidencyAndVA(buffer->current());

    return S_OK;
  };

  ~MTLD3D12Buffer() {
    if (buffer && buffer->current())
      device_->UnregisterResidencyAndVA(buffer->current());
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
    if (Subresource || !ppData)
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
    *__ret = desc_;
    return __ret;
  };

  virtual D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE
  GetGPUVirtualAddress() {
    return buffer->current()->gpuAddress();
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

  virtual HRESULT STDMETHODCALLTYPE
  GetHeapProperties(D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS *pFlags) {
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
    BufferViewDescriptor view_descriptor{Format.PixelFormat};
    if (!MakeBufferSlice(ViewDesc.Buffer.FirstElement, ViewDesc.Buffer.NumElements, Format.BytesPerTexel, desc_.Width, Slice))
      return E_INVALIDARG;

    auto view = buffer->createView(view_descriptor);
    return Heap->AddShaderResourceView(Index, buffer.ptr(), view, Slice);
  };

  virtual HRESULT STDMETHODCALLTYPE
  CreateUnorderedAccessView(
      ID3D12Resource *pCounter, const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor
  ) {
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
          (ViewDesc.Buffer.CounterOffsetInBytes & 3) ||
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
    WARN("D3D12 buffer GetResourceTiling is not implemented");
    if (TotalTileCount)
      *TotalTileCount = 0;
    if (PackedMipInfo)
      *PackedMipInfo = {};
    if (StandardTitleShape)
      *StandardTitleShape = {};
    if (SubresourceTilingCount)
      *SubresourceTilingCount = 0;
  };
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

} // namespace dxmt

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

namespace dxmt {

class MTLD3D12HeapImpl : public MTLD3D12Pageable<MTLD3D12Heap> {

  D3D12_HEAP_DESC desc_;
  WMT::Reference<WMT::Heap> heap_;

  static WMTResourceOptions
  GetResourceOptions(const D3D12_HEAP_PROPERTIES &properties) {
    WMTResourceOptions options = WMTResourceHazardTrackingModeUntracked;
    switch (properties.Type) {
    case D3D12_HEAP_TYPE_DEFAULT:
      options |= WMTResourceStorageModePrivate;
      break;
    case D3D12_HEAP_TYPE_UPLOAD:
    case D3D12_HEAP_TYPE_READBACK:
      break;
    case D3D12_HEAP_TYPE_CUSTOM:
      if (properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
        options |= WMTResourceStorageModePrivate;
      if (properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE)
        options |= WMTResourceOptionCPUCacheModeWriteCombined;
      break;
    default:
      break;
    }
    return options;
  }

public:
  MTLD3D12HeapImpl(MTLD3D12Device *pDevice) : MTLD3D12Pageable<MTLD3D12Heap>(pDevice) {}

  ~MTLD3D12HeapImpl() {
    if (heap_)
      device_->UnregisterResidency(heap_);
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12Heap)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12Resource), riid)) {
      WARN("D3D12Heap: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  HRESULT
  Initialize(const D3D12_HEAP_DESC *pDesc) {
    if (!pDesc || !pDesc->SizeInBytes)
      return E_INVALIDARG;
    if (pDesc->Flags & D3D12_HEAP_FLAG_ALLOW_DISPLAY)
      return E_INVALIDARG; // must be committed resource

    const auto type_flags = pDesc->Flags &
                            (D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS |
                             D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES |
                             D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES);
    if ((type_flags == (D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS | D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES)) ||
        (type_flags == (D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS | D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES)) ||
        (type_flags == (D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES | D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES)))
      return E_INVALIDARG;

    desc_ = *pDesc;
    desc_.Properties.CreationNodeMask = 1;
    desc_.Properties.VisibleNodeMask = 1;


    switch (pDesc->Alignment) {
    case 0:
      desc_.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      [[fallthrough]];
    case D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT:
    case D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT:
      break;
    default:
      return E_INVALIDARG;
    }

    auto size_aligned = align(pDesc->SizeInBytes, desc_.Alignment);
    if (!size_aligned)
      return E_INVALIDARG;

    WMTHeapInfo info = {};
    info.size = desc_.SizeInBytes;
    info.options = GetResourceOptions(desc_.Properties);
    info.type = WMTHeapTypePlacement;
    info.sparse_page_size = WMTSparsePageSize64;
    heap_ = device_->GetMTLDevice().newHeap(info);
    if (!heap_)
      return E_OUTOFMEMORY;

    return device_->RegisterResidency(heap_);
  }

  WMT::Heap
  GetMetalHeap() override {
    return heap_;
  }

  virtual D3D12_HEAP_DESC *STDMETHODCALLTYPE
  GetDesc(D3D12_HEAP_DESC *__ret) {
    *__ret = desc_;
    return __ret;
  };
};

HRESULT
CreateHeap(MTLD3D12Device *pDevice, const D3D12_HEAP_DESC *pDesc, REFIID riid, void **ppHeap) {
  InitReturnPtr(ppHeap);
  auto heap = Com(new MTLD3D12HeapImpl(pDevice));
  HRESULT hr = heap->Initialize(pDesc);
  if (FAILED(hr))
    return hr;
  if (!ppHeap)
    return S_FALSE;
  return heap->QueryInterface(riid, ppHeap);
}

}; // namespace dxmt

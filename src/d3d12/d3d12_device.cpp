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
#include "d3d12_device_child.hpp"
#include "d3d12sdklayers.h"
#include "Metal.hpp"
#include "com/com_pointer.hpp"
#include "com/com_object.hpp"
#include "dxgi_interfaces.h"
#include "dxmt_format.hpp"
#include "log/log.hpp"
#include <chrono>
#include <map>
#include <thread>
#include <vector>

namespace dxmt {

namespace {

bool
ConvertResourceDesc1(const D3D12_RESOURCE_DESC1 *source, D3D12_RESOURCE_DESC *destination) {
  if (!source || !destination || source->SamplerFeedbackMipRegion.Width || source->SamplerFeedbackMipRegion.Height ||
      source->SamplerFeedbackMipRegion.Depth)
    return false;

  destination->Dimension = source->Dimension;
  destination->Alignment = source->Alignment;
  destination->Width = source->Width;
  destination->Height = source->Height;
  destination->DepthOrArraySize = source->DepthOrArraySize;
  destination->MipLevels = source->MipLevels;
  destination->Format = source->Format;
  destination->SampleDesc = source->SampleDesc;
  destination->Layout = source->Layout;
  destination->Flags = source->Flags;
  return true;
}

bool
ConvertResourceDesc1WithLayout(
    const D3D12_RESOURCE_DESC1 *source, D3D12_RESOURCE_DESC *destination, D3D12_BARRIER_LAYOUT layout,
    D3D12_RESOURCE_STATES *initial_state
) {
  return ConvertResourceDesc1(source, destination) && ConvertBarrierLayout(source->Dimension, layout, initial_state);
}

} // namespace

bool
ConvertBarrierLayout(
    D3D12_RESOURCE_DIMENSION dimension, D3D12_BARRIER_LAYOUT source, D3D12_RESOURCE_STATES *destination
) {
  if (!destination)
    return false;

  if (dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
    if (source != D3D12_BARRIER_LAYOUT_UNDEFINED)
      return false;
    *destination = D3D12_RESOURCE_STATE_COMMON;
    return true;
  }

  switch (source) {
  case D3D12_BARRIER_LAYOUT_COMMON:
  case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COMMON:
  case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COMMON:
  case D3D12_BARRIER_LAYOUT_VIDEO_QUEUE_COMMON:
    *destination = D3D12_RESOURCE_STATE_COMMON;
    return true;
  case D3D12_BARRIER_LAYOUT_GENERIC_READ:
  case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ:
  case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_GENERIC_READ:
    *destination = D3D12_RESOURCE_STATE_GENERIC_READ;
    return true;
  case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
    *destination = D3D12_RESOURCE_STATE_RENDER_TARGET;
    return true;
  case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
  case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS:
  case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS:
    *destination = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    return true;
  case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
    *destination = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    return true;
  case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
    *destination = D3D12_RESOURCE_STATE_DEPTH_READ;
    return true;
  case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
  case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE:
  case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE:
    *destination = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    return true;
  case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
  case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE:
  case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_SOURCE:
    *destination = D3D12_RESOURCE_STATE_COPY_SOURCE;
    return true;
  case D3D12_BARRIER_LAYOUT_COPY_DEST:
  case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST:
  case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_DEST:
    *destination = D3D12_RESOURCE_STATE_COPY_DEST;
    return true;
  case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
    *destination = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    return true;
  case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
    *destination = D3D12_RESOURCE_STATE_RESOLVE_DEST;
    return true;
  case D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE:
    *destination = D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
    return true;
  case D3D12_BARRIER_LAYOUT_VIDEO_DECODE_READ:
    *destination = D3D12_RESOURCE_STATE_VIDEO_DECODE_READ;
    return true;
  case D3D12_BARRIER_LAYOUT_VIDEO_DECODE_WRITE:
    *destination = D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE;
    return true;
  case D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_READ:
    *destination = D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ;
    return true;
  case D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_WRITE:
    *destination = D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE;
    return true;
  case D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_READ:
    *destination = D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ;
    return true;
  case D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_WRITE:
    *destination = D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE;
    return true;
  default:
    return false;
  }
}

class MTLD3D12InfoQueue final : public ComObject<ID3D12InfoQueue> {
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;
    *ppvObject = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12InfoQueue)) {
      *ppvObject = ref(static_cast<ID3D12InfoQueue *>(this));
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE SetMessageCountLimit(UINT64 limit) override {
    message_count_limit_ = limit;
    return S_OK;
  }

  void STDMETHODCALLTYPE ClearStoredMessages() override {}

  HRESULT STDMETHODCALLTYPE GetMessage(UINT64, D3D12_MESSAGE *, SIZE_T *length) override {
    if (!length)
      return E_INVALIDARG;
    *length = 0;
    return DXGI_ERROR_NOT_FOUND;
  }

  UINT64 STDMETHODCALLTYPE GetNumMessagesAllowedByStorageFilter() override { return 0; }
  UINT64 STDMETHODCALLTYPE GetNumMessagesDeniedByStorageFilter() override { return 0; }
  UINT64 STDMETHODCALLTYPE GetNumStoredMessages() override { return 0; }
  UINT64 STDMETHODCALLTYPE GetNumStoredMessagesAllowedByRetrievalFilter() override { return 0; }
  UINT64 STDMETHODCALLTYPE GetNumMessagesDiscardedByMessageCountLimit() override { return 0; }
  UINT64 STDMETHODCALLTYPE GetMessageCountLimit() override { return message_count_limit_; }

  HRESULT STDMETHODCALLTYPE AddStorageFilterEntries(D3D12_INFO_QUEUE_FILTER *) override { return S_OK; }

  HRESULT STDMETHODCALLTYPE GetStorageFilter(D3D12_INFO_QUEUE_FILTER *, SIZE_T *length) override {
    if (!length)
      return E_INVALIDARG;
    *length = 0;
    return DXGI_ERROR_NOT_FOUND;
  }

  void STDMETHODCALLTYPE ClearStorageFilter() override {}
  HRESULT STDMETHODCALLTYPE PushEmptyStorageFilter() override {
    storage_filter_stack_size_++;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE PushCopyOfStorageFilter() override {
    storage_filter_stack_size_++;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE PushStorageFilter(D3D12_INFO_QUEUE_FILTER *) override {
    storage_filter_stack_size_++;
    return S_OK;
  }
  void STDMETHODCALLTYPE PopStorageFilter() override {
    if (storage_filter_stack_size_)
      storage_filter_stack_size_--;
  }
  UINT STDMETHODCALLTYPE GetStorageFilterStackSize() override { return storage_filter_stack_size_; }

  HRESULT STDMETHODCALLTYPE AddRetrievalFilterEntries(D3D12_INFO_QUEUE_FILTER *) override { return S_OK; }

  HRESULT STDMETHODCALLTYPE GetRetrievalFilter(D3D12_INFO_QUEUE_FILTER *, SIZE_T *length) override {
    if (!length)
      return E_INVALIDARG;
    *length = 0;
    return DXGI_ERROR_NOT_FOUND;
  }

  void STDMETHODCALLTYPE ClearRetrievalFilter() override {}
  HRESULT STDMETHODCALLTYPE PushEmptyRetrievalFilter() override {
    retrieval_filter_stack_size_++;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE PushCopyOfRetrievalFilter() override {
    retrieval_filter_stack_size_++;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE PushRetrievalFilter(D3D12_INFO_QUEUE_FILTER *) override {
    retrieval_filter_stack_size_++;
    return S_OK;
  }
  void STDMETHODCALLTYPE PopRetrievalFilter() override {
    if (retrieval_filter_stack_size_)
      retrieval_filter_stack_size_--;
  }
  UINT STDMETHODCALLTYPE GetRetrievalFilterStackSize() override { return retrieval_filter_stack_size_; }

  HRESULT STDMETHODCALLTYPE AddMessage(
      D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY, D3D12_MESSAGE_ID, const char *) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE AddApplicationMessage(D3D12_MESSAGE_SEVERITY, const char *) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE SetBreakOnCategory(D3D12_MESSAGE_CATEGORY, WINBOOL) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY, WINBOOL) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE SetBreakOnID(D3D12_MESSAGE_ID, WINBOOL) override { return S_OK; }
  WINBOOL STDMETHODCALLTYPE GetBreakOnCategory(D3D12_MESSAGE_CATEGORY) override { return FALSE; }
  WINBOOL STDMETHODCALLTYPE GetBreakOnSeverity(D3D12_MESSAGE_SEVERITY) override { return FALSE; }
  WINBOOL STDMETHODCALLTYPE GetBreakOnID(D3D12_MESSAGE_ID) override { return FALSE; }
  void STDMETHODCALLTYPE SetMuteDebugOutput(WINBOOL mute) override { mute_debug_output_ = mute; }
  WINBOOL STDMETHODCALLTYPE GetMuteDebugOutput() override { return mute_debug_output_; }

private:
  UINT64 message_count_limit_ = ~UINT64(0);
  UINT storage_filter_stack_size_ = 0;
  UINT retrieval_filter_stack_size_ = 0;
  WINBOOL mute_debug_output_ = FALSE;
};

class MTLD3D12DeviceImpl : public MTLD3D12Object<ComObject<MTLD3D12Device>> {

  Com<IMTLDXGIAdapter> adapter_;
  D3D_FEATURE_LEVEL feature_level_;

  bool advertise_numa_ = false;

  dxmt::mutex residency_lock_;
  WMT::Reference<WMT::ResidencySet> residency_set_;
  std::map<uint64_t, BufferAllocation *> interval_map_;
  FormatCapabilityInspector format_capabilities_;

  InternalCommandLibrary command_library;

public:
  MTLD3D12DeviceImpl(IMTLDXGIAdapter *adapter, D3D_FEATURE_LEVEL feature_level)
      : adapter_(adapter), feature_level_(feature_level), command_library(adapter_->GetMTLDevice()) {}

  ~MTLD3D12DeviceImpl() {}

  HRESULT
  Initialize() {
    WMT::Reference<WMT::Error> err;
    residency_set_ = adapter_->GetMTLDevice().newResidencySet(0, err);
    if (!residency_set_) {
      ERR("Failed to create MTLResidencySet: ", err.description().getUTF8String());
      return E_FAIL;
    }
    format_capabilities_.Inspect(GetMTLDevice());
    return S_OK;
  };

  WMT::Device
  GetMTLDevice() {
    return adapter_->GetMTLDevice();
  };

  D3D_FEATURE_LEVEL
  GetFeatureLevel() {
    return feature_level_;
  };

  HRESULT
  GetAdapter(REFIID riid, void **ppAdapter) {
    return adapter_->QueryInterface(riid, ppAdapter);
  };

  UINT STDMETHODCALLTYPE
  GetNodeCount() {
    return 1; // FIXME
  };

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == DXMT_STREAMLINE_RETRIEVE_BASE_INTERFACE) {
      *ppvObject = ref(static_cast<ID3D12Device *>(this));
      return S_OK;
    }

    if (riid == __uuidof(ID3D12Device1) || riid == __uuidof(ID3D12Device2) || riid == __uuidof(ID3D12Device3) ||
        riid == __uuidof(ID3D12Device4)) {
      auto *device = static_cast<ID3D12Device4 *>(this);
      *ppvObject = ref(device);
      return S_OK;
    }

    if (riid == __uuidof(ID3D12Device5)) {
      *ppvObject = ref(static_cast<ID3D12Device5 *>(this));
      return S_OK;
    }

    if (riid == __uuidof(ID3D12Device6)) {
      *ppvObject = ref(static_cast<ID3D12Device6 *>(this));
      return S_OK;
    }

    if (riid == __uuidof(ID3D12Device7)) {
      *ppvObject = ref(static_cast<ID3D12Device7 *>(this));
      return S_OK;
    }

    if (riid == __uuidof(ID3D12Device8)) {
      *ppvObject = ref(static_cast<ID3D12Device8 *>(this));
      return S_OK;
    }

    if (riid == __uuidof(ID3D12Device9)) {
      *ppvObject = ref(static_cast<ID3D12Device9 *>(this));
      return S_OK;
    }

    if (riid == __uuidof(ID3D12Device10)) {
      *ppvObject = ref(static_cast<ID3D12Device10 *>(this));
      return S_OK;
    }

    if (riid == __uuidof(ID3D12InfoQueue)) {
      *ppvObject = ref(new MTLD3D12InfoQueue());
      return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12Device)) {
      *ppvObject = ref(static_cast<ID3D12Device *>(this));
      return S_OK;
    }

    // Keep newer interfaces explicit until their methods have an implementation.
    if (riid == DXMT_STREAMLINE_D3D12_DEVICE_GUID || riid == DXMT_ID3D11_DEVICE_GUID) {
      return E_NOINTERFACE;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12Device1), riid)) {
      WARN("D3D12Device: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE
  CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue) {
    if (!pDesc)
      return E_INVALIDARG;
    if (pDesc->NodeMask & ~1u)
      return E_INVALIDARG;
    switch (pDesc->Type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
    case D3D12_COMMAND_LIST_TYPE_COMPUTE:
    case D3D12_COMMAND_LIST_TYPE_COPY:
      break;
    default:
      return E_INVALIDARG;
    }
    if (pDesc->Flags)
      WARN("CreateCommandQueue: flags ignored: ", pDesc->Flags);
    return dxmt::CreateCommandQueue(this, pDesc, riid, ppCommandQueue);
  };

  HRESULT STDMETHODCALLTYPE
  CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE Type, REFIID riid, void **ppCommandAllocator) {
    return dxmt::CreateCommandAllocator(this, Type, riid, ppCommandAllocator);
  };

  HRESULT STDMETHODCALLTYPE
  CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) {
    return dxmt::CreateGraphicsPipelineState(this, pDesc, riid, ppPipelineState);
  };

  HRESULT STDMETHODCALLTYPE
  CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) {
    return dxmt::CreateComputePipelineState(this, pDesc, riid, ppPipelineState);
  };

  HRESULT STDMETHODCALLTYPE
  CreateCommandList(
      UINT NodeMask, D3D12_COMMAND_LIST_TYPE Type, ID3D12CommandAllocator *pCommandAllocator,
      ID3D12PipelineState *pInitialPipelineState, REFIID riid, void **ppCommandList
  ) {
    if ((NodeMask & ~1u) || !pCommandAllocator || !IsSameDevice(this, pCommandAllocator) ||
        (pInitialPipelineState && !IsSameDevice(this, pInitialPipelineState)))
      return E_INVALIDARG;
    auto allocator = static_cast<MTLD3D12CommandAllocator *>(pCommandAllocator);
    if (allocator->GetType() != Type)
      return E_INVALIDARG;
    return allocator->CreateCommandList(NodeMask, Type, pInitialPipelineState, riid, ppCommandList);
  };

  HRESULT STDMETHODCALLTYPE
  CheckFeatureSupport(D3D12_FEATURE Feature, void *pFeatureData, UINT DataSize) {
    if (!pFeatureData)
      return E_INVALIDARG;

    auto metal = GetMTLDevice();
    switch (Feature) {
    case D3D12_FEATURE_ARCHITECTURE: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_ARCHITECTURE))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_ARCHITECTURE *>(pFeatureData);
      if (out->NodeIndex > 0)
        return E_INVALIDARG;
      out->CacheCoherentUMA = metal.hasUnifiedMemory();
      out->TileBasedRenderer = TRUE;
      out->UMA = metal.hasUnifiedMemory() && !advertise_numa_;
      return S_OK;
    }
    case D3D12_FEATURE_ARCHITECTURE1: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_ARCHITECTURE1))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_ARCHITECTURE1 *>(pFeatureData);
      if (out->NodeIndex > 0)
        return E_INVALIDARG;
      out->CacheCoherentUMA = metal.hasUnifiedMemory();
      out->TileBasedRenderer = TRUE;
      out->UMA = metal.hasUnifiedMemory() && !advertise_numa_;
      out->IsolatedMMU = FALSE;
      return S_OK;
    }
    case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS *>(pFeatureData);

      if (out->SampleCount == 0) {
        out->Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        out->NumQualityLevels = 0;
        return E_FAIL;
      }

      if (out->Format == DXGI_FORMAT_UNKNOWN) {
        out->NumQualityLevels = out->SampleCount == 0 ? 1 : 0;
        return S_OK;
      }

      MTL_DXGI_FORMAT_DESC format_desc;
      HRESULT hr = MTLQueryDXGIFormat(metal, out->Format, format_desc);
      if (SUCCEEDED(hr) && out->SampleCount) {
        out->Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        out->NumQualityLevels = metal.supportsTextureSampleCount(out->SampleCount) ? 1 : 0;
      } else {
        out->Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        out->NumQualityLevels = 0;
        return E_FAIL;
      }
      return S_OK;
    }
    case D3D12_FEATURE_ROOT_SIGNATURE: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_ROOT_SIGNATURE))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_ROOT_SIGNATURE *>(pFeatureData);
      switch (out->HighestVersion) {
      default:
        return E_INVALIDARG;
      case D3D_ROOT_SIGNATURE_VERSION_1:
        out->HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1;
        break;
      case D3D_ROOT_SIGNATURE_VERSION_1_1:
        out->HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        break;
      }
      return S_OK;
    }
    case D3D12_FEATURE_FEATURE_LEVELS: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS *>(pFeatureData);
      if (!out->NumFeatureLevels || !out->pFeatureLevelsRequested)
        return E_INVALIDARG;
      D3D_FEATURE_LEVEL max_level = {};
      const auto supported_level = GetFeatureLevel();
      for (unsigned i = 0; i < out->NumFeatureLevels; i++)
        if (out->pFeatureLevelsRequested[i] <= supported_level)
          max_level = std::max(out->pFeatureLevelsRequested[i], max_level);
      if (!max_level)
        return E_FAIL;
      out->MaxSupportedFeatureLevel = max_level;
      return S_OK;
    }
    case D3D12_FEATURE_FORMAT_INFO:  {
       if (DataSize != sizeof(D3D12_FEATURE_DATA_FORMAT_INFO))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_FORMAT_INFO *>(pFeatureData);
      if (out->Format == DXGI_FORMAT_UNKNOWN) {
        out->PlaneCount = 1;
        return S_OK;
      }
      MTL_DXGI_FORMAT_DESC format_desc;
      HRESULT hr = MTLQueryDXGIFormat(metal, out->Format, format_desc);
      if (FAILED(hr))
        return E_FAIL;

      out->PlaneCount = format_desc.PlanarCount;
      return S_OK;
    }
    case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT *>(pFeatureData);
      out->MaxGPUVirtualAddressBitsPerProcess = 48;
      out->MaxGPUVirtualAddressBitsPerResource = 48;
      return S_OK;
    }
    case D3D12_FEATURE_SHADER_MODEL: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_SHADER_MODEL))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_SHADER_MODEL *>(pFeatureData);
      switch (out->HighestShaderModel) {
      case D3D_SHADER_MODEL_5_1:
        return S_OK;
      case D3D_SHADER_MODEL_6_0:
        return S_OK;
      default:
        return E_INVALIDARG;
      }
    }
    case D3D12_FEATURE_D3D12_OPTIONS: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS *>(pFeatureData);
      out->DoublePrecisionFloatShaderOps = FALSE;
#ifdef DXMT_NO_PRIVATE_API
      out->OutputMergerLogicOp = FALSE;
#else
      out->OutputMergerLogicOp = TRUE;
#endif
      out->MinPrecisionSupport = D3D12_SHADER_MIN_PRECISION_SUPPORT_16_BIT;
      out->TiledResourcesTier = D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
      out->ResourceBindingTier = metal.supportsFamily(WMTGPUFamilyApple6)
                                     ? D3D12_RESOURCE_BINDING_TIER_2
                                     : D3D12_RESOURCE_BINDING_TIER_1;
      out->PSSpecifiedStencilRefSupported = TRUE;
      out->TypedUAVLoadAdditionalFormats = FALSE;
      out->ROVsSupported = FALSE;
      out->ConservativeRasterizationTier = D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED;
      out->MaxGPUVirtualAddressBitsPerResource = 48;
      out->StandardSwizzle64KBSupported = FALSE;
      out->CrossNodeSharingTier = D3D12_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED;
      out->CrossAdapterRowMajorTextureSupported = FALSE;
      out->VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation = FALSE;
      out->ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_2;
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS1: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS1))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS1 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS2: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS2))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS2 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_SHADER_CACHE: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_SHADER_CACHE))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_SHADER_CACHE *>(pFeatureData);
      out->SupportFlags = D3D12_SHADER_CACHE_SUPPORT_NONE;
      return S_OK;
    }
    case D3D12_FEATURE_COMMAND_QUEUE_PRIORITY: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY *>(pFeatureData);
      const bool supported_type = out->CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT ||
                                  out->CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE ||
                                  out->CommandListType == D3D12_COMMAND_LIST_TYPE_COPY;
      out->PriorityForTypeIsSupported = supported_type && out->Priority == D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS3: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS3))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS3 *>(pFeatureData);
      *out = {};
      out->CopyQueueTimestampQueriesSupported = TRUE;
      out->CastingFullyTypedFormatSupported = TRUE;
      return S_OK;
    }
    case D3D12_FEATURE_EXISTING_HEAPS: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_EXISTING_HEAPS))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_EXISTING_HEAPS *>(pFeatureData);
      out->Supported = FALSE;
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS4: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS4))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS4 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_SERIALIZATION: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_SERIALIZATION))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_SERIALIZATION *>(pFeatureData);
      if (out->NodeIndex > 0)
        return E_INVALIDARG;
      out->HeapSerializationTier = D3D12_HEAP_SERIALIZATION_TIER_0;
      return S_OK;
    }
    case D3D12_FEATURE_CROSS_NODE: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_CROSS_NODE))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_CROSS_NODE *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS5: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS5 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT *>(pFeatureData);
      if (out->NodeIndex > 0)
        return E_INVALIDARG;
      out->Support = D3D12_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_NONE;
      return S_OK;
    }
    case D3D12_FEATURE_DISPLAYABLE: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_DISPLAYABLE))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_DISPLAYABLE *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS6: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS6))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS6 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS7: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS7 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS8: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS8))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS8 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS9: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS9))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS9 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS10: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS10))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS10 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS11: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS11))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS11 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS12: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS12))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS12 *>(pFeatureData);
      *out = {};
      out->MSPrimitivesPipelineStatisticIncludesCulledPrimitives = D3D12_TRI_STATE_UNKNOWN;
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS13: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS13))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS13 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS14: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS14))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS14 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS15: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS15))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS15 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS16: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS16))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS16 *>(pFeatureData);
      *out = {};
      out->GPUUploadHeapSupported = FALSE;    // TODO(d3d12): gpu upload heap
      out->DynamicDepthBiasSupported = FALSE; // TODO(d3d12): ID3D12GraphicsCommandList9::RSSetDepthBias
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS17: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS17))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS17 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_D3D12_OPTIONS18: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS18))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS18 *>(pFeatureData);
      *out = {};
      return S_OK;
    }
    case D3D12_FEATURE_FORMAT_SUPPORT: {
      if (DataSize != sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT))
        return E_INVALIDARG;
      auto *out = reinterpret_cast<D3D12_FEATURE_DATA_FORMAT_SUPPORT *>(pFeatureData);
      if (!out)
        return E_INVALIDARG;

      if (out->Format == DXGI_FORMAT_UNKNOWN) {
        out->Support1 = D3D12_FORMAT_SUPPORT1_BUFFER;
        out->Support2 = {};
        return S_OK;
      }

      MTL_DXGI_FORMAT_DESC format_desc;
      if (FAILED(MTLQueryDXGIFormat(metal, out->Format, format_desc)) ||
          format_desc.PixelFormat == WMTPixelFormatInvalid)
        return E_INVALIDARG;

      auto capability_it = format_capabilities_.textureCapabilities.find(format_desc.PixelFormat);
      auto has_capability = [&](FormatCapability capability) {
        return capability_it != format_capabilities_.textureCapabilities.end() &&
               (static_cast<int>(capability_it->second) & static_cast<int>(capability));
      };

      out->Support1 = D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                      D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE |
                      D3D12_FORMAT_SUPPORT1_SHADER_LOAD | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
                      D3D12_FORMAT_SUPPORT1_MIP | D3D12_FORMAT_SUPPORT1_CAST_WITHIN_BIT_LAYOUT;
      out->Support2 = D3D12_FORMAT_SUPPORT2_NONE;

      if (format_desc.AttributeFormat != WMTAttributeFormatInvalid)
        out->Support1 |= D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER;
      if (format_desc.PixelFormat == WMTPixelFormatR16Uint || format_desc.PixelFormat == WMTPixelFormatR32Uint)
        out->Support1 |= D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER;
      if (out->Format == DXGI_FORMAT_R32_FLOAT || out->Format == DXGI_FORMAT_R32_UINT ||
          out->Format == DXGI_FORMAT_R32_SINT || out->Format == DXGI_FORMAT_R32G32_FLOAT ||
          out->Format == DXGI_FORMAT_R32G32_UINT || out->Format == DXGI_FORMAT_R32G32_SINT ||
          out->Format == DXGI_FORMAT_R32G32B32_FLOAT || out->Format == DXGI_FORMAT_R32G32B32_UINT ||
          out->Format == DXGI_FORMAT_R32G32B32_SINT || out->Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
          out->Format == DXGI_FORMAT_R32G32B32A32_UINT || out->Format == DXGI_FORMAT_R32G32B32A32_SINT)
        out->Support1 |= D3D12_FORMAT_SUPPORT1_SO_BUFFER;
      if (format_desc.Flag & MTL_DXGI_FORMAT_BACKBUFFER)
        out->Support1 |= D3D12_FORMAT_SUPPORT1_DISPLAY | D3D12_FORMAT_SUPPORT1_BACK_BUFFER_CAST;
      if (has_capability(FormatCapability::Color))
        out->Support1 |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
      if (has_capability(FormatCapability::Blend))
        out->Support1 |= D3D12_FORMAT_SUPPORT1_BLENDABLE;
      if (has_capability(FormatCapability::DepthStencil))
        out->Support1 |= D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON;
      if (has_capability(FormatCapability::Resolve))
        out->Support1 |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE;
      if (has_capability(FormatCapability::MSAA))
        out->Support1 |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD;
      if (has_capability(FormatCapability::TextureBufferRead) || has_capability(FormatCapability::TextureBufferReadWrite))
        out->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
      if (has_capability(FormatCapability::TextureBufferWrite) || has_capability(FormatCapability::TextureBufferReadWrite))
        out->Support1 |= D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
      if (has_capability(FormatCapability::TextureBufferWrite) || has_capability(FormatCapability::TextureBufferReadWrite))
        out->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
      if (has_capability(FormatCapability::Atomic))
        out->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
                         D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE |
                         D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
                         D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;
      return S_OK;
    }
    default:
      break;
    }
    ERR("CheckFeatureSupport: unhandled feature ", Feature);
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE
  CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC *pDesc, REFIID riid, void **ppDescriptorHeap) {
    return dxmt::CreateDescriptorHeap(this, pDesc, riid, ppDescriptorHeap);
  };

  UINT STDMETHODCALLTYPE
  GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) {
    switch (DescriptorHeapType) {
    case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
    case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
    case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
    case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
      return 32;
    default:
      break;
    }
    return 0;
  };

  HRESULT STDMETHODCALLTYPE
  CreateRootSignature(
      UINT NodeMask, const void *pBytecode, SIZE_T BytecodeLength, REFIID riid, void **ppRootSignature
  ) {
    return dxmt::CreateRootSignature(this, NodeMask, pBytecode, BytecodeLength, riid, ppRootSignature);
  };

  void STDMETHODCALLTYPE
  CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) {
    if (!pDesc)
      return;
    auto [Heap, Index] = GetShaderVisibleDescriptorHeap(this, Descriptor);
    if (Heap)
      Heap->AddConstantBufferView(Index, pDesc->BufferLocation, pDesc->SizeInBytes);
  };

  void STDMETHODCALLTYPE
  CreateShaderResourceView(
      ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor
  ) {
    if (!pResource) {
      auto [Heap, Index] = GetShaderVisibleDescriptorHeap(this, Descriptor);
      if (Heap)
        Heap->AddShaderResourceView(Index, pDesc);
      return;
    }
    auto d3d12res = static_cast<MTLD3D12Resource *>(pResource);
    HRESULT hr = d3d12res->CreateShaderResourceView(pDesc, Descriptor);
    if (FAILED(hr))
      WARN("CreateShaderResourceView failed: ", hr);
  };

  void STDMETHODCALLTYPE
  CreateUnorderedAccessView(
      ID3D12Resource *pResource, ID3D12Resource *pCounter, const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc,
      D3D12_CPU_DESCRIPTOR_HANDLE Descriptor
  ) {
    if (!pResource) {
      auto [Heap, Index] = GetShaderVisibleDescriptorHeap(this, Descriptor);
      if (Heap)
        Heap->AddUnorderedAccessView(Index, pDesc);
      return;
    }
    auto d3d12res = static_cast<MTLD3D12Resource *>(pResource);
    HRESULT hr = d3d12res->CreateUnorderedAccessView(pCounter, pDesc, Descriptor);
    if (FAILED(hr))
      WARN("CreateUnorderedAccessView failed: ", hr);
  };

  void STDMETHODCALLTYPE
  CreateRenderTargetView(
      ID3D12Resource *pResource, const D3D12_RENDER_TARGET_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor
  ) {
    if (!pResource) {
      // null descriptor
      auto [Heap, Index] = GetRenderTargetHeap(this, Descriptor);
      if (Heap)
        Heap->AddRenderTarget(Index, nullptr);
      return;
    }
    auto d3d12res = static_cast<MTLD3D12Resource *>(pResource);
    HRESULT hr = d3d12res->CreateRenderTargetView(pDesc, Descriptor);
    if (FAILED(hr))
      WARN("CreateRenderTargetView failed: ", hr);
  };

  void STDMETHODCALLTYPE
  CreateDepthStencilView(
      ID3D12Resource *pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor
  ) {
    if (!pResource) {
      // null descriptor
      auto [Heap, Index] = GetRenderTargetHeap(this, Descriptor);
      if (Heap)
        Heap->AddRenderTarget(Index, nullptr);
      return;
    }
    auto d3d12res = static_cast<MTLD3D12Resource *>(pResource);
    HRESULT hr = d3d12res->CreateDepthStencilView(pDesc, Descriptor);
    if (FAILED(hr))
      WARN("CreateDepthStencilView failed: ", hr);
  };

  void STDMETHODCALLTYPE
  CreateSampler(const D3D12_SAMPLER_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) {
    auto [Heap, Index] = GetSamplerDescriptorHeap(this, Descriptor);
    if (Heap)
      Heap->AddSampler(Index, pDesc);
  };

  void STDMETHODCALLTYPE
  CopyDescriptors(
      UINT DstDescriptorRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE *DstDescriptorRangeOffsets,
      const UINT *DstDescriptorRangeSizes, UINT SrcDescriptorRangeCount,
      const D3D12_CPU_DESCRIPTOR_HANDLE *SrcDescriptorRangeOffsets, const UINT *SrcDescriptorRangeSizes,
      D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType
  ) {
    if (!DstDescriptorRangeCount || !SrcDescriptorRangeCount || !DstDescriptorRangeOffsets ||
        !DstDescriptorRangeSizes || !SrcDescriptorRangeOffsets || !SrcDescriptorRangeSizes)
      return;

    unsigned int dst_range_idx, dst_idx, src_range_idx, src_idx;
    unsigned int dst_range_size, src_range_size, copy_count;

    dst_range_idx = dst_idx = 0;
    src_range_idx = src_idx = 0;
    while (dst_range_idx < DstDescriptorRangeCount && src_range_idx < SrcDescriptorRangeCount) {
      dst_range_size = DstDescriptorRangeSizes ? DstDescriptorRangeSizes[dst_range_idx] : 1;
      src_range_size = SrcDescriptorRangeSizes ? SrcDescriptorRangeSizes[src_range_idx] : 1;
      if (!dst_range_size || !src_range_size)
        return;

      copy_count = std::min(dst_range_size - dst_idx, src_range_size - src_idx);

      switch (DescriptorHeapType) {
      case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: {
        auto [DstRangeHeap, DstRangeIndex] =
            GetShaderVisibleDescriptorHeap(this, DstDescriptorRangeOffsets[dst_range_idx]);
        auto [SrcRangeHeap, SrcRangeIndex] =
            GetShaderVisibleDescriptorHeap(this, SrcDescriptorRangeOffsets[src_range_idx]);
        if (!DstRangeHeap || !SrcRangeHeap)
          return;
        SrcRangeHeap->CopyDescriptors(SrcRangeIndex + src_idx, DstRangeHeap, DstRangeIndex + dst_idx, copy_count);
        break;
      }

      case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: {
        auto [DstRangeHeap, DstRangeIndex] = GetSamplerDescriptorHeap(this, DstDescriptorRangeOffsets[dst_range_idx]);
        auto [SrcRangeHeap, SrcRangeIndex] = GetSamplerDescriptorHeap(this, SrcDescriptorRangeOffsets[src_range_idx]);
        if (!DstRangeHeap || !SrcRangeHeap)
          return;
        SrcRangeHeap->CopyDescriptors(SrcRangeIndex + src_idx, DstRangeHeap, DstRangeIndex + dst_idx, copy_count);
        break;
      }
      case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
      case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: {
        auto [DstRangeHeap, DstRangeIndex] = GetRenderTargetHeap(this, DstDescriptorRangeOffsets[dst_range_idx]);
        auto [SrcRangeHeap, SrcRangeIndex] = GetRenderTargetHeap(this, SrcDescriptorRangeOffsets[src_range_idx]);
        if (!DstRangeHeap || !SrcRangeHeap)
          return;
        SrcRangeHeap->CopyDescriptors(SrcRangeIndex + src_idx, DstRangeHeap, DstRangeIndex + dst_idx, copy_count);
        break;
      }
      default:
        return;
      }

      dst_idx += copy_count;
      src_idx += copy_count;

      if (dst_idx >= dst_range_size) {
        ++dst_range_idx;
        dst_idx = 0;
      }
      if (src_idx >= src_range_size) {
        ++src_range_idx;
        src_idx = 0;
      }
    }
  };

  void STDMETHODCALLTYPE
  CopyDescriptorsSimple(
      UINT DescriptorCount, const D3D12_CPU_DESCRIPTOR_HANDLE DstDescriptorRangeOffset,
      const D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeOffset, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType
  ) {
    CopyDescriptors(
        1, &DstDescriptorRangeOffset, &DescriptorCount, 1, &SrcDescriptorRangeOffset, &DescriptorCount,
        DescriptorHeapType
    );
  };

  D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE GetResourceAllocationInfo(
       D3D12_RESOURCE_ALLOCATION_INFO *__ret, UINT VisibleMask, UINT ResourceDestCount, const D3D12_RESOURCE_DESC *pDescs
  ) {
    return GetResourceAllocationInfo1(__ret, VisibleMask, ResourceDestCount, pDescs, nullptr);
  };

  D3D12_HEAP_PROPERTIES *STDMETHODCALLTYPE
  GetCustomHeapProperties(D3D12_HEAP_PROPERTIES *__ret, UINT NodeMask, D3D12_HEAP_TYPE HeapType) {
    if (!__ret || (NodeMask & ~1u))
      return __ret;

    *__ret = {};
    __ret->Type = D3D12_HEAP_TYPE_CUSTOM;
    __ret->CreationNodeMask = 1;
    __ret->VisibleNodeMask = 1;
    switch (HeapType) {
    case D3D12_HEAP_TYPE_DEFAULT:
      __ret->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
      __ret->MemoryPoolPreference = advertise_numa_ ? D3D12_MEMORY_POOL_L1 : D3D12_MEMORY_POOL_L0;
      break;
    case D3D12_HEAP_TYPE_UPLOAD:
      __ret->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
      __ret->MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
      break;
    case D3D12_HEAP_TYPE_READBACK:
      __ret->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
      __ret->MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
      break;
    default:
      __ret->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
      __ret->MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
      break;
    }

    return __ret;
  };

  HRESULT STDMETHODCALLTYPE
  CreateCommittedResource(
      const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc,
      D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *OptimizedClearValue, REFIID riid, void **ppResource
  ) {
    InitReturnPtr(ppResource);
    if (!pHeapProps || !pDesc)
      return E_INVALIDARG;
    HRESULT hr = S_OK;
    hr = ValidateHeapProperties(pHeapProps, HeapFlags, advertise_numa_);
    if (FAILED(hr))
      return hr;
    hr = ValidateResourceHeapCompatibility(pDesc, HeapFlags);
    if (FAILED(hr))
      return hr;
    if (HeapFlags & D3D12_HEAP_FLAG_ALLOW_DISPLAY) {
      D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support = {};
      format_support.Format = pDesc->Format;
      if (FAILED(CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &format_support, sizeof(format_support))) ||
          !(format_support.Support1 & D3D12_FORMAT_SUPPORT1_DISPLAY))
        return E_INVALIDARG;
    }
    hr = ValidateResourceHeapFlags(pDesc, HeapFlags);
    if (FAILED(hr))
      return hr;
    hr = ValidateResourceDescs(pDesc, pHeapProps);
    if (FAILED(hr))
      return hr;
    hr = ValidateResourceStates(InitialState, pHeapProps, pDesc);
    if (FAILED(hr))
      return hr;
    if (!ppResource)
      return S_FALSE;
    switch (pDesc->Dimension) {
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
      return CreateCommittedTexture(
          this, pHeapProps, HeapFlags, pDesc, InitialState, OptimizedClearValue, riid, ppResource
      );
    case D3D12_RESOURCE_DIMENSION_BUFFER:
      return CreateCommittedBuffer(
          this, pHeapProps, HeapFlags, pDesc, InitialState, OptimizedClearValue, riid, ppResource
      );
    default:
      break;
    }
    return E_INVALIDARG;
  };

  HRESULT STDMETHODCALLTYPE
  CreateHeap(const D3D12_HEAP_DESC *pDesc, REFIID riid, void **ppHeap) {
    InitReturnPtr(ppHeap);
    if (!pDesc)
      return E_INVALIDARG;
    HRESULT hr = S_OK;
    hr = ValidateHeapProperties(&pDesc->Properties, pDesc->Flags, advertise_numa_);
    if (FAILED(hr))
      return hr;
    return dxmt::CreateHeap(this, pDesc, riid, ppHeap);
  };

  HRESULT STDMETHODCALLTYPE
  CreatePlacedResource(
      ID3D12Heap *pHeap, UINT64 Offset, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
      const D3D12_CLEAR_VALUE *OptimizedClearValue, REFIID riid, void **ppResource
  ) {
    InitReturnPtr(ppResource);
    if (!pHeap || !pDesc || !IsSameDevice(this, pHeap))
      return E_INVALIDARG;
    auto d3d12heap = static_cast<MTLD3D12Heap *>(pHeap);
    auto heap_desc = d3d12heap->GetDesc();

    if (FAILED(ValidateResourceHeapCompatibility(pDesc, heap_desc.Flags))) {
      ERR("CreatePlacedResource: resource/heap compatibility mismatch");
      return E_INVALIDARG;
    }
    if (FAILED(ValidateResourceHeapFlags(pDesc, heap_desc.Flags))) {
      ERR("CreatePlacedResource: heap/resource type mismatch");
      return E_INVALIDARG;
    }

    D3D12_RESOURCE_ALLOCATION_INFO allocation_info;
    GetResourceAllocationInfo(&allocation_info, 1, 1, pDesc);
    if (allocation_info.SizeInBytes == UINT64_MAX || allocation_info.Alignment == UINT64_MAX) {
      ERR("CreatePlacedResource: invalid allocation info");
      return E_INVALIDARG;
    }
    if ((Offset % allocation_info.Alignment) || Offset > heap_desc.SizeInBytes ||
        allocation_info.SizeInBytes > heap_desc.SizeInBytes - Offset) {
      ERR("CreatePlacedResource: offset outside heap, offset=", Offset, " size=", allocation_info.SizeInBytes,
          " alignment=", allocation_info.Alignment, " heap=", heap_desc.SizeInBytes);
      return E_INVALIDARG;
    }

    HRESULT hr = S_OK;
    hr = ValidateHeapProperties(&heap_desc.Properties, heap_desc.Flags, advertise_numa_);
    if (FAILED(hr)) {
      ERR("CreatePlacedResource: invalid heap properties");
      return hr;
    }
    hr = ValidateResourceDescs(pDesc, &heap_desc.Properties);
    if (FAILED(hr)) {
      ERR("CreatePlacedResource: invalid resource description");
      return hr;
    }
    hr = ValidateResourceStates(InitialState, &heap_desc.Properties, pDesc);
    if (FAILED(hr)) {
      ERR("CreatePlacedResource: invalid initial state");
      return hr;
    }
    if (!ppResource)
      return S_FALSE;
    switch (pDesc->Dimension) {
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
      return CreatePlacedTexture(this, d3d12heap, pDesc, InitialState, Offset, OptimizedClearValue, riid, ppResource);
    case D3D12_RESOURCE_DIMENSION_BUFFER:
      return CreatePlacedBuffer(this, d3d12heap, pDesc, InitialState, Offset, OptimizedClearValue, riid, ppResource);
    default:
      break;
    }
    return E_INVALIDARG;
  };

  HRESULT STDMETHODCALLTYPE
  CreateReservedResource(
      const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
      const D3D12_CLEAR_VALUE *OptimizedClearValue, REFIID riid, void **resource
  ) {
    InitReturnPtr(resource);
    if (!pDesc)
      return E_INVALIDARG;
    if (FAILED(ValidateResourceHeapCompatibility(pDesc, D3D12_HEAP_FLAG_NONE)))
      return E_INVALIDARG;
    switch (pDesc->Dimension) {
    case D3D12_RESOURCE_DIMENSION_BUFFER:
      return CreateReservedBuffer(this, pDesc, InitialState, OptimizedClearValue, riid, resource);
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
      return CreateReservedTexture(this, pDesc, InitialState, OptimizedClearValue, riid, resource);
    default:
      return E_INVALIDARG;
    }
  };

  HRESULT STDMETHODCALLTYPE
  CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC *pDesc, REFIID riid, void **ppPipelineState) {
    return dxmt::CreateD3D12PipelineStateFromStream(this, pDesc, riid, ppPipelineState);
  }

  HRESULT STDMETHODCALLTYPE
  OpenExistingHeapFromAddress(const void *pAddress, REFIID riid, void **ppHeap) {
    InitReturnPtr(ppHeap);
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE
  OpenExistingHeapFromFileMapping(HANDLE hFileMapping, REFIID riid, void **ppHeap) {
    InitReturnPtr(ppHeap);
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE
  EnqueueMakeResident(
      D3D12_RESIDENCY_FLAGS Flags, UINT NumObjects, ID3D12Pageable *const *ppObjects, ID3D12Fence *pFence,
      UINT64 FenceValue
  ) {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE
  CreateCommandList1(
      UINT NodeMask, D3D12_COMMAND_LIST_TYPE Type, D3D12_COMMAND_LIST_FLAGS Flags, REFIID riid, void **ppCommandList
  ) {
    InitReturnPtr(ppCommandList);
    return dxmt::CreateCommandList1(this, NodeMask, Type, Flags, riid, ppCommandList);
  }

  HRESULT STDMETHODCALLTYPE
  CreateProtectedResourceSession(const D3D12_PROTECTED_RESOURCE_SESSION_DESC *pDesc, REFIID riid, void **ppSession) {
    InitReturnPtr(ppSession);
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE
  CreateCommittedResource1(
      const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc,
      D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *OptimizedClearValue,
      ID3D12ProtectedResourceSession *pSession, REFIID riid, void **ppResource
  ) {
    InitReturnPtr(ppResource);
    if (pSession)
      return E_NOTIMPL;
    return CreateCommittedResource(pHeapProps, HeapFlags, pDesc, InitialState, OptimizedClearValue, riid, ppResource);
  }

  HRESULT STDMETHODCALLTYPE
  CreateHeap1(const D3D12_HEAP_DESC *pDesc, ID3D12ProtectedResourceSession *pSession, REFIID riid, void **ppHeap) {
    InitReturnPtr(ppHeap);
    if (pSession)
      return E_NOTIMPL;
    return CreateHeap(pDesc, riid, ppHeap);
  }

  HRESULT STDMETHODCALLTYPE
  CreateReservedResource1(
      const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
      const D3D12_CLEAR_VALUE *OptimizedClearValue, ID3D12ProtectedResourceSession *pSession, REFIID riid,
      void **ppResource
  ) {
    InitReturnPtr(ppResource);
    if (pSession)
      return E_NOTIMPL;
    return CreateReservedResource(pDesc, InitialState, OptimizedClearValue, riid, ppResource);
  }

  D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE
  GetResourceAllocationInfo1(
      D3D12_RESOURCE_ALLOCATION_INFO *__ret, UINT VisibleMask, UINT ResourceDestCount,
      const D3D12_RESOURCE_DESC *pDescs, D3D12_RESOURCE_ALLOCATION_INFO1 *pAllocationInfos
  ) {
    if (!__ret)
      return nullptr;

    constexpr UINT64 default_alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    constexpr UINT64 msaa_alignment = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
    *__ret = {0, default_alignment};
    if (pAllocationInfos) {
      for (UINT i = 0; i < ResourceDestCount; i++)
        pAllocationInfos[i] = {};
    }
    if ((VisibleMask & ~1u) || (ResourceDestCount && !pDescs)) {
      *__ret = {UINT64_MAX, UINT64_MAX};
      return __ret;
    }

    UINT64 total_size = 0;
    UINT64 max_alignment = default_alignment;
    bool has_msaa_resource = false;
    for (UINT i = 0; i < ResourceDestCount; i++) {
      const auto &desc = pDescs[i];
      D3D12_RESOURCE_ALLOCATION_INFO1 resource_info = {};
      WMTSizeAndAlign size_and_align = {};
      UINT64 minimum_alignment = desc.Alignment ? desc.Alignment : default_alignment;
      has_msaa_resource |= desc.SampleDesc.Count > 1;

      switch (desc.Alignment) {
      case 0:
      case D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT:
      case D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT:
      case D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT:
        break;
      default:
        goto invalid;
      }

      if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        if (!IsValidBufferResourceDesc(desc))
          goto invalid;
        resource_info.Alignment = default_alignment;
        size_and_align = GetMTLDevice().heapBufferSizeAndAlign(
            desc.Width, WMTResourceHazardTrackingModeUntracked | WMTResourceStorageModePrivate
        );
      } else {
        if (FAILED(ValidateTextureResourceDesc(desc)) || FAILED(ValidateTextureResourceLayout(desc)) ||
            FAILED(ValidateTextureResourceFlags(desc)))
          goto invalid;
        WMTTextureInfo texture_info = {};
        if (FAILED(PopulateWMTTextureInfo(GetMTLDevice(), texture_info, desc)))
          goto invalid;
        size_and_align = GetMTLDevice().heapTextureSizeAndAlign(texture_info);
        resource_info.Alignment = size_and_align.align;
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.SampleDesc.Count > 1)
          minimum_alignment = std::max<UINT64>(minimum_alignment, msaa_alignment);
      }

      resource_info.Alignment = std::max<UINT64>(resource_info.Alignment, minimum_alignment);
      if (!size_and_align.size || !resource_info.Alignment ||
          size_and_align.size > UINT64_MAX - (resource_info.Alignment - 1) ||
          total_size > UINT64_MAX - (resource_info.Alignment - 1))
        goto invalid;
      resource_info.SizeInBytes = align(size_and_align.size, resource_info.Alignment);
      resource_info.Offset = align(total_size, resource_info.Alignment);
      if (resource_info.Offset < total_size || resource_info.SizeInBytes > UINT64_MAX - resource_info.Offset)
        goto invalid;

      if (pAllocationInfos)
        pAllocationInfos[i] = resource_info;

      total_size = resource_info.Offset + resource_info.SizeInBytes;
      max_alignment = std::max(max_alignment, resource_info.Alignment);
    }

    if (total_size > UINT64_MAX - (max_alignment - 1))
      goto invalid;
    __ret->SizeInBytes = align(total_size, max_alignment);
    __ret->Alignment = max_alignment;
    return __ret;

  invalid:
    *__ret = {UINT64_MAX, has_msaa_resource ? msaa_alignment : default_alignment};
    return __ret;
  }

  HRESULT STDMETHODCALLTYPE
  CreateSharedHandle(
      ID3D12DeviceChild *object, const SECURITY_ATTRIBUTES *attributes, DWORD access, const WCHAR *name, HANDLE *handle
  ) {
    InitReturnPtr(handle);
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE
  OpenSharedHandle(HANDLE handle, REFIID riid, void **object) {
    InitReturnPtr(object);
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE
  OpenSharedHandleByName(const WCHAR *name, DWORD access, HANDLE *handle) {
    InitReturnPtr(handle);
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE
  MakeResident(UINT ObjectCount, ID3D12Pageable *const *objects) {
    if (ObjectCount && !objects)
      return E_INVALIDARG;
    for (UINT i = 0; i < ObjectCount; i++) {
      if (!objects[i])
        return E_INVALIDARG;
    }
    // DXMT keeps created resources in the device residency set.
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE
  Evict(UINT ObjectCount, ID3D12Pageable *const *objects) {
    if (ObjectCount && !objects)
      return E_INVALIDARG;
    for (UINT i = 0; i < ObjectCount; i++) {
      if (!objects[i])
        return E_INVALIDARG;
    }
    // Metal controls residency for these resources; eviction is intentionally a no-op.
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE
  CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence) {
    return dxmt::CreateFence(this, InitialValue, Flags, riid, ppFence);
  };

  HRESULT STDMETHODCALLTYPE
  GetDeviceRemovedReason() {
    return S_OK;
  };

  void STDMETHODCALLTYPE GetCopyableFootprints(
      const D3D12_RESOURCE_DESC *pDesc, UINT FirstSubresource, UINT SubresourceCount, UINT64 BaseOffset,
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows, UINT64 *pRowSizeInBytes, UINT64 *pTotalBytes
  ) {
    UINT64 TotalBytes = 0;
    UINT64 Offset = 0;
    MTL_DXGI_FORMAT_DESC FormatDesc = {};
    UINT64 BlockWidth = 1;
    UINT64 BlockHeight = 1;
    UINT64 PlaneBytesPerTexel[2] = {};
    DXGI_FORMAT PlaneFormats[2] = {};
    UINT PlaneCount = 1;
    UINT64 TotalSubresources = 0;
    UINT mip_levels = 0;
    bool valid = true;

    if (pDesc && pDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      if (IsValidBufferResourceDesc(*pDesc)) {
        PlaneFormats[0] = DXGI_FORMAT_UNKNOWN;
        PlaneBytesPerTexel[0] = 1;
        mip_levels = 1;
        TotalSubresources = 1;
      }
    } else if (pDesc && pDesc->DepthOrArraySize &&
               pDesc->Dimension != D3D12_RESOURCE_DIMENSION_UNKNOWN &&
               SUCCEEDED(ValidateTextureResourceDesc(*pDesc)) &&
               SUCCEEDED(ValidateTextureResourceLayout(*pDesc)) &&
               SUCCEEDED(ValidateTextureResourceFlags(*pDesc))) {
      if (SUCCEEDED(MTLQueryDXGIFormat(GetMTLDevice(), pDesc->Format, FormatDesc)) &&
          pDesc->SampleDesc.Count == 1) {
        mip_levels = pDesc->MipLevels;
        if (!mip_levels) {
          UINT64 max_dimension = pDesc->Width;
          if (pDesc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE1D)
            max_dimension = std::max(max_dimension, UINT64(pDesc->Height));
          if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            max_dimension = std::max(max_dimension, UINT64(pDesc->DepthOrArraySize));
          mip_levels = 1;
          while (max_dimension > 1) {
            max_dimension >>= 1;
            ++mip_levels;
          }
        }
        if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D && pDesc->Height != 1)
          mip_levels = 0;

        if (FormatDesc.Flag & MTL_DXGI_FORMAT_BC) {
          BlockWidth = 4;
          BlockHeight = 4;
        }
        if (FormatDesc.PlanarCount > 1) {
          if (FormatDesc.PlanarCount > 2 ||
              !(FormatDesc.Flag & (MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER))) {
            mip_levels = 0;
          } else {
            PlaneCount = FormatDesc.PlanarCount;
            PlaneFormats[0] = DXGI_FORMAT_R32_TYPELESS;
            PlaneBytesPerTexel[0] = 4;
            PlaneFormats[1] = DXGI_FORMAT_R8_TYPELESS;
            PlaneBytesPerTexel[1] = 1;
          }
        } else {
          PlaneFormats[0] = pDesc->Format;
          PlaneBytesPerTexel[0] =
              FormatDesc.Flag & MTL_DXGI_FORMAT_BC ? FormatDesc.BlockSize : FormatDesc.BytesPerTexel;
          if (!PlaneBytesPerTexel[0])
            mip_levels = 0;
        }
        if (mip_levels) {
          auto array_size = pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : pDesc->DepthOrArraySize;
          TotalSubresources = uint64_t(mip_levels) * array_size * PlaneCount;
        }
      }
    }

    if (PlaneBytesPerTexel[0] && FirstSubresource <= TotalSubresources &&
        SubresourceCount <= TotalSubresources - FirstSubresource) {
      for (UINT i = 0; i < SubresourceCount; i++) {
        auto subresource = uint64_t(FirstSubresource) + i;
        UINT mip_level = 0;
        UINT plane = 0;
        DecomposeSubresource(*pDesc, static_cast<UINT>(subresource), &mip_level, nullptr, &plane);
        if (plane >= PlaneCount) {
          valid = false;
          break;
        }

        auto extent = GetResourceExtent(*pDesc, mip_level);
        auto width = UINT64(extent.right);
        auto height = UINT64(extent.bottom);
        auto aligned_width = align(width, BlockWidth);
        auto aligned_height = align(height, BlockHeight);
        auto depth = UINT64(extent.back);
        auto row_count = aligned_height / BlockHeight;
        if (aligned_width / BlockWidth > UINT64_MAX / PlaneBytesPerTexel[plane]) {
          valid = false;
          break;
        }
        auto row_size = (aligned_width / BlockWidth) * PlaneBytesPerTexel[plane];
        constexpr UINT64 pitch_alignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        if (row_size > UINT64_MAX - (pitch_alignment - 1)) {
          valid = false;
          break;
        }
        auto row_pitch = align(row_size, pitch_alignment);
        if (row_count && row_pitch > UINT64_MAX / row_count) {
          valid = false;
          break;
        }
        auto slice_pitch = row_pitch * row_count;
        if (depth && slice_pitch > UINT64_MAX / depth) {
          valid = false;
          break;
        }
        auto subresource_size = slice_pitch * depth;
        if (row_pitch > UINT_MAX || subresource_size > UINT64_MAX - Offset) {
          valid = false;
          break;
        }

        if (pLayouts) {
          if (BaseOffset > UINT64_MAX - Offset) {
            valid = false;
            break;
          }
          pLayouts[i].Offset = BaseOffset + Offset;
          pLayouts[i].Footprint.Format = PlaneFormats[plane];
          pLayouts[i].Footprint.Width = static_cast<UINT>(width);
          pLayouts[i].Footprint.Height = static_cast<UINT>(height);
          pLayouts[i].Footprint.Depth = static_cast<UINT>(depth);
          pLayouts[i].Footprint.RowPitch = static_cast<UINT>(row_pitch);
        }
        if (pNumRows)
          pNumRows[i] = static_cast<UINT>(row_count);
        if (pRowSizeInBytes)
          pRowSizeInBytes[i] = row_size;

        TotalBytes = Offset + subresource_size;
        if (i + 1 < SubresourceCount) {
          if (TotalBytes > UINT64_MAX - (D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1)) {
            valid = false;
            break;
          }
          Offset = align(TotalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
          if (Offset < TotalBytes) {
            valid = false;
            break;
          }
        }
      }
      if (valid) {
        if (pTotalBytes)
          *pTotalBytes = TotalBytes;
        return;
      }
    }

    for (unsigned i = 0; i < SubresourceCount; i++) {
      if (pLayouts) {
        pLayouts[i].Offset = ~0ull;
        pLayouts[i].Footprint.Format = ~(DXGI_FORMAT)0u;
        pLayouts[i].Footprint.Width = ~0u;
        pLayouts[i].Footprint.Height = ~0u;
        pLayouts[i].Footprint.Depth = ~0u;
        pLayouts[i].Footprint.RowPitch = ~0u;
      }
      if (pNumRows)
        pNumRows[i] = ~0u;
      if (pRowSizeInBytes)
        pRowSizeInBytes[i] = ~0ull;
    }
    if (pTotalBytes)
      *pTotalBytes = UINT64_MAX;
  };

  HRESULT STDMETHODCALLTYPE
  CreateQueryHeap(const D3D12_QUERY_HEAP_DESC *pDesc, REFIID riid, void **ppHeap) {
    return dxmt::CreateQueryHeap(this, pDesc, riid, ppHeap);
  };

  HRESULT STDMETHODCALLTYPE
  SetStablePowerState(WINBOOL Enable) {
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE
  CreateCommandSignature(
      const D3D12_COMMAND_SIGNATURE_DESC *pDesc, ID3D12RootSignature *pRootSignature, REFIID riid,
      void **ppCommandSignature
  ) {
    return dxmt::CreateCommandSignature(this, pDesc, pRootSignature, riid, ppCommandSignature);
  };

  void STDMETHODCALLTYPE GetResourceTiling(
      ID3D12Resource *pResource, UINT *TotalTileCount, D3D12_PACKED_MIP_INFO *PackedMipInfo,
      D3D12_TILE_SHAPE *StandardTileShape, UINT *SubresourceTilingCount, UINT FirstSubresourceTiling,
      D3D12_SUBRESOURCE_TILING *SubresourceTilings
  ) {
    if (!pResource || !IsSameDevice(this, pResource)) {
      if (TotalTileCount)
        *TotalTileCount = 0;
      if (PackedMipInfo)
        *PackedMipInfo = {};
      if (StandardTileShape)
        *StandardTileShape = {};
      if (SubresourceTilingCount)
        *SubresourceTilingCount = 0;
      return;
    }

    static_cast<MTLD3D12Resource *>(pResource)->GetResourceTiling(
        TotalTileCount, PackedMipInfo, StandardTileShape, SubresourceTilingCount, FirstSubresourceTiling,
        SubresourceTilings
    );
  };

  LUID *STDMETHODCALLTYPE
  GetAdapterLuid(LUID *ret) {
    *ret = std::bit_cast<LUID>(__builtin_bswap64(adapter_->GetMTLDevice().registryID()));
    return ret;
  }

  HRESULT STDMETHODCALLTYPE
  CreatePipelineLibrary(const void *blob, SIZE_T blob_size, REFIID iid, void **lib) {
    return dxmt::CreateD3D12PipelineLibrary(this, blob, blob_size, iid, lib);
  };

  HRESULT STDMETHODCALLTYPE
  SetEventOnMultipleFenceCompletion(
      ID3D12Fence *const *pFences, const UINT64 *pValues, UINT FenceCount, D3D12_MULTIPLE_FENCE_WAIT_FLAGS Flags,
      HANDLE hEvent
  ) {
    if (!FenceCount || !pFences || !pValues || !hEvent ||
        (Flags != D3D12_MULTIPLE_FENCE_WAIT_FLAG_NONE && Flags != D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY) ||
        FenceCount > MAXIMUM_WAIT_OBJECTS)
      return E_INVALIDARG;

    const bool wait_any = Flags == D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY;
    bool completed = !wait_any;
    std::vector<Rc<Fence>> fences;
    std::vector<UINT64> values;
    fences.reserve(FenceCount);
    values.reserve(FenceCount);

    for (UINT i = 0; i < FenceCount; i++) {
      if (!IsSameDevice(this, pFences[i]))
        return E_INVALIDARG;
      auto fence = static_cast<MTLD3D12Fence *>(pFences[i]);
      if (!fence->fence)
        return E_INVALIDARG;

      const bool fence_completed = fence->fence->completedValue() >= pValues[i];
      if (wait_any)
        completed |= fence_completed;
      else
        completed &= fence_completed;

      fences.emplace_back(fence->fence);
      values.push_back(pValues[i]);
    }

    if (completed) {
      SetEvent(hEvent);
      return S_OK;
    }

    try {
      std::thread([fences = std::move(fences), values = std::move(values), wait_any, hEvent]() mutable {
        for (;;) {
          bool complete = !wait_any;
          for (size_t i = 0; i < fences.size(); i++) {
            const bool fence_completed = fences[i]->completedValue() >= values[i];
            if (wait_any)
              complete |= fence_completed;
            else
              complete &= fence_completed;
          }
          if (complete) {
            SetEvent(hEvent);
            return;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }).detach();
    } catch (...) {
      return E_OUTOFMEMORY;
    }
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE
  SetResidencyPriority(UINT ObjectCount, ID3D12Pageable *const *pObjects, const D3D12_RESIDENCY_PRIORITY *pPriorities) {
    if (ObjectCount && (!pObjects || !pPriorities))
      return E_INVALIDARG;
    for (UINT i = 0; i < ObjectCount; i++) {
      if (!pObjects[i])
        return E_INVALIDARG;
    }
    // Metal residency sets do not expose per-resource priorities.
    return S_OK;
  };

  WMT::ResidencySet
  GetGlobalResidencySet() {
    return residency_set_;
  };

  HRESULT
  RegisterResidency(WMT::Allocation allocation) {
    std::unique_lock<dxmt::mutex> lock(residency_lock_);
    residency_set_.addAllocations(&allocation, 1);
    residency_set_.commit();
    return S_OK;
  }

  HRESULT
  UnregisterResidency(WMT::Allocation allocation) {
    std::unique_lock<dxmt::mutex> lock(residency_lock_);
    residency_set_.removeAllocations(&allocation, 1);
    residency_set_.commit();
    return S_OK;
  }

  HRESULT
  RegisterResidencyAndVA(BufferAllocation *allocation) {
    std::unique_lock<dxmt::mutex> lock(residency_lock_);
    interval_map_.emplace(allocation->gpuAddress(), allocation);
    auto buffer = allocation->buffer();
    residency_set_.addAllocations(&buffer, 1);
    residency_set_.commit();
    return S_OK;
  }

  HRESULT
  UnregisterResidencyAndVA(BufferAllocation *allocation) {
    std::unique_lock<dxmt::mutex> lock(residency_lock_);
    interval_map_.erase(allocation->gpuAddress());
    auto buffer = allocation->buffer();
    residency_set_.removeAllocations(&buffer, 1);
    residency_set_.commit();
    return S_OK;
  }

  BufferAllocation *
  LookupBufferByVA(D3D12_GPU_VIRTUAL_ADDRESS VA, uint64_t *pOffset) {
    if (!pOffset)
      return {};
    std::unique_lock<dxmt::mutex> lock(residency_lock_);
    auto iter = interval_map_.upper_bound(VA);
    if (iter == interval_map_.begin()) {
      *pOffset = 0;
      return {};
    }
    --iter;
    auto *allocation = iter->second;
    auto offset = VA - iter->first;
    if (offset >= allocation->length()) {
      *pOffset = 0;
      return {};
    }
    *pOffset = offset;
    return allocation;
  }

  InternalCommandLibrary &
  GetLib() {
    return command_library;
  }

  FormatCapability
  GetMTLPixelFormatCapability(WMTPixelFormat Format) final {
    Format = ORIGINAL_FORMAT(Format);
    if (!format_capabilities_.textureCapabilities.contains(Format))
      return FormatCapability(0);
    return format_capabilities_.textureCapabilities.at(Format);
  }

  HRESULT STDMETHODCALLTYPE
  CreateLifetimeTracker(ID3D12LifetimeOwner *pOwner, REFIID riid, void **ppvTracker) {
    InitReturnPtr(ppvTracker);
    return E_NOTIMPL;
  }

  void STDMETHODCALLTYPE RemoveDevice() {}

  HRESULT STDMETHODCALLTYPE
  EnumerateMetaCommands(UINT *pNumMetaCommands, D3D12_META_COMMAND_DESC *pDescs) {
    if (pNumMetaCommands)
      *pNumMetaCommands = 0;
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE
  EnumerateMetaCommandParameters(
      REFGUID CommandId, D3D12_META_COMMAND_PARAMETER_STAGE Stage, UINT *pTotalStructureSizeInBytes,
      UINT *pParameterCount, D3D12_META_COMMAND_PARAMETER_DESC *pParameterDescs
  ) {
    if (pTotalStructureSizeInBytes)
      *pTotalStructureSizeInBytes = 0;
    if (pParameterCount)
      *pParameterCount = 0;
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE
  CreateMetaCommand(
      REFGUID CommandId, UINT NodeMask, const void *pCreationParametersData, SIZE_T CreationParametersDataSizeInBytes,
      REFIID riid, void **ppMetaCommand
  ) {
    InitReturnPtr(ppMetaCommand);
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE
  CreateStateObject(const D3D12_STATE_OBJECT_DESC *pDesc, REFIID riid, void **ppStateObject) {
    InitReturnPtr(ppStateObject);
    return E_NOTIMPL;
  }

  void STDMETHODCALLTYPE GetRaytracingAccelerationStructurePrebuildInfo(
      const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *pDesc,
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *pInfo
  ) {
    if (pInfo)
      *pInfo = {};
  }

  D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS STDMETHODCALLTYPE CheckDriverMatchingIdentifier(
      D3D12_SERIALIZED_DATA_TYPE SerializedDataType,
      const D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER *pIdentifierToCheck
  ) {
    return D3D12_DRIVER_MATCHING_IDENTIFIER_UNSUPPORTED_TYPE;
  }

  HRESULT STDMETHODCALLTYPE SetBackgroundProcessingMode(
      D3D12_BACKGROUND_PROCESSING_MODE Mode, D3D12_MEASUREMENTS_ACTION Action, HANDLE hEvent,
      WINBOOL *pbFurtherMeasurementsDesired
  ) {
    if (pbFurtherMeasurementsDesired)
      *pbFurtherMeasurementsDesired = FALSE;
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE AddToStateObject(
      const D3D12_STATE_OBJECT_DESC *pAddition, ID3D12StateObject *pStateObjectToGrowFrom, REFIID riid,
      void **ppNewStateObject
  ) {
    InitReturnPtr(ppNewStateObject);
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE CreateProtectedResourceSession1(
      const D3D12_PROTECTED_RESOURCE_SESSION_DESC1 *pDesc, REFIID riid, void **ppSession
  ) {
    InitReturnPtr(ppSession);
    return E_NOTIMPL;
  }

  D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE
  GetResourceAllocationInfo2(
      D3D12_RESOURCE_ALLOCATION_INFO *__ret, UINT VisibleMask, UINT ResourceDescCount,
      const D3D12_RESOURCE_DESC1 *pDescs, D3D12_RESOURCE_ALLOCATION_INFO1 *pAllocationInfos
  ) {
    if (!__ret)
      return nullptr;
    if (pAllocationInfos) {
      for (UINT i = 0; i < ResourceDescCount; i++)
        pAllocationInfos[i] = {};
    }
    if (ResourceDescCount && !pDescs) {
      *__ret = {UINT64_MAX, UINT64_MAX};
      return __ret;
    }

    *__ret = {UINT64_MAX, UINT64_MAX};
    try {
      std::vector<D3D12_RESOURCE_DESC> resource_descs(ResourceDescCount);
      for (UINT i = 0; i < ResourceDescCount; i++) {
        if (!ConvertResourceDesc1(&pDescs[i], &resource_descs[i])) {
          return __ret;
        }
      }
      std::vector<D3D12_RESOURCE_ALLOCATION_INFO1> allocation_infos;
      if (pAllocationInfos)
        allocation_infos.resize(ResourceDescCount);
      auto *allocation_info_data = pAllocationInfos ? allocation_infos.data() : nullptr;
      auto *result = GetResourceAllocationInfo1(
          __ret, VisibleMask, ResourceDescCount, resource_descs.data(), allocation_info_data
      );
      if (pAllocationInfos && result->SizeInBytes != UINT64_MAX) {
        for (UINT i = 0; i < ResourceDescCount; i++)
          pAllocationInfos[i] = allocation_infos[i];
      }
      return result;
    } catch (...) {
      return __ret;
    }
  }

  HRESULT STDMETHODCALLTYPE
  CreateCommittedResource2(
      const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC1 *pDesc,
      D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE *pOptimizedClearValue,
      ID3D12ProtectedResourceSession *pProtectedSession, REFIID riidResource, void **ppvResource
  ) {
    InitReturnPtr(ppvResource);
    if (pProtectedSession)
      return E_NOTIMPL;
    if (!pDesc)
      return CreateCommittedResource(
          pHeapProperties, HeapFlags, nullptr, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource
      );

    D3D12_RESOURCE_DESC resource_desc = {};
    if (!ConvertResourceDesc1(pDesc, &resource_desc))
      return E_NOTIMPL;
    return CreateCommittedResource(
        pHeapProperties, HeapFlags, &resource_desc, InitialResourceState, pOptimizedClearValue, riidResource,
        ppvResource
    );
  }

  HRESULT STDMETHODCALLTYPE
  CreatePlacedResource1(
      ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC1 *pDesc, D3D12_RESOURCE_STATES InitialState,
      const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid, void **ppvResource
  ) {
    InitReturnPtr(ppvResource);
    if (!pDesc)
      return CreatePlacedResource(pHeap, HeapOffset, nullptr, InitialState, pOptimizedClearValue, riid, ppvResource);

    D3D12_RESOURCE_DESC resource_desc = {};
    if (!ConvertResourceDesc1(pDesc, &resource_desc))
      return E_NOTIMPL;
    return CreatePlacedResource(
        pHeap, HeapOffset, &resource_desc, InitialState, pOptimizedClearValue, riid, ppvResource
    );
  }

  void STDMETHODCALLTYPE CreateSamplerFeedbackUnorderedAccessView(
      ID3D12Resource *pTargetedResource, ID3D12Resource *pFeedbackResource,
      D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor
  ) {}

  void STDMETHODCALLTYPE
  GetCopyableFootprints1(
      const D3D12_RESOURCE_DESC1 *pResourceDesc, UINT FirstSubresource, UINT NumSubresources, UINT64 BaseOffset,
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows, UINT64 *pRowSizeInBytes, UINT64 *pTotalBytes
  ) {
    D3D12_RESOURCE_DESC resource_desc = {};
    if (!ConvertResourceDesc1(pResourceDesc, &resource_desc)) {
      GetCopyableFootprints(
          nullptr, FirstSubresource, NumSubresources, BaseOffset, pLayouts, pNumRows, pRowSizeInBytes, pTotalBytes
      );
      return;
    }
    GetCopyableFootprints(
        &resource_desc, FirstSubresource, NumSubresources, BaseOffset, pLayouts, pNumRows, pRowSizeInBytes, pTotalBytes
    );
  }

  HRESULT STDMETHODCALLTYPE CreateShaderCacheSession(
      const D3D12_SHADER_CACHE_SESSION_DESC *pDesc, REFIID riid, void **ppSession
  ) {
    InitReturnPtr(ppSession);
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE ShaderCacheControl(
      D3D12_SHADER_CACHE_KIND_FLAGS Kinds, D3D12_SHADER_CACHE_CONTROL_FLAGS Control
  ) {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE CreateCommandQueue1(
      const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID CreatorID, REFIID riid, void **ppCommandQueue
  ) {
    InitReturnPtr(ppCommandQueue);
    if (!pDesc)
      return E_INVALIDARG;
    return dxmt::CreateCommandQueue(this, pDesc, riid, ppCommandQueue);
  }

  HRESULT STDMETHODCALLTYPE
  CreateCommittedResource3(
      const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC1 *pDesc,
      D3D12_BARRIER_LAYOUT InitialLayout, const D3D12_CLEAR_VALUE *pOptimizedClearValue,
      ID3D12ProtectedResourceSession *pProtectedSession, UINT32 CastableFormatsCount, DXGI_FORMAT *pCastableFormats,
      REFIID riidResource, void **ppvResource
  ) {
    InitReturnPtr(ppvResource);
    if (pProtectedSession || CastableFormatsCount)
      return E_NOTIMPL;
    if (!pDesc)
      return E_INVALIDARG;

    D3D12_RESOURCE_DESC resource_desc = {};
    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
    if (!ConvertResourceDesc1WithLayout(pDesc, &resource_desc, InitialLayout, &initial_state))
      return E_NOTIMPL;
    return CreateCommittedResource(
        pHeapProperties, HeapFlags, &resource_desc, initial_state, pOptimizedClearValue, riidResource, ppvResource
    );
  }

  HRESULT STDMETHODCALLTYPE
  CreatePlacedResource2(
      ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC1 *pDesc, D3D12_BARRIER_LAYOUT InitialLayout,
      const D3D12_CLEAR_VALUE *pOptimizedClearValue, UINT32 CastableFormatsCount, DXGI_FORMAT *pCastableFormats,
      REFIID riid, void **ppvResource
  ) {
    InitReturnPtr(ppvResource);
    if (CastableFormatsCount)
      return E_NOTIMPL;
    if (!pDesc)
      return E_INVALIDARG;

    D3D12_RESOURCE_DESC resource_desc = {};
    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
    if (!ConvertResourceDesc1WithLayout(pDesc, &resource_desc, InitialLayout, &initial_state))
      return E_NOTIMPL;
    return CreatePlacedResource(
        pHeap, HeapOffset, &resource_desc, initial_state, pOptimizedClearValue, riid, ppvResource
    );
  }

  HRESULT STDMETHODCALLTYPE CreateReservedResource2(
      const D3D12_RESOURCE_DESC *pDesc, D3D12_BARRIER_LAYOUT InitialLayout,
      const D3D12_CLEAR_VALUE *pOptimizedClearValue, ID3D12ProtectedResourceSession *pProtectedSession,
      UINT32 CastableFormatsCount, DXGI_FORMAT *pCastableFormats, REFIID riid, void **ppvResource
  ) {
    InitReturnPtr(ppvResource);
    if (pProtectedSession || CastableFormatsCount)
      return E_NOTIMPL;
    if (!pDesc)
      return E_INVALIDARG;

    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
    if (!ConvertBarrierLayout(pDesc->Dimension, InitialLayout, &initial_state))
      return E_NOTIMPL;
    return CreateReservedResource(
        pDesc, initial_state, pOptimizedClearValue, riid, ppvResource
    );
  }
};

bool
IsSameDevice(MTLD3D12Device *device, ID3D12DeviceChild *child) {
  if (!device || !child)
    return false;

  IUnknown *child_identity = nullptr;
  if (FAILED(child->GetDevice(__uuidof(IUnknown), reinterpret_cast<void **>(&child_identity))))
    return false;
  Com<IUnknown> child_identity_ref = Com<IUnknown>::transfer(child_identity);

  IUnknown *device_identity = nullptr;
  if (FAILED(device->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&device_identity))))
    return false;
  Com<IUnknown> device_identity_ref = Com<IUnknown>::transfer(device_identity);
  return child_identity_ref.ptr() == device_identity_ref.ptr();
}

HRESULT
CreateD3D12Device(IMTLDXGIAdapter *adapter, D3D_FEATURE_LEVEL feature_level, const IID &riid, void **ppDevice) {
  auto device = Com(new MTLD3D12DeviceImpl(adapter, feature_level));
  HRESULT hr = device->Initialize();
  if (FAILED(hr))
    return hr;
  return device->QueryInterface(riid, ppDevice);
};

} // namespace dxmt

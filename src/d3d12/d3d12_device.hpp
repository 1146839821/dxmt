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

#pragma once
#include "d3d12.h"
#include "d3d12_command_encoder.hpp"
#include "d3d12_descriptor_heap.hpp"
#include "dxgi1_2.h"
#include "dxgi_interfaces.h"
#include "airconv_public.h"
#include "dxmt_buffer.hpp"
#include "dxmt_command.hpp"
#include "dxmt_format.hpp"
#include "dxmt_fence.hpp"
#include "dxmt_presenter.hpp"
#include "d3d12_shader_converter.hpp"
#include "dxmt_texture.hpp"
#include "log/log.hpp"
#include <vector>

#define IMPLEMENT_ME                                                                                                   \
  do {                                                                                                                 \
    WARN(__FILE__, ":", __FUNCTION__, "(", __LINE__, ") is not implemented.");                                      \
  } while (0);

namespace dxmt {

class MTLD3D12Resource;

class MTLD3D12GraphicsCommandList : public ID3D12GraphicsCommandList {
public:
  EncoderData *entry;
  size_t encoder_count;

  virtual void CommitResourceStates() = 0;
};

class MTLD3D12CommandAllocator : public ID3D12CommandAllocator {
public:
  virtual D3D12_COMMAND_LIST_TYPE GetType() const = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateCommandList(
      UINT NodeMask, D3D12_COMMAND_LIST_TYPE Type, ID3D12PipelineState *pInitialPipelineState, REFIID riid,
      void **ppCommandList
  ) = 0;
};

class MTLD3D12CommandQueue : public ID3D12CommandQueue {
public:
  virtual HRESULT Present(Presenter *presenter, ID3D12Resource *backbuffer, HANDLE hLantecyWaitable) = 0;
};

class MTLD3D12Resource : public ID3D12Resource {
public:
  Rc<Texture> texture;
  Rc<Buffer> buffer;
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  std::vector<D3D12_RESOURCE_STATES> subresource_states;

  void
  InitializeStateTracking(const D3D12_RESOURCE_DESC &desc, WMT::Device device) {
    UINT mip_levels = std::max<UINT>(1, desc.MipLevels);
    UINT array_size = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : std::max<UINT>(1, desc.DepthOrArraySize);
    UINT plane_count = 1;
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
      MTL_DXGI_FORMAT_DESC format_desc;
      if (SUCCEEDED(MTLQueryDXGIFormat(device, desc.Format, format_desc)))
        plane_count = std::max<UINT>(1, format_desc.PlanarCount);
    }
    subresource_states.assign(size_t(mip_levels) * array_size * plane_count, state);
  }

  bool
  HasSubresource(UINT subresource) const {
    return subresource < subresource_states.size();
  }

  D3D12_RESOURCE_STATES
  GetSubresourceState(UINT subresource) const {
    return HasSubresource(subresource) ? subresource_states[subresource] : state;
  }

  void
  SetSubresourceState(UINT subresource, D3D12_RESOURCE_STATES new_state) {
    if (HasSubresource(subresource)) {
      subresource_states[subresource] = new_state;
      return;
    }
    state = new_state;
  }

  void
  SetAllSubresourceStates(D3D12_RESOURCE_STATES new_state) {
    std::fill(subresource_states.begin(), subresource_states.end(), new_state);
    state = new_state;
  }

  virtual HRESULT STDMETHODCALLTYPE
  CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(
      ID3D12Resource *pCounter, const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor
  ) = 0;

  virtual HRESULT STDMETHODCALLTYPE
  CreateRenderTargetView(const D3D12_RENDER_TARGET_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) = 0;

  virtual HRESULT STDMETHODCALLTYPE
  CreateDepthStencilView(const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor) = 0;

  virtual void STDMETHODCALLTYPE GetResourceTiling(
      UINT *TotalTileCount, D3D12_PACKED_MIP_INFO *PackedMipInfo, D3D12_TILE_SHAPE *StandardTileShape,
      UINT *SubresourceTilingCount, UINT FirstSubresourceTiling, D3D12_SUBRESOURCE_TILING *SubresourceTilings
  ) = 0;
};

class MTLD3D12Heap : public ID3D12Heap {
public:
  virtual WMT::Heap GetMetalHeap() = 0;
};

class MTLD3D12Fence : public ID3D12Fence1 {
public:
  Rc<Fence> fence;
};

class MTLD3D12RootSignature : public ID3D12RootSignature {
public:
  virtual UINT GetBlob(const void **ppBlob) = 0;
  virtual HRESULT InitializeMSCLayout() = 0;

  virtual void AddRefPrivate() = 0;
  virtual void ReleasePrivate() = 0;

  uint32_t UploadQwords;
  uint32_t ParameterSlots;
  uint32_t const *SlotQwordOffsets;

  size_t NumStaticSamplers;
  uint64_t const *EncodedStaticSamplers;

  uint64_t MSCArgumentBufferSize = 0;
  uint32_t MSCParameterCount = 0;
  const dxmt_msc_root_parameter_layout *MSCParameterLayouts = nullptr;
};

class MTLD3D12CommandSignature : public ID3D12CommandSignature {
public:
  D3D12_INDIRECT_ARGUMENT_TYPE CommandType;
  UINT UpdateRootArguments : 1;
  UINT UpdateVertexBuffers : 1;
  UINT UpdateIndexBuffer   : 1;

  WMT::Reference<WMT::RenderPipelineState> render_resolver;
  WMT::Reference<WMT::ComputePipelineState> compute_resolver;

  virtual void AddRefPrivate() = 0;
  virtual void ReleasePrivate() = 0;
};

class MTLD3D12QueryHeap : public ID3D12QueryHeap {
public:
  WMT::Reference<WMT::Buffer> visibility_buffer;
  WMT::Reference<WMT::CounterSampleBuffer> timestamp_buffer;
  D3D12_QUERY_HEAP_TYPE type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  UINT count = 0;
};

class MTLD3D12PipelineState : public ID3D12PipelineState {
public:
  UINT IsComputePipelineState;
  D3D12ShaderBackend shader_backend = D3D12ShaderBackend::Airconv;
};

class MTLD3D12GraphicsPipelineState : public MTLD3D12PipelineState {
public:
  WMT::Reference<WMT::RenderPipelineState> pso;
  WMT::Reference<WMT::DepthStencilState> dsso;
  WMT::Reference<WMT::DepthStencilState> dsso_stencil_disabled;
  WMT::Reference<WMT::DepthStencilState> dsso_depth_disabled;
  WMT::Reference<WMT::DepthStencilState> dsso_depth_stencil_disabled;
  uint32_t slot_mask = 0;
  enum WMTTriangleFillMode fill_mode;
  enum WMTCullMode cull_mode;
  enum WMTDepthClipMode depth_clip_mode;
  enum WMTWinding winding;
  float depth_bias;
  float scole_scale;
  float depth_bias_clamp;
  uint32_t forced_sample_count;

  virtual void AddRefPrivate() = 0;
  virtual void ReleasePrivate() = 0;
};

class MTLD3D12ComputePipelineState : public MTLD3D12PipelineState {
public:
  WMT::Reference<WMT::ComputePipelineState> pso;
  WMTSize threadgroup_size;

  virtual void AddRefPrivate() = 0;
  virtual void ReleasePrivate() = 0;
};

class MTLD3D12Device : public ID3D12Device1 {
public:
  virtual WMT::Device GetMTLDevice() = 0;

  virtual D3D_FEATURE_LEVEL GetFeatureLevel() = 0;

  virtual WMT::ResidencySet GetGlobalResidencySet() = 0;

  virtual HRESULT RegisterResidency(WMT::Allocation allocation) = 0;

  virtual HRESULT UnregisterResidency(WMT::Allocation allocation) = 0;

  virtual HRESULT RegisterResidencyAndVA(BufferAllocation *allocation) = 0;

  virtual HRESULT UnregisterResidencyAndVA(BufferAllocation *allocation) = 0;

  virtual BufferAllocation *LookupBufferByVA(D3D12_GPU_VIRTUAL_ADDRESS VA, uint64_t *pOffset) = 0;

  virtual InternalCommandLibrary& GetLib() = 0;

  virtual FormatCapability GetMTLPixelFormatCapability(WMTPixelFormat Format) = 0;

  EventListener event_listener;
};

HRESULT CreateD3D12Device(IMTLDXGIAdapter *adapter, REFIID riid, void **ppDevice);

HRESULT
CreateCommandQueue(MTLD3D12Device *pDevice, const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue);

HRESULT
CreateCommandAllocator(MTLD3D12Device *pDevice, D3D12_COMMAND_LIST_TYPE Type, REFIID riid, void **ppCommandAllocator);

HRESULT
CreateDescriptorHeap(
    MTLD3D12Device *pDevice, const D3D12_DESCRIPTOR_HEAP_DESC *pDesc, REFIID riid, void **ppDescriptorHeap
);

HRESULT
CreateQueryHeap(MTLD3D12Device *pDevice, const D3D12_QUERY_HEAP_DESC *pDesc, REFIID riid, void **ppQueryHeap);

HRESULT CreateCommittedTexture(
    MTLD3D12Device *pDevice, const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS HeapFlags,
    const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *OptimizedClearValue,
    REFIID riid, void **ppResource
);

HRESULT
CreatePlacedTexture(
    MTLD3D12Device *pDevice, MTLD3D12Heap *pHeap, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
    UINT64 HeapOffset, const D3D12_CLEAR_VALUE *OptimizedClearValue, REFIID riid, void **ppResource
);

HRESULT CreateCommittedBuffer(
    MTLD3D12Device *pDevice, const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS HeapFlags,
    const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *OptimizedClearValue,
    REFIID riid, void **ppResource
);

HRESULT
CreatePlacedBuffer(
    MTLD3D12Device *pDevice, MTLD3D12Heap *pHeap, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
    UINT64 HeapOffset, const D3D12_CLEAR_VALUE *OptimizedClearValue, REFIID riid, void **ppResource
);

HRESULT
CreateHeap(MTLD3D12Device *pDevice, const D3D12_HEAP_DESC *pDesc, REFIID riid, void **ppHeap);

HRESULT
CreateRootSignature(
    MTLD3D12Device *pDevice, UINT NodeMask, const void *pBytecode, SIZE_T BytecodeLength, REFIID riid,
    void **ppRootSignature
);

HRESULT
CreateCommandSignature(
    MTLD3D12Device *pDevice, const D3D12_COMMAND_SIGNATURE_DESC *pDesc, ID3D12RootSignature *pRootSignature,
    REFIID riid, void **ppCommandSignature
);

HRESULT
CreateGraphicsPipelineState(
    MTLD3D12Device *pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState
);

HRESULT
CreateComputePipelineState(
    MTLD3D12Device *pDevice, const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState
);

HRESULT
CreateSwapChain(
    IDXGIFactory1 *pFactory, MTLD3D12Device *pDevice, MTLD3D12CommandQueue *pQueue, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
    IDXGISwapChain1 **ppSwapChain
);

HRESULT
CreateFence(MTLD3D12Device *pDevice, UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence);

void PopulateWMTSamplerInfo(WMT::Device Device, WMTSamplerInfo &InfoOut, D3D12_STATIC_SAMPLER_DESC const &Desc);

void PopulateWMTSamplerInfo(WMT::Device Device, WMTSamplerInfo &InfoOut, D3D12_SAMPLER_DESC const &Desc);

HRESULT PopulateWMTTextureInfo(WMT::Device Device, WMTTextureInfo &InfoOut, const D3D12_RESOURCE_DESC &Desc);

inline std::tuple<MTLD3D12RenderTargetDescriptorHeap *, UINT>
GetRenderTargetHeap(MTLD3D12Device *pDevice, D3D12_CPU_DESCRIPTOR_HANDLE Handle) {
  EMBEDDED_DESCRIPTOR_HANDLE impl(Handle);
  auto *heap = impl.extract<MTLD3D12RenderTargetDescriptorHeap>();
  if (!heap)
    return {nullptr, 0};

  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  heap->GetDesc(&desc);
  if ((desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV && desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV) ||
      impl.Descriptor >= desc.NumDescriptors)
    return {nullptr, 0};
  return {heap, (UINT)impl.Descriptor};
}

inline D3D12_CPU_DESCRIPTOR_HANDLE
GetRenderTargetDescriptor(MTLD3D12RenderTargetDescriptorHeap *pHeap, UINT Index) {
  return EMBEDDED_DESCRIPTOR_HANDLE(pHeap, Index);
}

inline std::tuple<MTLD3D12DescriptorHeap *, UINT>
GetShaderVisibleDescriptorHeap(MTLD3D12Device *pDevice, D3D12_CPU_DESCRIPTOR_HANDLE Handle) {
  EMBEDDED_DESCRIPTOR_HANDLE impl(Handle);
  auto *heap = impl.extract<MTLD3D12DescriptorHeap>();
  if (!heap)
    return {nullptr, 0};

  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  heap->GetDesc(&desc);
  if (desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || impl.Descriptor >= desc.NumDescriptors)
    return {nullptr, 0};
  return {heap, (UINT)impl.Descriptor};
}

inline D3D12_CPU_DESCRIPTOR_HANDLE
GetShaderVisibleDescriptor(MTLD3D12DescriptorHeap *pHeap, UINT Index) {
  return EMBEDDED_DESCRIPTOR_HANDLE(pHeap, Index);
}

inline std::tuple<MTLD3D12SamplerDescriptorHeap *, UINT>
GetSamplerDescriptorHeap(MTLD3D12Device *pDevice, D3D12_CPU_DESCRIPTOR_HANDLE Handle) {
  EMBEDDED_DESCRIPTOR_HANDLE impl(Handle);
  auto *heap = impl.extract<MTLD3D12SamplerDescriptorHeap>();
  if (!heap)
    return {nullptr, 0};

  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  heap->GetDesc(&desc);
  if (desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER || impl.Descriptor >= desc.NumDescriptors)
    return {nullptr, 0};
  return {heap, (UINT)impl.Descriptor};
}

inline D3D12_CPU_DESCRIPTOR_HANDLE
GetSamplerDescriptor(MTLD3D12SamplerDescriptorHeap *pHeap, UINT Index) {
  return EMBEDDED_DESCRIPTOR_HANDLE(pHeap, Index);
}

template <typename VIEW_DESC>
HRESULT ExtractEntireResourceViewDescription(const D3D12_RESOURCE_DESC &ResourceDesc, VIEW_DESC *pViewDescOut);

constexpr auto kDefaultShader4Component = 0b1'011'010'001'000;

HRESULT ValidateResourceStates(D3D12_RESOURCE_STATES State, const D3D12_HEAP_PROPERTIES *pHeapProps);

HRESULT ValidateResourceDescs(const D3D12_RESOURCE_DESC *pDesc, const D3D12_HEAP_PROPERTIES *pHeapProps);

HRESULT ValidateHeapProperties(const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS Flags, bool AdapterIsNUMA);

} // namespace dxmt

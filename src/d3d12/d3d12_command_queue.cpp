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

#include "com/com_guid.hpp"
#include "com/com_pointer.hpp"
#include "d3d12_device.hpp"
#include "d3d12_pageable.hpp"
#include "dxgi_interfaces.h"
#include "log/log.hpp"

namespace dxmt {

constexpr auto kCommandQueueSize = 32u;

class MTLD3D12CommandQueueImpl : public MTLD3D12Pageable<MTLD3D12CommandQueue, IMTLSwapChainFactory> {

  D3D12_COMMAND_QUEUE_DESC desc_;

  WMT::Reference<WMT::CommandQueue> queue_;
  WMT::Reference<WMT::Fence> fence_;
  WMT::Reference<WMT::Buffer> timestamp_dummy_buffer_;

public:
  MTLD3D12CommandQueueImpl(MTLD3D12Device *pDevice) :
      MTLD3D12Pageable<MTLD3D12CommandQueue, IMTLSwapChainFactory>(pDevice) {}

  HRESULT
  Initialize(const D3D12_COMMAND_QUEUE_DESC *pDesc) {
    // TODO: validate and normalize
    desc_ = *pDesc;
    desc_.NodeMask = 1; // typically 1 GPU only

    auto metal_device = device_->GetMTLDevice();
    queue_ = metal_device.newCommandQueue(kCommandQueueSize);
    if (!queue_)
      return E_FAIL;
    queue_.addResidencySet(device_->GetGlobalResidencySet());

    fence_ = metal_device.newFence();

    WMTBufferInfo timestamp_dummy_info = {};
    timestamp_dummy_info.length = sizeof(uint32_t);
    timestamp_dummy_info.options = WMTResourceHazardTrackingModeUntracked;
    timestamp_dummy_info.memory.set(nullptr);
    timestamp_dummy_buffer_ = metal_device.newBuffer(timestamp_dummy_info);
    if (!timestamp_dummy_buffer_)
      return E_OUTOFMEMORY;

    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12CommandQueue)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(IMTLSwapChainFactory)) {
      *ppvObject = ref_and_cast<IMTLSwapChainFactory>(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12CommandQueue), riid)) {
      WARN("D3D12CommandQueue: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  void STDMETHODCALLTYPE UpdateTileMappings(
      ID3D12Resource *resource, UINT region_count, const D3D12_TILED_RESOURCE_COORDINATE *region_start_coordinates,
      const D3D12_TILE_REGION_SIZE *region_sizes, ID3D12Heap *heap, UINT range_count,
      const D3D12_TILE_RANGE_FLAGS *range_flags, const UINT *heap_range_offsets, const UINT *range_tile_counts,
      D3D12_TILE_MAPPING_FLAGS flags
  ) {
    IMPLEMENT_ME
  };

  void STDMETHODCALLTYPE CopyTileMappings(
      ID3D12Resource *dst_resource, const D3D12_TILED_RESOURCE_COORDINATE *dst_region_start_coordinate,
      ID3D12Resource *src_resource, const D3D12_TILED_RESOURCE_COORDINATE *src_region_start_coordinate,
      const D3D12_TILE_REGION_SIZE *region_size, D3D12_TILE_MAPPING_FLAGS flags
  ) {
    IMPLEMENT_ME
  };

  void STDMETHODCALLTYPE
  ExecuteCommandLists(UINT Count, ID3D12CommandList *const *ppCommandLists) {
    auto pool = WMT::MakeAutoreleasePool();

    auto cmdbuf = queue_.commandBuffer();
    for (unsigned i = 0; i < Count; i++) {
      auto pCommandList = static_cast<MTLD3D12GraphicsCommandList *>(ppCommandLists[i]);
      EncoderData *current = pCommandList->entry;
      while (current) {
        switch (current->type) {
        case EncoderType::Null:
          break;
        case EncoderType::Clear: {
          auto data = static_cast<ClearEncoderData *>(current);
          {
            WMTRenderPassInfo info;
            WMT::InitializeRenderPassInfo(info);
            if (data->clear_dsv) {
              if (data->clear_dsv & 1) {
                info.depth.clear_depth = data->depth_stencil.first;
                info.depth.texture = data->attachment.texture();
                info.depth.load_action = WMTLoadActionClear;
                info.depth.store_action = WMTStoreActionStore;
              }
              if (data->clear_dsv & 2) {
                info.stencil.clear_stencil = data->depth_stencil.second;
                info.stencil.texture = data->attachment.texture();
                info.stencil.load_action = WMTLoadActionClear;
                info.stencil.store_action = WMTStoreActionStore;
              }
              info.render_target_width = data->width;
              info.render_target_height = data->height;
            } else {
              info.colors[0].clear_color = data->color;
              info.colors[0].texture = data->attachment.texture();
              info.colors[0].load_action = WMTLoadActionClear;
              info.colors[0].store_action = WMTStoreActionStore;
            }
            info.render_target_array_length = data->array_length;
            auto encoder = cmdbuf.renderCommandEncoder(info);
            encoder.setLabel(WMT::String::string("ClearPass", WMTUTF8StringEncoding));
            encoder.waitForFence(fence_, WMTRenderStageFragment);
            encoder.updateFence(fence_, WMTRenderStageFragment);
            encoder.endEncoding();
          }
          break;
        }
        case EncoderType::Render: {
          auto data = static_cast<RenderEncoderData *>(current);
          WMTRenderPassInfo render_pass_info;
          WMT::InitializeRenderPassInfo(render_pass_info);
          {
            for (unsigned i = 0; i < std::size(render_pass_info.colors); i++) {
              auto &color_data = data->colors[i];
              if (!color_data.attachment)
                continue;
              auto &color_info = render_pass_info.colors[i];
              color_info.texture = color_data.attachment.texture();
              color_info.load_action = color_data.load_action;
              color_info.store_action = color_data.store_action;
              color_info.level = color_data.level;
              color_info.slice = color_data.slice;
              color_info.depth_plane = color_data.depth_plane;
              color_info.clear_color = color_data.clear_color;
              color_info.resolve_texture = color_data.resolve_attachment.texture();
              color_info.resolve_level = color_data.resolve_level;
              color_info.resolve_slice = color_data.resolve_slice;
              color_info.resolve_depth_plane = color_data.resolve_depth_plane;
            }
            if (data->depth.attachment) {
              auto &depth_info = render_pass_info.depth;
              auto &depth_data = data->depth;
              depth_info.texture = depth_data.attachment.texture();
              depth_info.load_action = depth_data.load_action;
              depth_info.store_action = depth_data.store_action;
              depth_info.level = depth_data.level;
              depth_info.slice = depth_data.slice;
              depth_info.depth_plane = depth_data.depth_plane;
              depth_info.clear_depth = depth_data.clear_depth;
            }
            if (data->stencil.attachment) {
              auto &stencil_info = render_pass_info.stencil;
              auto &stencil_data = data->stencil;
              stencil_info.texture = stencil_data.attachment.texture();
              stencil_info.load_action = stencil_data.load_action;
              stencil_info.store_action = stencil_data.store_action;
              stencil_info.level = stencil_data.level;
              stencil_info.slice = stencil_data.slice;
              stencil_info.depth_plane = stencil_data.depth_plane;
              stencil_info.clear_stencil = stencil_data.clear_stencil;
            }
            render_pass_info.default_raster_sample_count = data->default_raster_sample_count;
            render_pass_info.render_target_array_length = data->render_target_array_length;
            render_pass_info.render_target_width = data->render_target_width;
            render_pass_info.render_target_height = data->render_target_height;
            if (data->use_visibility_result)
              render_pass_info.visibility_buffer = data->visibility_buffer.handle;
          }
          auto encoder = cmdbuf.renderCommandEncoder(render_pass_info);
          encoder.waitForFence(fence_, WMTRenderStageVertex);
          encoder.encodeCommands(&data->cmd_head);
          encoder.updateFence(fence_, WMTRenderStageFragment);
          encoder.endEncoding();
          break;
        }
        case EncoderType::Blit: {
          auto data = static_cast<BlitEncoderData *>(current);
          auto encoder = cmdbuf.blitCommandEncoder();
          encoder.waitForFence(fence_);
          encoder.encodeCommands(&data->cmd_head);
          encoder.updateFence(fence_);
          encoder.endEncoding();
          break;
        }
        case EncoderType::Compute: {
          auto data = static_cast<ComputeEncoderData *>(current);
          auto encoder = cmdbuf.computeCommandEncoder(false);
          encoder.waitForFence(fence_);
          encoder.encodeCommands(&data->cmd_head);
          encoder.updateFence(fence_);
          encoder.endEncoding();
          break;
        }
        case EncoderType::Resolve: {
          auto data = static_cast<ResolveEncoderData *>(current);

          WMTRenderPassInfo info;
          WMT::InitializeRenderPassInfo(info);
          info.colors[0].texture = data->src.texture();
          info.colors[0].load_action = WMTLoadActionLoad;
          info.colors[0].store_action = WMTStoreActionStoreAndMultisampleResolve;
          info.colors[0].resolve_texture = data->dst.texture();

          auto encoder = cmdbuf.renderCommandEncoder(info);
          encoder.waitForFence(fence_, WMTRenderStageFragment);
          encoder.setLabel(WMT::String::string("ResolvePass", WMTUTF8StringEncoding));
          encoder.updateFence(fence_, WMTRenderStageFragment);
          encoder.endEncoding();

          break;
        }
        case EncoderType::SampleTimestamp: {
          auto data = static_cast<SampleTimestampData *>(current);
          if (!data->sample_buffer || !timestamp_dummy_buffer_)
            break;

          WMTSampleBufferAttachmentInfo attachment = {};
          attachment.sample_buffer = data->sample_buffer.handle;
          attachment.start_of_encoder_sample_index = data->sample_index;
          attachment.end_of_encoder_sample_index = ~0ull;
          auto encoder = cmdbuf.blitCommandEncoderWithSampleBuffers(&attachment, 1);
          encoder.waitForFence(fence_);

          wmtcmd_blit_fillbuffer fill = {};
          fill.type = WMTBlitCommandFillBuffer;
          fill.next.set(nullptr);
          fill.buffer = timestamp_dummy_buffer_;
          fill.offset = 0;
          fill.length = sizeof(uint32_t);
          fill.value = 0;
          MTLBlitCommandEncoder_encodeCommands(encoder, (const wmtcmd_base *)&fill);

          encoder.updateFence(fence_);
          encoder.endEncoding();
          break;
        }
        }
        current = current->next;
      }
    }
    cmdbuf.commit();
    // temporary workaround
    cmdbuf.waitUntilCompleted();
  };

  void STDMETHODCALLTYPE SetMarker(UINT metadata, const void *data, UINT size) {};

  void STDMETHODCALLTYPE BeginEvent(UINT metadata, const void *data, UINT size) {};

  void STDMETHODCALLTYPE EndEvent() {};

  HRESULT STDMETHODCALLTYPE
  Signal(ID3D12Fence *pFence, UINT64 Value) {
    auto pool = WMT::MakeAutoreleasePool();
    auto cmdbuf = queue_.commandBuffer();
    static_cast<MTLD3D12Fence *>(pFence)->fence->signal(cmdbuf, Value);
    cmdbuf.commit();
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE
  Wait(ID3D12Fence *pFence, UINT64 Value) {
    auto pool = WMT::MakeAutoreleasePool();
    auto cmdbuf = queue_.commandBuffer();
    static_cast<MTLD3D12Fence *>(pFence)->fence->wait(cmdbuf, Value);
    cmdbuf.commit();
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE
  GetTimestampFrequency(UINT64 *pFrequency) {
    if (!pFrequency)
      return E_INVALIDARG;
    *pFrequency = 1000000000ull;
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE
  GetClockCalibration(UINT64 *gpu_timestamp, UINT64 *cpu_timestamp) {
    if (!gpu_timestamp || !cpu_timestamp)
      return E_INVALIDARG;

    auto pool = WMT::MakeAutoreleasePool();
    auto sample_buffer = device_->GetMTLDevice().newCounterSampleBuffer(1, true);
    if (!sample_buffer)
      return E_FAIL;

    LARGE_INTEGER cpu_before = {};
    LARGE_INTEGER cpu_after = {};
    if (!QueryPerformanceCounter(&cpu_before))
      return E_FAIL;

    WMTSampleBufferAttachmentInfo attachment = {};
    attachment.sample_buffer = sample_buffer.handle;
    attachment.start_of_encoder_sample_index = 0;
    attachment.end_of_encoder_sample_index = ~0ull;

    auto cmdbuf = queue_.commandBuffer();
    auto encoder = cmdbuf.blitCommandEncoderWithSampleBuffers(&attachment, 1);
    encoder.waitForFence(fence_);

    wmtcmd_blit_fillbuffer fill = {};
    fill.type = WMTBlitCommandFillBuffer;
    fill.next.set(nullptr);
    fill.buffer = timestamp_dummy_buffer_;
    fill.offset = 0;
    fill.length = sizeof(uint32_t);
    fill.value = 0;
    MTLBlitCommandEncoder_encodeCommands(encoder, (const wmtcmd_base *)&fill);

    encoder.updateFence(fence_);
    encoder.endEncoding();
    cmdbuf.commit();
    cmdbuf.waitUntilCompleted();

    if (!QueryPerformanceCounter(&cpu_after))
      return E_FAIL;

    UINT64 gpu_value = 0;
    sample_buffer.resolveCounterRange(0, 1, &gpu_value, sizeof(gpu_value));
    if (!gpu_value)
      return E_FAIL;

    const auto cpu_start = static_cast<UINT64>(cpu_before.QuadPart);
    const auto cpu_end = static_cast<UINT64>(cpu_after.QuadPart);
    *gpu_timestamp = gpu_value;
    *cpu_timestamp = cpu_start + (cpu_end - cpu_start) / 2;
    return S_OK;
  };

  D3D12_COMMAND_QUEUE_DESC *STDMETHODCALLTYPE
  GetDesc(D3D12_COMMAND_QUEUE_DESC *__ret) {
    *__ret = desc_;
    return __ret;
  };

  HRESULT STDMETHODCALLTYPE
  CreateSwapChain(
      IDXGIFactory1 *pFactory, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGISwapChain1 **ppSwapChain
  ) {
    return dxmt::CreateSwapChain(pFactory, device_, this, hWnd, pDesc, pFullscreenDesc, ppSwapChain);
  }

  HRESULT
  Present(Presenter *presenter, ID3D12Resource *backbuffer, HANDLE hLantecyWaitable) {
    auto pool = WMT::MakeAutoreleasePool();
    auto cmdbuf = queue_.commandBuffer();

    auto g = reinterpret_cast<MTLD3D12Resource *>(backbuffer);
    auto &view = g->texture->view(g->texture->fullView);

    auto state = presenter->synchronizeLayerProperties();
    auto drawable = presenter->encodeCommands(
        cmdbuf, view.texture, state.metadata,
        [&](auto encoder) { encoder.waitForFence(fence_, WMTRenderStageFragment); },
        [&](auto encoder) { encoder.updateFence(fence_, WMTRenderStageFragment); }
    );

    cmdbuf.presentDrawable(drawable);
    cmdbuf.commit();

    {
      // temporary workaround
      cmdbuf.waitUntilCompleted();
      ReleaseSemaphore(hLantecyWaitable, 1, nullptr);
    }

    return S_OK;
  }
};

HRESULT
CreateCommandQueue(MTLD3D12Device *pDevice, const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue) {
  auto command_queue = Com(new MTLD3D12CommandQueueImpl(pDevice));
  HRESULT hr = command_queue->Initialize(pDesc);
  if (FAILED(hr))
    return hr;
  return command_queue->QueryInterface(riid, ppCommandQueue);
};

} // namespace dxmt

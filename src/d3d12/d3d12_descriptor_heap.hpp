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
#include "dxmt_buffer.hpp"
#include "dxmt_texture.hpp"
#include "metalirconverter_thunks.h"
#include <cstdint>

namespace dxmt {

SIZE_T RegisterDescriptorHeap(const void *heap);
void UnregisterDescriptorHeap(const void *heap);
const void *LookupDescriptorHeap(SIZE_T index);

constexpr SIZE_T kDescriptorHeapTag = 0x1f;

struct EMBEDDED_DESCRIPTOR_HANDLE {
  SIZE_T Tag        : 5;
  SIZE_T Descriptor : 20;
  SIZE_T Heap       : sizeof(SIZE_T) == 4 ? 7 : 39;

  const void *
  getHeap() const {
    return Tag == kDescriptorHeapTag ? LookupDescriptorHeap(Heap) : nullptr;
  }

  template <typename T>
  T *
  extract() {
    return reinterpret_cast<T *>(const_cast<void *>(getHeap()));
  }

  EMBEDDED_DESCRIPTOR_HANDLE(const void *heap, SIZE_T index) {
    Heap = RegisterDescriptorHeap(heap);
    Tag = kDescriptorHeapTag;
    Descriptor = index;
  }

  EMBEDDED_DESCRIPTOR_HANDLE(D3D12_CPU_DESCRIPTOR_HANDLE Handle) {
    union {
      D3D12_CPU_DESCRIPTOR_HANDLE d3d12;
      EMBEDDED_DESCRIPTOR_HANDLE impl;
    } _;
    _.d3d12 = Handle;
    *this = _.impl;
  }

  operator D3D12_CPU_DESCRIPTOR_HANDLE() const {
    union {
      D3D12_CPU_DESCRIPTOR_HANDLE d3d12;
      EMBEDDED_DESCRIPTOR_HANDLE impl;
    } _;
    _.impl = *this;
    return _.d3d12;
  };

  EMBEDDED_DESCRIPTOR_HANDLE() = default;
};

static_assert(sizeof(EMBEDDED_DESCRIPTOR_HANDLE) == sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));

enum class ShaderVisibleDescriptorType {
  Null,
  SRVTexture,
  ConstantBuffer,
  UAVTexture,
  UAVTexelBuffer,
  UAVBuffer,
  SRVTexelBuffer,
  SRVBuffer,
};

struct SRVTextureCPUStorage {
  Texture *texture = nullptr;
  TextureViewKey view{};
};

using UAVTextureCPUStorage = SRVTextureCPUStorage;

struct UAVTexelBufferCPUStorage {
  Buffer *buffer = nullptr;
  BufferViewKey view{};
  BufferSlice slice{};
};

using SRVTexelBufferCPUStorage = UAVTexelBufferCPUStorage;

struct CBVCommonStorage {
  uint64_t address = 0;
  uint64_t size = 0;
};

struct UAVBufferCPUStorage {
  Buffer *buffer = nullptr;
  BufferSlice slice{};
};

using SRVBufferCPUStorage = UAVBufferCPUStorage;

struct ShaderVisibleDescriptorCPUStorage {
  ShaderVisibleDescriptorType type;
  union {
    SRVTextureCPUStorage SRVTexture;
    CBVCommonStorage ConstantBuffer;
    UAVTextureCPUStorage UAVTexture;
    UAVTexelBufferCPUStorage UAVTexelBuffer;
    UAVBufferCPUStorage UAVBuffer;
    SRVTexelBufferCPUStorage SRVTexelBuffer;
    SRVBufferCPUStorage SRVBuffer;
  };

  ShaderVisibleDescriptorCPUStorage() : type(ShaderVisibleDescriptorType::Null) {}
};

class MTLD3D12DescriptorHeap : public ID3D12DescriptorHeap {
public:
  virtual uint64_t GetMSCDescriptorTableAddress(D3D12_GPU_DESCRIPTOR_HANDLE Handle) = 0;
  virtual WMT::Buffer GetMSCDescriptorHeapBuffer() = 0;

  virtual HRESULT
  AddShaderResourceView(UINT Index, Texture *Texture, TextureViewKey View, FLOAT ResourceMinLODClamp) = 0;

  virtual HRESULT AddConstantBufferView(UINT Index, UINT64 VA, UINT32 SizeInBytes) = 0;

  virtual HRESULT AddUnorderedAccessView(UINT Index, Texture *Texture, TextureViewKey View) = 0;

  virtual HRESULT AddUnorderedAccessView(UINT Index, Buffer *Buffer, BufferViewKey View, BufferSlice Slice) = 0;

  virtual HRESULT AddUnorderedAccessView(
      UINT Index, Buffer *UAVBuffer, BufferSlice Slice, Buffer *Counter, UINT CounterOffsetInBytes
  ) = 0;

  virtual HRESULT AddShaderResourceView(UINT Index, Buffer *Buffer, BufferViewKey View, BufferSlice Slice) = 0;

  virtual HRESULT AddShaderResourceView(UINT Index, Buffer *Buffer, BufferSlice Slice) = 0;

  virtual HRESULT AddShaderResourceView(UINT Index, D3D12_SHADER_RESOURCE_VIEW_DESC const *pDesc) = 0;

  virtual HRESULT AddUnorderedAccessView(UINT Index, D3D12_UNORDERED_ACCESS_VIEW_DESC const *pDesc) = 0;

  virtual ShaderVisibleDescriptorCPUStorage const &GetDescriptor(UINT Index) = 0;

  virtual void CopyDescriptors(UINT From, MTLD3D12DescriptorHeap *pHeapTo, UINT DescriptorTo, UINT CopyCount) = 0;
};

class MTLD3D12SamplerDescriptorHeap : public ID3D12DescriptorHeap {
public:
  virtual uint64_t GetMSCDescriptorTableAddress(D3D12_GPU_DESCRIPTOR_HANDLE Handle) = 0;
  virtual WMT::Buffer GetMSCDescriptorHeapBuffer() = 0;

  virtual HRESULT AddSampler(UINT Index, const D3D12_SAMPLER_DESC *Desc) = 0;

  virtual void CopyDescriptors(UINT From, MTLD3D12SamplerDescriptorHeap *pHeapTo, UINT DescriptorTo, UINT CopyCount) = 0;
};

struct MTL_RENDER_TARGET_DESC {
  Texture *Texture;
  TextureViewKey View;
  UINT Width;
  UINT Height;
  UINT DepthPlane;
  UINT RenderTargetArrayLength;
  UINT Flags;
};

class MTLD3D12RenderTargetDescriptorHeap : public ID3D12DescriptorHeap {
public:
  virtual HRESULT AddRenderTarget(UINT Index, MTL_RENDER_TARGET_DESC const *pDesc) = 0;
  virtual MTL_RENDER_TARGET_DESC GetRenderTarget(UINT Index) = 0;

  virtual void CopyDescriptors(UINT From, MTLD3D12RenderTargetDescriptorHeap *pHeapTo, UINT DescriptorTo, UINT CopyCount) = 0;
};

} // namespace dxmt

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
#include "d3d12_descriptor_heap.hpp"
#include "d3d12_pageable.hpp"
#include "com/com_pointer.hpp"
#include "dxmt_format.hpp"
#include "dxmt_sampler.hpp"
#include "log/log.hpp"

namespace dxmt {

struct SRVTextureGPUStorage {
  uint64_t resource_id;
  uint64_t metadata;
  uint64_t padding[2];
};

using UAVTextureGPUStorage = SRVTextureGPUStorage;

struct UAVTexelBufferGPUStorage {
  uint64_t resource_id;
  uint64_t metadata;
  uint64_t padding[2];
};

using SRVTexelBufferGPUStorage = UAVTexelBufferGPUStorage;

struct UAVBufferGPUStorage {
  uint64_t pointer;
  uint64_t metadata;
  uint64_t counter_pointer;
  uint64_t padding;
};

using SRVBufferGPUStorage = UAVBufferGPUStorage;

struct ShaderVisibleDescriptorGPUStorage {
  union {
    SRVTextureGPUStorage SRVTexture;
    CBVCommonStorage ConstantBuffer;
    UAVTextureGPUStorage UAVTexture;
    UAVTexelBufferGPUStorage UAVTexelBuffer;
    UAVBufferGPUStorage UAVBuffer;
    SRVTexelBufferGPUStorage SRVTexelBuffer;
    SRVBufferGPUStorage SRVBuffer;
    std::array<uint64_t, 4> ZeroFilled;
  };

  ShaderVisibleDescriptorGPUStorage();
};

ShaderVisibleDescriptorGPUStorage::ShaderVisibleDescriptorGPUStorage() : ZeroFilled{} {}

static_assert(sizeof(ShaderVisibleDescriptorGPUStorage) == 32);
static_assert(sizeof(dxmt_msc_descriptor_entry) == 24);

inline uint64_t
TextureMetadata(uint32_t array_length, float min_lod) {
  return ((uint64_t)array_length << 32) | (uint64_t)std::bit_cast<uint32_t>(min_lod);
}

class MTLD3D12DescriptorHeapImpl : public MTLD3D12Pageable<MTLD3D12DescriptorHeap> {

  D3D12_DESCRIPTOR_HEAP_DESC desc_;

  std::vector<ShaderVisibleDescriptorCPUStorage> descriptors_;
  Rc<Buffer> buffer_;
  ShaderVisibleDescriptorGPUStorage *mapped_argument_buffer_ = nullptr;
  uint64_t argument_buffer_gpu_address_ = 0;
  Rc<Buffer> msc_buffer_;
  dxmt_msc_descriptor_entry *mapped_msc_argument_buffer_ = nullptr;
  uint64_t msc_argument_buffer_gpu_address_ = 0;

  void
  SetMSCDescriptor(UINT Index, dxmt_msc_descriptor_entry Entry) {
    if (mapped_msc_argument_buffer_)
      mapped_msc_argument_buffer_[Index] = Entry;
  }

public:
  MTLD3D12DescriptorHeapImpl(MTLD3D12Device *pDevice) : MTLD3D12Pageable<MTLD3D12DescriptorHeap>(pDevice) {}

  HRESULT
  Initialize(const D3D12_DESCRIPTOR_HEAP_DESC *pDesc) {
    if (!pDesc)
      return E_INVALIDARG;
    desc_ = *pDesc;
    switch (pDesc->Type) {
    case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: {
      break;
    }
    default:
      return E_INVALIDARG;
    }
    descriptors_.resize(pDesc->NumDescriptors);

    if (pDesc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
      buffer_ = new Buffer(descriptors_.size() * sizeof(ShaderVisibleDescriptorGPUStorage), device_->GetMTLDevice());

      Flags<BufferAllocationFlag> flags;
#ifdef __i386__
      IMPLEMENT_ME
#endif
      buffer_->rename(buffer_->allocate(flags));
      mapped_argument_buffer_ =
          reinterpret_cast<ShaderVisibleDescriptorGPUStorage *>(buffer_->current()->mappedMemory(0));
      argument_buffer_gpu_address_ = buffer_->current()->gpuAddress();
      device_->RegisterResidencyAndVA(buffer_->current());

      msc_buffer_ = new Buffer(descriptors_.size() * sizeof(dxmt_msc_descriptor_entry), device_->GetMTLDevice());
      msc_buffer_->rename(msc_buffer_->allocate(flags));
      mapped_msc_argument_buffer_ =
          reinterpret_cast<dxmt_msc_descriptor_entry *>(msc_buffer_->current()->mappedMemory(0));
      msc_argument_buffer_gpu_address_ = msc_buffer_->current()->gpuAddress();
      device_->RegisterResidencyAndVA(msc_buffer_->current());
      for (size_t i = 0; i < descriptors_.size(); i++)
        mapped_argument_buffer_[i] = {};
      memset(mapped_msc_argument_buffer_, 0, descriptors_.size() * sizeof(dxmt_msc_descriptor_entry));
    } else {
      mapped_argument_buffer_ = reinterpret_cast<ShaderVisibleDescriptorGPUStorage *>(
          malloc(descriptors_.size() * sizeof(ShaderVisibleDescriptorGPUStorage))
      );
      mapped_msc_argument_buffer_ = reinterpret_cast<dxmt_msc_descriptor_entry *>(
          calloc(descriptors_.size(), sizeof(dxmt_msc_descriptor_entry))
      );
    }

    return S_OK;
  };

  ~MTLD3D12DescriptorHeapImpl() {
    if (buffer_) {
      device_->UnregisterResidencyAndVA(buffer_->current());
      device_->UnregisterResidencyAndVA(msc_buffer_->current());
    } else {
      free(mapped_argument_buffer_);
      free(mapped_msc_argument_buffer_);
    }
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12DescriptorHeap)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12DescriptorHeap), riid)) {
      WARN("D3D12DescriptorHeap: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  virtual D3D12_DESCRIPTOR_HEAP_DESC *STDMETHODCALLTYPE
  GetDesc(D3D12_DESCRIPTOR_HEAP_DESC *__ret) {
    *__ret = desc_;
    return __ret;
  }

  virtual D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE
  GetCPUDescriptorHandleForHeapStart(D3D12_CPU_DESCRIPTOR_HANDLE *__ret) {
    *__ret = GetShaderVisibleDescriptor(this, 0);
    return __ret;
  }

  virtual D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE
  GetGPUDescriptorHandleForHeapStart(D3D12_GPU_DESCRIPTOR_HANDLE *__ret) {
    __ret->ptr = argument_buffer_gpu_address_;
    return __ret;
  }

  uint64_t
  GetMSCDescriptorTableAddress(D3D12_GPU_DESCRIPTOR_HANDLE Handle) override {
    if (!msc_buffer_ || Handle.ptr < argument_buffer_gpu_address_)
      return 0;
    uint64_t byte_offset = Handle.ptr - argument_buffer_gpu_address_;
    if (byte_offset % sizeof(ShaderVisibleDescriptorGPUStorage))
      return 0;
    uint64_t index = byte_offset / sizeof(ShaderVisibleDescriptorGPUStorage);
    if (index >= descriptors_.size())
      return 0;
    return msc_argument_buffer_gpu_address_ + index * sizeof(dxmt_msc_descriptor_entry);
  }

  WMT::Buffer
  GetMSCDescriptorHeapBuffer() override {
    return msc_buffer_ ? msc_buffer_->current()->buffer() : WMT::Buffer{};
  }

  virtual HRESULT
  AddShaderResourceView(UINT Index, Texture *Texture, TextureViewKey View, FLOAT ResourceMinLODClamp) {
    if (Index >= descriptors_.size())
      return E_INVALIDARG;
    auto &cpu_storage = descriptors_[Index];
    cpu_storage.type = ShaderVisibleDescriptorType::SRVTexture;
    cpu_storage.SRVTexture.texture = Texture;
    cpu_storage.SRVTexture.view = View;
    if (mapped_argument_buffer_) {
      auto &texture_view = Texture->view(View);
      auto &gpu_storage = mapped_argument_buffer_[Index];
      gpu_storage.SRVTexture.resource_id = texture_view.gpuResourceID;
      gpu_storage.SRVTexture.metadata = TextureMetadata(Texture->arrayLength(View), ResourceMinLODClamp);
      SetMSCDescriptor(Index, {0, texture_view.gpuResourceID, std::bit_cast<uint32_t>(ResourceMinLODClamp)});
    }
    return S_OK;
  }
    virtual HRESULT
  AddConstantBufferView(UINT Index, UINT64 VA, UINT32 SizeInBytes) {
    if (Index >= descriptors_.size())
      return E_INVALIDARG;
    auto &cpu_storage = descriptors_[Index];
    cpu_storage.type = ShaderVisibleDescriptorType::ConstantBuffer;
    cpu_storage.ConstantBuffer.address = VA;
    cpu_storage.ConstantBuffer.size = SizeInBytes;
    if (mapped_argument_buffer_) {
      auto &gpu_storage = mapped_argument_buffer_[Index];
      gpu_storage.ConstantBuffer.address = VA;
      gpu_storage.ConstantBuffer.size = SizeInBytes;
    }
    SetMSCDescriptor(Index, {VA, 0, SizeInBytes});
    return S_OK;
  }

  virtual HRESULT
  AddUnorderedAccessView(UINT Index, Texture *Texture, TextureViewKey View) {
    if (Index >= descriptors_.size())
      return E_INVALIDARG;
    auto &cpu_storage = descriptors_[Index];
    cpu_storage.type = ShaderVisibleDescriptorType::UAVTexture;
    cpu_storage.UAVTexture.texture = Texture; // 
    cpu_storage.UAVTexture.view = View;
    if (mapped_argument_buffer_) {
      auto &texture_view = Texture->view(View);
      auto &gpu_storage = mapped_argument_buffer_[Index];
      gpu_storage.UAVTexture.resource_id = texture_view.gpuResourceID;
      gpu_storage.UAVTexture.metadata = TextureMetadata(Texture->arrayLength(View), 0);
      SetMSCDescriptor(Index, {0, texture_view.gpuResourceID, 0});
    }
    return S_OK;
  }

  virtual HRESULT
  AddUnorderedAccessView(UINT Index, Buffer *UAVBuffer, BufferViewKey View, BufferSlice Slice) {
    if (Index >= descriptors_.size())
      return E_INVALIDARG;
    auto &cpu_storage = descriptors_[Index];
    cpu_storage.type = ShaderVisibleDescriptorType::UAVTexelBuffer;
    cpu_storage.UAVTexelBuffer.buffer = UAVBuffer;
    cpu_storage.UAVTexelBuffer.slice = Slice;
    cpu_storage.UAVTexelBuffer.view = View;
    if (mapped_argument_buffer_) { 
      auto &gpu_storage = mapped_argument_buffer_[Index];
      if (UAVBuffer) {
        auto &buffer_view = UAVBuffer->view_(View);
        gpu_storage.UAVTexelBuffer.resource_id = buffer_view.gpu_resource_id;
        gpu_storage.UAVTexelBuffer.metadata = ((uint64_t)Slice.elementCount << 32) | (uint64_t)(Slice.firstElement);
        auto texel_size = MTLGetTexelSize(UAVBuffer->pixelFormat(View));
        uint64_t metadata = Slice.byteLength;
        metadata |= ((uint64_t)(Slice.byteOffset / texel_size) & 0xff) << 32;
        metadata |= 1ull << 63;
        SetMSCDescriptor(
            Index, {UAVBuffer->current()->gpuAddress() + Slice.byteOffset, buffer_view.gpu_resource_id, metadata}
        );
      } else {
        gpu_storage.UAVTexelBuffer.resource_id = 0;
        gpu_storage.UAVTexelBuffer.metadata = 0;
        SetMSCDescriptor(Index, {});
      }
    }
    return S_OK;
  }

  virtual HRESULT
  AddUnorderedAccessView(UINT Index, Buffer *UAVBuffer, BufferSlice Slice, Buffer *Counter, UINT CounterOffsetInBytes) {
    if (Index >= descriptors_.size())
      return E_INVALIDARG;
    auto &cpu_storage = descriptors_[Index];
    cpu_storage.type = ShaderVisibleDescriptorType::UAVBuffer;
    cpu_storage.UAVBuffer.buffer = UAVBuffer;
    cpu_storage.UAVBuffer.slice = Slice;
    if (mapped_argument_buffer_) {
      auto &gpu_storage = mapped_argument_buffer_[Index];
      if (UAVBuffer) {
        gpu_storage.UAVBuffer.pointer = UAVBuffer->current()->gpuAddress() + Slice.byteOffset;
        gpu_storage.UAVBuffer.metadata = Slice.byteLength;
        gpu_storage.UAVBuffer.counter_pointer = Counter ? Counter->current()->gpuAddress() + CounterOffsetInBytes : 0;
        SetMSCDescriptor(Index, {gpu_storage.UAVBuffer.pointer, 0, Slice.byteLength});
      } else {
        gpu_storage.UAVBuffer.pointer = 0;
        gpu_storage.UAVBuffer.metadata = 0;
        gpu_storage.UAVBuffer.counter_pointer = 0;
        SetMSCDescriptor(Index, {});
      }
    }
    return S_OK;
  }

  virtual HRESULT AddShaderResourceView(UINT Index, Buffer *Buffer, BufferViewKey View, BufferSlice Slice) {
    if (Index >= descriptors_.size())
      return E_INVALIDARG;
    auto &cpu_storage = descriptors_[Index];
    cpu_storage.type = ShaderVisibleDescriptorType::SRVTexelBuffer;
    cpu_storage.SRVTexelBuffer.buffer = Buffer;
    cpu_storage.SRVTexelBuffer.slice = Slice;
    cpu_storage.SRVTexelBuffer.view = View;
    if (mapped_argument_buffer_) { 
      auto &gpu_storage = mapped_argument_buffer_[Index];
      if (Buffer) {
        auto &buffer_view = Buffer->view_(View);
        gpu_storage.UAVTexelBuffer.resource_id = buffer_view.gpu_resource_id;
        gpu_storage.UAVTexelBuffer.metadata = ((uint64_t)Slice.elementCount << 32) | (uint64_t)(Slice.firstElement);
        auto texel_size = MTLGetTexelSize(Buffer->pixelFormat(View));
        uint64_t metadata = Slice.byteLength;
        metadata |= ((uint64_t)(Slice.byteOffset / texel_size) & 0xff) << 32;
        metadata |= 1ull << 63;
        SetMSCDescriptor(Index, {Buffer->current()->gpuAddress() + Slice.byteOffset, buffer_view.gpu_resource_id, metadata});
      } else {
        gpu_storage.UAVTexelBuffer.resource_id = 0;
        gpu_storage.UAVTexelBuffer.metadata = 0;
        SetMSCDescriptor(Index, {});
      }
    }
    return S_OK;
  }

  virtual HRESULT AddShaderResourceView(UINT Index, Buffer *Buffer, BufferSlice Slice) {
    if (Index >= descriptors_.size())
      return E_INVALIDARG;
    auto &cpu_storage = descriptors_[Index];
    cpu_storage.type = ShaderVisibleDescriptorType::SRVBuffer;
    cpu_storage.SRVBuffer.buffer = Buffer;
    cpu_storage.SRVBuffer.slice = Slice;
    if (mapped_argument_buffer_) {
      auto &gpu_storage = mapped_argument_buffer_[Index];
      if (Buffer) {
        gpu_storage.SRVBuffer.pointer = Buffer->current()->gpuAddress() + Slice.byteOffset;
        gpu_storage.SRVBuffer.metadata = Slice.byteLength;
        SetMSCDescriptor(Index, {gpu_storage.SRVBuffer.pointer, 0, Slice.byteLength});
      } else {
        gpu_storage.SRVBuffer.pointer = 0;
        gpu_storage.SRVBuffer.metadata = 0;
        SetMSCDescriptor(Index, {});
      }
    }
    return S_OK;
  }

  virtual HRESULT
  AddShaderResourceView(UINT Index, D3D12_SHADER_RESOURCE_VIEW_DESC const *pDesc) {
    if (Index >= descriptors_.size())
      return E_INVALIDARG;
    if (!pDesc)
      return E_INVALIDARG;
    /**
     * TODO: support null descriptor properly (respect different view dimensions)
     */
    auto &cpu_storage = descriptors_[Index];
    cpu_storage.type = ShaderVisibleDescriptorType::Null;
    if (mapped_argument_buffer_) {
      auto &gpu_storage = mapped_argument_buffer_[Index];
      gpu_storage.ZeroFilled = {{}};
    }
    SetMSCDescriptor(Index, {});
    return S_OK;
  }

  virtual HRESULT
  AddUnorderedAccessView(UINT Index, D3D12_UNORDERED_ACCESS_VIEW_DESC const *pDesc) {
    if (Index >= descriptors_.size())
      return E_INVALIDARG;
    if (!pDesc)
      return E_INVALIDARG;
    /**
     * TODO: support null descriptor properly (respect different view dimensions)
     */
    auto &cpu_storage = descriptors_[Index];
    cpu_storage.type = ShaderVisibleDescriptorType::Null;
    if (mapped_argument_buffer_) {
      auto &gpu_storage = mapped_argument_buffer_[Index];
      gpu_storage.ZeroFilled = {{}};
    }
    SetMSCDescriptor(Index, {});
    return S_OK;
  }

  virtual ShaderVisibleDescriptorCPUStorage const &
  GetDescriptor(UINT Index) {
    return descriptors_[Index];
  }

  virtual void
  CopyDescriptors(UINT From, MTLD3D12DescriptorHeap *pHeapTo, UINT DescriptorTo, UINT CopyCount) {
    for (unsigned i = 0; i < CopyCount; i++) {
      static_cast<MTLD3D12DescriptorHeapImpl *>(pHeapTo)->descriptors_[DescriptorTo + i] = descriptors_[From + i];
      static_cast<MTLD3D12DescriptorHeapImpl *>(pHeapTo)->mapped_argument_buffer_[DescriptorTo + i] =
          mapped_argument_buffer_[From + i];
      if (mapped_msc_argument_buffer_ && static_cast<MTLD3D12DescriptorHeapImpl *>(pHeapTo)->mapped_msc_argument_buffer_)
        static_cast<MTLD3D12DescriptorHeapImpl *>(pHeapTo)->mapped_msc_argument_buffer_[DescriptorTo + i] =
            mapped_msc_argument_buffer_[From + i];
    }
  }
};

class MTLD3D12RenderTargetDescriptorHeapImpl : public MTLD3D12Pageable<MTLD3D12RenderTargetDescriptorHeap> {

  D3D12_DESCRIPTOR_HEAP_DESC desc_;

  std::vector<MTL_RENDER_TARGET_DESC> render_targets_;

public:
  MTLD3D12RenderTargetDescriptorHeapImpl(MTLD3D12Device *pDevice) :
      MTLD3D12Pageable<MTLD3D12RenderTargetDescriptorHeap>(pDevice) {}

  HRESULT
  Initialize(const D3D12_DESCRIPTOR_HEAP_DESC *pDesc) {
    if (!pDesc)
      return E_INVALIDARG;
    desc_ = *pDesc;
    switch (pDesc->Type) {
    case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
    case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: {
      if (pDesc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        return E_INVALIDARG;
      render_targets_.resize(pDesc->NumDescriptors);
      break;
    }
    default:
      return E_INVALIDARG;
    }
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12DescriptorHeap)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12DescriptorHeap), riid)) {
      WARN("D3D12DescriptorHeap: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  virtual D3D12_DESCRIPTOR_HEAP_DESC *STDMETHODCALLTYPE
  GetDesc(D3D12_DESCRIPTOR_HEAP_DESC *__ret) {
    *__ret = desc_;
    return __ret;
  }

  virtual D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE
  GetCPUDescriptorHandleForHeapStart(D3D12_CPU_DESCRIPTOR_HANDLE *__ret) {
    *__ret = GetRenderTargetDescriptor(this, 0);
    return __ret;
  }

  virtual D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE
  GetGPUDescriptorHandleForHeapStart(D3D12_GPU_DESCRIPTOR_HANDLE *__ret) {
    __ret->ptr = 0;
    return __ret;
  }

  virtual HRESULT
  AddRenderTarget(UINT Index, MTL_RENDER_TARGET_DESC const *pDesc) {
    if (Index >= render_targets_.size())
      return E_INVALIDARG;
    if (pDesc)
      render_targets_[Index] = *pDesc;
    else
      render_targets_[Index] = {};
    return S_OK;
  }

  virtual MTL_RENDER_TARGET_DESC
  GetRenderTarget(UINT Index) {
    return render_targets_[Index];
  }

  virtual void
  CopyDescriptors(UINT From, MTLD3D12RenderTargetDescriptorHeap *pHeapTo, UINT DescriptorTo, UINT CopyCount) {
    for (unsigned i = 0; i < CopyCount; i++) {
      static_cast<MTLD3D12RenderTargetDescriptorHeapImpl *>(pHeapTo)->render_targets_[DescriptorTo + i] =
          render_targets_[From + i];
    }
  }
};

struct SamplerGPUStorage {
  uint64_t sampler;
  uint64_t cube_sampler;
  uint64_t metadata;
  uint64_t padding;
};

class MTLD3D12SamplerDescriptorHeapImpl : public MTLD3D12Pageable<MTLD3D12SamplerDescriptorHeap> {

  D3D12_DESCRIPTOR_HEAP_DESC desc_;

  std::vector<Rc<Sampler>> samplers_;

  Rc<Buffer> buffer_;
  SamplerGPUStorage *mapped_argument_buffer_ = nullptr;
  uint64_t argument_buffer_gpu_address_ = 0;
  Rc<Buffer> msc_buffer_;
  dxmt_msc_descriptor_entry *mapped_msc_argument_buffer_ = nullptr;
  uint64_t msc_argument_buffer_gpu_address_ = 0;

  void
  SetMSCDescriptor(UINT Index, dxmt_msc_descriptor_entry Entry) {
    if (mapped_msc_argument_buffer_)
      mapped_msc_argument_buffer_[Index] = Entry;
  }

public:
  MTLD3D12SamplerDescriptorHeapImpl(MTLD3D12Device *pDevice) :
      MTLD3D12Pageable<MTLD3D12SamplerDescriptorHeap>(pDevice) {}

  HRESULT
  Initialize(const D3D12_DESCRIPTOR_HEAP_DESC *pDesc) {
    if (!pDesc)
      return E_INVALIDARG;
    desc_ = *pDesc;
    switch (pDesc->Type) {
    case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: {
      break;
    }
    default:
      return E_INVALIDARG;
    }
    samplers_.resize(pDesc->NumDescriptors);

    if (pDesc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
      buffer_ = new Buffer(samplers_.size() * sizeof(SamplerGPUStorage), device_->GetMTLDevice());

      Flags<BufferAllocationFlag> flags;
#ifdef __i386__
      IMPLEMENT_ME
#endif
      buffer_->rename(buffer_->allocate(flags));
      mapped_argument_buffer_ = reinterpret_cast<SamplerGPUStorage *>(buffer_->current()->mappedMemory(0));
      argument_buffer_gpu_address_ = buffer_->current()->gpuAddress();
      // FIXME: is residency required for descriptor heap? Should be the case for Metal 4
      device_->RegisterResidencyAndVA(buffer_->current());

      msc_buffer_ = new Buffer(samplers_.size() * sizeof(dxmt_msc_descriptor_entry), device_->GetMTLDevice());
      msc_buffer_->rename(msc_buffer_->allocate(flags));
      mapped_msc_argument_buffer_ =
          reinterpret_cast<dxmt_msc_descriptor_entry *>(msc_buffer_->current()->mappedMemory(0));
      msc_argument_buffer_gpu_address_ = msc_buffer_->current()->gpuAddress();
      device_->RegisterResidencyAndVA(msc_buffer_->current());
      memset(mapped_argument_buffer_, 0, samplers_.size() * sizeof(SamplerGPUStorage));
      memset(mapped_msc_argument_buffer_, 0, samplers_.size() * sizeof(dxmt_msc_descriptor_entry));
    } else {
      mapped_argument_buffer_ =
          reinterpret_cast<SamplerGPUStorage *>(malloc(samplers_.size() * sizeof(SamplerGPUStorage)));
      mapped_msc_argument_buffer_ = reinterpret_cast<dxmt_msc_descriptor_entry *>(
          calloc(samplers_.size(), sizeof(dxmt_msc_descriptor_entry))
      );
    }

    return S_OK;
  };

  ~MTLD3D12SamplerDescriptorHeapImpl() {
     if (buffer_) {
       device_->UnregisterResidencyAndVA(buffer_->current());
       device_->UnregisterResidencyAndVA(msc_buffer_->current());
     } else {
       free(mapped_argument_buffer_);
       free(mapped_msc_argument_buffer_);
    }
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12DescriptorHeap)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12DescriptorHeap), riid)) {
      WARN("D3D12DescriptorHeap: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  virtual D3D12_DESCRIPTOR_HEAP_DESC *STDMETHODCALLTYPE
  GetDesc(D3D12_DESCRIPTOR_HEAP_DESC *__ret) {
    *__ret = desc_;
    return __ret;
  }

  virtual D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE
  GetCPUDescriptorHandleForHeapStart(D3D12_CPU_DESCRIPTOR_HANDLE *__ret) {
    *__ret = GetSamplerDescriptor(this, 0);
    return __ret;
  }

  virtual D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE
  GetGPUDescriptorHandleForHeapStart(D3D12_GPU_DESCRIPTOR_HANDLE *__ret) {
    __ret->ptr = argument_buffer_gpu_address_;
    return __ret;
  }

  uint64_t
  GetMSCDescriptorTableAddress(D3D12_GPU_DESCRIPTOR_HANDLE Handle) override {
    if (!msc_buffer_ || Handle.ptr < argument_buffer_gpu_address_)
      return 0;
    uint64_t byte_offset = Handle.ptr - argument_buffer_gpu_address_;
    if (byte_offset % sizeof(SamplerGPUStorage))
      return 0;
    uint64_t index = byte_offset / sizeof(SamplerGPUStorage);
    if (index >= samplers_.size())
      return 0;
    return msc_argument_buffer_gpu_address_ + index * sizeof(dxmt_msc_descriptor_entry);
  }

  WMT::Buffer
  GetMSCDescriptorHeapBuffer() override {
    return msc_buffer_ ? msc_buffer_->current()->buffer() : WMT::Buffer{};
  }


  virtual HRESULT
  AddSampler(UINT Index, const D3D12_SAMPLER_DESC *pDesc) {
    if (!pDesc)
      return E_INVALIDARG;
    if (Index >= samplers_.size())
      return E_INVALIDARG;
  
    WMTSamplerInfo info;
    PopulateWMTSamplerInfo(device_->GetMTLDevice(), info, *pDesc);
    auto sampler = Sampler::createSampler(device_->GetMTLDevice(), info, pDesc->MipLODBias);

    samplers_[Index] = sampler;
    if (mapped_argument_buffer_) {
      auto &gpu_storage = mapped_argument_buffer_[Index];
      gpu_storage.sampler = sampler->sampler_state_handle;
      gpu_storage.cube_sampler = sampler->sampler_state_cube_handle;
      gpu_storage.metadata = (uint64_t)std::bit_cast<uint32_t>(sampler->lod_bias);
    }
    SetMSCDescriptor(Index, {sampler->sampler_state_handle, 0, std::bit_cast<uint32_t>(sampler->lod_bias)});

    return S_OK;
  }

  virtual void
  CopyDescriptors(UINT From, MTLD3D12SamplerDescriptorHeap *pHeapTo, UINT DescriptorTo, UINT CopyCount) {
    for (unsigned i = 0; i < CopyCount; i++) {
      static_cast<MTLD3D12SamplerDescriptorHeapImpl *>(pHeapTo)->samplers_[DescriptorTo + i] = samplers_[From + i];
      static_cast<MTLD3D12SamplerDescriptorHeapImpl *>(pHeapTo)->mapped_argument_buffer_[DescriptorTo + i] =
          mapped_argument_buffer_[From + i];
      if (mapped_msc_argument_buffer_ && static_cast<MTLD3D12SamplerDescriptorHeapImpl *>(pHeapTo)->mapped_msc_argument_buffer_)
        static_cast<MTLD3D12SamplerDescriptorHeapImpl *>(pHeapTo)->mapped_msc_argument_buffer_[DescriptorTo + i] =
            mapped_msc_argument_buffer_[From + i];
    }
  }
};

HRESULT
CreateDescriptorHeap(
    MTLD3D12Device *pDevice, const D3D12_DESCRIPTOR_HEAP_DESC *pDesc, REFIID riid, void **ppDescriptorHeap
) {
  InitReturnPtr(ppDescriptorHeap);
  if (!pDesc)
    return E_INVALIDARG;
  if (pDesc->NumDescriptors > 0xFFFFF) {
    ERR("CreateDescriptorHeap: NumDescriptors is too large");
    return E_INVALIDARG;
  }
  switch (pDesc->Type) {
  case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: {
    auto descriptor_heap = Com(new MTLD3D12DescriptorHeapImpl(pDevice));
    HRESULT hr = descriptor_heap->Initialize(pDesc);
    if (FAILED(hr))
      return hr;
    if (!ppDescriptorHeap)
      return S_FALSE;
    return descriptor_heap->QueryInterface(riid, ppDescriptorHeap);
  }
  case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: {
    auto sampler_heap = Com(new MTLD3D12SamplerDescriptorHeapImpl(pDevice));
    HRESULT hr = sampler_heap->Initialize(pDesc);
    if (FAILED(hr))
      return hr;
    if (!ppDescriptorHeap)
      return S_FALSE;
    return sampler_heap->QueryInterface(riid, ppDescriptorHeap);
  }
  case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
  case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: {
    auto descriptor_heap = Com(new MTLD3D12RenderTargetDescriptorHeapImpl(pDevice));
    HRESULT hr = descriptor_heap->Initialize(pDesc);
    if (FAILED(hr))
      return hr;
    if (!ppDescriptorHeap)
      return S_FALSE;
    return descriptor_heap->QueryInterface(riid, ppDescriptorHeap);
  }
  default:
    break;
  }
  return E_INVALIDARG;
}

} // namespace dxmt

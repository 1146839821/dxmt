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

#include <cstring>
#include <vector>
#include <cstdint>
#include <limits>

namespace dxmt {

struct RawRootSignatureDesc {
  uint32_t Version;
  uint32_t NumParameters;
  uint32_t RootParametersOffset;
  uint32_t NumStaticSamplers;
  uint32_t StaticSamplersOffset;
  uint32_t Flags;
};

struct RawRootParameter {
  uint32_t ParameterType;
  uint32_t ShaderVisibility;
  uint32_t PayloadOffset;
};

struct RawRootDescriptorTable {
  uint32_t NumDescriptorRanges;
  uint32_t DescriptorRangesOffset;
};

template <typename TO, typename FROM>
void
SetFlag(TO *A, const FROM *B) {}

template <>
inline void
SetFlag(D3D12_ROOT_DESCRIPTOR1 *A, const D3D12_ROOT_DESCRIPTOR *B) {
  A->Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
};

template <>
inline void
SetFlag(D3D12_ROOT_DESCRIPTOR1 *A, const D3D12_ROOT_DESCRIPTOR1 *B) {
  memcpy(&A->Flags, &B->Flags, sizeof(A->Flags));
};

template <>
inline void
SetFlag(D3D12_DESCRIPTOR_RANGE1 *A, const D3D12_DESCRIPTOR_RANGE *B) {
  if (B->RangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
    A->Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
  else
    A->Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
};

template <>
inline void
SetFlag(D3D12_DESCRIPTOR_RANGE1 *A, const D3D12_DESCRIPTOR_RANGE1 *B) {
  memcpy(&A->Flags, &B->Flags, sizeof(A->Flags));
};

template <typename T> struct GetType {
  using ROOT_PARAMETER = std::decay_t<decltype(std::declval<T>().pParameters[0])>;
  using ROOT_DESCRIPTOR = decltype(std::declval<T>().pParameters[0].Descriptor);
  using DESCRIPTOR_RANGE =
      std::decay_t<decltype(std::declval<T>().pParameters[0].DescriptorTable.pDescriptorRanges[0])>;
};

class RootSignatureDeserializer {
public:
  RootSignatureDeserializer() {}

  template <typename T>
  T *
  AllocateBuffer(size_t Count, size_t Size = sizeof(T)) {
    if (!Count || !Size || Count > std::numeric_limits<size_t>::max() / Size)
      return nullptr;
    T *ptr = (T *)malloc(Size * Count);
    if (ptr)
      malloc_ptrs_.push_back(ptr);
    return ptr;
  }

  template <typename ROOT_SIGNATURE_DESC>
  HRESULT
  DeserializeVersionedRootSignatureImpl(ROOT_SIGNATURE_DESC &Output) {
    using ROOT_PARAMETER = GetType<ROOT_SIGNATURE_DESC>::ROOT_PARAMETER;
    using ROOT_DESCRIPTOR = GetType<ROOT_SIGNATURE_DESC>::ROOT_DESCRIPTOR;
    using DESCRIPTOR_RANGE = GetType<ROOT_SIGNATURE_DESC>::DESCRIPTOR_RANGE;

    // The native build's older SDK omits modern root flags. Validate their
    // serialized bits, then preserve opaque flags without an enum-value load.
    constexpr uint32_t root_flags = 0xfff; // through SAMPLER_HEAP_DIRECTLY_INDEXED (0x800)
    if (raw_root_sig_->Flags & ~root_flags)
      return E_INVALIDARG;
    memcpy(&Output.Flags, &raw_root_sig_->Flags, sizeof(Output.Flags));
    /* Root Parameters */
    const RawRootParameter *Parameters;
    if (!ReadArray(raw_root_sig_->RootParametersOffset, raw_root_sig_->NumParameters, Parameters))
      return E_INVALIDARG;
    auto pParameters = AllocateBuffer<ROOT_PARAMETER>(raw_root_sig_->NumParameters);
    if (raw_root_sig_->NumParameters && !pParameters)
      return E_OUTOFMEMORY;
    for (unsigned i = 0; i < raw_root_sig_->NumParameters; i++) {
      ROOT_PARAMETER &ParameterOut = pParameters[i];
      auto RootParameter = Parameters[i];
      auto ParameterType = RootParameter.ParameterType;
      if (ParameterType > D3D12_ROOT_PARAMETER_TYPE_UAV ||
          RootParameter.ShaderVisibility > 7 /* MESH, absent in the older native SDK */)
        return E_INVALIDARG;
      auto ShaderVisibility = (D3D12_SHADER_VISIBILITY)RootParameter.ShaderVisibility;
      ParameterOut.ParameterType = (D3D12_ROOT_PARAMETER_TYPE)ParameterType;
      ParameterOut.ShaderVisibility = ShaderVisibility;
      switch (ParameterType) {
      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
        const RawRootDescriptorTable *Table;
        if (!ReadArray(RootParameter.PayloadOffset, 1, Table))
          return E_INVALIDARG;
        const DESCRIPTOR_RANGE *Ranges;
        if (!ReadArray(Table->DescriptorRangesOffset, Table->NumDescriptorRanges, Ranges))
          return E_INVALIDARG;
        ParameterOut.DescriptorTable.NumDescriptorRanges = Table->NumDescriptorRanges;
        ParameterOut.DescriptorTable.pDescriptorRanges = Ranges;

        break;
      }
      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: {
        const D3D12_ROOT_CONSTANTS *Constants;
        if (!ReadArray(RootParameter.PayloadOffset, 1, Constants))
          return E_INVALIDARG;
        ParameterOut.Constants = *Constants;
        break;
      }
      case D3D12_ROOT_PARAMETER_TYPE_CBV:
      case D3D12_ROOT_PARAMETER_TYPE_SRV:
      case D3D12_ROOT_PARAMETER_TYPE_UAV: {
        const ROOT_DESCRIPTOR *Descriptor;
        if (!ReadArray(RootParameter.PayloadOffset, 1, Descriptor))
          return E_INVALIDARG;
        if constexpr (std::is_same_v<ROOT_DESCRIPTOR, D3D12_ROOT_DESCRIPTOR1>) {
          uint32_t flags;
          memcpy(&flags, &Descriptor->Flags, sizeof(flags));
          constexpr uint32_t allowed = uint32_t(D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE) |
                                       D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE |
                                       D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
          if (flags & ~allowed)
            return E_INVALIDARG;
        }
        ParameterOut.Descriptor = *Descriptor;
        break;
      }
      default:
        return E_INVALIDARG;
      }
    }
    Output.NumParameters = raw_root_sig_->NumParameters;
    Output.pParameters = pParameters;

    /* Static Samplers */
    Output.NumStaticSamplers = raw_root_sig_->NumStaticSamplers;
    if (!ReadArray(raw_root_sig_->StaticSamplersOffset, Output.NumStaticSamplers, Output.pStaticSamplers))
      return E_INVALIDARG;

    return S_OK;
  };

  template <typename ROOT_SIGNATURE_DESC_FROM, typename ROOT_SIGNATURE_DESC_TO>
  HRESULT
  ConvertVersionedRootSignature(ROOT_SIGNATURE_DESC_FROM &From, ROOT_SIGNATURE_DESC_TO &To) {
    using ROOT_PARAMETER_TO = GetType<ROOT_SIGNATURE_DESC_TO>::ROOT_PARAMETER;
    using DESCRIPTOR_RANGE_TO = GetType<ROOT_SIGNATURE_DESC_TO>::DESCRIPTOR_RANGE;
    using DESCRIPTOR_RANGE_FROM = GetType<ROOT_SIGNATURE_DESC_FROM>::DESCRIPTOR_RANGE;

    auto pParametersTo = AllocateBuffer<ROOT_PARAMETER_TO>(From.NumParameters);
    if (From.NumParameters && !pParametersTo)
      return E_OUTOFMEMORY;

    memcpy(&To.Flags, &From.Flags, sizeof(To.Flags));
    To.NumParameters = From.NumParameters;
    To.pParameters = pParametersTo;
    To.NumStaticSamplers = From.NumStaticSamplers;
    To.pStaticSamplers = From.pStaticSamplers;

    for (unsigned i = 0; i < From.NumParameters; i++) {
      const auto &ParameterFrom = From.pParameters[i];
      auto &ParameterTo = pParametersTo[i];
      ParameterTo.ParameterType = ParameterFrom.ParameterType;
      ParameterTo.ShaderVisibility = ParameterFrom.ShaderVisibility;
      switch (ParameterFrom.ParameterType) {
      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
        ParameterTo.DescriptorTable.NumDescriptorRanges = ParameterFrom.DescriptorTable.NumDescriptorRanges;

        auto pRanges = AllocateBuffer<DESCRIPTOR_RANGE_TO>(ParameterTo.DescriptorTable.NumDescriptorRanges);
        if (ParameterTo.DescriptorTable.NumDescriptorRanges && !pRanges)
          return E_OUTOFMEMORY;
        ParameterTo.DescriptorTable.pDescriptorRanges = pRanges;

        for (unsigned j = 0; j < ParameterTo.DescriptorTable.NumDescriptorRanges; j++) {
          const auto &RangeFrom = ParameterFrom.DescriptorTable.pDescriptorRanges[j];
          auto &RangeTo = pRanges[j];
          uint32_t range_type;
          memcpy(&range_type, &RangeFrom.RangeType, sizeof(range_type));
          if (range_type > D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
            return E_INVALIDARG;
          if constexpr (std::is_same_v<DESCRIPTOR_RANGE_FROM, D3D12_DESCRIPTOR_RANGE1>) {
            uint32_t flags;
            memcpy(&flags, &RangeFrom.Flags, sizeof(flags));
            constexpr uint32_t allowed =
                uint32_t(D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE) | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE |
                D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC |
                0x10000u; // DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS
            if (flags & ~allowed)
              return E_INVALIDARG;
          }
          RangeTo.OffsetInDescriptorsFromTableStart = RangeFrom.OffsetInDescriptorsFromTableStart;
          RangeTo.NumDescriptors = RangeFrom.NumDescriptors;
          RangeTo.BaseShaderRegister = RangeFrom.BaseShaderRegister;
          RangeTo.RegisterSpace = RangeFrom.RegisterSpace;
          RangeTo.RangeType = RangeFrom.RangeType;
          SetFlag(&RangeTo, &RangeFrom);
        }

        break;
      }
      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
        ParameterTo.Constants = ParameterFrom.Constants;
        break;
      case D3D12_ROOT_PARAMETER_TYPE_CBV:
      case D3D12_ROOT_PARAMETER_TYPE_SRV:
      case D3D12_ROOT_PARAMETER_TYPE_UAV:
        SetFlag(&ParameterTo.Descriptor, &ParameterFrom.Descriptor);
        ParameterTo.Descriptor.RegisterSpace = ParameterFrom.Descriptor.RegisterSpace;
        ParameterTo.Descriptor.ShaderRegister = ParameterFrom.Descriptor.ShaderRegister;
        break;
      }
    }

    return S_OK;
  }

  HRESULT
  Deserialize(const void *pBytecode, SIZE_T DataSize) {
    if (!pBytecode || DataSize < sizeof(RawRootSignatureDesc))
      return E_INVALIDARG;
    raw_size_ = DataSize;
    raw_root_sig_ = AllocateBuffer<RawRootSignatureDesc>(1, DataSize);
    if (!raw_root_sig_)
      return E_OUTOFMEMORY;

    memcpy(raw_root_sig_, pBytecode, DataSize);

    desc_1_0_.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
    desc_1_1_.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;

    HRESULT hr;
    switch (raw_root_sig_->Version) {
    case D3D_ROOT_SIGNATURE_VERSION_1_0: {
      hr = DeserializeVersionedRootSignatureImpl(desc_1_0_.Desc_1_0);
      if (FAILED(hr))
        return hr;
      hr = ConvertVersionedRootSignature(desc_1_0_.Desc_1_0, desc_1_1_.Desc_1_1);
      if (FAILED(hr))
        return hr;
      break;
    }
    case D3D_ROOT_SIGNATURE_VERSION_1_1: {
      hr = DeserializeVersionedRootSignatureImpl(desc_1_1_.Desc_1_1);
      if (FAILED(hr))
        return hr;
      hr = ConvertVersionedRootSignature(desc_1_1_.Desc_1_1, desc_1_0_.Desc_1_0);
      if (FAILED(hr))
        return hr;
      break;
    }
    default:
      hr = E_INVALIDARG;
    }

    return hr;
  }

  ~RootSignatureDeserializer() {
    for (auto ptr : malloc_ptrs_) {
      free(ptr);
    }
    malloc_ptrs_.clear();
    raw_root_sig_ = nullptr;
  }

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc_1_0_ = {};
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc_1_1_ = {};
  RawRootSignatureDesc *raw_root_sig_ = nullptr;

private:
  template <typename T>
  bool
  ReadArray(size_t offset, size_t count, const T *&array) const {
    array = nullptr;
    // Check the extent before pointer arithmetic or count multiplication.
    // Empty arrays still have bounded offsets, but form no typed pointer.
    if (offset > raw_size_ || count > (raw_size_ - offset) / sizeof(T) || (count && offset % alignof(T)))
      return false;
    if (count)
      array = reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(raw_root_sig_) + offset);
    return true;
  }

  size_t raw_size_ = 0;
  std::vector<void *> malloc_ptrs_;
};

} // namespace dxmt
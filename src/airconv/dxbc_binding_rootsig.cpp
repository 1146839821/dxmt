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

#include "air_signature.hpp"
#include "DXBCParser/BlobContainer.h"
#include "air_sampler_abi.hpp"
#include "dxbc_converter.hpp"
#include "dxbc_root_signature.hpp"
#include "shader_common.hpp"
#include "llvm/IR/DerivedTypes.h"
#include <cassert>
#include <tuple>

namespace dxmt::dxbc {

using namespace llvm::air;

class RootSignatureBindingMap : public BindingMap {
public:
  uint32_t RootFeedbackArgumentIndex = ~0u;

  BufferDescriptor GetRootFeedbackDescriptor(llvm::air::AIRBuilder &AIR, llvm::Value *Pointer,
                                             uint32_t Stride, bool Coherent) {
    auto &B = AIR.builder;
    auto Fn = B.GetInsertBlock()->getParent();
    auto Table = Fn->getArg(RootFeedbackArgumentIndex);
    auto Entry = B.GetInsertBlock();
    auto Loop = llvm::BasicBlock::Create(AIR.getContext(), "root.feedback.loop", Fn);
    auto Read = llvm::BasicBlock::Create(AIR.getContext(), "root.feedback.read", Fn);
    auto Next = llvm::BasicBlock::Create(AIR.getContext(), "root.feedback.next", Fn);
    auto Found = llvm::BasicBlock::Create(AIR.getContext(), "root.feedback.found", Fn);
    auto Done = llvm::BasicBlock::Create(AIR.getContext(), "root.feedback.done", Fn);
    auto VA = B.CreatePtrToInt(Pointer, B.getInt64Ty());
    auto Count = B.CreateLoad(B.getInt64Ty(), Table);
    B.CreateBr(Loop);
    B.SetInsertPoint(Loop);
    auto Index = B.CreatePHI(B.getInt64Ty(), 2);
    Index->addIncoming(B.getInt64(0), Entry);
    B.CreateCondBr(B.CreateICmpULT(Index, Count), Read, Done);
    B.SetInsertPoint(Read);
    auto Row = B.CreateAdd(B.CreateMul(Index, B.getInt64(3)), B.getInt64(1));
    auto Load = [&](unsigned field) {
      return B.CreateLoad(B.getInt64Ty(), B.CreateGEP(B.getInt64Ty(), Table, B.CreateAdd(Row, B.getInt64(field))));
    };
    auto Base = Load(0);
    auto Size = Load(1);
    auto Offset = B.CreateSub(VA, Base);
    B.CreateCondBr(B.CreateAnd(B.CreateICmpUGE(VA, Base), B.CreateICmpULT(Offset, Size)), Found, Next);
    B.SetInsertPoint(Next);
    auto Increment = B.CreateAdd(Index, B.getInt64(1));
    Index->addIncoming(Increment, Next);
    B.CreateBr(Loop);
    B.SetInsertPoint(Found);
    auto Header = Load(2);
    auto Remaining = B.CreateSub(Size, Offset);
    auto Length = B.CreateSelect(B.CreateICmpUGT(Remaining, B.getInt64(0xffffffff)), B.getInt64(0xffffffff), Remaining);
    B.CreateBr(Done);
    B.SetInsertPoint(Done);
    auto Metadata = B.CreatePHI(B.getInt64Ty(), 2);
    Metadata->addIncoming(B.getInt64(0), Loop);
    Metadata->addIncoming(Length, Found);
    auto Feedback = B.CreatePHI(B.getInt64Ty(), 2);
    Feedback->addIncoming(B.getInt64(0), Loop);
    Feedback->addIncoming(Header, Found);
    return {Pointer, Metadata, Stride, Coherent, Feedback};
  }
  llvm::Value *
  GetArgument(llvm::air::AIRBuilder &AIR, uint32_t TableIndex, uint32_t Index) {
    auto &B = AIR.builder;
    auto Fn = B.GetInsertBlock()->getParent();
    auto Arg = Fn->getArg(TableIndex);
    auto TyArg = llvm::cast<llvm::PointerType>(Arg->getType())->getNonOpaquePointerElementType();
    auto TyStruct = llvm::cast<llvm::StructType>(TyArg);
    return B.CreateLoad(TyStruct->getElementType(Index), B.CreateStructGEP(TyStruct, Arg, Index));
  };

  std::pair<llvm::Value *, llvm::Value *>
  GetBufferDescriptor(
      llvm::air::AIRBuilder &AIR, llvm::Value *IntPtr, llvm::Value *Index, llvm::Type *HandleType, RangeId RangeId,
      uint32_t DescriptorOffset
  ) {
    auto TyBufferDescriptor = llvm::StructType::get(
        AIR.getContext(), {HandleType, llvm::Type::getInt64Ty(AIR.getContext()),
                           llvm::Type::getInt64Ty(AIR.getContext()), llvm::Type::getInt64Ty(AIR.getContext())}
    );
    auto &B = AIR.builder;
    auto IdxDescriptor = B.CreateAdd(B.CreateSub(Index, AIR.getInt(RangeId)), AIR.getInt(DescriptorOffset));
    return {
        B.CreateLoad(
            HandleType, B.CreateGEP(
                            TyBufferDescriptor, B.CreatePointerCast(IntPtr, TyBufferDescriptor->getPointerTo(2)),
                            {IdxDescriptor, AIR.getInt(0) /* pointer */}
                        )
        ),
        B.CreateLoad(
            llvm::Type::getInt64Ty(AIR.getContext()),
            B.CreateGEP(
                TyBufferDescriptor, B.CreatePointerCast(IntPtr, TyBufferDescriptor->getPointerTo(2)),
                {IdxDescriptor, AIR.getInt(1) /* metadata */}
            )
        )
    };
  }

  llvm::Value *
  GetSparseBufferFeedbackHeader(
      llvm::air::AIRBuilder &AIR, llvm::Value *IntPtr, llvm::Value *Index, RangeId RangeId,
      uint32_t DescriptorOffset
  ) {
    // AIR uses four qwords; this is not the MSC three-qword descriptor ABI.
    auto &B = AIR.builder;
    auto TyDescriptor = llvm::ArrayType::get(B.getInt64Ty(), 4);
    auto IdxDescriptor = B.CreateAdd(B.CreateSub(Index, AIR.getInt(RangeId)), AIR.getInt(DescriptorOffset));
    return B.CreateLoad(B.getInt64Ty(), B.CreateGEP(
        TyDescriptor, B.CreatePointerCast(IntPtr, TyDescriptor->getPointerTo(2)),
        {IdxDescriptor, AIR.getInt(3)}));
  }

  llvm::Value *
  GetUAVCounterDescriptor(
      llvm::air::AIRBuilder &AIR, llvm::Value *IntPtr, llvm::Value *Index, RangeId RangeId, uint32_t DescriptorOffset
  ) {
    auto TyCounter = AIR.getIntTy()->getPointerTo(1);
    auto TyBufferDescriptor = llvm::StructType::get(
        AIR.getContext(), {llvm::Type::getInt64Ty(AIR.getContext()), llvm::Type::getInt64Ty(AIR.getContext()),
                           TyCounter, llvm::Type::getInt64Ty(AIR.getContext())}
    );
    auto &B = AIR.builder;
    auto IdxDescriptor = B.CreateAdd(B.CreateSub(Index, AIR.getInt(RangeId)), AIR.getInt(DescriptorOffset));
    return B.CreateLoad(
        TyCounter, B.CreateGEP(
                       TyBufferDescriptor, B.CreatePointerCast(IntPtr, TyBufferDescriptor->getPointerTo(2)),
                       {IdxDescriptor, AIR.getInt(2) /* metadata */}
                   )
    );
  }

  llvm::Value *
  GetRootConstantPointer(llvm::air::AIRBuilder &AIR, uint32_t TableIndex, uint32_t Index) {
    auto &B = AIR.builder;
    auto Fn = B.GetInsertBlock()->getParent();
    auto Arg = Fn->getArg(TableIndex);
    auto TyArg = llvm::cast<llvm::PointerType>(Arg->getType())->getNonOpaquePointerElementType();
    auto TyStruct = llvm::cast<llvm::StructType>(TyArg);
    return B.CreatePointerCast(B.CreateStructGEP(TyStruct, Arg, Index), AIR.getIntTy(4)->getPointerTo(2));
  };

  virtual llvm::Optional<ConstantBufferDescriptor>
  GetConstantBuffer(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) {
    if (~RootSignatureArgumentIndex == 0)
      return {};
    auto Iter = ConstantBuffers.find(Range);
    if (Iter == ConstantBuffers.end())
      return {};
    auto &CBuf = Iter->second.first;
    auto DescriptorOffset = Iter->second.second;
    if (DescriptorOffset == ~0u - 1) {
      auto Pointer = GetRootConstantPointer(Builder, RootSignatureArgumentIndex, CBuf.arg_index);
      return ConstantBufferDescriptor{Pointer, nullptr};
    }
    if (DescriptorOffset == ~0u) {
      auto Pointer = GetArgument(Builder, RootSignatureArgumentIndex, CBuf.arg_index);
      return ConstantBufferDescriptor{Pointer, nullptr};
    }
    auto HeapPointer = GetArgument(Builder, RootSignatureArgumentIndex, CBuf.arg_index);
    auto [Pointer, Metadata] = GetBufferDescriptor(
        Builder, HeapPointer, Index, Builder.getIntTy(4)->getPointerTo(2), CBuf.range.lower_bound, DescriptorOffset
    );
    return ConstantBufferDescriptor{Pointer, Metadata};
  }

  std::tuple<llvm::Value *, llvm::Value *, llvm::Value *, llvm::Value *>
  GetSamplerDescriptor(
      llvm::air::AIRBuilder &AIR, llvm::Value *IntPtr, llvm::Value *Index, RangeId RangeId, uint32_t DescriptorOffset
  ) {
    auto TySampleHandler = AIR.getSamplerHandleType();
    auto TySamplerDescriptor = llvm::StructType::get(
        AIR.getContext(), {TySampleHandler, TySampleHandler, llvm::Type::getInt64Ty(AIR.getContext()),
                           llvm::Type::getInt64Ty(AIR.getContext())}
    );
    auto &B = AIR.builder;
    auto IdxDescriptor = B.CreateAdd(B.CreateSub(Index, AIR.getInt(RangeId)), AIR.getInt(DescriptorOffset));
    return {
        B.CreateLoad(
            TySampleHandler, //
            B.CreateGEP(
                TySamplerDescriptor, B.CreatePointerCast(IntPtr, TySamplerDescriptor->getPointerTo(2)),
                {IdxDescriptor, AIR.getInt(0) /*sampler*/}
            )
        ),
        B.CreateLoad(
            TySampleHandler, //
            B.CreateGEP(
                TySamplerDescriptor, B.CreatePointerCast(IntPtr, TySamplerDescriptor->getPointerTo(2)),
                {IdxDescriptor, AIR.getInt(1) /* cube_sampler */}
            )
        ),
        B.CreateLoad(
            llvm::Type::getInt64Ty(AIR.getContext()),
            B.CreateGEP(
                TySamplerDescriptor, B.CreatePointerCast(IntPtr, TySamplerDescriptor->getPointerTo(2)),
                {IdxDescriptor, AIR.getInt(2) /* metadata*/}
            )
        ),
        B.CreateLoad(
            llvm::Type::getInt64Ty(AIR.getContext()),
            B.CreateGEP(
                TySamplerDescriptor, B.CreatePointerCast(IntPtr, TySamplerDescriptor->getPointerTo(2)),
                {IdxDescriptor, AIR.getInt(3) /* LOD clamps */}
            )
        )
    };
  }

  SamplerReductionState DecodeSamplerReductionState(
      llvm::air::AIRBuilder &Builder, llvm::Value *Metadata, llvm::Value *LODClamps, bool Unsupported) {
    auto &B = Builder.builder;
    return {
        B.CreateTrunc(B.CreateLShr(Metadata, 32), Builder.getIntTy()),
        B.CreateBitCast(B.CreateTrunc(LODClamps, Builder.getIntTy()), Builder.getFloatTy()),
        B.CreateBitCast(B.CreateTrunc(B.CreateLShr(LODClamps, 32), Builder.getIntTy()), Builder.getFloatTy()),
        Unsupported};
  }

  virtual llvm::Optional<SamplerDescriptor>
  GetSampler(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) {
    auto Iter = Samplers.find(Range);
    if (Iter == Samplers.end()) {
      return {};
    }
    auto &Sampler = Iter->second.first;
    auto DescriptorOffset = Iter->second.second;
    if (DescriptorOffset == ~0u) {
      if (~StaticSamplerArgumentIndex == 0)
        return {};
      auto [SamplerH, CubeSampler, Metadata, LODClamps] = GetSamplerDescriptor(
          Builder, Builder.builder.GetInsertBlock()->getParent()->getArg(StaticSamplerArgumentIndex), Index,
          Sampler.range.lower_bound, Sampler.arg_index
      );
      SamplerDescriptor result{SamplerH, CubeSampler, Metadata};
      if (auto reduction = Reductions.find(Range); reduction != Reductions.end()) {
        result.Reduction = DecodeSamplerReductionState(Builder, Metadata, LODClamps, reduction->second);
      }
      return result;
    }
    if (~RootSignatureArgumentIndex == 0)
      return {};

    auto HeapPointer = GetArgument(Builder, RootSignatureArgumentIndex, Sampler.arg_index);
    auto [SamplerH, CubeSampler, Metadata, LODClamps] =
        GetSamplerDescriptor(Builder, HeapPointer, Index, Sampler.range.lower_bound, DescriptorOffset);
    SamplerDescriptor result{SamplerH, CubeSampler, Metadata};
    if (Sampler.reduction_consumer_seen && Sampler.reduction_sampling_only) {
      result.Reduction = DecodeSamplerReductionState(Builder, Metadata, LODClamps, false);
      auto &B = Builder.builder;
      result.Reduction->RuntimePredicate = B.CreateICmpNE(
          B.CreateAnd(result.Reduction->Flags, Builder.getInt(dxmt::air::SamplerReduction)), Builder.getInt(0));
    }
    return result;
  }

  std::tuple<llvm::Value *, llvm::Value *, llvm::Value *>
  GetTextureDescriptor(
      llvm::air::AIRBuilder &AIR, llvm::Value *IntPtr, llvm::Value *Index, Texture::ResourceKind Kind, RangeId RangeId,
      uint32_t DescriptorOffset
  ) {
    auto TyTextureHandle = AIR.getTextureHandleType(Kind);
    auto TyTextureDescriptor = llvm::StructType::get(
        AIR.getContext(), {TyTextureHandle, llvm::Type::getInt64Ty(AIR.getContext()),
                           llvm::Type::getInt64Ty(AIR.getContext()), llvm::Type::getInt64Ty(AIR.getContext())}
    );
    auto &B = AIR.builder;
    auto IdxDescriptor = B.CreateAdd(B.CreateSub(Index, AIR.getInt(RangeId)), AIR.getInt(DescriptorOffset));
    return {
        B.CreateLoad(
            TyTextureHandle, //
            B.CreateGEP(
                TyTextureDescriptor, B.CreatePointerCast(IntPtr, TyTextureDescriptor->getPointerTo(2)),
                {IdxDescriptor, AIR.getInt(0) /* pointer */}
            )
        ),
        B.CreateLoad(
            llvm::Type::getInt64Ty(AIR.getContext()),
            B.CreateGEP(
                TyTextureDescriptor, B.CreatePointerCast(IntPtr, TyTextureDescriptor->getPointerTo(2)),
                {IdxDescriptor, AIR.getInt(1) /* metadata */}
            )
        ),
        B.CreateLoad(
            llvm::Type::getInt64Ty(AIR.getContext()),
            B.CreateGEP(TyTextureDescriptor, B.CreatePointerCast(IntPtr, TyTextureDescriptor->getPointerTo(2)),
                {IdxDescriptor, AIR.getInt(2) /* OOB default components */}))
    };
  }

  virtual llvm::Optional<TextureDescirptor>
  GetSRVTexture(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) {
    if (~RootSignatureArgumentIndex == 0)
      return {};
    auto Iter = SRVs.find(Range);
    if (Iter == SRVs.end())
      return {};
    auto &SRV = Iter->second.first;
    if (SRV.resource_type == shader::common::ResourceType::NonApplicable)
      return {};
    auto DescriptorOffset = Iter->second.second;
    auto HeapPointer = GetArgument(Builder, RootSignatureArgumentIndex, SRV.arg_index);
    if (DescriptorOffset == ~0u)
      return {}; // root descriptor can only be a buffer
    auto ResourceKindLogical = air::to_air_resource_type(SRV.resource_type, SRV.compared);
    auto ResourceKind = air::lowering_texture_1d_to_2d(ResourceKindLogical);
    auto SampleType = std::visit(
        patterns{
            [](air::MSLInt) { return Texture::sample_int; }, [](air::MSLUint) { return Texture::sample_uint; },
            [](auto) { return Texture::sample_float; }
        },
        air::to_air_scaler_type(SRV.scaler_type)
    );
    auto MemoryAccess = SRV.sampled ? Texture::MemoryAccess::access_sample : Texture::MemoryAccess::access_read;

    auto [Handle, Metadata, DefaultComponents] =
        GetTextureDescriptor(Builder, HeapPointer, Index, ResourceKind, SRV.range.lower_bound, DescriptorOffset);
    return TextureDescirptor{Handle, Metadata, false, ResourceKind, ResourceKindLogical, MemoryAccess, SampleType,
        DefaultComponents};
  }

  virtual llvm::Optional<TextureDescirptor>
  GetUAVTexture(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) {
    if (~RootSignatureArgumentIndex == 0)
      return {};
    auto Iter = UAVs.find(Range);
    if (Iter == UAVs.end())
      return {};
    auto &UAV = Iter->second.first;
    if (UAV.resource_type == shader::common::ResourceType::NonApplicable)
      return {};
    auto DescriptorOffset = Iter->second.second;
    auto HeapPointer = GetArgument(Builder, RootSignatureArgumentIndex, UAV.arg_index);
    if (DescriptorOffset == ~0u)
      return {}; // root descriptor can only be a buffer
    auto ResourceKindLogical = air::to_air_resource_type(UAV.resource_type);
    auto ResourceKind = air::lowering_texture_1d_to_2d(ResourceKindLogical);
    auto SampleType = std::visit(
        patterns{
            [](air::MSLInt) { return Texture::sample_int; }, [](air::MSLUint) { return Texture::sample_uint; },
            [](auto) { return Texture::sample_float; }
        },
        air::to_air_scaler_type(UAV.scaler_type)
    );
    auto MemoryAccess = UAV.written
                            ? (UAV.read ? Texture::MemoryAccess::acesss_readwrite : Texture::MemoryAccess::access_write)
                            : Texture::MemoryAccess::access_read;
    auto [Handle, Metadata, DefaultComponents] =
        GetTextureDescriptor(Builder, HeapPointer, Index, ResourceKind, UAV.range.lower_bound, DescriptorOffset);
    return TextureDescirptor{Handle,       Metadata,  UAV.global_coherent, ResourceKind, ResourceKindLogical,
                             MemoryAccess, SampleType};
  }

  virtual llvm::Optional<BufferDescriptor>
  GetSRVBuffer(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) {
    if (~RootSignatureArgumentIndex == 0)
      return {};
    auto Iter = SRVs.find(Range);
    if (Iter == SRVs.end())
      return {};
    auto &SRV = Iter->second.first;
    if (SRV.resource_type != shader::common::ResourceType::NonApplicable)
      return {};
    auto DescriptorOffset = Iter->second.second;
    auto HeapPointer = GetArgument(Builder, RootSignatureArgumentIndex, SRV.arg_index);
    if (DescriptorOffset == ~0u) {
      if (SRV.buffer_feedback && RootFeedbackArgumentIndex != ~0u)
        return GetRootFeedbackDescriptor(Builder, HeapPointer, SRV.structure_stride, false);
      return BufferDescriptor{HeapPointer, Builder.builder.getInt64(0xffffffff), SRV.structure_stride, false};
    }
    auto [Pointer, Metadata] = GetBufferDescriptor(
        Builder, HeapPointer, Index, Builder.getIntTy()->getPointerTo(1), SRV.range.lower_bound, DescriptorOffset
    );

    return BufferDescriptor{Pointer, Metadata, SRV.structure_stride, false,
        GetSparseBufferFeedbackHeader(Builder, HeapPointer, Index, SRV.range.lower_bound, DescriptorOffset)};
  }

  virtual llvm::Optional<BufferDescriptor>
  GetUAVBuffer(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) {
    if (~RootSignatureArgumentIndex == 0)
      return {};
    auto Iter = UAVs.find(Range);
    if (Iter == UAVs.end())
      return {};
    auto &UAV = Iter->second.first;
    if (UAV.resource_type != shader::common::ResourceType::NonApplicable)
      return {};
    auto DescriptorOffset = Iter->second.second;
    auto HeapPointer = GetArgument(Builder, RootSignatureArgumentIndex, UAV.arg_index);
    if (DescriptorOffset == ~0u) {
      if (UAV.buffer_feedback && RootFeedbackArgumentIndex != ~0u)
        return GetRootFeedbackDescriptor(Builder, HeapPointer, UAV.structure_stride, UAV.global_coherent);
      return BufferDescriptor{
          HeapPointer, Builder.builder.getInt64(0xffffffff), UAV.structure_stride, UAV.global_coherent
      };
    }
    auto [Pointer, Metadata] = GetBufferDescriptor(
        Builder, HeapPointer, Index, Builder.getIntTy()->getPointerTo(1), UAV.range.lower_bound, DescriptorOffset
    );

    return BufferDescriptor{Pointer, Metadata, UAV.structure_stride, UAV.global_coherent,
        GetSparseBufferFeedbackHeader(Builder, HeapPointer, Index, UAV.range.lower_bound, DescriptorOffset)};
  }

  virtual llvm::Optional<CounterDescriptor>
  GetUAVCounter(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) {
    if (~RootSignatureArgumentIndex == 0)
      return {};
    auto Iter = UAVs.find(Range);
    if (Iter == UAVs.end())
      return {};
    auto &UAV = Iter->second.first;
    if (UAV.resource_type != shader::common::ResourceType::NonApplicable)
      return {};
    auto DescriptorOffset = Iter->second.second;
    auto HeapPointer = GetArgument(Builder, RootSignatureArgumentIndex, UAV.arg_index);
    if (DescriptorOffset == ~0u)
      return {}; // root descriptor can not have counters associated
    auto Pointer = GetUAVCounterDescriptor(Builder, HeapPointer, Index, UAV.range.lower_bound, DescriptorOffset);

    return CounterDescriptor{Pointer};
  }

  uint32_t RootSignatureArgumentIndex = ~0u;
  uint32_t StaticSamplerArgumentIndex = ~0u;

  std::map<RangeId, std::pair<ConstantBufferInfo, uint64_t>> ConstantBuffers;
  std::map<RangeId, std::pair<SamplerInfo, uint64_t>> Samplers;
  std::map<RangeId, bool> Reductions;
  std::map<RangeId, std::pair<ShaderResourceViewInfo, uint64_t>> SRVs;
  std::map<RangeId, std::pair<UnorderedAccessViewInfo, uint64_t>> UAVs;
};

std::unique_ptr<BindingMap>
setup_binding_rootsig(
    const ShaderInfo *shader_info, air::FunctionSignatureBuilder &func_signature, llvm::Module &module,
    microsoft::D3D10_SB_TOKENIZED_PROGRAM_TYPE shader_type, const void *bytecode, size_t bytecode_length,
    uint32_t root_sig_slot, uint32_t static_sampler_slot
) {
  using namespace microsoft;
  using namespace dxmt::dxbc;
  using namespace dxmt::air;
  using namespace dxmt::shader::common;

  auto binding_map = std::make_unique<RootSignatureBindingMap>();

  const void *pRawRootSig;
  UINT RawRootSigSize;
  HRESULT hr = microsoft::DXBCGetRootSignature(bytecode, &pRawRootSig, &RawRootSigSize);
  if (FAILED(hr)) {
    assert(0 && "invalid root signature blob");
    return {};
  }

  RootSignatureDeserializer deserializer;
  hr = deserializer.Deserialize(pRawRootSig, RawRootSigSize);
  if (FAILED(hr)) {
    assert(0 && "invalid root signature");
    return {};
  }

  auto &root_sig = deserializer.desc_1_1_.Desc_1_1;

  air::ArgumentBufferBuilder builder;
  unsigned inc_attribute_index = 0;

  auto check_visibility = [=](D3D12_SHADER_VISIBILITY visibility) -> bool {
    if (visibility == D3D12_SHADER_VISIBILITY_ALL)
      return true;
    switch (visibility) {
    case D3D12_SHADER_VISIBILITY_VERTEX:
      if (shader_type == D3D10_SB_VERTEX_SHADER)
        return true;
      break;
    case D3D12_SHADER_VISIBILITY_HULL:
      if (shader_type == D3D11_SB_HULL_SHADER)
        return true;
      break;
    case D3D12_SHADER_VISIBILITY_DOMAIN:
      if (shader_type == D3D11_SB_DOMAIN_SHADER)
        return true;
      break;
    case D3D12_SHADER_VISIBILITY_GEOMETRY:
      if (shader_type == D3D10_SB_GEOMETRY_SHADER)
        return true;
      break;
    case D3D12_SHADER_VISIBILITY_PIXEL:
      if (shader_type == D3D10_SB_PIXEL_SHADER)
        return true;
      break;
    default:
      break;
    }
    return false;
  };

  auto range_contained = [=](const ResourceRange &range, const D3D12_DESCRIPTOR_RANGE1 &range_rootsig) -> bool {
    if (range.lower_bound < range_rootsig.BaseShaderRegister)
      return false;
    if (range_rootsig.NumDescriptors == ~0u)
      return true;
    if (range.size == ~0u)
      return true;
    if (range.lower_bound + range.size <= range_rootsig.BaseShaderRegister + range_rootsig.NumDescriptors)
      return true;
    return false;
  };

  for (unsigned i = 0; i < root_sig.NumParameters; i++) {
    auto &Parameter = root_sig.pParameters[i];

    switch (Parameter.ParameterType) {
    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
      auto &table = Parameter.DescriptorTable;
      unsigned descriptor_offset = 0;
      auto table_index = builder.DefineBuffer(
          "table" + std::to_string(i), AddressSpace::constant, MemoryAccess::read, msl_int, inc_attribute_index++
      );
      if (!check_visibility(Parameter.ShaderVisibility))
        break;
      for (unsigned i = 0; i < table.NumDescriptorRanges; i++) {
        auto &range = table.pDescriptorRanges[i];
        auto calculated_descriptor_offset = range.OffsetInDescriptorsFromTableStart == ~0u
                                                ? descriptor_offset
                                                : range.OffsetInDescriptorsFromTableStart;
        descriptor_offset = calculated_descriptor_offset + range.NumDescriptors;
        switch (range.RangeType) {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: {
          for (auto &[_, srv] : shader_info->srvMap) {
            if (srv.range.space != range.RegisterSpace)
              continue;
            if (range_contained(srv.range, range)) {
              auto range_id = srv.range.range_id;
              binding_map->SRVs[range_id] = {
                  srv, calculated_descriptor_offset + (srv.range.lower_bound - range.BaseShaderRegister)
              };
              binding_map->SRVs[range_id].first.arg_index = table_index;
            }
          }
          break;
        }
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
          for (auto &[_, uav] : shader_info->uavMap) {
            if (uav.range.space != range.RegisterSpace)
              continue;
            if (range_contained(uav.range, range)) {
              auto range_id = uav.range.range_id;
              binding_map->UAVs[range_id] = {
                  uav, calculated_descriptor_offset + (uav.range.lower_bound - range.BaseShaderRegister)
              };
              binding_map->UAVs[range_id].first.arg_index = table_index;
            }
          }
          break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
          for (auto &[_, cbuf] : shader_info->cbufferMap) {
            if (cbuf.range.space != range.RegisterSpace)
              continue;
            if (range_contained(cbuf.range, range)) {
              auto range_id = cbuf.range.range_id;
              binding_map->ConstantBuffers[range_id] = {
                  cbuf, calculated_descriptor_offset + (cbuf.range.lower_bound - range.BaseShaderRegister)
              };
              binding_map->ConstantBuffers[range_id].first.arg_index = table_index;
            }
          }
          break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
          for (auto &[_, sampler] : shader_info->samplerMap) {
            if (sampler.range.space != range.RegisterSpace)
              continue;
            if (range_contained(sampler.range, range)) {
              auto range_id = sampler.range.range_id;
              binding_map->Samplers[range_id] = {
                  sampler, calculated_descriptor_offset + (sampler.range.lower_bound - range.BaseShaderRegister)
              };
              binding_map->Samplers[range_id].first.arg_index = table_index;
            }
          }
          break;
        default:
          assert(0 && "unknown range type");
          return {};
        }
      }
      break;
    }
    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: {
      auto &constants = Parameter.Constants;
      auto qwords = (constants.Num32BitValues >> 1) + (constants.Num32BitValues & 1);
      auto first = builder.DefineInteger64("root_constant" + std::to_string(i), inc_attribute_index++);
      for (unsigned x = 1; x < qwords; x++) {
        builder.DefineInteger64("root_constant" + std::to_string(i) + "_" + std::to_string(x), inc_attribute_index++);
      }
      if (!check_visibility(Parameter.ShaderVisibility))
        break;
      for (auto &[_, cbuf] : shader_info->cbufferMap) {
        if (cbuf.range.lower_bound == constants.ShaderRegister && cbuf.range.space == constants.RegisterSpace) {
          auto range_id = cbuf.range.range_id;
          binding_map->ConstantBuffers[range_id] = {cbuf, (~0u - 1)};
          binding_map->ConstantBuffers[range_id].first.arg_index = first;
        }
      }
      break;
    }
    case D3D12_ROOT_PARAMETER_TYPE_CBV: {
      auto &descriptor = Parameter.Descriptor;
      auto descriptor_index = builder.DefineBuffer(
          "cb" + std::to_string(i), AddressSpace::constant, MemoryAccess::read, msl_uint4, inc_attribute_index++
      );
      if (!check_visibility(Parameter.ShaderVisibility))
        break;
      for (auto &[_, cbuf] : shader_info->cbufferMap) {
        if (cbuf.range.lower_bound == descriptor.ShaderRegister && cbuf.range.space == descriptor.RegisterSpace) {
          auto range_id = cbuf.range.range_id;
          binding_map->ConstantBuffers[range_id] = {cbuf, ~0u};
          binding_map->ConstantBuffers[range_id].first.arg_index = descriptor_index;
        }
      }
      break;
    }
    case D3D12_ROOT_PARAMETER_TYPE_SRV: {
      auto &descriptor = Parameter.Descriptor;
      auto descriptor_index = builder.DefineBuffer(
          "t" + std::to_string(i), AddressSpace::device, MemoryAccess::read, msl_uint, inc_attribute_index++
      );
      if (!check_visibility(Parameter.ShaderVisibility))
        break;
      for (auto &[_, srv] : shader_info->srvMap) {
        if (srv.range.lower_bound == descriptor.ShaderRegister && srv.range.space == descriptor.RegisterSpace) {
          assert(srv.resource_type == shader::common::ResourceType::NonApplicable);
          auto range_id = srv.range.range_id;
          binding_map->SRVs[range_id] = {srv, ~0u};
          binding_map->SRVs[range_id].first.arg_index = descriptor_index;
        }
      }
      break;
    }
    case D3D12_ROOT_PARAMETER_TYPE_UAV: {
      auto &descriptor = Parameter.Descriptor;
      auto descriptor_index = builder.DefineBuffer(
          "u" + std::to_string(i), AddressSpace::device, MemoryAccess::read_write /* FIXME */, msl_uint,
          inc_attribute_index++, std::nullopt /* FIXME: */
      );
      if (!check_visibility(Parameter.ShaderVisibility))
        break;
      for (auto &[_, uav] : shader_info->uavMap) {
        if (uav.range.lower_bound == descriptor.ShaderRegister && uav.range.space == descriptor.RegisterSpace) {
          assert(uav.resource_type == shader::common::ResourceType::NonApplicable);
          auto range_id = uav.range.range_id;
          binding_map->UAVs[range_id] = {uav, ~0u};
          binding_map->UAVs[range_id].first.arg_index = descriptor_index;
        }
      }
      break;
    }
    }
  }

  {
    bool root_feedback = false;
    for (const auto &[range, binding] : binding_map->SRVs)
      root_feedback |= binding.second == ~0u && binding.first.buffer_feedback;
    for (const auto &[range, binding] : binding_map->UAVs)
      root_feedback |= binding.second == ~0u && binding.first.buffer_feedback;
    if (root_feedback)
      binding_map->RootFeedbackArgumentIndex = func_signature.DefineInput(air::ArgumentBindingBuffer{
          .buffer_size = {}, .location_index = SM50_BINDING_INDEX_ROOT_BUFFER_FEEDBACK, .array_size = 0,
          .memory_access = air::MemoryAccess::read, .address_space = air::AddressSpace::constant,
          .type = air::msl_ulong, .arg_name = "root_feedback", .raster_order_group = {},
      });
  }
  auto [type, metadata] = builder.Build(module.getContext(), module.getDataLayout());

  binding_map->RootSignatureArgumentIndex = func_signature.DefineInput(air::ArgumentBindingIndirectBuffer{
      .location_index = root_sig_slot, // SM50_BINDING_INDEX_ROOT_ARGUMENTS
      .array_size = 1,
      .memory_access = air::MemoryAccess::read,
      .address_space = air::AddressSpace::constant,
      .struct_type = type,
      .struct_type_info = metadata,
      .arg_name = "rootsig",
  });

  if (root_sig.NumStaticSamplers == 0)
    return binding_map;

  for (unsigned i = 0; i < root_sig.NumStaticSamplers; i++) {
    auto &State = root_sig.pStaticSamplers[i];

    for (auto &[_, sampler] : shader_info->samplerMap) {
      if (sampler.range.space != State.RegisterSpace)
        continue;
      if (sampler.range.lower_bound != State.ShaderRegister)
        continue;
      if (!check_visibility(State.ShaderVisibility))
        break;
      auto range_id = sampler.range.range_id;
      binding_map->Samplers[range_id] = {sampler, ~0u};
      binding_map->Samplers[range_id].first.arg_index = i;
      const auto reduction = D3D12_DECODE_FILTER_REDUCTION(State.Filter);
      if (reduction == D3D12_FILTER_REDUCTION_TYPE_MINIMUM || reduction == D3D12_FILTER_REDUCTION_TYPE_MAXIMUM) {
        binding_map->Reductions[range_id] =
            D3D12_DECODE_IS_ANISOTROPIC_FILTER(State.Filter) || sampler.range.size != 1;
      }
    }
  }

  binding_map->StaticSamplerArgumentIndex = func_signature.DefineInput(air::ArgumentBindingBuffer{
      .buffer_size = {},
      .location_index = static_sampler_slot, // SM50_BINDING_INDEX_STATIC_SAMPLERS
      .array_size = 0,
      .memory_access = air::MemoryAccess::read,
      .address_space = air::AddressSpace::constant,
      .type = air::msl_ulong,
      .arg_name = "static_samplers",
      .raster_order_group = {},
  });

  return binding_map;
}

} // namespace dxmt::dxbc

AIRCONV_API int SM50UsesRootBufferFeedback(sm50_shader_t shader, const void *bytecode, size_t size) {
  if (!shader || !bytecode || !size) return -1;
  microsoft::CDXBCParser parser;
  if (parser.ReadDXBC(bytecode, size) != S_OK) return -1;
  const void *raw = nullptr;
  UINT raw_size = 0;
  if (FAILED(microsoft::DXBCGetRootSignature(bytecode, &raw, &raw_size))) return -1;
  using namespace dxmt;
  auto bounded = [&](uint32_t offset, uint32_t count, size_t stride) {
    return offset % alignof(uint32_t) == 0 && offset <= raw_size && count <= (raw_size - offset) / stride;
  };
  if (raw_size < sizeof(RawRootSignatureDesc)) return -1;
  const auto &header = *static_cast<const RawRootSignatureDesc *>(raw);
  if ((header.Version != 1 && header.Version != 2) ||
      !bounded(header.RootParametersOffset, header.NumParameters, sizeof(RawRootParameter)) ||
      !bounded(header.StaticSamplersOffset, header.NumStaticSamplers, sizeof(D3D12_STATIC_SAMPLER_DESC))) return -1;
  const auto parameters = reinterpret_cast<const RawRootParameter *>(
      static_cast<const char *>(raw) + header.RootParametersOffset);
  for (uint32_t i = 0; i < header.NumParameters; ++i) {
    const auto &parameter = parameters[i];
    size_t payload_size = 0;
    switch (parameter.ParameterType) {
    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {
      if (!bounded(parameter.PayloadOffset, 1, sizeof(RawRootDescriptorTable))) return -1;
      const auto &table = *reinterpret_cast<const RawRootDescriptorTable *>(
          static_cast<const char *>(raw) + parameter.PayloadOffset);
      if (!bounded(table.DescriptorRangesOffset, table.NumDescriptorRanges,
                   header.Version == 1 ? sizeof(D3D12_DESCRIPTOR_RANGE) : sizeof(D3D12_DESCRIPTOR_RANGE1))) return -1;
      break;
    }
    case D3D12_ROOT_PARAMETER_TYPE_CBV:
    case D3D12_ROOT_PARAMETER_TYPE_SRV:
    case D3D12_ROOT_PARAMETER_TYPE_UAV:
      payload_size = header.Version == 1 ? sizeof(D3D12_ROOT_DESCRIPTOR) : sizeof(D3D12_ROOT_DESCRIPTOR1);
      break;
    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: payload_size = sizeof(D3D12_ROOT_CONSTANTS); break;
    default: return -1;
    }
    if (payload_size && !bounded(parameter.PayloadOffset, 1, payload_size)) return -1;
  }
  {
    dxmt::RootSignatureDeserializer deserializer;
    if (FAILED(deserializer.Deserialize(raw, raw_size))) return -1;
    const auto *internal = static_cast<dxmt::dxbc::SM50ShaderInternal *>(shader);
    const auto &desc = deserializer.desc_1_1_.Desc_1_1;
    auto visible = [&](D3D12_SHADER_VISIBILITY visibility) {
      using namespace microsoft;
      switch (visibility) {
      case D3D12_SHADER_VISIBILITY_ALL: return true;
      case D3D12_SHADER_VISIBILITY_VERTEX: return internal->shader_type == D3D10_SB_VERTEX_SHADER;
      case D3D12_SHADER_VISIBILITY_PIXEL: return internal->shader_type == D3D10_SB_PIXEL_SHADER;
      case D3D12_SHADER_VISIBILITY_GEOMETRY: return internal->shader_type == D3D10_SB_GEOMETRY_SHADER;
      case D3D12_SHADER_VISIBILITY_HULL: return internal->shader_type == D3D11_SB_HULL_SHADER;
      case D3D12_SHADER_VISIBILITY_DOMAIN: return internal->shader_type == D3D11_SB_DOMAIN_SHADER;
      default: return false;
      }
    };
    auto consumes = [](const auto &resources, const D3D12_ROOT_DESCRIPTOR1 &root) {
      for (const auto &[id, resource] : resources)
        if (resource.buffer_feedback && resource.range.lower_bound == root.ShaderRegister &&
            resource.range.space == root.RegisterSpace) return true;
      return false;
    };
    for (UINT i = 0; i < desc.NumParameters; ++i) {
      const auto &parameter = desc.pParameters[i];
      if (!visible(parameter.ShaderVisibility)) continue;
      if (parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV &&
          consumes(internal->shader_info.srvMap, parameter.Descriptor)) return 1;
      if (parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_UAV &&
          consumes(internal->shader_info.uavMap, parameter.Descriptor)) return 1;
    }
    return 0;
  }
}

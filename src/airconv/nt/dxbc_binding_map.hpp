#pragma once

#include "air_builder.hpp"
#include "llvm/IR/Value.h"
#include <optional>

namespace dxmt::dxbc {

struct ConstantBufferDescriptor {
  llvm::Value *Pointer;
  llvm::Value *Metadata; // may be null
};

struct SamplerReductionState {
  llvm::Value *Flags;
  llvm::Value *MinLOD;
  llvm::Value *MaxLOD;
  bool Unsupported = false;
  // Null for a statically known reduction sampler; otherwise an i1 decoded
  // from the live descriptor tag. Only one sampling operation may execute.
  llvm::Value *RuntimePredicate = nullptr;
};

struct SamplerDescriptor {
  llvm::Value *SamplerHandle;
  llvm::Value *CubeSamplerHandle;
  llvm::Value *Metadata;
  std::optional<SamplerReductionState> Reduction;
};

struct TextureDescirptor {
  llvm::Value *ResourceHandle;
  llvm::Value *Metadata;
  bool GlobalCoherent;
  llvm::air::Texture::ResourceKind ResourceKind;
  llvm::air::Texture::ResourceKind ResourceKindLogical;
  llvm::air::Texture::MemoryAccess MemoryAccess;
  llvm::air::Texture::SampleType SampleType;

};

struct BufferDescriptor {
  llvm::Value *Pointer;
  llvm::Value *Metadata;
  uint32_t StructureStride;
  bool GlobalCoherent;
};

struct CounterDescriptor {
  llvm::Value *Pointer;
};

using RangeId = uint32_t;

class BindingMap {
public:
  virtual ~BindingMap() {};
  virtual llvm::Optional<ConstantBufferDescriptor>
  GetConstantBuffer(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) = 0;
  virtual llvm::Optional<SamplerDescriptor>
  GetSampler(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) = 0;
  virtual llvm::Optional<TextureDescirptor>
  GetSRVTexture(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) = 0;
  virtual llvm::Optional<TextureDescirptor>
  GetUAVTexture(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) = 0;
  virtual llvm::Optional<BufferDescriptor>
  GetSRVBuffer(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) = 0;
  virtual llvm::Optional<BufferDescriptor>
  GetUAVBuffer(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) = 0;
  virtual llvm::Optional<CounterDescriptor>
  GetUAVCounter(llvm::air::AIRBuilder &Builder, RangeId Range, llvm::Value *Index) = 0;
};

} // namespace dxmt::dxbc

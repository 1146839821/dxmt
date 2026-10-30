#include "msc_sample_level_lod.hpp"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

namespace dxmt::air {
namespace {
struct Sample {
  llvm::CallInst *call;
  llvm::GetElementPtrInst *texture_pointer;
  llvm::GetElementPtrInst *sampler_pointer;
  llvm::Argument *root;
  unsigned sidecar_offset;
};

bool
descriptorLayout(llvm::Module &module, llvm::GetElementPtrInst *gep) {
  auto *structure = gep ? llvm::dyn_cast<llvm::StructType>(gep->getSourceElementType()) : nullptr;
  if (!structure || !structure->isSized() || structure->getNumElements() != 3)
    return false;
  const auto *layout = module.getDataLayout().getStructLayout(structure);
  return layout->getSizeInBytes() == 24 && layout->getElementOffset(0) == 0 && layout->getElementOffset(1) == 8 &&
         layout->getElementOffset(2) == 16;
}

bool
compatibleDeclaration(llvm::Module &module, llvm::StringRef name, llvm::FunctionType *type) {
  auto *value = module.getNamedValue(name);
  if (!value)
    return true;
  auto *function = llvm::dyn_cast<llvm::Function>(value);
  return function && function->getFunctionType() == type;
}

llvm::GetElementPtrInst *
descriptorPointer(llvm::CallInst &call) {
  auto *texture = llvm::dyn_cast<llvm::LoadInst>(call.getArgOperand(0));
  if (!texture)
    return nullptr;
  auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(texture->getPointerOperand()->stripPointerCasts());
  if (!gep || gep->getNumIndices() != 2)
    return nullptr;
  auto *field = llvm::dyn_cast<llvm::ConstantInt>(gep->getOperand(2));
  auto *structure = llvm::dyn_cast<llvm::StructType>(gep->getSourceElementType());
  if (!field || !field->equalsInt(1) || !structure || structure->getNumElements() != 3 ||
      !structure->getElementType(2)->isIntegerTy(64))
    return nullptr;
  return gep;
}

llvm::GetElementPtrInst *
samplerPointer(llvm::CallInst &call, llvm::Argument *root, unsigned expected_offset) {
  auto *sampler = llvm::dyn_cast<llvm::LoadInst>(call.getArgOperand(1));
  llvm::Value *sampler_address = sampler ? sampler->getPointerOperand() : nullptr;
  while (auto *cast = llvm::dyn_cast_or_null<llvm::BitCastInst>(sampler_address))
    sampler_address = cast->getOperand(0);
  auto *gep = llvm::dyn_cast_or_null<llvm::GetElementPtrInst>(sampler_address);
  if (!gep || gep->getNumIndices() != 2)
    return nullptr;
  auto *field = llvm::dyn_cast<llvm::ConstantInt>(gep->getOperand(2));
  auto *structure = llvm::dyn_cast<llvm::StructType>(gep->getSourceElementType());
  if (!field || !field->isZero() || !structure || structure->getNumElements() != 3 ||
      structure->getElementType(0) != sampler->getType() || !structure->getElementType(1)->isIntegerTy(64) ||
      !structure->getElementType(2)->isIntegerTy(64))
    return nullptr;
  auto *table = llvm::dyn_cast<llvm::LoadInst>(gep->getPointerOperand());
  if (!table)
    return nullptr;
  auto *slot = table->getPointerOperand()->stripPointerCasts();
  if (slot == root)
    return expected_offset == 0 ? gep : nullptr;
  auto *offset = llvm::dyn_cast<llvm::GetElementPtrInst>(slot);
  if (!offset || offset->getPointerOperand()->stripPointerCasts() != root || offset->getNumIndices() != 1 ||
      !offset->getSourceElementType()->isIntegerTy(8))
    return nullptr;
  auto *constant = llvm::dyn_cast<llvm::ConstantInt>(offset->getOperand(1));
  return constant && constant->equalsInt(expected_offset) ? gep : nullptr;
}
} // namespace

MSCSampleLevelLODResult
patchMSCSampleLevelLOD(llvm::Module &module, MSCSamplerSidecarLayout layout) {
  llvm::SmallVector<Sample, 8> samples;
  if ((!layout.tables && layout.table_count) || (layout.original_root_size % 8))
    return MSCSampleLevelLODResult::Unsupported;
  for (std::size_t i = 0; i < layout.table_count; ++i) {
    if ((layout.tables[i].sampler_table_offset % 8) || (layout.tables[i].sidecar_offset % 8) ||
        layout.tables[i].sampler_table_offset >= layout.original_root_size ||
        layout.tables[i].sidecar_offset < layout.original_root_size)
      return MSCSampleLevelLODResult::Unsupported;
    for (std::size_t j = 0; j < layout.table_count; ++j)
      if (layout.tables[i].sidecar_offset == layout.tables[j].sampler_table_offset ||
          (i != j && (layout.tables[i].sampler_table_offset == layout.tables[j].sampler_table_offset ||
                      layout.tables[i].sidecar_offset == layout.tables[j].sidecar_offset)))
        return MSCSampleLevelLODResult::Unsupported;
  }
  for (auto &function : module)
    for (auto &block : function)
      for (auto &inst : block) {
        auto *call_base = llvm::dyn_cast<llvm::CallBase>(&inst);
        auto *call = llvm::dyn_cast<llvm::CallInst>(&inst);
        auto *target = call_base ? call_base->getCalledFunction() : nullptr;
        if (layout.require_exclusive_texture_operations && call_base && (!call || !target))
          return MSCSampleLevelLODResult::Unsupported;
        // Only the recognized sample intrinsic and LLVM intrinsics are known
        // external calls here. An opaque helper could hide texture operations.
        if (layout.require_exclusive_texture_operations && target && target->isDeclaration() &&
            !target->isIntrinsic() && target->getName() != "air.sample_texture_2d.v4f32")
          return MSCSampleLevelLODResult::Unsupported;
        if (layout.require_exclusive_texture_operations && target && !target->isDeclaration() &&
            target->getName() == "air.sample_texture_2d.v4f32")
          return MSCSampleLevelLODResult::Unsupported;
        if (!target || target->getName() != "air.sample_texture_2d.v4f32") {
          if (layout.require_exclusive_texture_operations && target && target->getName().startswith("air.") &&
              (target->getName().contains("texture") || target->getName().startswith("air.sample") ||
               target->getName().startswith("air.gather")))
            return MSCSampleLevelLODResult::Unsupported;
          continue;
        }
        if (call->arg_size() != 9)
          return MSCSampleLevelLODResult::Unsupported;
        auto *explicit_level = llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(5));
        if (!explicit_level)
          return MSCSampleLevelLODResult::Unsupported;
        if (explicit_level->isZero() && layout.require_exclusive_texture_operations)
          return MSCSampleLevelLODResult::Unsupported;
        if (explicit_level->isZero())
          continue;
        if (!explicit_level->isOne())
          return MSCSampleLevelLODResult::Unsupported;
        auto *pointer = descriptorPointer(*call);
        if (layout.root_argument_index >= function.arg_size())
          return MSCSampleLevelLODResult::Unsupported;
        auto *root = function.getArg(layout.root_argument_index);
        llvm::GetElementPtrInst *sampler_pointer = nullptr;
        unsigned sidecar_offset = 0;
        for (std::size_t i = 0; i < layout.table_count; ++i) {
          if (auto *candidate = samplerPointer(*call, root, layout.tables[i].sampler_table_offset)) {
            sampler_pointer = candidate;
            sidecar_offset = layout.tables[i].sidecar_offset;
            break;
          }
        }
        if (!descriptorLayout(module, pointer) || !descriptorLayout(module, sampler_pointer) ||
            !root->getType()->isPointerTy() || root->getType()->getPointerAddressSpace() != 2 ||
            !call->getArgOperand(6)->getType()->isFloatTy())
          return MSCSampleLevelLODResult::Unsupported;
        auto *i32 = llvm::Type::getInt32Ty(module.getContext());
        auto *coordinates = llvm::FixedVectorType::get(i32, 2);
        auto *texture = call->getArgOperand(0)->getType();
        auto *sampler = call->getArgOperand(1)->getType();
        if (!compatibleDeclaration(
                module, "air.get_num_mip_levels_texture_2d", llvm::FunctionType::get(i32, {texture}, false)
            ) ||
            !compatibleDeclaration(module, "air.get_read_sampler", llvm::FunctionType::get(sampler, false)) ||
            !compatibleDeclaration(
                module, "air.read_texture_2d.v4f32",
                llvm::FunctionType::get(call->getType(), {texture, sampler, coordinates, coordinates, i32, i32}, false)
            ))
          return MSCSampleLevelLODResult::Unsupported;
        samples.push_back({call, pointer, sampler_pointer, root, sidecar_offset});
      }
  if (samples.empty())
    return MSCSampleLevelLODResult::Unchanged;

  auto &context = module.getContext();
  auto *i32 = llvm::Type::getInt32Ty(context);
  auto *i64 = llvm::Type::getInt64Ty(context);
  auto *f32 = llvm::Type::getFloatTy(context);
  auto *coordinates = llvm::FixedVectorType::get(i32, 2);
  for (const auto &sample : samples) {
    llvm::IRBuilder<> builder(sample.call);
    auto *texture = sample.call->getArgOperand(0);
    auto *gep = sample.texture_pointer;
    auto *metadata_ptr = builder.CreateGEP(
        gep->getSourceElementType(), gep->getPointerOperand(), {gep->getOperand(1), builder.getInt32(2)},
        "lod.metadata.ptr"
    );
    auto *metadata = builder.CreateLoad(i64, metadata_ptr, "lod.metadata");
    auto *clamp = builder.CreateBitCast(builder.CreateTrunc(metadata, i32), f32, "lod.minimum");
    auto *maximum = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::maxnum, {f32});
    auto *minimum = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::minnum, {f32});
    auto *sampler_type = sample.call->getArgOperand(1)->getType();
    auto *sidecar_type = llvm::StructType::get(context, {sampler_type, f32, f32});
    auto *sidecar_ptr_type = llvm::PointerType::get(sidecar_type, 2);
    auto *root_bytes = builder.CreateBitCast(sample.root, llvm::PointerType::get(builder.getInt8Ty(), 2));
    auto *slot_bytes = builder.CreateGEP(builder.getInt8Ty(), root_bytes, builder.getInt64(sample.sidecar_offset));
    auto *sidecar_slot = builder.CreateBitCast(slot_bytes, llvm::PointerType::get(sidecar_ptr_type, 2));
    auto *sidecar = builder.CreateLoad(sidecar_ptr_type, sidecar_slot);
    auto *entry = builder.CreateGEP(sidecar_type, sidecar, sample.sampler_pointer->getOperand(1));
    auto *relaxed_sampler = builder.CreateLoad(sampler_type, builder.CreateStructGEP(sidecar_type, entry, 0));
    auto *sampler_min = builder.CreateLoad(f32, builder.CreateStructGEP(sidecar_type, entry, 1));
    auto *sampler_max = builder.CreateLoad(f32, builder.CreateStructGEP(sidecar_type, entry, 2));
    auto *lowered = builder.CreateCall(maximum, {sample.call->getArgOperand(6), sampler_min});
    auto *bounded = builder.CreateCall(minimum, {lowered, sampler_max});
    auto *level = builder.CreateCall(maximum, {bounded, clamp}, "lod.clamped");
    sample.call->setArgOperand(6, level);
    sample.call->setArgOperand(1, relaxed_sampler);

    auto *mip_type = llvm::FunctionType::get(i32, {texture->getType()}, false);
    auto mip_fn = module.getOrInsertFunction("air.get_num_mip_levels_texture_2d", mip_type);
    auto *count = builder.CreateCall(mip_fn, {texture}, "lod.mip.count");
    auto *last = builder.CreateUIToFP(builder.CreateSub(count, builder.getInt32(1)), f32);
    auto *empty = builder.CreateFCmpOGT(clamp, last, "lod.empty");
    llvm::Instruction *empty_terminator = nullptr;
    llvm::Instruction *sample_terminator = nullptr;
    llvm::SplitBlockAndInsertIfThenElse(empty, sample.call, &empty_terminator, &sample_terminator);
    sample.call->moveBefore(sample_terminator);
    builder.SetInsertPoint(empty_terminator);
    auto sampler_fn = module.getOrInsertFunction("air.get_read_sampler", llvm::FunctionType::get(sampler_type, false));
    auto *sampler = builder.CreateCall(sampler_fn);
    auto read_type = llvm::FunctionType::get(
        sample.call->getType(), {texture->getType(), sampler_type, coordinates, coordinates, i32, i32}, false
    );
    auto read_fn = module.getOrInsertFunction("air.read_texture_2d.v4f32", read_type);
    auto *oob = builder.CreateCall(
        read_fn,
        {texture, sampler, llvm::ConstantVector::get({builder.getInt32(-1), builder.getInt32(0)}),
         llvm::Constant::getNullValue(coordinates), builder.getInt32(0), builder.getInt32(1)},
        "lod.oob"
    );
    auto *merge = empty_terminator->getSuccessor(0);
    builder.SetInsertPoint(&*merge->getFirstInsertionPt());
    auto *result = builder.CreatePHI(sample.call->getType(), 2, "lod.result");
    result->addIncoming(oob, empty_terminator->getParent());
    result->addIncoming(sample.call, sample_terminator->getParent());
    sample.call->replaceUsesWithIf(result, [result](const llvm::Use &use) { return use.getUser() != result; });
  }
  return MSCSampleLevelLODResult::Patched;
}
} // namespace dxmt::air

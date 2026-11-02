#include "msc_typed_buffer_sidecar.hpp"
#include "msc_root_argument.hpp"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include <algorithm>

namespace dxmt::air {
namespace {
struct Operation {
  llvm::CallInst *call;
  llvm::GetElementPtrInst *handle;
  unsigned index;
  bool width;
  llvm::Argument *root = nullptr;
  uint64_t sidecar_offset = 0;
};

using TableMapping = MSCTypedBufferTableMapping;

bool
isSafePopcount(const llvm::CallBase &call) {
  const auto *target = call.getCalledFunction();
  return llvm::isa<llvm::CallInst>(call) && target && target->isDeclaration() &&
         target->getName() == "air.popcount.i32" && call.arg_size() == 1 &&
         call.getType()->isIntegerTy(32) && call.getArgOperand(0)->getType()->isIntegerTy(32) &&
         !target->isVarArg();
}

bool
collect(llvm::Module &module, std::vector<Operation> &operations, const std::vector<TableMapping> &mappings) {
  auto *tlab = mappings.empty() ? nullptr : findMSCRootArgument(module);
  if (!mappings.empty() && !tlab)
    return false;
  for (auto &function : module)
    for (auto &block : function)
      for (auto &instruction : block) {
        auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction);
        if (!call)
          continue;
        auto *target = call->getCalledFunction();
        if (!target)
          return false;
        if (target->isDeclaration() && !target->isIntrinsic() && !target->getName().startswith("air."))
          return false;
        if (!target->getName().contains("texture_buffer_1d")) {
          // Other AIR calls are not automatically safe: an unknown intrinsic
          // could hide an operation that this transform must account for.
          if (target->isDeclaration()) {
            if (target->isIntrinsic()) {
              if (!target->getName().startswith("llvm.bswap."))
                return false;
            } else if (!isSafePopcount(*call) && target->getName() != "air.get_read_sampler" &&
                       target->getName() != "air.atomic.global.add.s.i32" &&
                       !target->getName().startswith("air.sample_texture_2d.")) {
              return false;
            }
          }
          continue;
        }
        const auto name = target->getName();
        if (name == "air.fence_texture_buffer_1d" || name == "air.is_null_texture_buffer_1d")
          continue;
        const bool width = name == "air.get_width_texture_buffer_1d";
        const bool read = name.startswith("air.read_texture_buffer_1d.");
        const bool write = name.startswith("air.write_texture_buffer_1d.");
        const bool access = read || write;
        auto *direct = llvm::dyn_cast<llvm::CallInst>(call);
        if ((!width && !access) || !direct || call->arg_empty())
          return false;
        if (read && !mappings.empty()) {
          auto *result = llvm::dyn_cast<llvm::StructType>(call->getType());
          if (!result || result->getNumElements() != 2 || !result->getElementType(1)->isIntegerTy(8))
            return false;
          // The second component's sparse-resource status semantics have not
          // been established after index remapping. Only value-only uses are
          // currently safe to investigate.
          for (const auto *user : call->users()) {
            auto *extract = llvm::dyn_cast<llvm::ExtractValueInst>(user);
            if (!extract || extract->getNumIndices() != 1 || *extract->idx_begin() != 0)
              return false;
          }
        }
        auto *load = llvm::dyn_cast<llvm::LoadInst>(call->getArgOperand(0));
        auto *gep = load ? llvm::dyn_cast<llvm::GetElementPtrInst>(load->getPointerOperand()) : nullptr;
        auto *type = gep ? llvm::dyn_cast<llvm::StructType>(gep->getSourceElementType()) : nullptr;
        if (!gep || !type || !type->isSized() || type->getNumElements() != 3 ||
            !type->getElementType(2)->isIntegerTy(64) || gep->getNumIndices() != 2 ||
            gep->getPointerAddressSpace() != 2)
          return false;
        const auto *descriptor = module.getDataLayout().getStructLayout(type);
        if (descriptor->getSizeInBytes() != 24 || descriptor->getElementOffset(0) != 0 ||
            descriptor->getElementOffset(1) != 8 || descriptor->getElementOffset(2) != 16 ||
            !type->getElementType(1)->isPointerTy() || load->isVolatile() || load->isAtomic())
          return false;
        auto *field = llvm::dyn_cast<llvm::ConstantInt>(gep->getOperand(gep->getNumOperands() - 1));
        if (!field || field->getZExtValue() != 1)
          return false;
        unsigned index = 0;
        if (access) {
          const bool read_sampler = read && call->arg_size() == 4;
          if (read_sampler) {
            auto *sampler = llvm::dyn_cast<llvm::CallInst>(call->getArgOperand(1));
            auto *sampler_fn = sampler ? sampler->getCalledFunction() : nullptr;
            if (!sampler_fn || sampler_fn->getName() != "air.get_read_sampler" || !sampler->arg_empty() ||
                !sampler->getType()->isPointerTy() || sampler->getType()->getPointerAddressSpace() != 2)
              return false;
          }
          index = read_sampler ? 2 : 1;
          if ((!read_sampler && call->arg_size() != (read ? 3u : 4u)) ||
              !call->getArgOperand(index)->getType()->isIntegerTy(32) ||
              !call->getArgOperand(call->arg_size() - 1)->getType()->isIntegerTy(32) ||
              (write && !call->getType()->isVoidTy()))
            return false;
        } else if (call->arg_size() != 1 || !call->getType()->isIntegerTy(32))
          return false;
        llvm::Argument *root = nullptr;
        uint64_t sidecar_offset = 0;
        if (!mappings.empty()) {
          auto *table = llvm::dyn_cast<llvm::LoadInst>(gep->getPointerOperand());
          if (!table || table->isVolatile() || table->isAtomic())
            return false;
          llvm::APInt offset(64, 0);
          auto *base =
              table->getPointerOperand()->stripAndAccumulateConstantOffsets(module.getDataLayout(), offset, true);
          root = llvm::dyn_cast<llvm::Argument>(base);
          if (!root || root != tlab || root->getParent() != &function ||
              root->getType()->getPointerAddressSpace() != 2 || !gep->getOperand(1)->getType()->isIntegerTy())
            return false;
          const auto match = std::find_if(mappings.begin(), mappings.end(), [&](const TableMapping &m) {
            return m.table_offset == offset.getZExtValue();
          });
          if (match == mappings.end())
            return false;
          sidecar_offset = match->sidecar_offset;
        }
        operations.push_back({direct, gep, index, width, root, sidecar_offset});
      }
  return !operations.empty();
}

void
patch(const std::vector<Operation> &operations) {
  for (const auto &op : operations) {
    llvm::IRBuilder<> b(op.call);
    llvm::Value *offset = nullptr, *count = nullptr;
    auto *root = b.CreateBitCast(op.root, b.getInt8PtrTy(2));
    auto *slot = b.CreateGEP(b.getInt8Ty(), root, b.getInt64(op.sidecar_offset));
    auto *sidecar_ptr_type = b.getInt64Ty()->getPointerTo(2);
    auto *slot_ptr = b.CreateBitCast(slot, sidecar_ptr_type->getPointerTo(2));
    auto *table = b.CreateLoad(sidecar_ptr_type, slot_ptr, "dxmt.sidecar");
    auto *index = b.CreateSExtOrTrunc(op.handle->getOperand(1), b.getInt64Ty());
    auto *word = b.CreateAdd(b.CreateMul(index, b.getInt64(4)), b.getInt64(1));
    auto *entry_ptr = b.CreateGEP(b.getInt64Ty(), table, word);
    auto *entry = b.CreateLoad(b.getInt64Ty(), entry_ptr, "dxmt.entry");
    offset = b.CreateTrunc(entry, b.getInt32Ty());
    count = b.CreateTrunc(b.CreateLShr(entry, 32), b.getInt32Ty());
    if (op.width) {
      op.call->replaceAllUsesWith(count);
      op.call->eraseFromParent();
    } else {
      auto *old = op.call->getArgOperand(op.index);
      auto *in_bounds = b.CreateICmpULT(old, count);
      auto *adjusted = b.CreateAdd(old, offset);
      auto *no_wrap = b.CreateICmpUGE(adjusted, offset);
      op.call->setArgOperand(
          op.index, b.CreateSelect(b.CreateAnd(in_bounds, no_wrap), adjusted, b.getInt32(UINT32_MAX))
      );
    }
  }
}
} // namespace

bool
definitelyNoMSCTypedBufferAccess(const llvm::Module &module) {
  bool has_definition = false;
  for (const auto &function : module) {
    if (function.isDeclaration())
      continue;
    has_definition = true;
    for (const auto &block : function)
      for (const auto &instruction : block) {
        const auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction);
        if (!call)
          continue;
        const auto *target = call->getCalledFunction();
        if (isSafePopcount(*call))
          continue;
        if (!target || target->getName().startswith("air.") ||
            (target->isDeclaration() &&
             !(target->isIntrinsic() && target->getName().startswith("llvm.bswap."))))
          return false;
      }
  }
  return has_definition;
}

bool
patchMSCTypedBufferSidecar(
    llvm::Module &module, const std::vector<MSCTypedBufferTableMapping> &mappings, size_t &count
) {
  if (mappings.empty())
    return false;
  for (size_t i = 0; i < mappings.size(); ++i) {
    if (mappings[i].table_offset > UINT32_MAX || mappings[i].sidecar_offset > UINT32_MAX ||
        mappings[i].table_offset % 8 || mappings[i].sidecar_offset % 8)
      return false;
    for (size_t j = 0; j < mappings.size(); ++j)
      if (mappings[i].table_offset == mappings[j].sidecar_offset ||
          (i != j && (mappings[i].table_offset == mappings[j].table_offset ||
                      mappings[i].sidecar_offset == mappings[j].sidecar_offset)))
        return false;
  }
  std::vector<Operation> operations;
  if (!collect(module, operations, mappings))
    return false;
  patch(operations);
  count = operations.size();
  return true;
}
} // namespace dxmt::air

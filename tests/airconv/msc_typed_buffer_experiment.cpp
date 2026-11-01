// Offline experiment only. This is not part of the shader conversion path.
// It assumes the MSC 4.x three-qword descriptor ABI and an EXPERIMENTAL
// texel-size byte in metadata bits 40..47. Production does not encode it.
#include "metallib_writer.hpp"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
#include <cstdlib>
#include <string>

namespace {
struct Operation {
  llvm::CallInst *call;
  llvm::GetElementPtrInst *handle;
  unsigned index;
  bool width;
  llvm::Argument *root = nullptr;
};

bool collect(llvm::Module &module, std::vector<Operation> &operations,
             bool sidecar, uint64_t table_offset) {
  for (auto &function : module)
    for (auto &block : function)
      for (auto &instruction : block) {
        auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction);
        if (!call)
          continue;
        auto *target = call->getCalledFunction();
        if (!target)
          return false;
        if (!target->getName().contains("texture_buffer_1d"))
          continue;
        const auto name = target->getName();
        if (name == "air.fence_texture_buffer_1d" ||
            name == "air.is_null_texture_buffer_1d")
          continue;
        const bool width = name == "air.get_width_texture_buffer_1d";
        const bool access = name.startswith("air.read_texture_buffer_1d.") ||
                            name.startswith("air.write_texture_buffer_1d.");
        auto *direct = llvm::dyn_cast<llvm::CallInst>(call);
        if ((!width && !access) || !direct || call->arg_empty())
          return false;
        auto *load = llvm::dyn_cast<llvm::LoadInst>(call->getArgOperand(0));
        auto *gep = load ? llvm::dyn_cast<llvm::GetElementPtrInst>(
                               load->getPointerOperand())
                         : nullptr;
        auto *type =
            gep ? llvm::dyn_cast<llvm::StructType>(gep->getSourceElementType())
                : nullptr;
        if (!gep || !type || type->getNumElements() != 3 ||
            !type->getElementType(2)->isIntegerTy(64) ||
            gep->getNumIndices() != 2 || gep->getPointerAddressSpace() != 2)
          return false;
        auto *field = llvm::dyn_cast<llvm::ConstantInt>(
            gep->getOperand(gep->getNumOperands() - 1));
        if (!field || field->getZExtValue() != 1)
          return false;
        unsigned index = 0;
        if (access) {
          for (unsigned i = 1; i < call->arg_size(); ++i)
            if (call->getArgOperand(i)->getType()->isIntegerTy(32)) {
              index = i;
              break;
            }
          if (!index)
            return false;
        } else if (!call->getType()->isIntegerTy(32))
          return false;
        llvm::Argument *root = nullptr;
        if (sidecar) {
          auto *table =
              llvm::dyn_cast<llvm::LoadInst>(gep->getPointerOperand());
          if (!table)
            return false;
          llvm::APInt offset(64, 0);
          auto *base =
              table->getPointerOperand()->stripAndAccumulateConstantOffsets(
                  module.getDataLayout(), offset, true);
          root = llvm::dyn_cast<llvm::Argument>(base);
          if (!root || root->getParent() != &function ||
              root->getType()->getPointerAddressSpace() != 2 ||
              offset.getZExtValue() != table_offset ||
              !gep->getOperand(1)->getType()->isIntegerTy())
            return false;
        }
        operations.push_back({direct, gep, index, width, root});
      }
  return !operations.empty();
}

void patch(const std::vector<Operation> &operations, bool sidecar,
           uint64_t sidecar_offset) {
  for (const auto &op : operations) {
    llvm::IRBuilder<> b(op.call);
    llvm::Value *offset = nullptr, *count = nullptr;
    if (sidecar) {
      auto *root = b.CreateBitCast(op.root, b.getInt8PtrTy(2));
      auto *slot = b.CreateGEP(b.getInt8Ty(), root, b.getInt64(sidecar_offset));
      auto *sidecar_ptr_type = b.getInt64Ty()->getPointerTo(2);
      auto *slot_ptr = b.CreateBitCast(slot, sidecar_ptr_type->getPointerTo(2));
      auto *table = b.CreateLoad(sidecar_ptr_type, slot_ptr, "dxmt.sidecar");
      auto *index =
          b.CreateSExtOrTrunc(op.handle->getOperand(1), b.getInt64Ty());
      auto *word =
          b.CreateAdd(b.CreateMul(index, b.getInt64(4)), b.getInt64(1));
      auto *entry_ptr = b.CreateGEP(b.getInt64Ty(), table, word);
      auto *entry = b.CreateLoad(b.getInt64Ty(), entry_ptr, "dxmt.entry");
      offset = b.CreateTrunc(entry, b.getInt32Ty());
      count = b.CreateTrunc(b.CreateLShr(entry, 32), b.getInt32Ty());
    } else {
      llvm::SmallVector<llvm::Value *, 4> indices;
      for (auto it = op.handle->idx_begin(); it != op.handle->idx_end(); ++it)
        indices.push_back(it->get());
      indices.back() = b.getInt32(2);
      auto *ptr = b.CreateGEP(op.handle->getSourceElementType(),
                              op.handle->getPointerOperand(), indices);
      auto *metadata = b.CreateLoad(b.getInt64Ty(), ptr, "dxmt.metadata");
      offset = b.CreateTrunc(b.CreateAnd(b.CreateLShr(metadata, 32), 255),
                             b.getInt32Ty());
      auto *bytes = b.CreateTrunc(metadata, b.getInt32Ty());
      auto *size = b.CreateTrunc(b.CreateAnd(b.CreateLShr(metadata, 40), 255),
                                 b.getInt32Ty());
      auto *nonzero = b.CreateICmpNE(size, b.getInt32(0));
      auto *safe_size = b.CreateSelect(nonzero, size, b.getInt32(1));
      count = b.CreateSelect(nonzero, b.CreateUDiv(bytes, safe_size),
                             b.getInt32(0), "dxmt.count");
    }
    if (op.width) {
      op.call->replaceAllUsesWith(count);
      op.call->eraseFromParent();
    } else {
      auto *old = op.call->getArgOperand(op.index);
      auto *in_bounds = b.CreateICmpULT(old, count);
      auto *adjusted = b.CreateAdd(old, offset);
      auto *no_wrap = b.CreateICmpUGE(adjusted, offset);
      op.call->setArgOperand(op.index,
                             b.CreateSelect(b.CreateAnd(in_bounds, no_wrap),
                                            adjusted, b.getInt32(UINT32_MAX)));
    }
  }
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3 && argc != 6)
    return 2;
  bool sidecar = argc == 6;
  uint64_t table_offset = 0, sidecar_offset = 0;
  if (sidecar) {
    if (std::string(argv[3]) != "--sidecar")
      return 2;
    char *end = nullptr;
    table_offset = std::strtoull(argv[4], &end, 0);
    if (!*argv[4] || *argv[4] == '-' || *end || table_offset > UINT32_MAX)
      return 2;
    sidecar_offset = std::strtoull(argv[5], &end, 0);
    if (!*argv[5] || *argv[5] == '-' || *end || sidecar_offset > UINT32_MAX ||
        sidecar_offset % 8 || table_offset == sidecar_offset)
      return 2;
  }
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  llvm::SMDiagnostic diagnostic;
  auto module = llvm::parseIRFile(argv[1], diagnostic, context);
  if (!module) {
    diagnostic.print(argv[0], llvm::errs());
    return 3;
  }
  std::vector<Operation> operations;
  if (!collect(*module, operations, sidecar, table_offset)) {
    llvm::errs() << "unsupported typed-buffer AIR\n";
    return 4;
  }
  patch(operations, sidecar, sidecar_offset);
  if (llvm::verifyModule(*module, &llvm::errs()))
    return 5;
  std::error_code error;
  llvm::raw_fd_ostream out(argv[2], error);
  if (error)
    return 6;
  dxmt::metallib::MetallibWriter writer;
  writer.Write(*module, out);
  llvm::outs() << "patched " << operations.size()
               << " typed-buffer operations\n";
  return 0;
}

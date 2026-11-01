// Offline experiment only. This is not part of the shader conversion path.
// It assumes the MSC 4.x three-qword descriptor ABI and an EXPERIMENTAL
// texel-size byte in metadata bits 40..47. Production does not encode it.
#include "metallib_writer.hpp"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

namespace {
struct Operation {
  llvm::CallInst *call;
  llvm::GetElementPtrInst *handle;
  unsigned index;
  bool width;
};

bool collect(llvm::Module &module, std::vector<Operation> &operations) {
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
        operations.push_back({direct, gep, index, width});
      }
  return !operations.empty();
}

void patch(const std::vector<Operation> &operations) {
  for (const auto &op : operations) {
    llvm::IRBuilder<> b(op.call);
    llvm::SmallVector<llvm::Value *, 4> indices;
    for (auto it = op.handle->idx_begin(); it != op.handle->idx_end(); ++it)
      indices.push_back(it->get());
    indices.back() = b.getInt32(2);
    auto *ptr = b.CreateGEP(op.handle->getSourceElementType(),
                            op.handle->getPointerOperand(), indices);
    auto *metadata = b.CreateLoad(b.getInt64Ty(), ptr, "dxmt.metadata");
    auto *offset = b.CreateTrunc(b.CreateAnd(b.CreateLShr(metadata, 32), 255),
                                 b.getInt32Ty());
    auto *bytes = b.CreateTrunc(metadata, b.getInt32Ty());
    auto *size = b.CreateTrunc(b.CreateAnd(b.CreateLShr(metadata, 40), 255),
                               b.getInt32Ty());
    auto *nonzero = b.CreateICmpNE(size, b.getInt32(0));
    auto *safe_size = b.CreateSelect(nonzero, size, b.getInt32(1));
    auto *count = b.CreateSelect(nonzero, b.CreateUDiv(bytes, safe_size),
                                 b.getInt32(0), "dxmt.count");
    if (op.width) {
      op.call->replaceAllUsesWith(count);
      op.call->eraseFromParent();
    } else {
      auto *old = op.call->getArgOperand(op.index);
      auto *in_bounds = b.CreateICmpULT(old, count);
      auto *adjusted = b.CreateAdd(old, offset);
      op.call->setArgOperand(op.index, b.CreateSelect(in_bounds, adjusted,
                                                      b.getInt32(UINT32_MAX)));
    }
  }
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  llvm::SMDiagnostic diagnostic;
  auto module = llvm::parseIRFile(argv[1], diagnostic, context);
  if (!module) {
    diagnostic.print(argv[0], llvm::errs());
    return 3;
  }
  std::vector<Operation> operations;
  if (!collect(*module, operations)) {
    llvm::errs() << "unsupported typed-buffer AIR\n";
    return 4;
  }
  patch(operations);
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

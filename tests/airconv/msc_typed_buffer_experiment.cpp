// Offline experiment only. This is not part of the shader conversion path.
// It assumes the MSC 4.x three-qword descriptor ABI and an EXPERIMENTAL
// texel-size byte in metadata bits 40..47. Production does not encode it.
#include "metallib_writer.hpp"
#include "airconv_public.h"
#include "compiled_bitcode.hpp"
#include "transforms/msc_typed_buffer_sidecar.hpp"
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
#include <algorithm>

namespace {
struct Operation {
  llvm::CallInst *call;
  llvm::GetElementPtrInst *handle;
  unsigned index;
  bool width;
};

struct TableMapping {
  uint64_t table_offset;
  uint64_t sidecar_offset;
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
        const bool read = name.startswith("air.read_texture_buffer_1d.");
        const bool write = name.startswith("air.write_texture_buffer_1d.");
        const bool access = read || write;
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
        if (!gep || !type || !type->isSized() || type->getNumElements() != 3 ||
            !type->getElementType(2)->isIntegerTy(64) ||
            gep->getNumIndices() != 2 || gep->getPointerAddressSpace() != 2)
          return false;
        const auto *descriptor = module.getDataLayout().getStructLayout(type);
        if (descriptor->getSizeInBytes() != 24 ||
            descriptor->getElementOffset(0) != 0 ||
            descriptor->getElementOffset(1) != 8 ||
            descriptor->getElementOffset(2) != 16 ||
            !type->getElementType(1)->isPointerTy() || load->isVolatile() ||
            load->isAtomic())
          return false;
        auto *field = llvm::dyn_cast<llvm::ConstantInt>(
            gep->getOperand(gep->getNumOperands() - 1));
        if (!field || field->getZExtValue() != 1)
          return false;
        unsigned index = 0;
        if (access) {
          if (call->arg_size() != (read ? 3u : 4u) ||
              !call->getArgOperand(1)->getType()->isIntegerTy(32) ||
              !call->getArgOperand(call->arg_size() - 1)
                   ->getType()
                   ->isIntegerTy(32) ||
              (write && !call->getType()->isVoidTy()))
            return false;
          index = 1;
        } else if (call->arg_size() != 1 || !call->getType()->isIntegerTy(32))
          return false;
        operations.push_back({direct, gep, index, width});
      }
  return !operations.empty();
}

void patch(const std::vector<Operation> &operations) {
  for (const auto &op : operations) {
    llvm::IRBuilder<> b(op.call);
    llvm::Value *offset = nullptr, *count = nullptr;
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
  const bool passthrough = argc == 4 && std::string(argv[3]) == "--unpatched";
  const bool use_api = argc >= 6 && std::string(argv[3]) == "--metallib-api";
  const int first_mapping = use_api ? 6 : 3;
  if (argc < 3 || (!passthrough && (argc - first_mapping) % 3))
    return 2;
  uint32_t original_size = 0, extended_size = 0;
  if (use_api) {
    char *end = nullptr;
    auto parse = [&](const char *value, uint32_t &result) {
      const auto n = std::strtoull(value, &end, 0);
      if (!*value || *value == '-' || *end || n > UINT32_MAX)
        return false;
      result = static_cast<uint32_t>(n);
      return true;
    };
    if (!parse(argv[4], original_size) || !parse(argv[5], extended_size))
      return 2;
  }
  std::vector<TableMapping> mappings;
  for (int i = first_mapping; !passthrough && i < argc; i += 3) {
    if (std::string(argv[i]) != "--sidecar")
      return 2;
    char *end = nullptr;
    uint64_t table_offset = std::strtoull(argv[i + 1], &end, 0);
    if (!*argv[i + 1] || *argv[i + 1] == '-' || *end ||
        table_offset > UINT32_MAX || table_offset % 8)
      return 2;
    uint64_t sidecar_offset = std::strtoull(argv[i + 2], &end, 0);
    if (!*argv[i + 2] || *argv[i + 2] == '-' || *end ||
        sidecar_offset > UINT32_MAX || sidecar_offset % 8)
      return 2;
    mappings.push_back({table_offset, sidecar_offset});
  }
  for (size_t i = 0; i < mappings.size(); ++i)
    for (size_t j = 0; j < mappings.size(); ++j)
      if (mappings[i].table_offset == mappings[j].sidecar_offset ||
          (i != j &&
           (mappings[i].table_offset == mappings[j].table_offset ||
            mappings[i].sidecar_offset == mappings[j].sidecar_offset)))
        return 2;
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  llvm::SMDiagnostic diagnostic;
  auto module = llvm::parseIRFile(argv[1], diagnostic, context);
  if (!module) {
    diagnostic.print(argv[0], llvm::errs());
    return 3;
  }
  if (use_api) {
    llvm::SmallVector<char, 0> input;
    llvm::raw_svector_ostream input_stream(input);
    dxmt::metallib::MetallibWriter writer;
    writer.Write(*module, input_stream);
    std::vector<SM50_MSC_TYPED_BUFFER_TABLE> tables;
    for (const auto &mapping : mappings)
      tables.push_back({static_cast<uint32_t>(mapping.table_offset),
                        static_cast<uint32_t>(mapping.sidecar_offset)});
    sm50_bitcode_t output = nullptr;
    const int status = SM50PatchMetalLibTypedBufferSidecar(
        input.data(), input.size(), original_size, extended_size, tables.data(),
        tables.size(), &output);
    if (status != SM50_MSC_TYPED_BUFFER_PATCHED || !output)
      return 4;
    auto *compiled = static_cast<SM50CompiledBitcodeInternal *>(output);
    std::error_code error;
    llvm::raw_fd_ostream out(argv[2], error);
    if (!error)
      out.write(compiled->vec.data(), compiled->vec.size());
    delete compiled;
    if (error || out.has_error())
      return 6;
    llvm::outs() << "metallib API patched\n";
    return 0;
  }
  std::vector<Operation> operations;
  size_t count = 0;
  std::vector<dxmt::air::MSCTypedBufferTableMapping> sidecar_tables;
  for (const auto &mapping : mappings)
    sidecar_tables.push_back({mapping.table_offset, mapping.sidecar_offset});
  if (!passthrough && !mappings.empty() &&
      !dxmt::air::patchMSCTypedBufferSidecar(*module, sidecar_tables, count)) {
    llvm::errs() << "unsupported typed-buffer AIR\n";
    return 4;
  }
  if (!passthrough && mappings.empty() && !collect(*module, operations)) {
    llvm::errs() << "unsupported typed-buffer AIR\n";
    return 4;
  }
  if (!passthrough && mappings.empty()) {
    patch(operations);
    count = operations.size();
  }
  if (llvm::verifyModule(*module, &llvm::errs()))
    return 5;
  std::error_code error;
  llvm::raw_fd_ostream out(argv[2], error);
  if (error)
    return 6;
  dxmt::metallib::MetallibWriter writer;
  writer.Write(*module, out);
  llvm::outs() << "patched " << count << " typed-buffer operations\n";
  return 0;
}

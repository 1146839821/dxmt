#include "nt/air_builder.hpp"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

int main() {
  llvm::LLVMContext context;
  llvm::Module module("minmax-linkage", context);
  llvm::IRBuilder<> ir(context);
  llvm::air::AIRBuilder air(ir, llvm::errs());
  using Texture = llvm::air::Texture;
  unsigned passed = 0;
  for (const auto kind : {Texture::texture2d, Texture::texture2d_array, Texture::texture3d}) {
    Texture texture{kind, Texture::sample_float, Texture::access_sample};
    std::vector<llvm::Type *> arguments{air.getTextureHandleType(texture), air.getSamplerHandleType(),
        air.getTextureSampleCoordType(texture), air.getFloatTy(), air.getIntTy()};
    if (kind == Texture::texture2d_array) arguments.push_back(air.getIntTy());
    auto *function = llvm::Function::Create(llvm::FunctionType::get(air.getFloatTy(4), arguments, false),
        llvm::GlobalValue::ExternalLinkage, "probe" + std::to_string(passed), module);
    ir.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", function));
    const int32_t offset[] = {0, 0, 0};
    auto value = air.CreateReductionSampleLevel(texture, function->getArg(0), function->getArg(1),
        function->getArg(2), kind == Texture::texture2d_array ? function->getArg(5) : nullptr,
        function->getArg(3), function->getArg(4), offset);
    if (!value) return 1;
    auto *call = llvm::dyn_cast<llvm::CallInst>(*value);
    // Bitcast calls can hide mismatched Metal/LLVM ABI signatures. Require an
    // actual same-signature linked definition, not merely a verified declaration.
    if (!call || !call->getCalledFunction() || call->getCalledFunction()->isDeclaration()) return 1;
    ir.CreateRet(*value);
    ++passed;
    const auto instruction_count = ir.GetInsertBlock()->size();
    // Reject unsupported kinds before touching the command's IR.
    Texture unsupported{Texture::texturecube, Texture::sample_float, Texture::access_sample};
    if (air.CreateReductionSampleLevel(unsupported, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, offset) || ir.GetInsertBlock()->size() != instruction_count) return 1;
  }
  if (llvm::verifyModule(module, &llvm::errs())) return 1;
  llvm::outs() << "AIR reduction linkage: passed=" << passed << " failed=0\n";
  return 0;
}

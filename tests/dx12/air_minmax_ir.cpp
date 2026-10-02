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

    arguments.push_back(air.getFloatTy());
    arguments.push_back(ir.getInt64Ty());
    auto *clamped = llvm::Function::Create(llvm::FunctionType::get(air.getFloatTy(4), arguments, false),
        llvm::GlobalValue::ExternalLinkage, "clamped" + std::to_string(passed), module);
    auto *entry = llvm::BasicBlock::Create(context, "entry", clamped);
    ir.SetInsertPoint(entry);
    const unsigned clamp_index = arguments.size() - 2;
    const unsigned defaults_index = arguments.size() - 1;
    auto result = air.CreateClampedReductionSampleLevel(texture, clamped->getArg(0), clamped->getArg(1),
        clamped->getArg(2), kind == Texture::texture2d_array ? clamped->getArg(5) : nullptr,
        clamped->getArg(3), clamped->getArg(4), offset,
        clamped->getArg(clamp_index), clamped->getArg(defaults_index));
    if (!result) return 1;
    auto *branch = llvm::dyn_cast<llvm::BranchInst>(entry->getTerminator());
    if (!branch || !branch->isConditional()) return 1;
    auto *empty = llvm::dyn_cast<llvm::FCmpInst>(branch->getCondition());
    // Strict > lastMip, not floor(clamp) > lastMip: fractional clamp past
    // the final mip is an empty set too (D3D11.3 5.8.5).
    if (!empty || empty->getPredicate() != llvm::CmpInst::FCMP_OGT ||
        empty->getOperand(0) != clamped->getArg(clamp_index)) return 1;
    auto *defaults_block = branch->getSuccessor(0);
    auto *active_block = branch->getSuccessor(1);
    for (const auto &instruction : *defaults_block)
      if (llvm::isa<llvm::CallInst>(instruction)) return 1;
    unsigned helper_calls = 0;
    for (const auto &instruction : *active_block) {
      if (const auto *helper = llvm::dyn_cast<llvm::CallInst>(&instruction))
        if (helper->getCalledFunction() && helper->getCalledFunction()->getName().startswith("dxmt.minmax.sample_level."))
          ++helper_calls;
    }
    auto *phi = llvm::dyn_cast<llvm::PHINode>(*result);
    if (helper_calls != 1 || !phi || phi->getNumIncomingValues() != 2 ||
        phi->getBasicBlockIndex(defaults_block) < 0 || phi->getBasicBlockIndex(active_block) < 0) return 1;
    ir.CreateRet(*result);
    ++passed;
    const auto block_count = clamped->size();
    const auto clamped_instruction_count = ir.GetInsertBlock()->size();
    if (air.CreateClampedReductionSampleLevel(unsupported, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, offset, nullptr, nullptr) || clamped->size() != block_count ||
        ir.GetInsertBlock()->size() != clamped_instruction_count) return 1;
  }
  if (llvm::verifyModule(module, &llvm::errs())) return 1;
  llvm::outs() << "AIR reduction linkage: passed=" << passed << " failed=0\n";
  return 0;
}

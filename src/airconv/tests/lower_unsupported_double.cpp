#include "transforms/lower_unsupported_double.hpp"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>

namespace {

std::unique_ptr<llvm::Module>
makeModule(llvm::LLVMContext &context, bool fast) {
  auto module = std::make_unique<llvm::Module>("lower-unsupported-double", context);
  auto *function_type = llvm::FunctionType::get(llvm::Type::getFloatTy(context), false);
  auto *function = llvm::Function::Create(function_type, llvm::GlobalValue::ExternalLinkage, "main", *module);
  auto *block = llvm::BasicBlock::Create(context, "entry", function);
  auto *double_type = llvm::Type::getDoubleTy(context);
  auto *sum = llvm::BinaryOperator::CreateFAdd(
      llvm::ConstantFP::get(double_type, 0.67), llvm::ConstantFP::get(double_type, 0.34), "sum", block
  );
  if (fast)
    sum->setFastMathFlags(llvm::FastMathFlags::getFast());
  auto *truncated = new llvm::FPTruncInst(sum, llvm::Type::getFloatTy(context), "truncated", block);
  llvm::ReturnInst::Create(context, truncated, block);
  return module;
}

} // namespace

int
main() {
  llvm::LLVMContext context;

  auto fast_module = makeModule(context, true);
  if (!dxmt::air::lowerUnsupportedDoublePrecision(*fast_module)) {
    llvm::errs() << "fast double expression was not lowered\n";
    return 1;
  }
  if (llvm::verifyModule(*fast_module, &llvm::errs()))
    return 1;

  auto precise_module = makeModule(context, false);
  if (dxmt::air::lowerUnsupportedDoublePrecision(*precise_module)) {
    llvm::errs() << "precise double expression was unexpectedly lowered\n";
    return 1;
  }
  if (llvm::verifyModule(*precise_module, &llvm::errs()))
    return 1;

  return 0;
}

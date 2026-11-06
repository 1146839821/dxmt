#include "lower_unsupported_double.hpp"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/Local.h"

namespace dxmt::air {

namespace {

llvm::Value *
lowerDoubleValue(
    llvm::Value *value, llvm::DenseMap<llvm::Value *, llvm::Value *> &lowered,
    llvm::SmallPtrSet<llvm::Value *, 8> &visiting
) {
  if (auto it = lowered.find(value); it != lowered.end())
    return it->second;
  if (!value->getType()->isDoubleTy() || !visiting.insert(value).second)
    return nullptr;

  llvm::Value *result = nullptr;
  if (auto *constant = llvm::dyn_cast<llvm::ConstantFP>(value)) {
    llvm::APFloat converted = constant->getValueAPF();
    bool loses_info = false;
    converted.convert(llvm::APFloat::IEEEsingle(), llvm::APFloat::rmNearestTiesToEven, &loses_info);
    result = llvm::ConstantFP::get(value->getContext(), converted);
  } else if (auto *extension = llvm::dyn_cast<llvm::FPExtInst>(value)) {
    if (extension->getSrcTy()->isFloatTy())
      result = extension->getOperand(0);
  } else if (auto *select = llvm::dyn_cast<llvm::SelectInst>(value)) {
    llvm::IRBuilder<> builder(select);
    auto *true_value = lowerDoubleValue(select->getTrueValue(), lowered, visiting);
    auto *false_value = lowerDoubleValue(select->getFalseValue(), lowered, visiting);
    if (true_value && false_value)
      result = builder.CreateSelect(select->getCondition(), true_value, false_value);
  } else if (auto *binary = llvm::dyn_cast<llvm::BinaryOperator>(value)) {
    if (!binary->getFastMathFlags().isFast()) {
      visiting.erase(value);
      return nullptr;
    }
    switch (binary->getOpcode()) {
    case llvm::Instruction::FAdd:
    case llvm::Instruction::FSub:
    case llvm::Instruction::FMul:
    case llvm::Instruction::FDiv:
    case llvm::Instruction::FRem: {
      llvm::IRBuilder<> builder(binary);
      auto *lhs = lowerDoubleValue(binary->getOperand(0), lowered, visiting);
      auto *rhs = lowerDoubleValue(binary->getOperand(1), lowered, visiting);
      if (lhs && rhs) {
        auto *lowered_binary = builder.CreateBinOp(binary->getOpcode(), lhs, rhs);
        if (auto *instruction = llvm::dyn_cast<llvm::Instruction>(lowered_binary))
          instruction->copyFastMathFlags(binary);
        result = lowered_binary;
      }
      break;
    }
    default:
      break;
    }
  }

  visiting.erase(value);
  if (result)
    lowered[value] = result;
  return result;
}

} // namespace

bool
lowerUnsupportedDoublePrecision(llvm::Module &module) {
  llvm::SmallVector<llvm::FPTruncInst *, 4> truncations;
  for (auto &function : module)
    for (auto &block : function)
      for (auto &instruction : block)
        if (auto *truncation = llvm::dyn_cast<llvm::FPTruncInst>(&instruction);
            truncation && truncation->getSrcTy()->isDoubleTy() && truncation->getDestTy()->isFloatTy())
          truncations.push_back(truncation);

  bool changed = false;
  for (auto *truncation : truncations) {
    llvm::DenseMap<llvm::Value *, llvm::Value *> lowered;
    llvm::SmallPtrSet<llvm::Value *, 8> visiting;
    if (auto *replacement = lowerDoubleValue(truncation->getOperand(0), lowered, visiting)) {
      truncation->replaceAllUsesWith(replacement);
      llvm::RecursivelyDeleteTriviallyDeadInstructions(truncation);
      changed = true;
    }
  }
  return changed;
}

} // namespace dxmt::air

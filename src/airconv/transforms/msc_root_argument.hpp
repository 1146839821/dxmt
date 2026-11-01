#pragma once

#include "../airconv_public.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"

namespace dxmt::air {
inline llvm::Argument *
findMSCRootArgument(llvm::Module &module) {
  auto *kernel = module.getNamedMetadata("air.kernel");
  auto *fragment = module.getNamedMetadata("air.fragment");
  if (!!kernel == !!fragment || module.getNamedMetadata("air.vertex") || module.getNamedMetadata("air.mesh") ||
      module.getNamedMetadata("air.object"))
    return nullptr;
  auto *entries = kernel ? kernel : fragment;
  if (entries->getNumOperands() != 1)
    return nullptr;
  auto *entry = entries->getOperand(0);
  if (!entry || entry->getNumOperands() < 3)
    return nullptr;
  auto *fm = llvm::dyn_cast_or_null<llvm::ValueAsMetadata>(entry->getOperand(0));
  auto *function = fm ? llvm::dyn_cast<llvm::Function>(fm->getValue()) : nullptr;
  auto *args = llvm::dyn_cast_or_null<llvm::MDNode>(entry->getOperand(2));
  if (!function || function->isDeclaration() || !args)
    return nullptr;
  llvm::Argument *result = nullptr;
  for (const auto &operand : args->operands()) {
    auto *arg = llvm::dyn_cast_or_null<llvm::MDNode>(operand);
    if (!arg || !arg->getNumOperands())
      return nullptr;
    bool indirect = false, has_location = false;
    uint64_t location = 0;
    for (unsigned i = 1; i < arg->getNumOperands(); ++i) {
      auto *tag = llvm::dyn_cast_or_null<llvm::MDString>(arg->getOperand(i));
      if (!tag)
        continue;
      if (tag->getString() == "air.indirect_buffer") {
        if (indirect)
          return nullptr;
        indirect = true;
      } else if (tag->getString() == "air.location_index") {
        if (has_location || i + 1 >= arg->getNumOperands())
          return nullptr;
        auto *number = llvm::mdconst::dyn_extract<llvm::ConstantInt>(arg->getOperand(i + 1).get());
        if (!number || number->getBitWidth() > 64)
          return nullptr;
        location = number->getZExtValue();
        has_location = true;
      }
    }
    if (indirect && !has_location)
      return nullptr;
    if (!indirect || location != SM50_MSC_ARGUMENT_BUFFER_BIND_POINT)
      continue;
    auto *index = llvm::mdconst::dyn_extract<llvm::ConstantInt>(arg->getOperand(0).get());
    if (result || !index || index->getBitWidth() > 32 || index->getZExtValue() >= function->arg_size())
      return nullptr;
    result = function->getArg(index->getZExtValue());
    if (!result->getType()->isPointerTy() || result->getType()->getPointerAddressSpace() != 2)
      return nullptr;
  }
  return result;
}

} // namespace dxmt::air

#include "dxil_logic_op.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>

namespace dxmt::dxil {
bool LowerIntegerLogicOutput(llvm::CallInst &store, llvm::Value *destination,
    uint32_t operation, uint32_t component_bits, std::string &error) {
  using namespace llvm;
  error.clear();
  auto reject = [&](const char *message) { error = message; return false; };
  auto *function = store.getCalledFunction();
  if (!function || function->getName() != "dx.op.storeOutput.i32" ||
      store.arg_size() != 5 || !store.getType()->isVoidTy())
    return reject("integer output store required");
  auto *opcode = dyn_cast<ConstantInt>(store.getArgOperand(0));
  auto *element = dyn_cast<ConstantInt>(store.getArgOperand(1));
  auto *row = dyn_cast<ConstantInt>(store.getArgOperand(2));
  auto *column = dyn_cast<ConstantInt>(store.getArgOperand(3));
  if (!opcode || !opcode->getType()->isIntegerTy(32) || opcode->getZExtValue() != 5 ||
      !element || !element->getType()->isIntegerTy(32) ||
      !row || !row->getType()->isIntegerTy(32) || !row->isZero() ||
      !column || !column->getType()->isIntegerTy(8) || column->getZExtValue() > 3 ||
      !store.getArgOperand(4)->getType()->isIntegerTy(32) || !destination ||
      destination->getType() != store.getArgOperand(4)->getType())
    return reject("constant scalar color output and i32 destination required");
  if (operation > 15 || !component_bits || component_bits > 32)
    return reject("invalid logic operation or component width");
  IRBuilder<> builder(&store);
  Value *source = store.getArgOperand(4), *result = nullptr;
  // D3D12_LOGIC_OP ordering, not the Metal fixed-function enum ordering.
  switch (operation) {
  case 0: result = builder.getInt32(0); break;
  case 1: result = builder.getInt32(UINT32_MAX); break;
  case 2: result = source; break;
  case 3: result = builder.CreateNot(source); break;
  case 4: result = destination; break;
  case 5: result = builder.CreateNot(destination); break;
  case 6: result = builder.CreateAnd(source, destination); break;
  case 7: result = builder.CreateNot(builder.CreateAnd(source, destination)); break;
  case 8: result = builder.CreateOr(source, destination); break;
  case 9: result = builder.CreateNot(builder.CreateOr(source, destination)); break;
  case 10: result = builder.CreateXor(source, destination); break;
  case 11: result = builder.CreateNot(builder.CreateXor(source, destination)); break;
  case 12: result = builder.CreateAnd(source, builder.CreateNot(destination)); break;
  case 13: result = builder.CreateAnd(builder.CreateNot(source), destination); break;
  case 14: result = builder.CreateOr(source, builder.CreateNot(destination)); break;
  case 15: result = builder.CreateOr(builder.CreateNot(source), destination); break;
  }
  // Mask inverted high bits before Metal's narrowing UINT conversion.
  if (component_bits < 32)
    result = builder.CreateAnd(result, builder.getInt32((uint32_t(1) << component_bits) - 1));
  store.setArgOperand(4, result);
  return true;
}
}

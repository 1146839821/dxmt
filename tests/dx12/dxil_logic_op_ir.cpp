#include "dxil_logic_op.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <array>
#include <iostream>

int main() {
  using namespace llvm;
  LLVMContext context;
  Module module("logic-output", context);
  IRBuilder<> builder(context);
  auto *i32 = builder.getInt32Ty();
  auto output = module.getOrInsertFunction("dx.op.storeOutput.i32", builder.getVoidTy(),
      i32, i32, i32, builder.getInt8Ty(), i32);
  auto *function = Function::Create(FunctionType::get(builder.getVoidTy(), false),
      Function::ExternalLinkage, "main", module);
  builder.SetInsertPoint(BasicBlock::Create(context, "entry", function));
  // Independent per-bit truth tables indexed by (source << 1) | destination.
  constexpr std::array<unsigned, 16> truth = {
      0x0, 0xf, 0xc, 0x3, 0xa, 0x5, 0x8, 0x7,
      0xe, 0x1, 0x6, 0x9, 0x4, 0x2, 0xd, 0xb};
  constexpr std::array<uint32_t, 8> values = {
      0, 1, 0xffffffff, 0xaaaaaaaa, 0x55555555, 0x80000000, 0x12345678, 0xf0ccaa55};
  unsigned checked = 0;
  std::string error;
  for (unsigned operation = 0; operation < 16; ++operation)
    for (unsigned width : {2u, 8u, 10u, 16u, 32u})
      for (auto source : values) for (auto destination : values) {
        auto *store = builder.CreateCall(output, {builder.getInt32(5), builder.getInt32(0),
            builder.getInt32(0), builder.getInt8(0), builder.getInt32(source)});
        if (!dxmt::dxil::LowerIntegerLogicOutput(*store, builder.getInt32(destination),
                operation, width, error)) return 1;
        uint32_t expected = 0;
        for (unsigned bit = 0; bit < width; ++bit) {
          unsigned index = (((source >> bit) & 1) << 1) | ((destination >> bit) & 1);
          expected |= ((truth[operation] >> index) & 1u) << bit;
        }
        auto *actual = dyn_cast<ConstantInt>(store->getArgOperand(4));
        if (!actual || actual->getZExtValue() != expected) return 2;
        store->eraseFromParent();
        ++checked;
      }
  auto *store = builder.CreateCall(output, {builder.getInt32(5), builder.getInt32(0),
      builder.getInt32(0), builder.getInt8(0), builder.getInt32(7)});
  auto *original = store->getArgOperand(4);
  for (auto invalid : {0u, 33u})
    if (dxmt::dxil::LowerIntegerLogicOutput(*store, builder.getInt32(3), 6, invalid, error) ||
        store->getArgOperand(4) != original) return 3;
  if (dxmt::dxil::LowerIntegerLogicOutput(*store, builder.getInt32(3), 16, 8, error) ||
      dxmt::dxil::LowerIntegerLogicOutput(*store, nullptr, 6, 8, error)) return 4;
  store->setArgOperand(3, builder.getInt8(4));
  if (dxmt::dxil::LowerIntegerLogicOutput(*store, builder.getInt32(3), 6, 8, error)) return 5;
  store->setArgOperand(3, builder.getInt8(0));
  builder.CreateRetVoid();
  auto *symbolic = Function::Create(FunctionType::get(builder.getVoidTy(), {i32, i32}, false),
      Function::ExternalLinkage, "symbolic", module);
  builder.SetInsertPoint(BasicBlock::Create(context, "entry", symbolic));
  for (unsigned operation = 0; operation < 16; ++operation) {
    auto *dynamic_store = builder.CreateCall(output, {builder.getInt32(5), builder.getInt32(0),
        builder.getInt32(0), builder.getInt8(3), symbolic->getArg(0)});
    if (!dxmt::dxil::LowerIntegerLogicOutput(*dynamic_store, symbolic->getArg(1), operation, 10, error))
      return 7;
  }
  builder.CreateRetVoid();
  if (verifyModule(module, &errs())) return 6;
  std::cout << "DXIL logic output CPU IR checks=" << checked << " PASS (not GPU qualification)\n";
  return 0;
}

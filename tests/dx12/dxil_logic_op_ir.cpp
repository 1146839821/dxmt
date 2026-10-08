#include "dxil_logic_op.hpp"
#include "metalirconverter_native.h"
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <array>
#include <iostream>

int main(int argc, char **argv) {
  using namespace llvm;
  LLVMContext context;
  context.setOpaquePointers(false);
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
  auto word = [&](unsigned value) -> Metadata * { return ConstantAsMetadata::get(builder.getInt32(value)); };
  auto *signature_record = MDNode::get(context, {word(0), MDString::get(context, "SV_Target"),
      ConstantAsMetadata::get(builder.getInt8(5)), ConstantAsMetadata::get(builder.getInt8(16)),
      MDNode::get(context, {word(3)}), ConstantAsMetadata::get(builder.getInt8(0)), word(1),
      ConstantAsMetadata::get(builder.getInt8(4)), word(0), ConstantAsMetadata::get(builder.getInt8(0)), nullptr});
  auto *signature = MDNode::get(context, {nullptr, MDNode::get(context, {signature_record}), nullptr});
  module.getOrInsertNamedMetadata("dx.shaderModel")->addOperand(
      MDNode::get(context, {MDString::get(context, "ps"), word(6), word(0)}));
  module.getOrInsertNamedMetadata("dx.entryPoints")->addOperand(
      MDNode::get(context, {ValueAsMetadata::get(symbolic), MDString::get(context, "symbolic"), signature, nullptr, nullptr}));
  std::array<std::array<uint32_t, 4>, 8> mapped_widths{};
  mapped_widths[3] = {10, 10, 10, 2};
  SmallVector<char, 0> bitcode;
  raw_svector_ostream bitcode_stream(bitcode);
  WriteBitcodeToFile(module, bitcode_stream);
  dxmt_msc_lower_logic_outputs_params params{};
  params.bitcode = uintptr_t(bitcode.data());
  params.bitcode_size = bitcode.size();
  params.operation = 10;
  for (unsigned i = 0; i < 4; ++i) params.component_bits[3][i] = mapped_widths[3][i];
  if (dxmt_msc_lower_logic_outputs(&params) != DXMT_MSC_SUCCESS || !params.ir_size ||
      params.framebuffer_space == UINT32_MAX) return 15;
  std::vector<char> transported(params.ir_size);
  const auto sized_space = params.framebuffer_space;
  params.ir = uintptr_t(transported.data());
  params.ir_capacity = transported.size() - 1;
  if (dxmt_msc_lower_logic_outputs(&params) != DXMT_MSC_ERROR_OUTPUT_TOO_SMALL ||
      params.framebuffer_space != UINT32_MAX) return 16;
  params.ir_capacity = transported.size();
  if (dxmt_msc_lower_logic_outputs(&params) != DXMT_MSC_SUCCESS || params.framebuffer_space != sized_space) return 17;
  params.ir = params.bitcode;
  params.ir_capacity = 1;
  if (dxmt_msc_lower_logic_outputs(&params) != DXMT_MSC_ERROR_INVALID_ARGUMENT) return 18;
  params.ir = uintptr_t(&params);
  if (dxmt_msc_lower_logic_outputs(&params) != DXMT_MSC_ERROR_INVALID_ARGUMENT) return 19;
  uint32_t mapped_space;
  auto modern_module = CloneModule(module);
  modern_module->getNamedMetadata("dx.shaderModel")->setOperand(0,
      MDNode::get(context, {MDString::get(context, "ps"), word(6), word(6)}));
  if (!dxmt::dxil::LowerIntegerLogicOutputs(*modern_module, 10, mapped_widths, mapped_space, error) ||
      !modern_module->getFunction("dx.op.createHandleFromBinding") ||
      !modern_module->getFunction("dx.op.annotateHandle") || verifyModule(*modern_module, &errs())) return 13;
  // The clone must not mutate the original metadata graph.
  if (module.getNamedMetadata("dx.entryPoints")->getOperand(0)->getOperand(3)) return 14;
  if (!dxmt::dxil::LowerIntegerLogicOutputs(module, 10, mapped_widths, mapped_space, error)) return 10;
  unsigned fetches = 0;
  for (auto &block : *symbolic) for (auto &instruction : block)
    if (auto *call = dyn_cast<CallInst>(&instruction))
      if (call->getCalledFunction()->getName() == "dx.op.createHandle") {
        if (cast<ConstantInt>(call->getArgOperand(3))->getZExtValue() != 3) return 11;
        ++fetches;
      }
  if (fetches != 16 || mapped_space == UINT32_MAX || verifyModule(module, &errs())) return 12;
  if (argc == 2) {
    SMDiagnostic diagnostics;
    auto shader = parseAssemblyFile(argv[1], diagnostics, context);
    if (!shader) { diagnostics.print(argv[0], errs()); return 8; }
    std::array<std::array<uint32_t, 4>, 8> widths{};
    widths[0] = {8, 8, 8, 8};
    uint32_t space;
    if (!dxmt::dxil::LowerIntegerLogicOutputs(*shader, 10, widths, space, error)) {
      errs() << error << "\n"; return 9;
    }
    for (auto &function : *shader) for (auto &block : function) {
      if (!block.hasName()) block.setName("dxmt.block");
      for (auto &instruction : block)
        if (!instruction.getType()->isVoidTy() && !instruction.hasName()) instruction.setName("dxmt.value");
    }
    shader->setSourceFileName("");
    shader->setModuleIdentifier("");
    shader->print(outs(), nullptr);
    std::cerr << "DXIL framebuffer injection verified space=" << space << " (not GPU qualification)\n";
  }
  (argc == 2 ? std::cerr : std::cout) << "DXIL logic output CPU IR checks=" << checked << " PASS (not GPU qualification)\n";
  return 0;
}

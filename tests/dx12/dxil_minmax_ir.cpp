#include "dxil_minmax.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <cstring>
#include <limits>

static uint32_t Word(const unsigned char *bytes) {
  return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}

static int TransformContainer(const char *path, const char *mode) {
  using namespace llvm;
  auto file = MemoryBuffer::getFile(path);
  if (!file) return 1;
  auto input = (*file)->getBuffer();
  auto *bytes = reinterpret_cast<const unsigned char *>(input.data());
  if (input.size() < 32 || std::memcmp(bytes, "DXBC", 4) || Word(bytes + 24) != input.size()) return 1;
  uint32_t parts = Word(bytes + 28);
  if (parts > (input.size() - 32) / 4) return 1;
  StringRef bitcode;
  for (uint32_t i = 0; i < parts; ++i) {
    auto offset = Word(bytes + 32 + 4 * i);
    if (offset > input.size() - 8) return 1;
    auto size = Word(bytes + offset + 4);
    if (size > input.size() - offset - 8) return 1;
    if (std::memcmp(bytes + offset, "DXIL", 4)) continue;
    if (!bitcode.empty() || size < 24) return 1;
    auto *program = bytes + offset + 8;
    auto start = Word(program + 16), length = Word(program + 20);
    if (std::memcmp(program + 8, "DXIL", 4) || start < 16 || start > size - 8 || length > size - 8 - start) return 1;
    bitcode = StringRef(reinterpret_cast<const char *>(program + 8 + start), length);
  }
  if (bitcode.empty()) return 1;
  LLVMContext context;
  context.setOpaquePointers(false);
  auto parsed = parseBitcodeFile(MemoryBufferRef(bitcode, path), context);
  if (!parsed) { logAllUnhandledErrors(parsed.takeError(), errs()); return 1; }
  SmallVector<CallInst *, 4> samples;
  for (auto &function : **parsed) for (auto &block : function) for (auto &instruction : block)
    if (auto *call = dyn_cast<CallInst>(&instruction))
      if (call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32") samples.push_back(call);
  if (samples.size() != 1) return 1;
  IRBuilder<> builder(context);
  auto number = [&](float value) { return ConstantFP::get(builder.getFloatTy(), value); };
  const bool maximum = !std::strcmp(mode, "maximum");
  const bool sampler_lod = !std::strcmp(mode, "sampler-lod");
  const bool fractional = !std::strcmp(mode, "fractional");
  const bool empty = !std::strcmp(mode, "empty");
  const bool huge_clamp = !std::strcmp(mode, "huge-clamp");
  const bool mirror = !std::strcmp(mode, "mirror-offset");
  const bool mirror_once = !std::strcmp(mode, "mirror-once-offset");
  if (std::strcmp(mode, "minimum") && !maximum && !sampler_lod && !fractional && !empty && !huge_clamp && !mirror && !mirror_once) return 1;
  if (huge_clamp) samples[0]->setArgOperand(3, number(std::numeric_limits<float>::max()));
  if (mirror || mirror_once) {
    samples[0]->setArgOperand(3, number(mirror ? 1.25f : -0.25f));
    samples[0]->setArgOperand(4, number(0.25f));
    samples[0]->setArgOperand(7, builder.getInt32(1));
  }
  dxmt::dxil::ReductionSampleState state{builder.getInt32(maximum ? 15 : 7), number(sampler_lod ? 1 : 0),
      number(100), number(empty ? 1.1f : fractional ? 0.75f : 0), builder.getInt32(0),
      samples[0]->getArgOperand(1), builder.getInt32(mirror ? 2 : mirror_once ? 5 : 3), builder.getInt32(3)};
  std::string error;
  if (!dxmt::dxil::LowerReductionSampleLevel2D(*samples[0], state, error)) { errs() << error; return 1; }
  // DXC's older text assembler requires explicit names for the newly inserted
  // unnamed values/blocks, exactly as the typed-origin preparation path does.
  for (auto &function : **parsed) for (auto &block : function) {
    if (!block.hasName()) block.setName("dxmt.block");
    for (auto &instruction : block)
      if (!instruction.getType()->isVoidTy() && !instruction.hasName()) instruction.setName("dxmt.value");
  }
  if (verifyModule(**parsed, &errs())) return 1;
  (*parsed)->setSourceFileName("");
  (*parsed)->setModuleIdentifier("");
  (*parsed)->print(outs(), nullptr);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 || argc == 3) return TransformContainer(argv[1], argc == 3 ? argv[2] : "minimum");
  if (argc != 1) return 1;
  using namespace llvm;
  LLVMContext context;
  context.setOpaquePointers(false);
  Module module("reduction", context);
  IRBuilder<> builder(context);
  auto *f32 = builder.getFloatTy();
  auto *i32 = builder.getInt32Ty();
  auto *handle = StructType::create(context, {builder.getInt8PtrTy()}, "dx.types.Handle");
  auto *result_type = StructType::create(context, {f32, f32, f32, f32, i32}, "dx.types.ResRet.f32");
  auto *sample_type = FunctionType::get(result_type,
      {i32, handle, handle, f32, f32, f32, f32, i32, i32, i32, f32}, false);
  auto sample = module.getOrInsertFunction("dx.op.sampleLevel.f32", sample_type);
  auto *function = Function::Create(FunctionType::get(f32,
      {handle, handle, f32, f32, f32, i32, f32, f32, f32, i32}, false),
      Function::ExternalLinkage, "test", module);
  builder.SetInsertPoint(BasicBlock::Create(context, "entry", function));
  auto *call = builder.CreateCall(sample, {builder.getInt32(62), function->getArg(0), function->getArg(1),
      function->getArg(2), function->getArg(3), UndefValue::get(f32), UndefValue::get(f32),
      builder.getInt32(1), builder.getInt32(-1), UndefValue::get(i32), function->getArg(4)});
  auto *extract = builder.CreateExtractValue(call, 0);
  builder.CreateRet(extract);
  dxmt::dxil::ReductionSampleState state{function->getArg(5), function->getArg(6), function->getArg(7),
      function->getArg(8), function->getArg(9), function->getArg(0), builder.getInt32(3), builder.getInt32(3)};
  std::string error;
  // A status consumer must reject before any CFG/declaration mutation.
  auto *status = ExtractValueInst::Create(call, {4}, "status", call->getNextNode());
  const auto blocks = function->size(), functions = module.size();
  if (dxmt::dxil::LowerReductionSampleLevel2D(*call, state, error) || error.empty() ||
      function->size() != blocks || module.size() != functions) return 1;
  status->eraseFromParent();
  auto *late = BinaryOperator::CreateAdd(function->getArg(5), builder.getInt32(1), "late", call->getNextNode());
  auto bad_state = state;
  bad_state.flags = late;
  if (dxmt::dxil::LowerReductionSampleLevel2D(*call, bad_state, error) || error.empty() ||
      function->size() != blocks || module.size() != functions) return 1;
  late->eraseFromParent();
  if (!dxmt::dxil::LowerReductionSampleLevel2D(*call, state, error) || !error.empty()) {
    errs() << error << '\n'; return 1;
  }
  if (verifyModule(module, &errs())) return 1;
  unsigned samples = 0, dimensions = 0, conditional = 0;
  for (auto &block : *function) for (auto &instruction : block) {
    if (auto *branch = dyn_cast<BranchInst>(&instruction)) conditional += branch->isConditional();
    auto *operation = dyn_cast<CallInst>(&instruction);
    if (!operation || !operation->getCalledFunction()) continue;
    auto name = operation->getCalledFunction()->getName();
    samples += name == "dx.op.sampleLevel.f32";
    dimensions += name == "dx.op.getDimensions";
  }
  // Two mip footprints, four tap sites each; six optional spatial taps, one
  // optional upper mip, and a no-sample empty-set branch.
  if (samples != 8 || dimensions != 3 || conditional != 8) return 1;
  module.print(outs(), nullptr);
  return 0;
}

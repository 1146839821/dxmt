#include "dxil_minmax.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <cstring>
#include <limits>

static uint32_t Word(const unsigned char *bytes) {
  return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}

static bool CheckBindingQualification(llvm::Module &module) {
  using namespace llvm;
  SmallVector<char, 0> bitcode;
  raw_svector_ostream serialized(bitcode);
  WriteBitcodeToFile(module, serialized);
  for (unsigned probe = 0; probe < 10; ++probe) {
    LLVMContext context;
    context.setOpaquePointers(false);
    auto parsed = parseBitcodeFile(MemoryBufferRef(StringRef(bitcode.data(), bitcode.size()), "probe"), context);
    if (!parsed) { consumeError(parsed.takeError()); return false; }
    auto &clone = *parsed;
    auto *resources = clone->getNamedMetadata("dx.resources")->getOperand(0);
    auto *texture = cast<MDNode>(cast<MDNode>(resources->getOperand(0))->getOperand(0));
    IRBuilder<> builder(clone->getContext());
    if (probe == 0) texture->replaceOperandWith(3, ConstantAsMetadata::get(builder.getInt32(DXMT_MSC_MINMAX_SPACE)));
    if (probe == 1) texture->replaceOperandWith(5, ConstantAsMetadata::get(builder.getInt32(2)));
    if (probe == 9) texture->replaceOperandWith(6, ConstantAsMetadata::get(builder.getInt32(5))); // Cube remains excluded.
    CallInst *sample = nullptr;
    for (auto &function : *clone) for (auto &block : function) for (auto &instruction : block)
      if (auto *call = dyn_cast<CallInst>(&instruction))
        if (call->getCalledFunction() && (call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32" ||
            call->getCalledFunction()->getName() == "dx.op.sampleGrad.f32")) sample = call;
    if (!sample) return false;
    if (probe == 2) cast<CallInst>(sample->getArgOperand(2))->setArgOperand(4, builder.getTrue());
    if (probe == 3) ExtractValueInst::Create(sample, {4}, "status", sample->getNextNode());
    if (probe == 4) {
      auto *duplicate = cast<CallInst>(sample->clone());
      duplicate->insertBefore(sample);
      ExtractValueInst::Create(duplicate, {0}, "duplicate.result", sample);
    }
    if (probe == 5) {
      std::vector<Metadata *> operands;
      for (auto &operand : texture->operands()) operands.push_back(operand.get());
      operands[0] = ConstantAsMetadata::get(builder.getInt32(1));
      resources->replaceOperandWith(0, MDNode::get(context, {texture, MDNode::get(context, operands)}));
    }
    if (probe == 6 || probe == 7) {
      std::vector<Metadata *> groups;
      for (auto &operand : resources->operands()) groups.push_back(operand.get());
      if (probe == 7) groups[0] = nullptr;
      auto *entry = clone->getNamedMetadata("dx.entryPoints")->getOperand(0);
      entry->replaceOperandWith(3, MDNode::getDistinct(context, groups));
    }
    if (probe == 8) {
      auto *later = cast<CallInst>(sample->clone());
      later->insertBefore(sample->getNextNode());
      later->setArgOperand(7, builder.getInt32(-9));
      ExtractValueInst::Create(later, {0}, "later.result", later->getNextNode());
    }
    std::vector<dxmt_msc_minmax_binding> output{{11, 22, 33, 44}};
    std::string error;
    bool accepted = dxmt::dxil::LowerReductionSamplerBindings(*clone, output, error);
    if (probe == 4) {
      if (!accepted || !error.empty() || output.size() != 1 || output[0].texture_space || output[0].texture_register ||
          output[0].sampler_space || output[0].sampler_register || verifyModule(*clone, &errs())) return false;
      unsigned samples = 0;
      for (auto &function : *clone) for (auto &block : function) for (auto &instruction : block)
        if (auto *call = dyn_cast<CallInst>(&instruction))
          if (call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32") ++samples;
      if (samples != 18) return false;
      auto *result_resources = clone->getNamedMetadata("dx.resources")->getOperand(0);
      auto *cbvs = cast<MDNode>(result_resources->getOperand(2));
      if (cbvs->getNumOperands() != 1) return false;
      auto *cbv = cast<MDNode>(cbvs->getOperand(0));
      auto metadata_word = [](Metadata *metadata) { return mdconst::extract<ConstantInt>(metadata)->getZExtValue(); };
      if (metadata_word(cbv->getOperand(3)) != DXMT_MSC_MINMAX_SPACE || metadata_word(cbv->getOperand(4)) ||
          metadata_word(cbv->getOperand(6)) != sizeof(dxmt_msc_minmax_state)) return false;
      for (unsigned kind : {0u, 3u}) {
        auto *list = cast<MDNode>(result_resources->getOperand(kind));
        if (list->getNumOperands() != (kind == 0 ? 2 : 3)) return false;
        auto *original = cast<MDNode>(list->getOperand(0));
        if (metadata_word(original->getOperand(0)) || metadata_word(original->getOperand(3)) ||
            metadata_word(original->getOperand(4))) return false;
        for (unsigned ordinal = 1; ordinal < list->getNumOperands(); ++ordinal) {
          auto *record = cast<MDNode>(list->getOperand(ordinal));
          if (metadata_word(record->getOperand(0)) != ordinal || metadata_word(record->getOperand(3)) != DXMT_MSC_MINMAX_SPACE ||
              metadata_word(record->getOperand(4)) != ordinal - 1) return false;
        }
      }
      if (clone->getNamedMetadata("dx.entryPoints")->getOperand(0)->getOperand(3) != result_resources) return false;
    } else if (accepted || error.empty() || output.size() != 1 || output[0].texture_space != 11 ||
        output[0].texture_register != 22 || output[0].sampler_space != 33 || output[0].sampler_register != 44) return false;
  }
  return true;
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
      if (call->getCalledFunction() && (call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32" ||
          call->getCalledFunction()->getName() == "dx.op.sampleGrad.f32")) samples.push_back(call);
  const bool binding_two = !std::strcmp(mode, "binding-two");
  const bool binding_grad = !std::strcmp(mode, "binding-grad");
  if (samples.size() != (binding_two ? 2 : 1)) return 1;
  // Keep every generated tap/ordinary branch on the original array layer.
  auto *array_coordinate = samples[0]->getArgOperand(5);
  IRBuilder<> builder(context);
  auto number = [&](float value) { return ConstantFP::get(builder.getFloatTy(), value); };
  const bool maximum = !std::strcmp(mode, "maximum");
  const bool sampler_lod = !std::strcmp(mode, "sampler-lod");
  const bool fractional = !std::strcmp(mode, "fractional");
  const bool empty = !std::strcmp(mode, "empty");
  const bool huge_clamp = !std::strcmp(mode, "huge-clamp");
  const bool mirror = !std::strcmp(mode, "mirror-offset");
  const bool mirror_once = !std::strcmp(mode, "mirror-once-offset");
  const bool binding = !std::strcmp(mode, "binding") || binding_two || binding_grad;
  if (std::strcmp(mode, "minimum") && !maximum && !sampler_lod && !fractional && !empty && !huge_clamp && !mirror && !mirror_once && !binding) return 1;
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
  if (binding) {
    if (!binding_two && !CheckBindingQualification(**parsed)) return 1;
    std::vector<dxmt_msc_minmax_binding> records;
    if (!dxmt::dxil::LowerReductionSamplerBindings(**parsed, records, error)) { errs() << error; return 1; }
    if (records.size() != (binding_two ? 2 : 1) || records[0].texture_space || records[0].texture_register ||
        records[0].sampler_space || records[0].sampler_register) return 1;
    if (binding_two && (records[1].texture_space || records[1].texture_register ||
        records[1].sampler_space || records[1].sampler_register != 1)) return 1;
  } else if (!dxmt::dxil::LowerReductionSampleLevel2D(*samples[0], state, error)) { errs() << error; return 1; }
  for (auto &function : **parsed) for (auto &block : function) for (auto &instruction : block)
    if (auto *call = dyn_cast<CallInst>(&instruction))
      if (!binding_two && call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32" &&
          call->getArgOperand(5) != array_coordinate) return 1;
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
  auto *gradient_function = Function::Create(FunctionType::get(f32,
      {handle, handle, f32, f32, f32, f32}, false), Function::ExternalLinkage, "gradient", module);
  builder.SetInsertPoint(BasicBlock::Create(context, "entry", gradient_function));
  auto gradient = module.getOrInsertFunction("dx.op.sampleGrad.f32", result_type,
      i32, handle, handle, f32, f32, f32, f32, i32, i32, i32, f32, f32, f32, f32, f32, f32, f32);
  auto *gradient_call = builder.CreateCall(gradient, {builder.getInt32(63), gradient_function->getArg(0),
      gradient_function->getArg(1), ConstantFP::get(f32, 0.5), ConstantFP::get(f32, 0.5),
      UndefValue::get(f32), UndefValue::get(f32), builder.getInt32(0), builder.getInt32(0),
      UndefValue::get(i32), gradient_function->getArg(2), gradient_function->getArg(3), UndefValue::get(f32),
      gradient_function->getArg(4), gradient_function->getArg(5), UndefValue::get(f32), UndefValue::get(f32)});
  builder.CreateRet(ConstantFP::get(f32, 0));
  const auto instructions_before = gradient_function->getEntryBlock().size();
  const auto declarations_before = module.size();
  gradient_call->setArgOperand(0, builder.getInt32(62));
  if (dxmt::dxil::CreateReductionGradientLOD2D(*gradient_call, error) || error.empty() ||
      gradient_function->getEntryBlock().size() != instructions_before || module.size() != declarations_before) return 1;
  gradient_call->setArgOperand(0, builder.getInt32(63));
  auto *gradient_lod = dxmt::dxil::CreateReductionGradientLOD2D(*gradient_call, error);
  if (!gradient_lod || !error.empty()) { errs() << error; return 1; }
  cast<ReturnInst>(gradient_function->getEntryBlock().getTerminator())->setOperand(0, gradient_lod);
  unsigned abs_calls = 0, sqrt_calls = 0, log_calls = 0, dimension_calls = 0;
  for (auto &instruction : gradient_function->getEntryBlock()) {
    if (auto *fp_operation = dyn_cast<FPMathOperator>(&instruction))
      if (fp_operation->getFastMathFlags().any()) return 1;
    auto *operation = dyn_cast<CallInst>(&instruction);
    if (!operation || !operation->getCalledFunction()) continue;
    auto name = operation->getCalledFunction()->getName();
    dimension_calls += name == "dx.op.getDimensions";
    if (name != "dx.op.unary.f32") continue;
    auto opcode = cast<ConstantInt>(operation->getArgOperand(0))->getZExtValue();
    abs_calls += opcode == 6;
    sqrt_calls += opcode == 24;
    log_calls += opcode == 23;
  }
  if (abs_calls != 4 || sqrt_calls != 1 || log_calls != 2 || dimension_calls != 1 || verifyModule(module, &errs())) return 1;
  module.print(outs(), nullptr);
  return 0;
}

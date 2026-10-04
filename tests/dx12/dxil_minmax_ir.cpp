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
#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>

// Evaluate the emitted scalar LOD graph, not a second projection algorithm.
// Dimensions supplies a synthetic cube side of eight. This is a CPU arithmetic
// oracle for this primitive, not DXIL validation, MSC compilation or GPU proof.
static double EvaluateCubeLOD(llvm::Value *root, llvm::Function &function,
    const std::array<float, 9> &input, bool &valid) {
  using namespace llvm;
  std::unordered_map<Value *, double> values;
  for (unsigned i = 0; i < input.size(); ++i) values[function.getArg(i + 2)] = input[i];
  std::function<double(Value *)> evaluate = [&](Value *value) -> double {
    if (auto found = values.find(value); found != values.end()) return found->second;
    if (auto *constant = dyn_cast<ConstantFP>(value)) return constant->getValueAPF().convertToDouble();
    if (auto *constant = dyn_cast<ConstantInt>(value)) return constant->getZExtValue();
    if (auto *select = dyn_cast<SelectInst>(value))
      return values[value] = evaluate(select->getCondition()) ? evaluate(select->getTrueValue()) : evaluate(select->getFalseValue());
    if (auto *extract = dyn_cast<ExtractValueInst>(value)) {
      auto *query = dyn_cast<CallInst>(extract->getAggregateOperand());
      if (query && query->getCalledFunction() && query->getCalledFunction()->getName() == "dx.op.getDimensions" &&
          extract->getNumIndices() == 1 && *extract->idx_begin() == 0) return values[value] = 8;
    }
    if (auto *call = dyn_cast<CallInst>(value)) {
      auto *callee = call->getCalledFunction();
      auto *opcode = dyn_cast<ConstantInt>(call->getArgOperand(0));
      if (callee && opcode && callee->getName() == "dx.op.unary.f32") {
        const double x = evaluate(call->getArgOperand(1));
        if (opcode->getZExtValue() == 6) return values[value] = std::fabs(x);
        if (opcode->getZExtValue() == 23) return values[value] = std::log2(x);
        if (opcode->getZExtValue() == 24) return values[value] = std::sqrt(x);
      }
      if (callee && opcode && callee->getName() == "dx.op.binary.f32" && opcode->getZExtValue() == 35)
        return values[value] = std::fmax(evaluate(call->getArgOperand(1)), evaluate(call->getArgOperand(2)));
    }
    if (auto *compare = dyn_cast<FCmpInst>(value)) {
      const double x = evaluate(compare->getOperand(0)), y = evaluate(compare->getOperand(1));
      if (compare->getPredicate() == CmpInst::FCMP_OGE) return values[value] = x >= y;
      if (compare->getPredicate() == CmpInst::FCMP_OGT) return values[value] = x > y;
      if (compare->getPredicate() == CmpInst::FCMP_OEQ) return values[value] = x == y;
    }
    if (auto *instruction = dyn_cast<Instruction>(value)) {
      if (instruction->getOpcode() == Instruction::UIToFP) return values[value] = evaluate(instruction->getOperand(0));
      if (isa<BinaryOperator>(instruction)) {
        const double x = evaluate(instruction->getOperand(0)), y = evaluate(instruction->getOperand(1));
        switch (instruction->getOpcode()) {
        case Instruction::FAdd: return values[value] = x + y;
        case Instruction::FSub: return values[value] = x - y;
        case Instruction::FMul: return values[value] = x * y;
        case Instruction::FDiv: return values[value] = x / y;
        case Instruction::And: return values[value] = bool(x) && bool(y);
        case Instruction::Or: return values[value] = bool(x) || bool(y);
        default: break;
        }
      }
    }
    valid = false;
    return std::numeric_limits<double>::quiet_NaN();
  };
  return evaluate(root);
}

// Execute the generated CFG against a synthetic two-by-two cube. This catches
// incorrect face routing and skipped/extra taps; it is not a GPU sampler oracle.
static double EvaluateCubeFootprint(llvm::Function &function, std::array<float, 3> direction,
    unsigned flags, bool &valid, std::unordered_map<unsigned, unsigned> &visited, bool unique = false) {
  using namespace llvm;
  std::unordered_map<Value *, double> values;
  for (unsigned axis = 0; axis < 3; ++axis) values[function.getArg(2 + axis)] = direction[axis];
  values[function.getArg(5)] = flags;
  values[function.getArg(6)] = 0; values[function.getArg(7)] = 100;
  values[function.getArg(8)] = 0; values[function.getArg(9)] = 0;
  std::function<double(Value *)> evaluate = [&](Value *value) -> double {
    if (auto found = values.find(value); found != values.end()) return found->second;
    if (auto *constant = dyn_cast<ConstantFP>(value)) return constant->getValueAPF().convertToDouble();
    if (auto *constant = dyn_cast<ConstantInt>(value)) return constant->getZExtValue();
    if (auto *select = dyn_cast<SelectInst>(value))
      return values[value] = evaluate(select->getCondition()) ? evaluate(select->getTrueValue()) : evaluate(select->getFalseValue());
    if (auto *extract = dyn_cast<ExtractValueInst>(value)) {
      auto *call = dyn_cast<CallInst>(extract->getAggregateOperand());
      if (call && call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.getDimensions")
        return values[value] = *extract->idx_begin() == 3 ? 1 : 2;
      if (call && call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32") {
        if (values.count(call)) return values[value] = values[call];
        double xyz[3];
        for (unsigned axis = 0; axis < 3; ++axis) xyz[axis] = evaluate(call->getArgOperand(3 + axis));
        const unsigned axis = std::fabs(xyz[2]) >= std::fabs(xyz[0]) && std::fabs(xyz[2]) >= std::fabs(xyz[1]) ? 2 :
            std::fabs(xyz[1]) >= std::fabs(xyz[0]) ? 1 : 0;
        const unsigned face = axis * 2 + (xyz[axis] < 0);
        double u = 0, v = 0;
        switch (face) {
        case 0: u = -xyz[2]; v = -xyz[1]; break;
        case 1: u = xyz[2]; v = -xyz[1]; break;
        case 2: u = xyz[0]; v = xyz[2]; break;
        case 3: u = xyz[0]; v = -xyz[2]; break;
        case 4: u = xyz[0]; v = -xyz[1]; break;
        case 5: u = -xyz[0]; v = -xyz[1]; break;
        }
        const unsigned x = u >= 0, y = v >= 0;
        const unsigned identity = face * 4 + y * 2 + x;
        ++visited[identity];
        if (unique) return values[value] = identity;
        const double constants[] = {0,112,32,160,80,144};
        if (face) return values[value] = constants[face];
        const double texels[] = {16,64,192,240};
        return values[value] = texels[y * 2 + x];
      }
    }
    if (auto *call = dyn_cast<CallInst>(value)) {
      auto *callee = call->getCalledFunction();
      auto *opcode = dyn_cast<ConstantInt>(call->getArgOperand(0));
      if (callee && opcode && callee->getName() == "dx.op.unary.f32") {
        const double x = evaluate(call->getArgOperand(1));
        if (opcode->getZExtValue() == 6) return values[value] = std::fabs(x);
        if (opcode->getZExtValue() == 27) return values[value] = std::floor(x);
      }
      if (callee && opcode && callee->getName() == "dx.op.binary.f32") {
        const double x = evaluate(call->getArgOperand(1)), y = evaluate(call->getArgOperand(2));
        if (opcode->getZExtValue() == 35) return values[value] = std::fmax(x, y);
        if (opcode->getZExtValue() == 36) return values[value] = std::fmin(x, y);
      }
    }
    if (auto *compare = dyn_cast<CmpInst>(value)) {
      const double x = evaluate(compare->getOperand(0)), y = evaluate(compare->getOperand(1));
      switch (compare->getPredicate()) {
      case CmpInst::FCMP_OGE: return values[value] = x >= y;
      case CmpInst::FCMP_OGT: return values[value] = x > y;
      case CmpInst::FCMP_OLT: return values[value] = x < y;
      case CmpInst::ICMP_EQ: return values[value] = x == y;
      case CmpInst::ICMP_NE: return values[value] = x != y;
      default: break;
      }
    }
    if (auto *instruction = dyn_cast<Instruction>(value)) {
      if (instruction->getOpcode() == Instruction::UIToFP || instruction->getOpcode() == Instruction::FPToUI)
        return values[value] = evaluate(instruction->getOperand(0));
      if (isa<BinaryOperator>(instruction)) {
        const double x = evaluate(instruction->getOperand(0)), y = evaluate(instruction->getOperand(1));
        switch (instruction->getOpcode()) {
        case Instruction::FAdd: return values[value] = x + y;
        case Instruction::FSub: return values[value] = x - y;
        case Instruction::FMul: return values[value] = x * y;
        case Instruction::FDiv: return values[value] = x / y;
        case Instruction::Sub: return values[value] = x - y;
        case Instruction::Add: return values[value] = x + y;
        case Instruction::And: return values[value] = unsigned(x) & unsigned(y);
        case Instruction::Or: return values[value] = unsigned(x) | unsigned(y);
        default: break;
        }
      }
    }
    valid = false;
    return std::numeric_limits<double>::quiet_NaN();
  };
  BasicBlock *block = &function.getEntryBlock(), *previous = nullptr;
  for (unsigned steps = 0; steps < 100 && valid; ++steps) {
    for (auto &phi : block->phis()) {
      const int incoming = phi.getBasicBlockIndex(previous);
      if (incoming < 0) { valid = false; return 0; }
      values[&phi] = evaluate(phi.getIncomingValue(incoming));
    }
    // Execute samples on block visitation, even if their result is discarded.
    for (auto &instruction : *block)
      if (auto *call = dyn_cast<CallInst>(&instruction)) {
        if (call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32") {
          ExtractValueInst *component = nullptr;
          for (auto *user : call->users()) if ((component = dyn_cast<ExtractValueInst>(user))) break;
          if (!component) { valid = false; return 0; }
          values[call] = evaluate(component);
        }
      }
    if (auto *result = dyn_cast<ReturnInst>(block->getTerminator())) return evaluate(result->getReturnValue());
    auto *branch = dyn_cast<BranchInst>(block->getTerminator());
    if (!branch) break;
    previous = block;
    block = branch->getSuccessor(branch->isConditional() && !evaluate(branch->getCondition()) ? 1 : 0);
  }
  valid = false;
  return 0;
}

static uint32_t Word(const unsigned char *bytes) {
  return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}

static bool CheckImplicitQualification(llvm::Module &module) {
  using namespace llvm;
  SmallVector<char, 0> bitcode; raw_svector_ostream serialized(bitcode);
  WriteBitcodeToFile(module, serialized);
  for (unsigned probe = 0; probe < 4; ++probe) {
    LLVMContext context; context.setOpaquePointers(false);
    auto clone = parseBitcodeFile(MemoryBufferRef(StringRef(bitcode.data(), bitcode.size()), "implicit-negative"), context);
    if (!clone) { consumeError(clone.takeError()); return false; }
    auto *model = (*clone)->getNamedMetadata("dx.shaderModel");
    IRBuilder<> builder(context);
    if (probe == 0) {
      if (!model || model->getNumOperands() != 1) return false;
      auto *record = model->getOperand(0);
      model->setOperand(0, MDNode::get(context, {MDString::get(context, "cs"), record->getOperand(1), record->getOperand(2)}));
    } else if (probe == 1) (*clone)->eraseNamedMetadata(model);
    else {
      CallInst *sample = nullptr;
      for (auto &function : **clone) for (auto &block : function) for (auto &instruction : block)
        if (auto *call = dyn_cast<CallInst>(&instruction))
          if (call->getCalledFunction() && (call->getCalledFunction()->getName() == "dx.op.sample.f32" ||
              call->getCalledFunction()->getName() == "dx.op.sampleBias.f32")) sample = call;
      if (!sample) return false;
      if (probe == 2) sample->setArgOperand(0, builder.getInt32(62));
      else ExtractValueInst::Create(sample, {4}, "status", sample->getNextNode());
    }
    std::vector<dxmt_msc_minmax_binding> output{{11, 22, 33, 44}}; std::string error;
    if (dxmt::dxil::LowerReductionSamplerBindings(**clone, output, error) || error.empty() || output.size() != 1 ||
        output[0].texture_space != 11 || output[0].texture_register != 22 ||
        output[0].sampler_space != 33 || output[0].sampler_register != 44) return false;
  }
  return true;
}

static bool CheckBindingQualification(llvm::Module &module) {
  using namespace llvm;
  SmallVector<char, 0> bitcode;
  raw_svector_ostream serialized(bitcode);
  WriteBitcodeToFile(module, serialized);
  for (unsigned probe = 0; probe < 11; ++probe) {
    LLVMContext context;
    context.setOpaquePointers(false);
    auto parsed = parseBitcodeFile(MemoryBufferRef(StringRef(bitcode.data(), bitcode.size()), "probe"), context);
    if (!parsed) { consumeError(parsed.takeError()); return false; }
    auto &clone = *parsed;
    auto *resources = clone->getNamedMetadata("dx.resources")->getOperand(0);
    auto *texture = cast<MDNode>(cast<MDNode>(resources->getOperand(0))->getOperand(0));
    IRBuilder<> builder(clone->getContext());
    if (probe == 0) texture->replaceOperandWith(3, ConstantAsMetadata::get(builder.getInt32(DXMT_MSC_MINMAX_SPACE)));
    if (probe == 9) texture->replaceOperandWith(6, ConstantAsMetadata::get(builder.getInt32(5))); // Cube remains excluded.
    CallInst *sample = nullptr;
    for (auto &function : *clone) for (auto &block : function) for (auto &instruction : block)
      if (auto *call = dyn_cast<CallInst>(&instruction))
        if (call->getCalledFunction() && (call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32" ||
            call->getCalledFunction()->getName() == "dx.op.sampleGrad.f32")) sample = call;
    if (!sample) return false;
    if (probe == 1) {
      texture->replaceOperandWith(5, ConstantAsMetadata::get(builder.getInt32(2)));
      cast<CallInst>(sample->getArgOperand(1))->setArgOperand(3, builder.getInt32(2));
    }
    if (probe == 10) {
      texture->replaceOperandWith(5, ConstantAsMetadata::get(builder.getInt32(2)));
      cast<CallInst>(sample->getArgOperand(1))->setArgOperand(3, builder.getInt32(1));
    }
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
    if (probe == 10) {
      if (!accepted || !error.empty() || output.size() != 1 || output[0].texture_register != 1 ||
          verifyModule(*clone, &errs())) return false;
      auto *list = cast<MDNode>(clone->getNamedMetadata("dx.resources")->getOperand(0)->getOperand(0));
      auto *original = cast<MDNode>(list->getOperand(0));
      auto *private_texture = cast<MDNode>(list->getOperand(1));
      if (mdconst::extract<ConstantInt>(original->getOperand(5))->getZExtValue() != 2 ||
          mdconst::extract<ConstantInt>(private_texture->getOperand(5))->getZExtValue() != 1) return false;
    } else if (probe == 4) {
      if (!accepted || !error.empty() || output.size() != 1 || output[0].texture_space || output[0].texture_register ||
          output[0].sampler_space || output[0].sampler_register || verifyModule(*clone, &errs())) return false;
      unsigned samples = 0;
      for (auto &function : *clone) for (auto &block : function) for (auto &instruction : block)
        if (auto *call = dyn_cast<CallInst>(&instruction))
          if (call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32") ++samples;
      const auto texture_kind = mdconst::extract<ConstantInt>(texture->getOperand(6))->getZExtValue();
      if (samples != (texture_kind == 4 ? 34 : texture_kind == 1 || texture_kind == 6 ? 10 : 18)) return false;
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

static bool CheckModernQualification(llvm::Module &module) {
  using namespace llvm;
  SmallVector<char, 0> bitcode;
  raw_svector_ostream serialized(bitcode);
  WriteBitcodeToFile(module, serialized);
  for (unsigned probe = 0; probe < 4; ++probe) {
    LLVMContext context;
    context.setOpaquePointers(false);
    auto parsed = parseBitcodeFile(MemoryBufferRef(StringRef(bitcode.data(), bitcode.size()), "modern.probe"), context);
    if (!parsed) { consumeError(parsed.takeError()); return false; }
    CallInst *sample = nullptr;
    for (auto &function : **parsed) for (auto &block : function) for (auto &instruction : block)
      if (auto *call = dyn_cast<CallInst>(&instruction))
        if (call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32") sample = call;
    if (!sample) return false;
    auto *annotation = cast<CallInst>(sample->getArgOperand(2));
    auto *binding = cast<CallInst>(annotation->getArgOperand(1));
    IRBuilder<> builder(context);
    if (probe == 1) binding->setArgOperand(2, builder.getInt32(2)); // Outside [0,1].
    if (probe == 2) binding->setArgOperand(3, builder.getTrue());
    if (probe == 3) {
      auto *properties = cast<StructType>(annotation->getArgOperand(2)->getType());
      annotation->setArgOperand(2, ConstantStruct::get(properties, {builder.getInt32(14), builder.getInt32(1)}));
    }
    std::vector<dxmt_msc_minmax_binding> records{{11, 22, 33, 44}};
    std::string error;
    const bool accepted = dxmt::dxil::LowerReductionSamplerBindings(**parsed, records, error);
    if (probe == 0) {
      if (!accepted || !error.empty() || records.size() != 1 || records[0].texture_register != 1 ||
          records[0].sampler_register != 1 || verifyModule(**parsed, &errs())) return false;
    } else if (accepted || error.empty() || records.size() != 1 || records[0].texture_space != 11 ||
        records[0].texture_register != 22 || records[0].sampler_space != 33 || records[0].sampler_register != 44)
      return false;
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
          call->getCalledFunction()->getName() == "dx.op.sampleGrad.f32" ||
          call->getCalledFunction()->getName() == "dx.op.sample.f32" ||
          call->getCalledFunction()->getName() == "dx.op.sampleBias.f32")) samples.push_back(call);
  const bool binding_implicit = !std::strcmp(mode, "binding-implicit");
  const bool binding_shared = !std::strcmp(mode, "binding-shared");
  const bool binding_two = !std::strcmp(mode, "binding-two") || binding_implicit || binding_shared;
  const bool binding_grad = !std::strcmp(mode, "binding-grad");
  const bool binding_array = !std::strcmp(mode, "binding-array");
  if (samples.size() != (binding_two ? 2 : 1)) return 1;
  // Keep every generated tap/ordinary branch on the original array layer.
  auto *resource_groups = (*parsed)->getNamedMetadata("dx.resources")->getOperand(0);
  auto *texture_record = cast<MDNode>(cast<MDNode>(resource_groups->getOperand(0))->getOperand(0));
  const auto texture_kind = mdconst::extract<ConstantInt>(texture_record->getOperand(6))->getZExtValue();
  const unsigned layer_operand = texture_kind == 6 ? 4 : texture_kind == 7 ? 5 : 6;
  auto *array_coordinate = samples[0]->getArgOperand(layer_operand);
  IRBuilder<> builder(context);
  auto number = [&](float value) { return ConstantFP::get(builder.getFloatTy(), value); };
  const bool maximum = !std::strcmp(mode, "maximum");
  const bool sampler_lod = !std::strcmp(mode, "sampler-lod");
  const bool fractional = !std::strcmp(mode, "fractional");
  const bool empty = !std::strcmp(mode, "empty");
  const bool huge_clamp = !std::strcmp(mode, "huge-clamp");
  const bool mirror = !std::strcmp(mode, "mirror-offset");
  const bool mirror_once = !std::strcmp(mode, "mirror-once-offset");
  const bool binding = !std::strcmp(mode, "binding") || binding_two || binding_grad || binding_array;
  if (std::strcmp(mode, "minimum") && !maximum && !sampler_lod && !fractional && !empty && !huge_clamp && !mirror && !mirror_once && !binding) return 1;
  if (huge_clamp) samples[0]->setArgOperand(3, number(std::numeric_limits<float>::max()));
  if (mirror || mirror_once) {
    samples[0]->setArgOperand(3, number(mirror ? 1.25f : -0.25f));
    samples[0]->setArgOperand(4, number(0.25f));
    samples[0]->setArgOperand(7, builder.getInt32(1));
  }
  dxmt::dxil::ReductionSampleState state{builder.getInt32(maximum ? 15 : 7), number(sampler_lod ? 1 : 0),
      number(100), number(empty ? 1.1f : fractional ? 0.75f : 0), builder.getInt32(0),
      samples[0]->getArgOperand(1), builder.getInt32(mirror ? 2 : mirror_once ? 5 : 3), builder.getInt32(3), builder.getInt32(3)};
  std::string error;
  if (binding) {
    if (binding_implicit && !CheckImplicitQualification(**parsed)) return 1;
    const bool modern = (*parsed)->getFunction("dx.op.createHandleFromBinding") != nullptr;
    if (modern && binding_array && !CheckModernQualification(**parsed)) return 1;
    if (!modern && !binding_two && !binding_array && !CheckBindingQualification(**parsed)) return 1;
    std::vector<dxmt_msc_minmax_binding> records;
    if (!dxmt::dxil::LowerReductionSamplerBindings(**parsed, records, error,
        binding_shared ? 2 : 0, binding_shared ? 4 : 0)) { errs() << error; return 1; }
    if (records.size() != (binding_two ? 2 : 1) || records[0].texture_space ||
        records[0].texture_register != (binding_array ? 1u : 0u) ||
        records[0].sampler_space || records[0].sampler_register != (binding_array ? 1u : 0u)) return 1;
    if (binding_two && (records[1].texture_space || records[1].texture_register ||
        records[1].sampler_space || records[1].sampler_register != 1)) return 1;
    if (binding_shared) {
      auto *resources = (*parsed)->getNamedMetadata("dx.resources")->getOperand(0);
      const auto word = [](Metadata *value) { return mdconst::extract<ConstantInt>(value)->getZExtValue(); };
      for (const unsigned kind : {0u, 3u}) {
        auto *list = cast<MDNode>(resources->getOperand(kind));
        const unsigned added = kind == 0 ? 2 : 4;
        for (unsigned i = 0; i < added; ++i) {
          auto *record = cast<MDNode>(list->getOperand(list->getNumOperands() - added + i));
          if (word(record->getOperand(3)) != DXMT_MSC_MINMAX_SPACE ||
              word(record->getOperand(4)) != 2 + i % 2 + (i >= 2 ? 4 : 0)) return 1;
        }
      }
      auto *cbvs = cast<MDNode>(resources->getOperand(2));
      if (word(cast<MDNode>(cbvs->getOperand(cbvs->getNumOperands() - 1))->getOperand(6)) != 128) return 1;
      unsigned loads = 0;
      for (auto &function : **parsed) for (auto &block : function) for (auto &instruction : block)
        if (auto *call = dyn_cast<CallInst>(&instruction))
          if (call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.cbufferLoadLegacy.i32") {
            const auto row = cast<ConstantInt>(call->getArgOperand(2))->getZExtValue();
            if (row < 4 || row > 7) return 1;
            ++loads;
          }
      if (loads != 4) return 1;
    }
    if (binding_implicit) {
      unsigned derivatives = 0;
      for (auto &function : **parsed) for (auto &block : function) for (auto &instruction : block)
        if (auto *call = dyn_cast<CallInst>(&instruction))
          if (call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.unary.f32") {
            const auto opcode = cast<ConstantInt>(call->getArgOperand(0))->getZExtValue();
            if (opcode == 83 || opcode == 84) {
              if (block.getName().startswith("dxmt.reduction.enabled") || block.getName().startswith("dxmt.ordinary")) return 1;
              ++derivatives;
            }
          }
      if (derivatives != 8 || (*parsed)->getFunction("dx.op.sample.f32") ||
          (*parsed)->getFunction("dx.op.sampleBias.f32") || (*parsed)->getFunction("dx.op.sampleGrad.f32")) return 1;
    }
  } else if (!dxmt::dxil::LowerReductionSampleLevel(*samples[0], state, error,
      texture_kind == 4 ? 3 : texture_kind == 1 || texture_kind == 6 ? 1 : 2)) { errs() << error; return 1; }
  for (auto &function : **parsed) for (auto &block : function) for (auto &instruction : block)
    if (auto *call = dyn_cast<CallInst>(&instruction))
      if (!binding_two && call->getCalledFunction() && call->getCalledFunction()->getName() == "dx.op.sampleLevel.f32" &&
          call->getArgOperand(layer_operand) != array_coordinate) { errs() << "coordinate preservation failed\n"; return 1; }
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
  if (dxmt::dxil::LowerReductionSampleLevel(*call, state, error, 0) || error.empty() ||
      function->size() != blocks || module.size() != functions) return 1;
  if (dxmt::dxil::LowerReductionSampleLevel(*call, state, error, 3) || error.empty() ||
      function->size() != blocks || module.size() != functions) return 1; // AddressW is required.
  if (dxmt::dxil::LowerReductionSampleLevel(*call, state, error) || error.empty() ||
      function->size() != blocks || module.size() != functions) return 1;
  status->eraseFromParent();
  auto *late = BinaryOperator::CreateAdd(function->getArg(5), builder.getInt32(1), "late", call->getNextNode());
  auto bad_state = state;
  bad_state.flags = late;
  if (dxmt::dxil::LowerReductionSampleLevel(*call, bad_state, error) || error.empty() ||
      function->size() != blocks || module.size() != functions) return 1;
  late->eraseFromParent();
  if (!dxmt::dxil::LowerReductionSampleLevel(*call, state, error) || !error.empty()) {
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
  if (dxmt::dxil::CreateReductionGradientLOD(*gradient_call, error) || error.empty() ||
      gradient_function->getEntryBlock().size() != instructions_before || module.size() != declarations_before) return 1;
  gradient_call->setArgOperand(0, builder.getInt32(63));
  if (dxmt::dxil::CreateReductionGradientLOD(*gradient_call, error, 4) || error.empty() ||
      gradient_function->getEntryBlock().size() != instructions_before || module.size() != declarations_before) return 1;
  auto *gradient_lod = dxmt::dxil::CreateReductionGradientLOD(*gradient_call, error);
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
  auto *volume = Function::Create(FunctionType::get(f32,
      {handle, handle, f32, f32, f32, f32, f32, f32}, false), Function::ExternalLinkage, "gradient3d", module);
  builder.SetInsertPoint(BasicBlock::Create(context, "entry", volume));
  auto *volume_call = builder.CreateCall(gradient, {builder.getInt32(63), volume->getArg(0), volume->getArg(1),
      ConstantFP::get(f32, 0.5), ConstantFP::get(f32, 0.5), ConstantFP::get(f32, 0.5), UndefValue::get(f32),
      builder.getInt32(0), builder.getInt32(0), builder.getInt32(0), volume->getArg(2), volume->getArg(3),
      volume->getArg(4), volume->getArg(5), volume->getArg(6), volume->getArg(7), UndefValue::get(f32)});
  auto *volume_return = builder.CreateRet(ConstantFP::get(f32, 0));
  auto *volume_lod = dxmt::dxil::CreateReductionGradientLOD(*volume_call, error, 3);
  if (!volume_lod || !error.empty()) { errs() << error; return 1; }
  volume_return->setOperand(0, volume_lod);
  unsigned volume_abs = 0, depth_reads = 0;
  for (auto &instruction : volume->getEntryBlock()) {
    if (auto *fp = dyn_cast<FPMathOperator>(&instruction)) if (fp->getFastMathFlags().any()) return 1;
    if (auto *extract = dyn_cast<ExtractValueInst>(&instruction))
      if (extract->getAggregateOperand()->getType() == StructType::getTypeByName(context, "dx.types.Dimensions") &&
          extract->getNumIndices() == 1 && *extract->idx_begin() == 2) ++depth_reads;
    if (auto *operation = dyn_cast<CallInst>(&instruction))
      if (operation->getCalledFunction() && operation->getCalledFunction()->getName() == "dx.op.unary.f32" &&
          cast<ConstantInt>(operation->getArgOperand(0))->getZExtValue() == 6) ++volume_abs;
  }
  if (volume_abs != 6 || depth_reads != 1 || verifyModule(module, &errs())) return 1;
  for (unsigned array = 0; array < 2; ++array) {
    std::vector<Type *> arguments{handle, handle};
    arguments.insert(arguments.end(), 9, f32);
    auto *cube = Function::Create(FunctionType::get(f32, arguments, false), Function::ExternalLinkage,
        array ? "gradient_cube_array" : "gradient_cube", module);
    builder.SetInsertPoint(BasicBlock::Create(context, "entry", cube));
    auto *layer = array ? static_cast<Value *>(ConstantFP::get(f32, 1)) : UndefValue::get(f32);
    auto *call = builder.CreateCall(gradient, {builder.getInt32(63), cube->getArg(0), cube->getArg(1),
        cube->getArg(2), cube->getArg(3), cube->getArg(4), layer,
        UndefValue::get(i32), UndefValue::get(i32), UndefValue::get(i32),
        cube->getArg(5), cube->getArg(6), cube->getArg(7),
        cube->getArg(8), cube->getArg(9), cube->getArg(10), UndefValue::get(f32)});
    auto *result = builder.CreateRet(ConstantFP::get(f32, 0));
    const auto before = cube->getEntryBlock().size();
    const auto declarations = module.size();
    if (dxmt::dxil::CreateReductionGradientLOD(*call, error, 3, true) || error.empty()) return 1;
    call->setArgOperand(5, UndefValue::get(f32));
    if (dxmt::dxil::CreateReductionGradientLOD(*call, error, 2, true) || error.empty()) return 1;
    call->setArgOperand(5, cube->getArg(4));
    call->setArgOperand(7, builder.getInt32(1));
    if (dxmt::dxil::CreateReductionGradientLOD(*call, error, 2, true) || error.empty() ||
        cube->getEntryBlock().size() != before || module.size() != declarations) return 1;
    call->setArgOperand(7, UndefValue::get(i32));
    auto *lod = dxmt::dxil::CreateReductionGradientLOD(*call, error, 2, true);
    if (!lod || !error.empty() || call->getArgOperand(6) != layer) return 1;
    result->setOperand(0, lod);
    unsigned side_reads = 0, cube_abs = 0, cube_sqrt = 0;
    for (auto &instruction : cube->getEntryBlock()) {
      if (auto *fp = dyn_cast<FPMathOperator>(&instruction)) if (fp->getFastMathFlags().any()) return 1;
      if (auto *extract = dyn_cast<ExtractValueInst>(&instruction))
        if (extract->getAggregateOperand()->getType() == StructType::getTypeByName(context, "dx.types.Dimensions")) {
          if (extract->getNumIndices() != 1 || *extract->idx_begin() != 0) return 1;
          ++side_reads;
        }
      if (auto *operation = dyn_cast<CallInst>(&instruction))
        if (operation->getCalledFunction() && operation->getCalledFunction()->getName() == "dx.op.unary.f32") {
          const auto opcode = cast<ConstantInt>(operation->getArgOperand(0))->getZExtValue();
          cube_abs += opcode == 6;
          cube_sqrt += opcode == 24;
        }
    }
    if (side_reads != 2 || cube_abs != 7 || cube_sqrt != 1 || verifyModule(module, &errs())) return 1;
    struct Probe { std::array<float, 9> input; double lod; };
    const double zero = -std::numeric_limits<double>::infinity();
    const Probe probes[] = {
        {{1,0,0, 0,2,0, 0,0,2}, 3}, {{-1,0,0, 0,2,0, 0,0,2}, 3},
        {{0,1,0, 2,0,0, 0,0,2}, 3}, {{0,-1,0, 2,0,0, 0,0,2}, 3},
        {{0,0,1, 2,0,0, 0,2,0}, 3}, {{0,0,-1, 2,0,0, 0,2,0}, 3},
        {{1,.5,0, 2,1,0, 0,0,0}, zero}, {{-1,.5,0, -2,1,0, 0,0,0}, zero},
        {{10,0,0, 0,20,0, 0,0,20}, 3}, {{-1,.5,0, -1,1,0, 0,0,0}, 1},
        {{1,.5,1, 0,1,1, 0,0,0}, .5 * std::log2(20)},
        {{1,1,.5, 0,1,1, 0,0,0}, .5 * std::log2(20)},
        {{1,1,1, 0,1,0, 0,0,0}, 2}};
    for (const auto &probe : probes) {
      bool valid = true;
      const double observed = EvaluateCubeLOD(lod, *cube, probe.input, valid);
      if (!valid || std::isnan(observed) || (std::isinf(probe.lod) ? observed != probe.lod :
          std::fabs(observed - probe.lod) > 1e-5)) return 1;
    }
  }
  for (unsigned array = 0; array < 2; ++array) {
    auto *cube = Function::Create(function->getFunctionType(), Function::ExternalLinkage,
        array ? "footprint_cube_array" : "footprint_cube", module);
    builder.SetInsertPoint(BasicBlock::Create(context, "entry", cube));
    auto *layer = array ? static_cast<Value *>(ConstantFP::get(f32, 1)) : UndefValue::get(f32);
    auto *call = builder.CreateCall(sample, {builder.getInt32(62), cube->getArg(0), cube->getArg(1),
        cube->getArg(2), cube->getArg(3), cube->getArg(4), layer,
        UndefValue::get(i32), UndefValue::get(i32), UndefValue::get(i32), ConstantFP::get(f32, 0)});
    builder.CreateRet(builder.CreateExtractValue(call, 0));
    dxmt::dxil::ReductionSampleState cube_state{cube->getArg(5), cube->getArg(6), cube->getArg(7),
        cube->getArg(8), cube->getArg(9), cube->getArg(0), builder.getInt32(3), builder.getInt32(3)};
    const auto before = cube->getEntryBlock().size(), declarations = module.size();
    if (dxmt::dxil::LowerReductionSampleLevel(*call, cube_state, error, 3, true) || error.empty()) return 1;
    call->setArgOperand(5, UndefValue::get(f32));
    if (dxmt::dxil::LowerReductionSampleLevel(*call, cube_state, error, 2, true) || error.empty()) return 1;
    call->setArgOperand(5, cube->getArg(4));
    call->setArgOperand(9, builder.getInt32(1));
    if (dxmt::dxil::LowerReductionSampleLevel(*call, cube_state, error, 2, true) || error.empty() ||
        cube->size() != 1 || cube->getEntryBlock().size() != before || module.size() != declarations) return 1;
    call->setArgOperand(9, UndefValue::get(i32));
    if (!dxmt::dxil::LowerReductionSampleLevel(*call, cube_state, error, 2, true) || !error.empty() ||
        verifyModule(module, &errs())) return 1;
    unsigned taps = 0, branches = 0, queries = 0;
    for (auto &block : *cube) for (auto &instruction : block) {
      if (auto *fp = dyn_cast<FPMathOperator>(&instruction)) if (fp->getFastMathFlags().any()) return 1;
      if (auto *branch = dyn_cast<BranchInst>(&instruction)) branches += branch->isConditional();
      auto *operation = dyn_cast<CallInst>(&instruction);
      if (!operation || !operation->getCalledFunction()) continue;
      if (operation->getCalledFunction()->getName() == "dx.op.getDimensions") ++queries;
      if (operation->getCalledFunction()->getName() != "dx.op.sampleLevel.f32") continue;
      ++taps;
      if (operation->getArgOperand(1) != cube->getArg(0) || operation->getArgOperand(6) != layer) return 1;
      for (unsigned axis = 0; axis < 3; ++axis)
        if (!isa<UndefValue>(operation->getArgOperand(7 + axis)) ||
            isa<UndefValue>(operation->getArgOperand(3 + axis))) return 1;
    }
    // Four taps per mip, each with two guarded three-face corner additions.
    if (taps != 24 || branches != 24 || queries != 3) return 1;
    struct FootprintProbe { std::array<float, 3> direction; unsigned flags; double expected; };
    const FootprintProbe probes[] = {
        {{1,0,0},7,16}, {{1,0,0},15,240},
        {{-1,0,0},7,112}, {{0,1,0},7,32}, {{0,-1,0},7,160},
        {{0,0,1},7,80}, {{0,0,-1},7,144},
        {{1,0,1},7,16}, {{1,0,1},15,192},
        {{1,1,1},7,16}, {{1,1,1},15,80},
        {{1,0,1},0,80}, {{10,0,10},7,16}, {{10,0,10},15,192}};
    for (const auto &probe : probes) {
      bool valid = true;
      std::unordered_map<unsigned, unsigned> visited;
      const double observed = EvaluateCubeFootprint(*cube, probe.direction, probe.flags, valid, visited);
      if (!valid || observed != probe.expected) {
        errs() << "cube footprint: expected " << probe.expected << ", observed " << observed << '\n';
        return 1;
      }
    }
    struct RoutingProbe { std::array<float, 3> direction; unsigned flags; std::unordered_map<unsigned, unsigned> taps; };
    const RoutingProbe routing[] = {
        {{1,.5,-.5},0,{{1,1}}}, {{-1,.5,.5},0,{{5,1}}},
        {{.5,1,-.5},0,{{9,1}}}, {{.5,-1,.5},0,{{13,1}}},
        {{.5,.5,1},0,{{17,1}}}, {{-.5,.5,-1},0,{{21,1}}},
        {{1,0,1},7,{{0,1},{2,1},{17,1},{19,1}}},
        {{-1,0,-1},7,{{4,1},{6,1},{21,1},{23,1}}},
        {{1,1,1},7,{{0,2},{11,2},{17,2}}},
        {{1,1,1},15,{{0,2},{11,2},{17,2}}}};
    for (const auto &probe : routing) {
      bool valid = true;
      std::unordered_map<unsigned, unsigned> visited;
      const double observed = EvaluateCubeFootprint(*cube, probe.direction, probe.flags, valid, visited, true);
      unsigned expected = probe.taps.begin()->first;
      for (const auto &[identity, count] : probe.taps)
        expected = probe.flags & 8 ? std::max(expected, identity) : std::min(expected, identity);
      if (!valid || observed != expected || visited != probe.taps) {
        errs() << "cube routing mismatch\n"; return 1;
      }
    }
  }
  module.print(outs(), nullptr);
  return 0;
}

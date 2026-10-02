#include "dxil_minmax.hpp"
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <array>
#include <map>
#include <tuple>

static_assert(sizeof(dxmt_msc_minmax_state) == 32);
static_assert(sizeof(dxmt_msc_minmax_binding) == 16);

namespace dxmt::dxil {
namespace {
constexpr unsigned CreateHandle = 57, CBufferLoadLegacy = 59, SampleLevel = 62, GetDimensions = 72, FMax = 35, FMin = 36;
constexpr unsigned SampleGrad = 63;
bool IsQualifiedSampleName(llvm::StringRef name) {
  return name == "dx.op.sampleLevel.f32" || name == "dx.op.sampleGrad.f32";
}
bool Word(llvm::Metadata *metadata, uint32_t &value) {
  auto *constant = llvm::mdconst::dyn_extract_or_null<llvm::ConstantInt>(metadata);
  if (!constant || !constant->getType()->isIntegerTy(32)) return false;
  value = constant->getZExtValue();
  return true;
}
bool Word(llvm::Value *value, uint32_t &word) {
  auto *constant = llvm::dyn_cast<llvm::ConstantInt>(value);
  if (!constant || !constant->getType()->isIntegerTy(32)) return false;
  word = constant->getZExtValue();
  return true;
}
struct Resource {
  llvm::MDNode *metadata;
  uint32_t space, reg, count;
};
struct Sample {
  llvm::CallInst *call;
  uint32_t pair;
};
struct Pair {
  dxmt_msc_minmax_binding binding;
  const Resource *texture;
  const Resource *sampler;
};
}

bool LowerReductionSamplerBindings(llvm::Module &module,
    std::vector<dxmt_msc_minmax_binding> &bindings, std::string &error) {
  using namespace llvm;
  error.clear();
  auto reject = [&](const char *message) { error = message; return false; };
  {
    raw_string_ostream diagnostics(error);
    if (verifyModule(module, &diagnostics)) return false;
  }
  auto &context = module.getContext();
  auto *named = module.getNamedMetadata("dx.resources");
  if (!named || named->getNumOperands() != 1 || named->getOperand(0)->getNumOperands() != 4)
    return reject("invalid DXIL resource metadata");
  auto *resources = named->getOperand(0);
  std::array<std::map<uint32_t, Resource>, 4> records;
  std::array<uint32_t, 4> next_id = {};
  std::array<std::map<std::pair<uint32_t, uint32_t>, uint64_t>, 4> intervals;
  for (unsigned kind = 0; kind < 4; ++kind) {
    auto *list = dyn_cast_or_null<MDNode>(resources->getOperand(kind));
    if (!list && resources->getOperand(kind)) return reject("invalid resource list");
    if (!list) continue;
    for (auto &operand : list->operands()) {
      auto *record = dyn_cast_or_null<MDNode>(operand);
      uint32_t id, space, reg, count;
      if (!record || record->getNumOperands() < 7 || !Word(record->getOperand(0), id) || id == UINT32_MAX ||
          !Word(record->getOperand(3), space) || !Word(record->getOperand(4), reg) ||
          !Word(record->getOperand(5), count) || !count || uint64_t(reg) + count > uint64_t(UINT32_MAX) + 1)
        return reject("invalid resource record");
      if (space == DXMT_MSC_MINMAX_SPACE) return reject("application resource overlaps private reduction space");
      const auto key = std::make_pair(space, reg);
      const uint64_t end = uint64_t(reg) + count;
      auto next = intervals[kind].lower_bound(key);
      if (next != intervals[kind].end() && next->first.first == space && next->first.second < end)
        return reject("overlapping application resource bindings");
      if (next != intervals[kind].begin()) {
        auto previous = std::prev(next);
        if (previous->first.first == space && previous->second > reg)
          return reject("overlapping application resource bindings");
      }
      intervals[kind].emplace(key, end);
      if (!records[kind].emplace(id, Resource{record, space, reg, count}).second)
        return reject("duplicate resource range identity");
      next_id[kind] = std::max(next_id[kind], id + 1);
    }
  }
  auto *entry_points = module.getNamedMetadata("dx.entryPoints");
  if (!entry_points || !entry_points->getNumOperands()) return reject("missing DXIL entry points");
  for (auto *entry : entry_points->operands())
    if (entry->getNumOperands() < 4 || entry->getOperand(3) != resources)
      return reject("entry-point resource root must match dx.resources");
  IRBuilder<> types(context);
  auto *i32 = types.getInt32Ty();
  auto *handle = StructType::getTypeByName(context, "dx.types.Handle");
  auto *create = module.getFunction("dx.op.createHandle");
  if (!handle || !create || create->getFunctionType() != FunctionType::get(handle,
      {i32, types.getInt8Ty(), i32, i32, types.getInt1Ty()}, false))
    return reject("legacy createHandle required");
  auto resolve = [&](Value *value, unsigned kind) -> const Resource * {
    auto *call = dyn_cast<CallInst>(value);
    if (!call || call->getCalledFunction() != create || call->arg_size() != 5) return nullptr;
    auto *resource_class = dyn_cast<ConstantInt>(call->getArgOperand(1));
    auto *nonuniform = dyn_cast<ConstantInt>(call->getArgOperand(4));
    uint32_t opcode, range, reg;
    if (!resource_class || resource_class->getZExtValue() != kind || !nonuniform || !nonuniform->isZero() ||
        !Word(call->getArgOperand(0), opcode) || opcode != CreateHandle || !Word(call->getArgOperand(2), range) ||
        !Word(call->getArgOperand(3), reg)) return nullptr;
    auto found = records[kind].find(range);
    return found != records[kind].end() && found->second.count == 1 && found->second.reg == reg ? &found->second : nullptr;
  };
  std::vector<Sample> samples;
  std::vector<Pair> pairs;
  bool has_gradient = false, has_mapping_check = false;
  std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, uint32_t> pair_indices;
  for (auto &function : module) for (auto &block : function) for (auto &instruction : block) {
    auto *call = dyn_cast<CallInst>(&instruction);
    if (!call || !call->getCalledFunction()) continue;
    auto name = call->getCalledFunction()->getName();
    has_mapping_check |= name.startswith("dx.op.checkAccessFullyMapped");
    if (name.startswith("dx.op.createHandleFrom") || name.startswith("dx.op.annotateHandle"))
      return reject("modern/dynamic handle provenance requires further lowering");
    if (name.startswith("dx.op.sample") && !IsQualifiedSampleName(name))
      return reject("only float SampleLevel/SampleGrad are currently qualified");
    // Qualify every sampler consumer, not only the samples being rewritten.
    if (call->getCalledFunction() == create) {
      auto *kind = dyn_cast<ConstantInt>(call->getArgOperand(1));
      if (kind && kind->getZExtValue() == 3) for (auto *user : call->users()) {
        auto *consumer = dyn_cast<CallInst>(user);
        if (!consumer || !consumer->getCalledFunction() || !IsQualifiedSampleName(consumer->getCalledFunction()->getName()) ||
            consumer->arg_size() != (consumer->getCalledFunction()->getName() == "dx.op.sampleGrad.f32" ? 17 : 11) ||
            consumer->getArgOperand(2) != call)
          return reject("unsupported sampler handle flow or consumer");
      }
    }
    if (!IsQualifiedSampleName(name)) continue;
    const bool gradient = name == "dx.op.sampleGrad.f32";
    has_gradient |= gradient;
    if (call->arg_size() != (gradient ? 17 : 11) || samples.size() >= 1024)
      return reject("invalid or oversized sampling module");
    auto *result = dyn_cast<StructType>(call->getType());
    uint32_t opcode;
    if (!result || result->isOpaque() || result->getNumElements() != 5 ||
        !result->getElementType(4)->isIntegerTy(32) || !Word(call->getArgOperand(0), opcode) ||
        opcode != (gradient ? SampleGrad : SampleLevel))
      return reject("invalid float sample result or opcode");
    for (unsigned i = 0; i < 4; ++i)
      if (!result->getElementType(i)->isFloatTy() || !call->getArgOperand(3 + i)->getType()->isFloatTy())
        return reject("invalid float SampleLevel components or coordinates");
    for (unsigned i = 7; i < call->arg_size(); ++i)
      if (call->getArgOperand(i)->getType() != (i < 10 ? i32 : types.getFloatTy()))
        return reject("invalid sampling offset/LOD/gradient operand");
    const auto *texture = resolve(call->getArgOperand(1), 0);
    const auto *sampler = resolve(call->getArgOperand(2), 3);
    uint32_t texture_kind, sampler_kind, component_tag, component_type;
    if (!texture || !sampler || texture->metadata->getNumOperands() != 9 ||
        sampler->metadata->getNumOperands() != 8 || !Word(texture->metadata->getOperand(6), texture_kind) || texture_kind != 2 ||
        !Word(sampler->metadata->getOperand(6), sampler_kind) || sampler_kind != 0)
      return reject("finite Texture2D/SamplerState pair required");
    auto *component = dyn_cast_or_null<MDNode>(texture->metadata->getOperand(8));
    if (!component || component->getNumOperands() != 2 || !Word(component->getOperand(0), component_tag) || component_tag != 0 ||
        !Word(component->getOperand(1), component_type) || component_type != 9)
      return reject("float Texture2D component metadata required");
    for (auto *user : call->users()) {
      auto *extract = dyn_cast<ExtractValueInst>(user);
      if (!extract || extract->getNumIndices() != 1 || *extract->idx_begin() >= 4)
        return reject("feedback/status or aggregate flow requires further lowering");
    }
    auto key = std::make_tuple(texture->space, texture->reg, sampler->space, sampler->reg);
    auto [index, inserted] = pair_indices.emplace(key, pairs.size());
    if (inserted) {
      if (pairs.size() >= 64) return reject("too many sampled pairs");
      pairs.push_back({{texture->space, texture->reg, sampler->space, sampler->reg}, texture, sampler});
    }
    samples.push_back({call, index->second});
  }
  if (samples.empty()) return reject("no qualified sampling pairs");
  if (uint64_t(next_id[0]) + pairs.size() > UINT32_MAX || uint64_t(next_id[3]) + pairs.size() * 2 > UINT32_MAX)
    return reject("private resource range identity overflow");
  auto *cb_ret = StructType::getTypeByName(context, "dx.types.CBufRet.i32");
  if (cb_ret && (cb_ret->isOpaque() || cb_ret->getNumElements() != 4)) return reject("invalid CBV return type");
  if (cb_ret) for (auto *element : cb_ret->elements()) if (element != i32) return reject("invalid CBV components");
  auto *existing_load = module.getFunction("dx.op.cbufferLoadLegacy.i32");
  if (existing_load && (!cb_ret || existing_load->getFunctionType() != FunctionType::get(cb_ret, {i32, handle, i32}, false)))
    return reject("invalid CBV load signature");
  auto *existing_dimensions = StructType::getTypeByName(context, "dx.types.Dimensions");
  if (existing_dimensions && (existing_dimensions->isOpaque() || existing_dimensions->getNumElements() != 4))
    return reject("invalid dimensions type");
  if (existing_dimensions) for (auto *element : existing_dimensions->elements())
    if (element != i32) return reject("invalid dimensions components");
  auto *dimensions_function = module.getFunction("dx.op.getDimensions");
  if (dimensions_function && (!existing_dimensions || dimensions_function->getFunctionType() !=
      FunctionType::get(existing_dimensions, {i32, handle, i32}, false))) return reject("invalid dimensions signature");
  auto *binary_function = module.getFunction("dx.op.binary.f32");
  if (binary_function && binary_function->getFunctionType() !=
      FunctionType::get(types.getFloatTy(), {i32, types.getFloatTy(), types.getFloatTy()}, false))
    return reject("invalid binary intrinsic signature");
  // From here the module is private and must be discarded on any failure.
  // CloneModule may share uniqued resource metadata in the same LLVMContext.
  // Replace the root and entry records instead of mutating that shared graph.
  auto *application_resources = resources;
  std::vector<Metadata *> resource_groups;
  for (auto &operand : resources->operands()) resource_groups.push_back(operand.get());
  resources = MDNode::getDistinct(context, resource_groups);
  named->setOperand(0, resources);
  if (auto *entries = module.getNamedMetadata("dx.entryPoints"))
    for (unsigned index = 0; index < entries->getNumOperands(); ++index) {
      auto *entry = entries->getOperand(index);
      if (entry->getNumOperands() < 4 || entry->getOperand(3) != application_resources) continue;
      std::vector<Metadata *> operands;
      for (auto &operand : entry->operands()) operands.push_back(operand.get());
      operands[3] = resources;
      entries->setOperand(index, MDNode::getDistinct(context, operands));
    }
  if (!cb_ret) cb_ret = StructType::create(context, {i32, i32, i32, i32}, "dx.types.CBufRet.i32");
  auto cb_load = module.getOrInsertFunction("dx.op.cbufferLoadLegacy.i32", cb_ret, i32, handle, i32);
  auto word = [&](uint32_t value) -> Metadata * { return ConstantAsMetadata::get(types.getInt32(value)); };
  auto append = [&](unsigned kind, const std::vector<Metadata *> &additional) {
    std::vector<Metadata *> combined;
    if (auto *list = dyn_cast_or_null<MDNode>(resources->getOperand(kind)))
      for (auto &operand : list->operands()) combined.push_back(operand.get());
    combined.insert(combined.end(), additional.begin(), additional.end());
    resources->replaceOperandWith(kind, MDNode::get(context, combined));
  };
  for (unsigned kind : {0u, 3u}) {
    std::vector<Metadata *> additional;
    for (unsigned slot = 0; slot < pairs.size() * (kind == 3 ? 2 : 1); ++slot) {
      const auto pair = slot % pairs.size();
      auto *source = kind == 0 ? pairs[pair].texture->metadata : pairs[pair].sampler->metadata;
      std::vector<Metadata *> operands;
      for (auto &operand : source->operands()) operands.push_back(operand.get());
      operands[0] = word(next_id[kind] + slot);
      operands[3] = word(DXMT_MSC_MINMAX_SPACE);
      operands[4] = word(slot);
      additional.push_back(MDNode::get(context, operands));
    }
    append(kind, additional);
  }
  auto *state_type = StructType::create(context,
      {ArrayType::get(FixedVectorType::get(i32, 4), pairs.size() * 2)}, "dxmt.ReductionStates");
  append(2, {MDNode::get(context, {word(next_id[2]), ConstantAsMetadata::get(UndefValue::get(PointerType::getUnqual(state_type))),
      MDString::get(context, ""), word(DXMT_MSC_MINMAX_SPACE), word(0), word(1), word(pairs.size() * 32), nullptr})});
  auto *dimensions_type = StructType::getTypeByName(context, "dx.types.Dimensions");
  if (!dimensions_type) dimensions_type = StructType::create(context, {i32, i32, i32, i32}, "dx.types.Dimensions");
  auto dimensions = module.getOrInsertFunction("dx.op.getDimensions", dimensions_type, i32, handle, i32);
  auto binary = module.getOrInsertFunction("dx.op.binary.f32", types.getFloatTy(), i32, types.getFloatTy(), types.getFloatTy());
  for (auto [sample, pair] : samples) {
    const bool gradient = sample->getCalledFunction()->getName() == "dx.op.sampleGrad.f32";
    Value *instruction_clamp = gradient ? sample->getArgOperand(16) : nullptr;
    if (gradient) {
      auto *gradient_lod = CreateReductionGradientLOD2D(*sample, error);
      if (!gradient_lod) return false;
      IRBuilder<> normalize(sample);
      SmallVector<Value *, 11> arguments;
      for (unsigned i = 0; i < 10; ++i) arguments.push_back(sample->getArgOperand(i));
      arguments[0] = normalize.getInt32(SampleLevel);
      arguments.push_back(gradient_lod);
      auto *type = FunctionType::get(sample->getType(),
          {i32, handle, handle, types.getFloatTy(), types.getFloatTy(), types.getFloatTy(), types.getFloatTy(),
              i32, i32, i32, types.getFloatTy()}, false);
      auto *existing = module.getFunction("dx.op.sampleLevel.f32");
      if (existing && existing->getFunctionType() != type) return reject("invalid SampleLevel normalization signature");
      auto level = module.getOrInsertFunction("dx.op.sampleLevel.f32", type);
      auto *normalized = normalize.CreateCall(level, arguments);
      sample->replaceAllUsesWith(normalized);
      sample->eraseFromParent();
      sample = normalized;
    }
    std::vector<ExtractValueInst *> original_extracts;
    for (auto *user : sample->users()) original_extracts.push_back(cast<ExtractValueInst>(user));
    auto *entry = sample->getParent();
    auto *merge = entry->splitBasicBlock(sample->getIterator(), "dxmt.reduction.merge");
    entry->getTerminator()->eraseFromParent();
    auto *function = entry->getParent();
    auto *ordinary = BasicBlock::Create(context, "dxmt.reduction.ordinary", function, merge);
    auto *reduction = BasicBlock::Create(context, "dxmt.reduction.enabled", function, merge);
    IRBuilder<> b(entry);
    auto make_handle = [&](IRBuilder<> &builder, unsigned kind, unsigned range, unsigned reg) {
      return builder.CreateCall(create, {builder.getInt32(CreateHandle), builder.getInt8(kind), builder.getInt32(range),
          builder.getInt32(reg), builder.getFalse()});
    };
    auto *cb = make_handle(b, 2, next_id[2], 0);
    auto *first = b.CreateCall(cb_load, {b.getInt32(CBufferLoadLegacy), cb, b.getInt32(pair * 2)});
    auto *second = b.CreateCall(cb_load, {b.getInt32(CBufferLoadLegacy), cb, b.getInt32(pair * 2 + 1)});
    auto *flags = b.CreateExtractValue(first, 0);
    Value *original_lod = sample->getArgOperand(10);
    if (gradient) original_lod = b.CreateFAdd(original_lod,
        b.CreateBitCast(b.CreateExtractValue(second, 3), b.getFloatTy()), "dxmt.gradient.biased.lod");
    b.CreateCondBr(b.CreateICmpNE(b.CreateAnd(flags, b.getInt32(DXMT_MSC_MINMAX_ENABLED)), b.getInt32(0)), reduction, ordinary);
    b.SetInsertPoint(ordinary);
    auto *ordinary_sampler = make_handle(b, 3, next_id[3] + pairs.size() + pair, pairs.size() + pair);
    auto as_float = [&](Value *value) { return b.CreateBitCast(value, b.getFloatTy()); };
    auto *minimum_lod = as_float(b.CreateExtractValue(first, 1));
    auto *maximum_lod = as_float(b.CreateExtractValue(first, 2));
    auto *resource_clamp = as_float(b.CreateExtractValue(first, 3));
    auto merge_clamp = [&](Value *resource) -> Value * {
      if (!instruction_clamp || isa<UndefValue>(instruction_clamp)) return resource;
      auto *zero = ConstantFP::get(b.getFloatTy(), 0);
      return b.CreateSelect(b.CreateFCmpUNE(instruction_clamp, zero),
          b.CreateCall(binary, {b.getInt32(FMax), resource, instruction_clamp}), resource);
    };
    resource_clamp = merge_clamp(resource_clamp);
    auto *component_defaults = b.CreateExtractValue(second, 0);
    auto *sampler_lod = b.CreateCall(binary, {b.getInt32(FMax), minimum_lod,
        b.CreateCall(binary, {b.getInt32(FMin), maximum_lod, original_lod})});
    auto *lod = b.CreateCall(binary, {b.getInt32(FMax), sampler_lod, resource_clamp});
    auto *dim0 = b.CreateCall(dimensions, {b.getInt32(GetDimensions), sample->getArgOperand(1), b.getInt32(0)});
    auto *last_mip = b.CreateUIToFP(b.CreateSub(b.CreateExtractValue(dim0, 3), b.getInt32(1)), b.getFloatTy());
    auto *ordinary_sample = BasicBlock::Create(context, "dxmt.ordinary.sample", function, merge);
    auto *ordinary_empty = BasicBlock::Create(context, "dxmt.ordinary.empty", function, merge);
    auto *ordinary_done = BasicBlock::Create(context, "dxmt.ordinary.result", function, merge);
    b.CreateCondBr(b.CreateFCmpOGT(resource_clamp, last_mip), ordinary_empty, ordinary_sample);
    b.SetInsertPoint(ordinary_sample);
    sample->removeFromParent();
    b.Insert(sample);
    sample->setArgOperand(2, ordinary_sampler);
    sample->setArgOperand(10, lod);
    std::array<Value *, 4> ordinary_values;
    for (unsigned i = 0; i < 4; ++i) ordinary_values[i] = b.CreateExtractValue(sample, i);
    b.CreateBr(ordinary_done);
    b.SetInsertPoint(ordinary_empty);
    std::array<Value *, 4> default_values;
    for (unsigned i = 0; i < 4; ++i)
      default_values[i] = b.CreateSelect(b.CreateICmpNE(b.CreateAnd(component_defaults, b.getInt32(1u << i)), b.getInt32(0)),
          ConstantFP::get(b.getFloatTy(), 1), ConstantFP::get(b.getFloatTy(), 0));
    b.CreateBr(ordinary_done);
    b.SetInsertPoint(ordinary_done);
    for (unsigned i = 0; i < 4; ++i) {
      auto *value = b.CreatePHI(b.getFloatTy(), 2);
      value->addIncoming(ordinary_values[i], ordinary_sample);
      value->addIncoming(default_values[i], ordinary_empty);
      ordinary_values[i] = value;
    }
    b.CreateBr(merge);
    b.SetInsertPoint(reduction);
    auto *point_texture = make_handle(b, 0, next_id[0] + pair, pair);
    auto *point_sampler = make_handle(b, 3, next_id[3] + pair, pair);
    ReductionSampleState state{flags, as_float(b.CreateExtractValue(first, 1)), as_float(b.CreateExtractValue(first, 2)),
        merge_clamp(as_float(b.CreateExtractValue(first, 3))), b.CreateExtractValue(second, 0), point_texture,
        b.CreateExtractValue(second, 1), b.CreateExtractValue(second, 2)};
    SmallVector<Value *, 11> arguments(sample->args());
    arguments[1] = point_texture;
    arguments[2] = point_sampler;
    arguments[10] = original_lod;
    auto *lowered = b.CreateCall(sample->getCalledFunction(), arguments);
    std::array<Value *, 4> reduction_values;
    for (unsigned i = 0; i < 4; ++i) reduction_values[i] = b.CreateExtractValue(lowered, i);
    b.CreateBr(merge);
    std::array<PHINode *, 4> values;
    for (unsigned i = 0; i < 4; ++i) {
      values[i] = PHINode::Create(b.getFloatTy(), 2, "dxmt.reduction.component", &*merge->begin());
      values[i]->addIncoming(ordinary_values[i], ordinary_done);
      values[i]->addIncoming(reduction_values[i], reduction);
    }
    for (auto *extract : original_extracts) {
      extract->replaceAllUsesWith(values[*extract->idx_begin()]);
      extract->eraseFromParent();
    }
    if (!LowerReductionSampleLevel2D(*lowered, state, error)) return false;
    BasicBlock *reduction_end = nullptr;
    for (auto *predecessor : predecessors(merge)) if (predecessor != ordinary_done) {
      if (reduction_end) return reject("invalid reduction branch merge");
      reduction_end = predecessor;
    }
    if (!reduction_end) return reject("missing reduction branch merge");
    for (auto *value : values) value->setIncomingBlock(1, reduction_end);
  }
  // DXC rejects unused dx.op declarations even when LLVM verification passes.
  // Remove only the operation whose complete consumer set was normalized.
  if (auto *gradient = module.getFunction("dx.op.sampleGrad.f32"))
    if (gradient->isDeclaration() && gradient->use_empty()) gradient->eraseFromParent();
  // No native clamped sampler operation remains. DXC's TiledResources flag
  // reflects LOD-clamp or CheckAccessFullyMapped use, not arithmetic clamps.
  // Preserve it if a mapping check remains; keep all unrelated shader flags.
  if (has_gradient && !has_mapping_check) for (unsigned index = 0; index < entry_points->getNumOperands(); ++index) {
    auto *entry = entry_points->getOperand(index);
    if (entry->getNumOperands() < 5) continue;
    auto *properties = dyn_cast_or_null<MDNode>(entry->getOperand(4));
    if (!properties) continue;
    if (properties->getNumOperands() % 2) return reject("invalid entry property pairs");
    std::vector<Metadata *> properties_copy;
    for (auto &operand : properties->operands()) properties_copy.push_back(operand.get());
    for (unsigned property = 0; property < properties_copy.size(); property += 2) {
      uint32_t tag;
      if (!Word(properties_copy[property], tag)) return reject("invalid entry property tag");
      if (tag != 0) continue;
      auto *flags = mdconst::dyn_extract_or_null<ConstantInt>(properties_copy[property + 1]);
      if (!flags || !flags->getType()->isIntegerTy(64)) return reject("invalid shader flags");
      constexpr uint64_t TiledResources = uint64_t(1) << 12;
      properties_copy[property + 1] = ConstantAsMetadata::get(types.getInt64(flags->getZExtValue() & ~TiledResources));
    }
    std::vector<Metadata *> entry_copy;
    for (auto &operand : entry->operands()) entry_copy.push_back(operand.get());
    entry_copy[4] = MDNode::getDistinct(context, properties_copy);
    entry_points->setOperand(index, MDNode::getDistinct(context, entry_copy));
  }
  raw_string_ostream diagnostics(error);
  if (verifyModule(module, &diagnostics)) return false;
  std::vector<dxmt_msc_minmax_binding> result;
  for (const auto &pair : pairs) result.push_back(pair.binding);
  bindings = std::move(result);
  error.clear();
  return true;
}
}

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
constexpr unsigned Sample = 60, SampleBias = 61, SampleGrad = 63, DerivCoarseX = 83, DerivCoarseY = 84;
constexpr unsigned SamplerResourceKind = 14, SamplerComparisonFlag = 1u << 15;
unsigned SampleOpcode(llvm::StringRef name) {
  if (name == "dx.op.sample.f32") return Sample;
  if (name == "dx.op.sampleBias.f32") return SampleBias;
  if (name == "dx.op.sampleLevel.f32") return SampleLevel;
  if (name == "dx.op.sampleGrad.f32") return SampleGrad;
  return 0;
}
unsigned SampleArgumentCount(llvm::StringRef name) {
  const auto opcode = SampleOpcode(name);
  return opcode == SampleGrad ? 17 : opcode == SampleBias ? 12 : opcode ? 11 : 0;
}
bool IsQualifiedSampleName(llvm::StringRef name) {
  return SampleOpcode(name) != 0;
}
unsigned ComparisonOpcode(llvm::StringRef name) {
  if (name == "dx.op.sampleCmp.f32") return 64;
  if (name == "dx.op.sampleCmpLevelZero.f32") return 65;
  return 0;
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
struct SampleSite {
  llvm::CallInst *call;
  uint32_t pair;
  unsigned spatial_dimensions;
  bool cube;
};
struct Pair {
  dxmt_msc_minmax_binding binding;
  const Resource *texture;
  const Resource *sampler;
};
}

bool LowerReductionSamplerBindings(llvm::Module &module,
    std::vector<dxmt_msc_minmax_binding> &bindings, std::string &error, unsigned pair_offset, unsigned pair_count) {
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
  auto *modern_create = module.getFunction("dx.op.createHandleFromBinding");
  auto *annotate = module.getFunction("dx.op.annotateHandle");
  auto *bind_type = StructType::getTypeByName(context, "dx.types.ResBind");
  auto *properties_type = StructType::getTypeByName(context, "dx.types.ResourceProperties");
  const bool modern = modern_create != nullptr;
  if (!handle || (create && modern) || (!create && !modern)) return reject("one handle model required");
  if (create && create->getFunctionType() != FunctionType::get(handle,
      {i32, types.getInt8Ty(), i32, i32, types.getInt1Ty()}, false)) return reject("invalid legacy handle signature");
  if (modern && (!bind_type || bind_type->isOpaque() || bind_type->elements() !=
      ArrayRef<Type *>({i32, i32, i32, types.getInt8Ty()}) || !properties_type || properties_type->isOpaque() ||
      properties_type->elements() != ArrayRef<Type *>({i32, i32}) || !annotate ||
      modern_create->getFunctionType() != FunctionType::get(handle, {i32, bind_type, i32, types.getInt1Ty()}, false) ||
      annotate->getFunctionType() != FunctionType::get(handle, {i32, handle, properties_type}, false)))
    return reject("invalid modern handle signatures");
  auto aggregate_word = [&](Value *value, unsigned index, uint32_t &word) {
    auto *constant = dyn_cast<Constant>(value);
    auto *element = constant ? dyn_cast_or_null<ConstantInt>(constant->getAggregateElement(index)) : nullptr;
    if (!element || element->getBitWidth() > 32) return false;
    word = element->getZExtValue();
    return true;
  };
  auto resolve = [&](Value *value, unsigned kind, uint32_t &resolved_register) -> const Resource * {
    auto *call = dyn_cast<CallInst>(value);
    if (modern) {
      uint32_t opcode, lower, upper, space, resource_class, reg, property_kind, component;
      if (!call || call->getCalledFunction() != annotate || call->arg_size() != 3 ||
          !Word(call->getArgOperand(0), opcode) || opcode != 216 ||
          !aggregate_word(call->getArgOperand(2), 0, property_kind) ||
          !aggregate_word(call->getArgOperand(2), 1, component)) return nullptr;
      auto *binding = dyn_cast<CallInst>(call->getArgOperand(1));
      if (!binding || binding->getCalledFunction() != modern_create || binding->arg_size() != 4 ||
          !Word(binding->getArgOperand(0), opcode) || opcode != 217 ||
          !aggregate_word(binding->getArgOperand(1), 0, lower) ||
          !aggregate_word(binding->getArgOperand(1), 1, upper) ||
          !aggregate_word(binding->getArgOperand(1), 2, space) ||
          !aggregate_word(binding->getArgOperand(1), 3, resource_class) || resource_class != kind ||
          !Word(binding->getArgOperand(2), reg) || upper == UINT32_MAX || reg < lower || reg > upper ||
          !isa<ConstantInt>(binding->getArgOperand(3)) || !cast<ConstantInt>(binding->getArgOperand(3))->isZero())
        return nullptr;
      for (auto &[id, resource] : records[kind]) {
        uint32_t resource_kind;
        if (resource.space != space || resource.reg != lower || uint64_t(lower) + resource.count != uint64_t(upper) + 1)
          continue;
        if (!Word(resource.metadata->getOperand(6), resource_kind) ||
            (kind == 3 && resource_kind > 1) ||
            property_kind != (kind == 3 ? (SamplerResourceKind | (resource_kind == 1 ? SamplerComparisonFlag : 0u)) : resource_kind) ||
            (kind == 3 ? component != 0 : (component & 255u) != 9u || (component >> 8) < 1 || (component >> 8) > 4))
          return nullptr;
        resolved_register = reg;
        return &resource;
      }
      return nullptr;
    }
    if (!call || call->getCalledFunction() != create || call->arg_size() != 5) return nullptr;
    auto *resource_class = dyn_cast<ConstantInt>(call->getArgOperand(1));
    auto *nonuniform = dyn_cast<ConstantInt>(call->getArgOperand(4));
    uint32_t opcode, range, reg;
    if (!resource_class || resource_class->getZExtValue() != kind || !nonuniform || !nonuniform->isZero() ||
        !Word(call->getArgOperand(0), opcode) || opcode != CreateHandle || !Word(call->getArgOperand(2), range) ||
        !Word(call->getArgOperand(3), reg)) return nullptr;
    auto found = records[kind].find(range);
    if (found == records[kind].end() || found->second.count == UINT32_MAX ||
        reg < found->second.reg || uint64_t(reg) >= uint64_t(found->second.reg) + found->second.count)
      return nullptr;
    resolved_register = reg;
    return &found->second;
  };
  std::vector<SampleSite> samples;
  std::vector<Pair> pairs;
  std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, uint32_t> pair_indices;
  auto comparison_consumer = [&](CallInst *call) {
    if (!call || !call->getCalledFunction()) return false;
    const unsigned opcode = ComparisonOpcode(call->getCalledFunction()->getName());
    if (!opcode || call->arg_size() != (opcode == 64 ? 12u : 11u)) return false;
    uint32_t actual_opcode, sampler_register, texture_register, sampler_kind;
    const auto *sampler = resolve(call->getArgOperand(2), 3, sampler_register);
    if (!Word(call->getArgOperand(0), actual_opcode) || actual_opcode != opcode || !sampler ||
        sampler->metadata->getNumOperands() != 8 || !Word(sampler->metadata->getOperand(6), sampler_kind) ||
        sampler_kind != 1 || !resolve(call->getArgOperand(1), 0, texture_register)) return false;
    auto *result = dyn_cast<StructType>(call->getType());
    if (!result || result->isOpaque() || result->getNumElements() != 5 ||
        !result->getElementType(4)->isIntegerTy(32)) return false;
    for (unsigned component = 0; component < 4; ++component)
      if (!result->getElementType(component)->isFloatTy()) return false;
    SmallVector<Type *, 12> parameters{i32, handle, handle};
    for (unsigned argument = 3; argument < call->arg_size(); ++argument)
      parameters.push_back(argument >= 7 && argument < 10 ? i32 : types.getFloatTy());
    if (call->getCalledFunction()->getFunctionType() != FunctionType::get(result, parameters, false)) return false;
    for (auto *user : call->users()) {
      auto *extract = dyn_cast<ExtractValueInst>(user);
      if (!extract || extract->getNumIndices() != 1 || *extract->idx_begin() >= 4) return false;
    }
    // Native comparison calls never enter the reduction pair list. Retain the
    // original sampler/texture and let regenerated DXIL validation plus MSC
    // enforce the operation's stage, resource and coordinate contract.
    return true;
  };
  for (auto &function : module) for (auto &block : function) for (auto &instruction : block) {
    auto *call = dyn_cast<CallInst>(&instruction);
    if (!call || !call->getCalledFunction()) continue;
    auto name = call->getCalledFunction()->getName();
    if (name.startswith("dx.op.createHandleFrom") && call->getCalledFunction() != modern_create)
      return reject("modern/dynamic handle provenance requires further lowering");
    if (name.startswith("dx.op.sample") && !IsQualifiedSampleName(name) && !comparison_consumer(call))
      return reject("unsupported float sampling operation");
    // Qualify every sampler consumer, not only the samples being rewritten.
    if (call->getCalledFunction() == create) {
      auto *kind = dyn_cast<ConstantInt>(call->getArgOperand(1));
      if (kind && kind->getZExtValue() == 3) for (auto *user : call->users()) {
        auto *consumer = dyn_cast<CallInst>(user);
        if (comparison_consumer(consumer) && consumer->getArgOperand(2) == call) continue;
        if (!consumer || !consumer->getCalledFunction() || !IsQualifiedSampleName(consumer->getCalledFunction()->getName()) ||
            consumer->arg_size() != SampleArgumentCount(consumer->getCalledFunction()->getName()) ||
            consumer->getArgOperand(2) != call)
          return reject("unsupported sampler handle flow or consumer");
      }
    }
    if (modern && call->getCalledFunction() == annotate) {
      uint32_t property;
      if (!aggregate_word(call->getArgOperand(2), 0, property)) return reject("constant annotation required");
      if (property == SamplerResourceKind || property == (SamplerResourceKind | SamplerComparisonFlag)) for (auto *user : call->users()) {
        auto *consumer = dyn_cast<CallInst>(user);
        if (comparison_consumer(consumer) && consumer->getArgOperand(2) == call) continue;
        if (!consumer || !consumer->getCalledFunction() || !IsQualifiedSampleName(consumer->getCalledFunction()->getName()) ||
            consumer->arg_size() < 3 || consumer->getArgOperand(2) != call)
          return reject("unsupported annotated sampler consumer");
      }
    }
    if (modern && call->getCalledFunction() == modern_create) {
      uint32_t resource_class;
      if (!aggregate_word(call->getArgOperand(1), 3, resource_class)) return reject("constant binding required");
      if (resource_class == 3) for (auto *user : call->users()) {
        uint32_t resolved_register;
        if (!resolve(user, 3, resolved_register)) return reject("unsupported modern sampler handle flow");
      }
    }
    if (!IsQualifiedSampleName(name)) continue;
    const auto sample_opcode = SampleOpcode(name);
    const bool implicit = sample_opcode == Sample || sample_opcode == SampleBias;
    if (implicit) {
      auto *model = module.getNamedMetadata("dx.shaderModel");
      auto *stage = model && model->getNumOperands() == 1 && model->getOperand(0)->getNumOperands() == 3 ?
          dyn_cast_or_null<MDString>(model->getOperand(0)->getOperand(0)) : nullptr;
      if (!stage || stage->getString() != "ps") return reject("implicit reduction sampling requires a pixel shader");
    }
    if (call->arg_size() != SampleArgumentCount(name) || samples.size() >= 1024)
      return reject("invalid or oversized sampling module");
    auto *result = dyn_cast<StructType>(call->getType());
    uint32_t opcode;
    if (!result || result->isOpaque() || result->getNumElements() != 5 ||
        !result->getElementType(4)->isIntegerTy(32) || !Word(call->getArgOperand(0), opcode) ||
        opcode != sample_opcode)
      return reject("invalid float sample result or opcode");
    for (unsigned i = 0; i < 4; ++i)
      if (!result->getElementType(i)->isFloatTy() || !call->getArgOperand(3 + i)->getType()->isFloatTy())
        return reject("invalid float SampleLevel components or coordinates");
    for (unsigned i = 7; i < call->arg_size(); ++i)
      if (call->getArgOperand(i)->getType() != (i < 10 ? i32 : types.getFloatTy()))
        return reject("invalid sampling offset/LOD/gradient operand");
    uint32_t texture_register, sampler_register;
    const auto *texture = resolve(call->getArgOperand(1), 0, texture_register);
    const auto *sampler = resolve(call->getArgOperand(2), 3, sampler_register);
    uint32_t texture_kind, sampler_kind, component_tag, component_type;
    if (!texture || !sampler || texture->metadata->getNumOperands() != 9 ||
        sampler->metadata->getNumOperands() != 8 || !Word(texture->metadata->getOperand(6), texture_kind) ||
        (texture_kind != 1 && texture_kind != 2 && texture_kind != 4 && texture_kind != 5 &&
            texture_kind != 6 && texture_kind != 7 && texture_kind != 9) ||
        !Word(sampler->metadata->getOperand(6), sampler_kind) || sampler_kind != 0)
      return reject("finite float texture and SamplerState pair required");
    const bool cube = texture_kind == 5 || texture_kind == 9;
    if (cube) for (unsigned axis = 0; axis < 3; ++axis) {
      if (isa<UndefValue>(call->getArgOperand(3 + axis)))
        return reject("cube direction requires three defined components");
      auto *offset = call->getArgOperand(7 + axis);
      auto *constant = dyn_cast<ConstantInt>(offset);
      if (!isa<UndefValue>(offset) && (!constant || !constant->isZero()))
        return reject("cube sampling offsets are unsupported");
      if (sample_opcode == SampleGrad && (isa<UndefValue>(call->getArgOperand(10 + axis)) ||
          isa<UndefValue>(call->getArgOperand(13 + axis))))
        return reject("cube gradients require three defined components");
    }
    auto *component = dyn_cast_or_null<MDNode>(texture->metadata->getOperand(8));
    if (!component || component->getNumOperands() != 2 || !Word(component->getOperand(0), component_tag) || component_tag != 0 ||
        !Word(component->getOperand(1), component_type) || component_type != 9)
      return reject("float texture component metadata required");
    for (auto *user : call->users()) {
      auto *extract = dyn_cast<ExtractValueInst>(user);
      if (!extract || extract->getNumIndices() != 1 || *extract->idx_begin() >= 4)
        return reject("feedback/status or aggregate flow requires further lowering");
    }
    auto key = std::make_tuple(texture->space, texture_register, sampler->space, sampler_register);
    auto [index, inserted] = pair_indices.emplace(key, pairs.size());
    if (inserted) {
      if (pairs.size() >= 64) return reject("too many sampled pairs");
      pairs.push_back({{texture->space, texture_register, sampler->space, sampler_register}, texture, sampler});
    }
    samples.push_back({call, index->second, texture_kind == 4 ? 3u : texture_kind == 1 || texture_kind == 6 ? 1u : 2u, cube});
  }
  if (samples.empty()) return reject("no qualified sampling pairs");
  if (!pair_count) {
    if (pair_offset) return reject("pair offset requires a shared layout");
    pair_count = pairs.size();
  }
  if (pair_count > 64 || pair_offset >= pair_count || pairs.size() > pair_count - pair_offset)
    return reject("sampling pairs exceed shared layout");
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
      operands[4] = word(pair_offset + pair + (kind == 3 && slot >= pairs.size() ? pair_count : 0));
      operands[5] = word(1); // Each private pair is one descriptor, not the application array.
      additional.push_back(MDNode::get(context, operands));
    }
    append(kind, additional);
  }
  auto *state_type = StructType::create(context,
      {ArrayType::get(FixedVectorType::get(i32, 4), pair_count * 2)}, "dxmt.ReductionStates");
  append(2, {MDNode::get(context, {word(next_id[2]), ConstantAsMetadata::get(UndefValue::get(PointerType::getUnqual(state_type))),
      MDString::get(context, ""), word(DXMT_MSC_MINMAX_SPACE), word(0), word(1), word(pair_count * 32), nullptr})});
  auto *dimensions_type = StructType::getTypeByName(context, "dx.types.Dimensions");
  if (!dimensions_type) dimensions_type = StructType::create(context, {i32, i32, i32, i32}, "dx.types.Dimensions");
  auto dimensions = module.getOrInsertFunction("dx.op.getDimensions", dimensions_type, i32, handle, i32);
  auto binary = module.getOrInsertFunction("dx.op.binary.f32", types.getFloatTy(), i32, types.getFloatTy(), types.getFloatTy());
  for (auto [sample, pair, spatial_dimensions, cube] : samples) {
    // Preserve the application operation for the ordinary branch. In particular,
    // SampleGrad's directional footprint cannot be reconstructed from scalar LOD.
    auto *ordinary_function = sample->getCalledFunction();
    SmallVector<Value *, 17> ordinary_arguments(sample->args());
    const auto opcode = SampleOpcode(sample->getCalledFunction()->getName());
    const bool implicit = opcode == Sample || opcode == SampleBias;
    const bool gradient = implicit || opcode == SampleGrad;
    Value *instruction_clamp = opcode == SampleGrad ? sample->getArgOperand(16) :
        implicit ? sample->getArgOperand(opcode == Sample ? 10 : 11) : nullptr;
    Value *instruction_bias = opcode == SampleBias ? sample->getArgOperand(10) : nullptr;
    if (implicit) {
      IRBuilder<> normalize(sample);
      auto *f32 = types.getFloatTy();
      auto *unary_type = FunctionType::get(f32, {i32, f32}, false);
      auto *existing_unary = module.getFunction("dx.op.unary.f32");
      if (existing_unary && existing_unary->getFunctionType() != unary_type)
        return reject("invalid derivative intrinsic signature");
      auto unary = module.getOrInsertFunction("dx.op.unary.f32", unary_type);
      auto *gradient_type = FunctionType::get(sample->getType(),
          {i32, handle, handle, f32, f32, f32, f32, i32, i32, i32, f32, f32, f32, f32, f32, f32, f32}, false);
      auto *existing_gradient = module.getFunction("dx.op.sampleGrad.f32");
      if (existing_gradient && existing_gradient->getFunctionType() != gradient_type)
        return reject("invalid implicit gradient normalization signature");
      auto gradient_function = module.getOrInsertFunction("dx.op.sampleGrad.f32", gradient_type);
      SmallVector<Value *, 17> arguments;
      for (unsigned i = 0; i < 10; ++i) arguments.push_back(sample->getArgOperand(i));
      arguments[0] = normalize.getInt32(SampleGrad);
      // Derive only spatial coordinates, never the array-layer component.
      // Keep derivatives ahead of the injected sampler-enabled branch.
      for (const auto derivative : {DerivCoarseX, DerivCoarseY})
        for (unsigned axis = 0; axis < 3; ++axis) {
          Value *value = UndefValue::get(f32);
          if (cube || axis < spatial_dimensions) value = normalize.CreateCall(unary,
              {normalize.getInt32(derivative), sample->getArgOperand(3 + axis)});
          arguments.push_back(value);
        }
      arguments.push_back(instruction_clamp);
      auto *normalized = normalize.CreateCall(gradient_function, arguments);
      sample->replaceAllUsesWith(normalized); sample->eraseFromParent(); sample = normalized;
    }
    if (gradient) {
      auto *gradient_lod = CreateReductionGradientLOD(*sample, error, spatial_dimensions, cube);
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
      if (modern) {
        auto *binding = ConstantStruct::get(bind_type, {builder.getInt32(reg), builder.getInt32(reg),
            builder.getInt32(DXMT_MSC_MINMAX_SPACE), builder.getInt8(kind)});
        auto *raw = builder.CreateCall(modern_create, {builder.getInt32(217), binding, builder.getInt32(reg), builder.getFalse()});
        Constant *properties;
        if (kind == 0) properties = cast<Constant>(cast<CallInst>(sample->getArgOperand(1))->getArgOperand(2));
        else properties = ConstantStruct::get(properties_type, {builder.getInt32(kind == 3 ? 14 : 13),
            builder.getInt32(kind == 3 ? 0 : pair_count * sizeof(dxmt_msc_minmax_state))});
        return builder.CreateCall(annotate, {builder.getInt32(216), raw, properties});
      }
      return builder.CreateCall(create, {builder.getInt32(CreateHandle), builder.getInt8(kind), builder.getInt32(range),
          builder.getInt32(reg), builder.getFalse()});
    };
    auto *cb = make_handle(b, 2, next_id[2], 0);
    auto *first = b.CreateCall(cb_load, {b.getInt32(CBufferLoadLegacy), cb, b.getInt32((pair_offset + pair) * 2)});
    auto *second = b.CreateCall(cb_load, {b.getInt32(CBufferLoadLegacy), cb, b.getInt32((pair_offset + pair) * 2 + 1)});
    auto *flags = b.CreateExtractValue(first, 0);
    Value *original_lod = sample->getArgOperand(10);
    original_lod = b.CreateFAdd(original_lod,
        b.CreateBitCast(b.CreateExtractValue(second, 3), b.getFloatTy()), "dxmt.sampler.biased.lod");
    if (instruction_bias) original_lod = b.CreateFAdd(original_lod, instruction_bias, "dxmt.sample.biased.lod");
    b.CreateCondBr(b.CreateICmpNE(b.CreateAnd(flags, b.getInt32(DXMT_MSC_MINMAX_ENABLED)), b.getInt32(0)), reduction, ordinary);
    b.SetInsertPoint(ordinary);
    auto as_float = [&](Value *value) { return b.CreateBitCast(value, b.getFloatTy()); };
    auto *resource_clamp = as_float(b.CreateExtractValue(first, 3));
    auto merge_clamp = [&](Value *resource) -> Value * {
      if (!instruction_clamp || isa<UndefValue>(instruction_clamp)) return resource;
      auto *zero = ConstantFP::get(b.getFloatTy(), 0);
      return b.CreateSelect(b.CreateFCmpUNE(instruction_clamp, zero),
          b.CreateCall(binary, {b.getInt32(FMax), resource, instruction_clamp}), resource);
    };
    resource_clamp = merge_clamp(resource_clamp);
    auto *component_defaults = b.CreateExtractValue(second, 0);
    auto *dim0 = b.CreateCall(dimensions, {b.getInt32(GetDimensions), sample->getArgOperand(1), b.getInt32(0)});
    auto *last_mip = b.CreateUIToFP(b.CreateSub(b.CreateExtractValue(dim0, 3), b.getInt32(1)), b.getFloatTy());
    auto *ordinary_sample = BasicBlock::Create(context, "dxmt.ordinary.sample", function, merge);
    auto *ordinary_empty = BasicBlock::Create(context, "dxmt.ordinary.empty", function, merge);
    auto *ordinary_done = BasicBlock::Create(context, "dxmt.ordinary.result", function, merge);
    if (opcode == SampleLevel) {
      // Native MSC ignores texture minLOD metadata on affected devices. Apply
      // sampler limits first, then the resource clamp, using the already-bound
      // unbiased/unclamped ordinary sampler and zero-clamp private texture.
      // Keep gradient/implicit operations directional rather than scalarizing.
      ordinary_arguments[1] = make_handle(b, 0, next_id[0] + pair, pair_offset + pair);
      ordinary_arguments[2] = make_handle(b, 3, next_id[3] + pairs.size() + pair,
                                         pair_offset + pair_count + pair);
      auto *sampler_max = as_float(b.CreateExtractValue(first, 2));
      auto *sampler_min = as_float(b.CreateExtractValue(first, 1));
      auto *limited = b.CreateCall(binary, {b.getInt32(FMin), original_lod, sampler_max});
      limited = b.CreateCall(binary, {b.getInt32(FMax), limited, sampler_min});
      ordinary_arguments[10] = b.CreateCall(binary, {b.getInt32(FMax), limited, resource_clamp});
    }
    // Other operations retain application handles, bias and sampler clamps.
    // Instruction clamp may vary within a quad. Execute implicit sampling before
    // its empty-view branch so that the injected branch cannot invalidate derivatives.
    auto *ordinary_call = b.CreateCall(ordinary_function, ordinary_arguments);
    b.CreateCondBr(b.CreateFCmpOGT(resource_clamp, last_mip), ordinary_empty, ordinary_sample);
    b.SetInsertPoint(ordinary_sample);
    std::array<Value *, 4> ordinary_values;
    for (unsigned i = 0; i < 4; ++i) ordinary_values[i] = b.CreateExtractValue(ordinary_call, i);
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
    auto *point_texture = make_handle(b, 0, next_id[0] + pair, pair_offset + pair);
    auto *point_sampler = make_handle(b, 3, next_id[3] + pair, pair_offset + pair);
    ReductionSampleState state{flags, as_float(b.CreateExtractValue(first, 1)), as_float(b.CreateExtractValue(first, 2)),
        merge_clamp(as_float(b.CreateExtractValue(first, 3))), b.CreateExtractValue(second, 0), point_texture,
        b.CreateExtractValue(second, 1),
        b.CreateAnd(b.CreateExtractValue(second, 2), b.getInt32(DXMT_MSC_MINMAX_ADDRESS_MASK)),
        b.CreateAnd(b.CreateLShr(b.CreateExtractValue(second, 2), b.getInt32(DXMT_MSC_MINMAX_ADDRESS_W_SHIFT)),
            b.getInt32(DXMT_MSC_MINMAX_ADDRESS_MASK))};
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
    sample->eraseFromParent();
    if (!LowerReductionSampleLevel(*lowered, state, error, spatial_dimensions, cube)) return false;
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
  for (const auto name : {"dx.op.sampleGrad.f32", "dx.op.sample.f32", "dx.op.sampleBias.f32"})
    if (auto *normalized = module.getFunction(name))
      if (normalized->isDeclaration() && normalized->use_empty()) normalized->eraseFromParent();
  // Ordinary calls still carry native LOD clamps. Preserve their shader flags,
  // including TiledResources, even without a CheckAccessFullyMapped consumer.
  raw_string_ostream diagnostics(error);
  if (verifyModule(module, &diagnostics)) return false;
  std::vector<dxmt_msc_minmax_binding> result;
  for (const auto &pair : pairs) result.push_back(pair.binding);
  bindings = std::move(result);
  error.clear();
  return true;
}
}

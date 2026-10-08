#include "dxil_typed_origin.hpp"
#include "metalirconverter_native.h"
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <cstring>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <algorithm>
#include <map>
#include <new>

static_assert(sizeof(dxmt_msc_lower_typed_origins_params) == 64);
static_assert(offsetof(dxmt_msc_lower_typed_origins_params, bindings) == 40);
static_assert(sizeof(dxmt_msc_typed_origin_binding) == 12);

static int LowerTypedOrigins(dxmt_msc_lower_typed_origins_params *params) {
  if (!params) return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  params->ir_size = 0;
  params->binding_count = 0;
  constexpr uint64_t max_input_size = 16 * 1024 * 1024;
  auto valid_address = [](uint64_t address, uint64_t size) {
    return address <= UINTPTR_MAX && size <= UINTPTR_MAX - address;
  };
  uint32_t record_offset = 0, record_count = 0;
  if (params->reserved) {
    if ((params->reserved & 0xffff0000u) != DXMT_MSC_TYPED_ORIGIN_LAYOUT_TAG)
      return DXMT_MSC_ERROR_INVALID_ARGUMENT;
    record_offset = params->reserved & 255u;
    record_count = (params->reserved >> 8) & 255u;
    if (!record_count || record_count > 64 || record_offset >= record_count)
      return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  }
  if (!params->bitcode || !params->bitcode_size ||
      params->bitcode_size > max_input_size || !valid_address(params->bitcode, params->bitcode_size) ||
      (!params->ir && params->ir_capacity) || (!params->bindings && params->binding_capacity) ||
      !valid_address(params->ir, params->ir_capacity) ||
      !valid_address(params->bindings, uint64_t(params->binding_capacity) * sizeof(dxmt_msc_typed_origin_binding)))
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  auto overlaps = [](uint64_t a, uint64_t a_size, uint64_t b, uint64_t b_size) {
    return a_size && b_size && a < b + b_size && b < a + a_size;
  };
  const uint64_t binding_bytes = uint64_t(params->binding_capacity) * sizeof(dxmt_msc_typed_origin_binding);
  const uint64_t parameter_address = uintptr_t(params);
  if (overlaps(params->ir, params->ir_capacity, params->bindings, binding_bytes) ||
      overlaps(params->ir, params->ir_capacity, params->bitcode, params->bitcode_size) ||
      overlaps(params->bindings, binding_bytes, params->bitcode, params->bitcode_size) ||
      overlaps(params->ir, params->ir_capacity, parameter_address, sizeof(*params)) ||
      overlaps(params->bindings, binding_bytes, parameter_address, sizeof(*params)))
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  llvm::StringRef input(reinterpret_cast<const char *>(uintptr_t(params->bitcode)), params->bitcode_size);
  auto module = llvm::parseBitcodeFile(llvm::MemoryBufferRef(input, "typed-origin"), context);
  if (!module) {
    llvm::consumeError(module.takeError());
    return DXMT_MSC_ERROR_INVALID_DXIL;
  }
  std::vector<dxmt::dxil::TypedOriginBinding> records;
  std::string error;
  if (!dxmt::dxil::LowerTypedBufferOrigins(**module, records, error, record_offset, record_count))
    return DXMT_MSC_ERROR_UNSUPPORTED_SHADER;
  (*module)->setSourceFileName("");
  (*module)->setModuleIdentifier("");
  std::string ir;
  llvm::raw_string_ostream output(ir);
  (*module)->print(output, nullptr);
  output.flush();
  params->ir_size = ir.size();
  params->binding_count = records.size();
  // Size-only query; IR is an explicit-length byte string, not NUL terminated.
  if (!params->ir && !params->bindings) return DXMT_MSC_SUCCESS;
  if (!params->ir || params->ir_capacity < ir.size() ||
      (!records.empty() && !params->bindings) || params->binding_capacity < records.size())
    return DXMT_MSC_ERROR_OUTPUT_TOO_SMALL;
  // Publish both outputs only after all capacities have passed validation.
  std::memcpy(reinterpret_cast<void *>(uintptr_t(params->ir)), ir.data(), ir.size());
  auto *bindings = reinterpret_cast<dxmt_msc_typed_origin_binding *>(uintptr_t(params->bindings));
  for (size_t i = 0; i < records.size(); ++i)
    bindings[i] = {records[i].resource_class, records[i].register_space, records[i].shader_register};
  return DXMT_MSC_SUCCESS;
}

extern "C" int dxmt_msc_lower_typed_origins(dxmt_msc_lower_typed_origins_params *params) {
  try {
    return LowerTypedOrigins(params);
  } catch (const std::bad_alloc &) {
    return DXMT_MSC_ERROR_OUT_OF_MEMORY;
  }
}

namespace dxmt::dxil {
namespace {
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
struct Handle {
  llvm::CallInst *call;
  uint32_t record, count, base_register;
  llvm::Value *index;
  llvm::CallInst *binding = nullptr;
};
struct Access {
  llvm::CallInst *call;
  uint32_t record;
  unsigned coordinate;
  uint32_t range_count, base_register;
  llvm::Value *index;
};
}

bool LowerTypedBufferOrigins(llvm::Module &module, std::vector<TypedOriginBinding> &bindings, std::string &error,
    uint32_t record_offset, uint32_t record_count) {
  using namespace llvm;
  error.clear();
  {
    raw_string_ostream diagnostics(error);
    if (verifyModule(module, &diagnostics)) { diagnostics.flush(); return false; }
  }
  auto reject = [&](const char *message) { error = message; return false; };
  if (record_count > 64 || (!record_count && record_offset) ||
      (record_count && record_offset >= record_count)) return reject("invalid shared typed-origin interval");
  auto *named = module.getNamedMetadata("dx.resources");
  if (!named || named->getNumOperands() != 1 || named->getOperand(0)->getNumOperands() != 4)
    return reject("invalid DXIL resource metadata");
  auto *resources = named->getOperand(0);
  std::vector<TypedOriginBinding> records;
  std::map<std::pair<uint32_t, uint32_t>, std::pair<uint32_t, uint32_t>> ranges;
  for (uint32_t resource_class = 0; resource_class < 2; ++resource_class) {
    auto *list = dyn_cast_or_null<MDNode>(resources->getOperand(resource_class));
    if (!list && resources->getOperand(resource_class)) return reject("invalid resource list");
    if (!list) continue;
    for (auto &operand : list->operands()) {
      auto *resource = dyn_cast_or_null<MDNode>(operand);
      uint32_t id, space, reg, count, kind;
      if (!resource || resource->getNumOperands() < 7 || !Word(resource->getOperand(0), id) ||
          !Word(resource->getOperand(3), space) || !Word(resource->getOperand(4), reg) ||
          !Word(resource->getOperand(5), count) || !Word(resource->getOperand(6), kind))
        return reject("invalid SRV/UAV record");
      if (kind != 10) continue; // TypedBuffer; leave textures/raw/structured alone.
      if (!count || count > 64 - records.size() || uint64_t(reg) + count > uint64_t(UINT32_MAX) + 1)
        return reject("typed resource range exceeds finite lowering contract");
      if (!ranges.emplace(std::make_pair(resource_class, id), std::make_pair(records.size(), count)).second)
        return reject("duplicate typed resource range");
      for (uint32_t offset = 0; offset < count; ++offset)
        records.push_back({resource_class, space, reg + offset});
    }
  }
  if (records.empty()) return reject("no typed buffer resources");
  uint32_t cbv_id = 0;
  auto *cbvs = dyn_cast_or_null<MDNode>(resources->getOperand(2));
  if (!cbvs && resources->getOperand(2)) return reject("invalid CBV list");
  if (cbvs) for (auto &operand : cbvs->operands()) {
    auto *cbv = dyn_cast_or_null<MDNode>(operand);
    uint32_t id, space, reg, count;
    if (!cbv || cbv->getNumOperands() < 8 || !Word(cbv->getOperand(0), id) ||
        !Word(cbv->getOperand(3), space) || !Word(cbv->getOperand(4), reg) ||
        !Word(cbv->getOperand(5), count) || id == UINT32_MAX || !count)
      return reject("invalid CBV record");
    if (space == 1 && reg == 0) return reject("private b0/space1 collides with shader CBV");
    cbv_id = std::max(cbv_id, id + 1);
  }
  auto *handle_type = StructType::getTypeByName(module.getContext(), "dx.types.Handle");
  if (!handle_type || StructType::getTypeByName(module.getContext(), "dxmt.TypedBufferOrigins"))
    return reject("invalid handle type or already lowered module");
  auto *create = module.getFunction("dx.op.createHandle");
  auto *modern_create = module.getFunction("dx.op.createHandleFromBinding");
  auto *annotate = module.getFunction("dx.op.annotateHandle");
  auto &input_context = module.getContext();
  auto *bind_type = StructType::getTypeByName(input_context, "dx.types.ResBind");
  auto *properties_type = StructType::getTypeByName(input_context, "dx.types.ResourceProperties");
  const bool modern = modern_create != nullptr;
  IRBuilder<> signature(input_context);
  auto *word_type = signature.getInt32Ty();
  if ((create && modern) || (!create && !modern)) return reject("one handle model required");
  if (create && create->getFunctionType() != FunctionType::get(handle_type,
      {Type::getInt32Ty(input_context), Type::getInt8Ty(input_context), Type::getInt32Ty(input_context),
       Type::getInt32Ty(input_context), Type::getInt1Ty(input_context)}, false))
    return reject("invalid legacy handle signature");
  if (modern && (!bind_type || bind_type->isOpaque() || bind_type->elements() !=
      ArrayRef<Type *>({word_type, word_type, word_type, signature.getInt8Ty()}) ||
      !properties_type || properties_type->isOpaque() || properties_type->elements() !=
      ArrayRef<Type *>({word_type, word_type}) || !annotate ||
      modern_create->getFunctionType() != FunctionType::get(handle_type,
          {word_type, bind_type, word_type, signature.getInt1Ty()}, false) ||
      annotate->getFunctionType() != FunctionType::get(handle_type, {word_type, handle_type, properties_type}, false)))
    return reject("invalid modern handle signatures");
  auto aggregate_word = [](Value *value, unsigned index, uint32_t &word) {
    auto *constant = dyn_cast<Constant>(value);
    auto *element = constant ? dyn_cast_or_null<ConstantInt>(constant->getAggregateElement(index)) : nullptr;
    if (!element || element->getBitWidth() > 32) return false;
    word = element->getZExtValue();
    return true;
  };
  std::vector<Handle> handles;
  for (auto &function : module) for (auto &block : function) for (auto &instruction : block) {
    auto *base = dyn_cast<CallBase>(&instruction);
    if (!base) continue;
    auto *call = dyn_cast<CallInst>(base);
    if (base->getType() == handle_type && (!call || !call->getCalledFunction() ||
        (call->getCalledFunction() != create && call->getCalledFunction() != modern_create &&
         call->getCalledFunction() != annotate)))
      return reject("unsupported handle-producing call/invoke/alias");
    if (!call || !call->getCalledFunction()) continue;
    const auto name = call->getCalledFunction()->getName();
    if ((name.startswith("dx.op.createHandleFrom") && call->getCalledFunction() != modern_create) ||
        (name.startswith("dx.op.annotateHandle") && call->getCalledFunction() != annotate))
      return reject("unsupported modern handle provenance");
    if (modern && call->getCalledFunction() == modern_create) {
      uint32_t lower, upper, space, resource_class;
      if (!aggregate_word(call->getArgOperand(1), 0, lower) ||
          !aggregate_word(call->getArgOperand(1), 1, upper) ||
          !aggregate_word(call->getArgOperand(1), 2, space) ||
          !aggregate_word(call->getArgOperand(1), 3, resource_class))
        return reject("constant modern binding required");
      for (auto &[key, range] : ranges) {
        if (key.first != resource_class || records[range.first].register_space != space ||
            records[range.first].shader_register != lower ||
            uint64_t(lower) + range.second != uint64_t(upper) + 1) continue;
        for (auto *user : call->users()) {
          auto *annotation = dyn_cast<CallInst>(user);
          uint32_t property;
          if (!annotation || annotation->getCalledFunction() != annotate ||
              annotation->getArgOperand(1) != call ||
              !aggregate_word(annotation->getArgOperand(2), 0, property) || (property & 255u) != 10)
            return reject("typed binding requires typed annotation");
        }
      }
    }
    if (modern && call->getCalledFunction() == annotate) {
      uint32_t opcode, lower, upper, space, resource_class, property, component;
      auto *binding = dyn_cast<CallInst>(call->getArgOperand(1));
      if (!Word(call->getArgOperand(0), opcode) || opcode != 216 || !binding ||
          binding->getCalledFunction() != modern_create || !Word(binding->getArgOperand(0), opcode) || opcode != 217 ||
          !aggregate_word(binding->getArgOperand(1), 0, lower) ||
          !aggregate_word(binding->getArgOperand(1), 1, upper) ||
          !aggregate_word(binding->getArgOperand(1), 2, space) ||
          !aggregate_word(binding->getArgOperand(1), 3, resource_class) ||
          !aggregate_word(call->getArgOperand(2), 0, property) ||
          !aggregate_word(call->getArgOperand(2), 1, component))
        return reject("constant modern binding and annotation required");
      if ((property & 255u) != 10) continue;
      if (resource_class > 1 || property != (10u | (resource_class ? 4096u : 0u)) ||
          (component >> 8) < 1 || (component >> 8) > 4 || (component & 255u) == 0 ||
          upper == UINT32_MAX || upper < lower || !isa<ConstantInt>(binding->getArgOperand(3)))
        return reject("unsupported modern typed properties");
      uint32_t first = UINT32_MAX, count = 0;
      for (auto &[key, range] : ranges) {
        if (key.first != resource_class || records[range.first].register_space != space ||
            records[range.first].shader_register != lower ||
            uint64_t(lower) + range.second != uint64_t(upper) + 1) continue;
        if (first != UINT32_MAX) return reject("ambiguous modern typed range");
        auto *list = cast<MDNode>(resources->getOperand(resource_class));
        MDNode *metadata = nullptr;
        for (auto &operand : list->operands()) {
          auto *candidate = cast<MDNode>(operand);
          uint32_t id;
          if (Word(candidate->getOperand(0), id) && id == key.second) metadata = candidate;
        }
        const unsigned component_operand = resource_class ? 10 : 8;
        auto *components = metadata && metadata->getNumOperands() > component_operand ?
            dyn_cast_or_null<MDNode>(metadata->getOperand(component_operand)) : nullptr;
        uint32_t tag, component_type;
        if (!components || components->getNumOperands() != 2 || !Word(components->getOperand(0), tag) ||
            tag != 0 || !Word(components->getOperand(1), component_type) || component_type != (component & 255u))
          return reject("modern typed component metadata mismatch");
        first = range.first; count = range.second;
      }
      if (first == UINT32_MAX) return reject("modern typed range does not match metadata");
      for (auto *user : binding->users()) {
        auto *annotation = dyn_cast<CallInst>(user);
        if (!annotation || annotation->getCalledFunction() != annotate ||
            annotation->getArgOperand(1) != binding || annotation->getArgOperand(2) != call->getArgOperand(2))
          return reject("unannotated or inconsistent modern typed handle");
      }
      auto *index = binding->getArgOperand(2);
      uint32_t reg;
      if (Word(index, reg)) {
        if (reg < lower || reg > upper) return reject("out-of-range modern typed handle");
        handles.push_back({call, first + reg - lower, 1, lower, nullptr, binding});
      } else handles.push_back({call, first, count, lower, index, binding});
      continue;
    }
    if (call->getCalledFunction() != create) continue;
    auto *resource_class = dyn_cast<ConstantInt>(call->getArgOperand(1));
    uint32_t range;
    if (!resource_class || !Word(call->getArgOperand(2), range)) return reject("dynamic handle range");
    const auto match = ranges.find({uint32_t(resource_class->getZExtValue()), range});
    if (match == ranges.end()) continue;
    uint32_t reg;
    auto *nonuniform = dyn_cast<ConstantInt>(call->getArgOperand(4));
    const auto [first, count] = match->second;
    const auto base_register = records[first].shader_register;
    if (!nonuniform || !call->getArgOperand(3)->getType()->isIntegerTy(32))
      return reject("invalid typed handle index or nonuniform flag");
    if (Word(call->getArgOperand(3), reg)) {
      if (reg < base_register || uint64_t(reg) >= uint64_t(base_register) + count)
        return reject("out-of-range typed handle");
      handles.push_back({call, first + (reg - base_register), 1, base_register, nullptr});
    } else handles.push_back({call, first, count, base_register, call->getArgOperand(3)});
  }
  std::vector<Access> accesses;
  for (const auto &resolved : handles) for (auto *user : resolved.call->users()) {
    auto *handle = resolved.call;
    const auto record = resolved.record, range_count = resolved.count, base_register = resolved.base_register;
    auto *index = resolved.index;
    auto *call = dyn_cast<CallInst>(user);
    uint32_t opcode;
    if (!call || !call->getCalledFunction() || call->arg_size() < 4 || call->getArgOperand(1) != handle ||
        !Word(call->getArgOperand(0), opcode)) return reject("unsupported typed handle flow");
    const auto name = call->getCalledFunction()->getName();
    const bool load = opcode == 68 && name.startswith("dx.op.bufferLoad.");
    const bool store = opcode == 69 && name.startswith("dx.op.bufferStore.");
    const bool atomic = (opcode == 78 && name.startswith("dx.op.atomicBinOp.")) ||
        (opcode == 79 && name.startswith("dx.op.atomicCompareExchange."));
    if (!load && !store && !atomic) return reject("typed handle operation requires further lowering");
    if ((store || atomic) && records[record].resource_class != 1) return reject("write through SRV");
    const unsigned coordinate = opcode == 78 ? 3 : 2;
    if (!call->getArgOperand(coordinate)->getType()->isIntegerTy(32)) return reject("invalid coordinate");
    if (atomic && !call->getType()->isIntegerTy(32)) return reject("only 32-bit typed atomics supported");
    if (load) for (auto *value_user : call->users()) {
      auto *extract = dyn_cast<ExtractValueInst>(value_user);
      if (!extract || extract->getNumIndices() != 1 || *extract->idx_begin() >= 4)
        return reject("typed load residency/status or aggregate flow requires further lowering");
    }
    accesses.push_back({call, record, coordinate, range_count, base_register, index});
  }
  if (accesses.empty()) return reject("no directly resolved typed accesses");

  // Declaring a finite range does not require unused slots to be initialized.
  // Keep only accessed slots in the private state and submission binding list.
  std::vector<bool> used(records.size(), false);
  for (const auto &access : accesses)
    for (uint32_t offset = 0; offset < access.range_count; ++offset) used[access.record + offset] = true;
  std::vector<uint32_t> remap(records.size());
  std::vector<TypedOriginBinding> accessed_records;
  for (uint32_t index = 0; index < records.size(); ++index) if (used[index]) {
    remap[index] = accessed_records.size();
    accessed_records.push_back(records[index]);
  }
  for (auto &access : accesses) access.record = remap[access.record];
  records = std::move(accessed_records);

  // All supported operations have been identified before modifying the module.
  // Size only after pruning unused declarations: published bindings and shader
  // CBV metadata must describe the same local or shared record layout.
  if (!record_count) record_count = records.size();
  if (records.size() > record_count - record_offset)
    return reject("typed-origin stage exceeds shared interval");
  // Per-access insertion works in existing branches and loops; no text grammar
  // or fixed main/register numbering is used, and output UAVs are guarded too.
  auto &context = module.getContext();
  IRBuilder<> types(context);
  auto *i32 = types.getInt32Ty();
  auto *cb_ret = StructType::getTypeByName(context, "dx.types.CBufRet.i32");
  if (cb_ret && (cb_ret->isOpaque() || cb_ret->getNumElements() != 4 ||
      !std::all_of(cb_ret->element_begin(), cb_ret->element_end(), [i32](Type *type) { return type == i32; })))
    return reject("invalid cbuffer return type");
  auto *existing_load = module.getFunction("dx.op.cbufferLoadLegacy.i32");
  if (existing_load && (!cb_ret || existing_load->getFunctionType() !=
      FunctionType::get(cb_ret, {i32, handle_type, i32}, false)))
    return reject("invalid cbuffer load signature");
  if (!cb_ret) cb_ret = StructType::create(context, {i32, i32, i32, i32}, "dx.types.CBufRet.i32");
  auto cb_load = module.getOrInsertFunction("dx.op.cbufferLoadLegacy.i32", cb_ret, i32, handle_type, i32);
  for (const auto &handle : handles) if (handle.index) {
    IRBuilder<> builder(handle.binding ? handle.binding : handle.call);
    auto *relative = builder.CreateSub(handle.index, builder.getInt32(handle.base_register));
    auto *in_range = builder.CreateICmpULT(relative, builder.getInt32(handle.count));
    auto *binding = handle.binding ? handle.binding : handle.call;
    binding->setArgOperand(handle.binding ? 2 : 3,
        builder.CreateSelect(in_range, handle.index, builder.getInt32(handle.base_register)));
  }
  for (auto access : accesses) {
    auto *call = access.call;
    IRBuilder<> builder(call);
    Value *cb;
    if (modern) {
      auto *binding = ConstantStruct::get(bind_type, {builder.getInt32(0), builder.getInt32(0),
          builder.getInt32(1), builder.getInt8(2)});
      auto *raw = builder.CreateCall(modern_create,
          {builder.getInt32(217), binding, builder.getInt32(0), builder.getFalse()});
      auto *properties = ConstantStruct::get(properties_type,
          {builder.getInt32(13), builder.getInt32(record_count * 16)});
      cb = builder.CreateCall(annotate, {builder.getInt32(216), raw, properties}, "dxmt.origin.cb");
    } else cb = builder.CreateCall(create, {builder.getInt32(57), builder.getInt8(2), builder.getInt32(cbv_id),
                                          builder.getInt32(0), builder.getFalse()}, "dxmt.origin.cb");
    Value *state_index = builder.getInt32(record_offset + access.record);
    Value *handle_valid = builder.getTrue();
    if (access.index) {
      auto *relative = builder.CreateSub(access.index, builder.getInt32(access.base_register));
      handle_valid = builder.CreateICmpULT(relative, builder.getInt32(access.range_count));
      state_index = builder.CreateSelect(handle_valid,
          builder.CreateAdd(relative, state_index), builder.getInt32(record_offset));
    }
    auto *data = builder.CreateCall(cb_load, {builder.getInt32(59), cb, state_index}, "dxmt.origin.data");
    auto *origin = builder.CreateExtractValue(data, 0);
    auto *count = builder.CreateExtractValue(data, 1);
    auto *coordinate = call->getArgOperand(access.coordinate);
    auto *adjusted = builder.CreateAdd(coordinate, origin, "dxmt.origin.coordinate");
    auto *valid = builder.CreateAnd(builder.CreateICmpULT(coordinate, count), builder.CreateICmpUGE(adjusted, coordinate));
    if (access.index) valid = builder.CreateAnd(handle_valid, valid);
    auto *then_end = SplitBlockAndInsertIfThen(valid, call, false);
    auto *skip = then_end->getParent()->getSinglePredecessor();
    auto *merge = call->getParent();
    call->setArgOperand(access.coordinate, adjusted);
    call->moveBefore(then_end);
    if (!call->getType()->isVoidTy()) {
      auto *phi = PHINode::Create(call->getType(), 2, "dxmt.origin.result", &*merge->begin());
      call->replaceAllUsesWith(phi);
      phi->addIncoming(call, then_end->getParent());
      phi->addIncoming(Constant::getNullValue(call->getType()), skip);
    }
  }
  auto *origin_type = StructType::create(context, {ArrayType::get(FixedVectorType::get(i32, 4), record_count)},
                                       "dxmt.TypedBufferOrigins");
  auto metadata_word = [&](uint32_t value) -> Metadata * { return ConstantAsMetadata::get(types.getInt32(value)); };
  auto *record = MDNode::get(context, {metadata_word(cbv_id),
      ConstantAsMetadata::get(UndefValue::get(PointerType::getUnqual(origin_type))), MDString::get(context, ""),
      metadata_word(1), metadata_word(0), metadata_word(1), metadata_word(record_count * 16), nullptr});
  std::vector<Metadata *> cbv_records;
  if (cbvs) for (auto &operand : cbvs->operands()) cbv_records.push_back(operand.get());
  cbv_records.push_back(record);
  resources->replaceOperandWith(2, MDNode::get(context, cbv_records));
  // DXC's older assembler numbers unnamed blocks differently from LLVM 15.
  // Give every unnamed value/block an explicit LLVM-uniqued name; operands
  // update structurally, without rewriting strings or register identifiers.
  for (auto &function : module) for (auto &block : function) {
    if (!block.hasName()) block.setName("dxmt.block");
    for (auto &instruction : block)
      if (!instruction.getType()->isVoidTy() && !instruction.hasName()) instruction.setName("dxmt.value");
  }
  raw_string_ostream diagnostics(error);
  if (verifyModule(module, &diagnostics)) { diagnostics.flush(); return false; }
  bindings = std::move(records);
  error.clear();
  return true;
}
}

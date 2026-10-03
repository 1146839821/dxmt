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
  if (params->reserved || !params->bitcode || !params->bitcode_size ||
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
  if (!dxmt::dxil::LowerTypedBufferOrigins(**module, records, error))
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
};
struct Access {
  llvm::CallInst *call;
  uint32_t record;
  unsigned coordinate;
  uint32_t range_count, base_register;
  llvm::Value *index;
};
}

bool LowerTypedBufferOrigins(llvm::Module &module, std::vector<TypedOriginBinding> &bindings, std::string &error) {
  using namespace llvm;
  error.clear();
  {
    raw_string_ostream diagnostics(error);
    if (verifyModule(module, &diagnostics)) { diagnostics.flush(); return false; }
  }
  auto reject = [&](const char *message) { error = message; return false; };
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
  auto &input_context = module.getContext();
  if (!create || create->getFunctionType() != FunctionType::get(handle_type,
      {Type::getInt32Ty(input_context), Type::getInt8Ty(input_context), Type::getInt32Ty(input_context),
       Type::getInt32Ty(input_context), Type::getInt1Ty(input_context)}, false))
    return reject("legacy createHandle required");
  std::vector<Handle> handles;
  for (auto &function : module) for (auto &block : function) for (auto &instruction : block) {
    auto *base = dyn_cast<CallBase>(&instruction);
    if (!base) continue;
    auto *call = dyn_cast<CallInst>(base);
    if (base->getType() == handle_type && (!call || !call->getCalledFunction() || call->getCalledFunction() != create))
      return reject("unsupported handle-producing call/invoke/alias");
    if (!call || !call->getCalledFunction()) continue;
    const auto name = call->getCalledFunction()->getName();
    if (name.startswith("dx.op.createHandleFrom") || name.startswith("dx.op.annotateHandle"))
      return reject("modern/dynamic handle provenance is not implemented");
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
  for (auto [handle, record, range_count, base_register, index] : handles) for (auto *user : handle->users()) {
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
    IRBuilder<> builder(handle.call);
    auto *relative = builder.CreateSub(handle.index, builder.getInt32(handle.base_register));
    auto *in_range = builder.CreateICmpULT(relative, builder.getInt32(handle.count));
    handle.call->setArgOperand(3, builder.CreateSelect(in_range, handle.index, builder.getInt32(handle.base_register)));
  }
  for (auto access : accesses) {
    auto *call = access.call;
    IRBuilder<> builder(call);
    auto *cb = builder.CreateCall(create, {builder.getInt32(57), builder.getInt8(2), builder.getInt32(cbv_id),
                                         builder.getInt32(0), builder.getFalse()}, "dxmt.origin.cb");
    Value *state_index = builder.getInt32(access.record);
    Value *handle_valid = builder.getTrue();
    if (access.index) {
      auto *relative = builder.CreateSub(access.index, builder.getInt32(access.base_register));
      handle_valid = builder.CreateICmpULT(relative, builder.getInt32(access.range_count));
      state_index = builder.CreateSelect(handle_valid,
          builder.CreateAdd(relative, state_index), builder.getInt32(0));
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
  auto *origin_type = StructType::create(context, {ArrayType::get(FixedVectorType::get(i32, 4), records.size())},
                                       "dxmt.TypedBufferOrigins");
  auto metadata_word = [&](uint32_t value) -> Metadata * { return ConstantAsMetadata::get(types.getInt32(value)); };
  auto *record = MDNode::get(context, {metadata_word(cbv_id),
      ConstantAsMetadata::get(UndefValue::get(PointerType::getUnqual(origin_type))), MDString::get(context, ""),
      metadata_word(1), metadata_word(0), metadata_word(1), metadata_word(records.size() * 16), nullptr});
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

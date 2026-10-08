#include "dxil_logic_op.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <set>
#include <vector>
#include <algorithm>

namespace dxmt::dxil {
bool LowerIntegerLogicOutputs(llvm::Module &module, uint32_t operation,
    const std::array<std::array<uint32_t, 4>, 8> &widths,
    uint32_t &framebuffer_space, std::string &error, uint32_t space_ceiling) {
  using namespace llvm;
  error.clear();
  framebuffer_space = UINT32_MAX;
  auto reject = [&](const char *message) { error = message; return false; };
  {
    raw_string_ostream diagnostics(error);
    if (verifyModule(module, &diagnostics)) return false;
  }
  if (operation > 15 || space_ceiling > 2147420893u) return reject("invalid logic operation or feature space ceiling");
  for (auto &target : widths) for (auto width : target)
    if (width > 32) return reject("invalid target width");
  auto *model = module.getNamedMetadata("dx.shaderModel");
  auto *entries = module.getNamedMetadata("dx.entryPoints");
  if (!model || model->getNumOperands() != 1 || model->getOperand(0)->getNumOperands() != 3 ||
      !entries || entries->getNumOperands() != 1)
    return reject("single pixel entry point required");
  auto *stage = dyn_cast<MDString>(model->getOperand(0)->getOperand(0));
  if (!stage || stage->getString() != "ps") return reject("pixel shader required");
  auto word = [](Metadata *metadata, uint32_t &value) {
    auto *constant = mdconst::dyn_extract_or_null<ConstantInt>(metadata);
    if (!constant || constant->getBitWidth() > 32) return false;
    value = constant->getZExtValue(); return true;
  };
  auto *entry = entries->getOperand(0);
  auto *function_md = entry->getNumOperands() == 5 ? dyn_cast_or_null<ValueAsMetadata>(entry->getOperand(0)) : nullptr;
  auto *function = function_md ? dyn_cast<Function>(function_md->getValue()) : nullptr;
  auto *signature = entry->getNumOperands() == 5 ? dyn_cast_or_null<MDNode>(entry->getOperand(2)) : nullptr;
  auto *outputs = signature && signature->getNumOperands() == 3 ? dyn_cast_or_null<MDNode>(signature->getOperand(1)) : nullptr;
  if (!function || !outputs) return reject("pixel output signature required");
  struct Target { unsigned index, columns, start_column; };
  std::map<uint32_t, Target> targets;
  std::set<unsigned> used_targets;
  for (auto &operand : outputs->operands()) {
    auto *record = dyn_cast_or_null<MDNode>(operand);
    uint32_t id, component, semantic, rows, columns, start_column;
    if (!record || record->getNumOperands() != 11 || !word(record->getOperand(0), id) ||
        !word(record->getOperand(3), semantic)) return reject("invalid output signature");
    if (semantic != 16) continue; // Preserve non-color system outputs.
    auto *indices = dyn_cast_or_null<MDNode>(record->getOperand(4));
    uint32_t target;
    if (!word(record->getOperand(2), component) || component != 5 || !indices ||
        indices->getNumOperands() != 1 || !word(indices->getOperand(0), target) || target >= 8 ||
        !word(record->getOperand(6), rows) || rows != 1 ||
        !word(record->getOperand(7), columns) || !columns || columns > 4 ||
        !word(record->getOperand(9), start_column) || start_column > 4 - columns ||
        !targets.emplace(id, Target{target, columns, start_column}).second ||
        !used_targets.insert(target).second)
      return reject("unique scalar-row UINT color signature required");
  }
  struct Site { CallInst *store; unsigned target, component; };
  std::vector<Site> sites;
  for (auto &block : *function) for (auto &instruction : block) {
    auto *call = dyn_cast<CallInst>(&instruction);
    if (!call || !call->getCalledFunction() || call->getCalledFunction()->getName() != "dx.op.storeOutput.i32") continue;
    if (call->arg_size() != 5) return reject("invalid output call");
    auto *id = dyn_cast<ConstantInt>(call->getArgOperand(1));
    auto found = id ? targets.find(id->getZExtValue()) : targets.end();
    if (!id) return reject("dynamic output signature index");
    if (found == targets.end()) continue;
    auto *column = dyn_cast<ConstantInt>(call->getArgOperand(3));
    auto *row = dyn_cast<ConstantInt>(call->getArgOperand(2));
    auto *opcode = dyn_cast<ConstantInt>(call->getArgOperand(0));
    if (!column || !column->getType()->isIntegerTy(8) || column->getZExtValue() >= found->second.columns ||
        !row || !row->isZero() || !row->getType()->isIntegerTy(32) || !opcode ||
        !opcode->getType()->isIntegerTy(32) || opcode->getZExtValue() != 5 ||
        !id->getType()->isIntegerTy(32) || !call->getType()->isVoidTy() ||
        !call->getArgOperand(4)->getType()->isIntegerTy(32)) return reject("invalid color store coordinates");
    const unsigned component = found->second.start_column + column->getZExtValue();
    if (widths[found->second.index][component]) sites.push_back({call, found->second.index, component});
  }
  if (sites.empty()) return reject("no bound UINT outputs");
  auto &context = module.getContext();
  IRBuilder<> types(context);
  auto *i32 = types.getInt32Ty();
  auto *named = module.getNamedMetadata("dx.resources");
  MDNode *resources = named && named->getNumOperands() == 1 ? named->getOperand(0) : nullptr;
  if ((named && !resources) || (resources && resources->getNumOperands() != 4) ||
      entry->getOperand(3).get() != resources) return reject("inconsistent resource metadata");
  std::set<uint32_t> spaces, ids;
  uint32_t next_id = 0;
  for (unsigned kind = 0; resources && kind < 4; ++kind) {
    auto *list = dyn_cast_or_null<MDNode>(resources->getOperand(kind));
    if (!list && resources->getOperand(kind)) return reject("invalid resource list");
    if (!list) continue;
    for (auto &operand : list->operands()) {
      auto *record = dyn_cast_or_null<MDNode>(operand);
      uint32_t id, space;
      if (!record || record->getNumOperands() < 6 || !word(record->getOperand(0), id) ||
          !word(record->getOperand(3), space)) return reject("invalid resource record");
      spaces.insert(space);
      if (!kind) {
        if (id == UINT32_MAX || !ids.insert(id).second) return reject("invalid SRV identity");
        next_id = std::max(next_id, id + 1);
      }
    }
  }
  if (next_id > UINT32_MAX - 8) return reject("SRV identity overflow");
  uint32_t space = space_ceiling;
  while (spaces.count(space)) { if (!space) return reject("no feature space available"); --space; }
  uint32_t major, minor;
  if (!word(model->getOperand(0)->getOperand(1), major) || major != 6 ||
      !word(model->getOperand(0)->getOperand(2), minor) || minor > 6)
    return reject("SM6.0 through SM6.6 pixel shader required");
  const bool modern = minor >= 6 || module.getFunction("dx.op.createHandleFromBinding");
  auto *handle = StructType::getTypeByName(context, "dx.types.Handle");
  if (!handle) handle = StructType::create(context, {types.getInt8PtrTy()}, "dx.types.Handle");
  auto *create_type = FunctionType::get(handle, {i32, types.getInt8Ty(), i32, i32, types.getInt1Ty()}, false);
  if (auto *create = module.getFunction("dx.op.createHandle"))
    if (create->getFunctionType() != create_type) return reject("invalid handle signature");
  auto *bind_type = StructType::getTypeByName(context, "dx.types.ResBind");
  auto *properties = StructType::getTypeByName(context, "dx.types.ResourceProperties");
  if (modern) {
    if (!bind_type) bind_type = StructType::create(context, {i32, i32, i32, types.getInt8Ty()}, "dx.types.ResBind");
    if (!properties) properties = StructType::create(context, {i32, i32}, "dx.types.ResourceProperties");
    if (bind_type->isOpaque() || bind_type->elements() != ArrayRef<Type *>({i32, i32, i32, types.getInt8Ty()}) ||
        properties->isOpaque() || properties->elements() != ArrayRef<Type *>({i32, i32}))
      return reject("invalid modern resource types");
    auto *modern_type = FunctionType::get(handle, {i32, bind_type, i32, types.getInt1Ty()}, false);
    auto *annotation_type = FunctionType::get(handle, {i32, handle, properties}, false);
    if (auto *existing = module.getFunction("dx.op.createHandleFromBinding"))
      if (existing->getFunctionType() != modern_type) return reject("invalid modern handle signature");
    if (auto *existing = module.getFunction("dx.op.annotateHandle"))
      if (existing->getFunctionType() != annotation_type) return reject("invalid resource annotation signature");
  }
  auto *result_type = StructType::getTypeByName(context, "dx.types.ResRet.i32");
  if (!result_type) result_type = StructType::create(context, {i32, i32, i32, i32, i32}, "dx.types.ResRet.i32");
  if (result_type->isOpaque() || result_type->elements() != ArrayRef<Type *>({i32, i32, i32, i32, i32}))
    return reject("invalid framebuffer load result type");
  auto *load_type = FunctionType::get(result_type, {i32, handle, i32, i32, i32, i32, i32, i32, i32}, false);
  if (auto *load = module.getFunction("dx.op.textureLoad.i32"))
    if (load->getFunctionType() != load_type) return reject("invalid texture load signature");
  auto create = modern ? module.getOrInsertFunction("dx.op.createHandleFromBinding",
      FunctionType::get(handle, {i32, bind_type, i32, types.getInt1Ty()}, false)) :
      module.getOrInsertFunction("dx.op.createHandle", create_type);
  auto load = module.getOrInsertFunction("dx.op.textureLoad.i32", load_type);
  auto mdword = [&](uint32_t value) -> Metadata * { return ConstantAsMetadata::get(types.getInt32(value)); };
  auto *texture = StructType::create(context, {FixedVectorType::get(i32, 4)}, "dxmt.FramebufferUINT");
  std::vector<Metadata *> srvs;
  if (resources) if (auto *list = dyn_cast_or_null<MDNode>(resources->getOperand(0)))
    for (auto &operand : list->operands()) srvs.push_back(operand.get());
  std::map<unsigned, uint32_t> ranges;
  for (auto &site : sites) if (!ranges.count(site.target)) {
    const uint32_t id = next_id++;
    ranges.emplace(site.target, id);
    srvs.push_back(MDNode::get(context, {mdword(id), ConstantAsMetadata::get(UndefValue::get(PointerType::getUnqual(texture))),
        MDString::get(context, ""), mdword(space), mdword(site.target), mdword(1), mdword(2), mdword(0),
        MDNode::get(context, {mdword(0), mdword(5)})}));
  }
  std::vector<Metadata *> groups{MDNode::get(context, srvs), nullptr, nullptr, nullptr};
  if (resources) for (unsigned kind = 1; kind < 4; ++kind) groups[kind] = resources->getOperand(kind).get();
  auto *replacement = MDNode::getDistinct(context, groups);
  if (!named) named = module.getOrInsertNamedMetadata("dx.resources");
  if (named->getNumOperands()) named->setOperand(0, replacement); else named->addOperand(replacement);
  std::vector<Metadata *> entry_operands;
  for (auto &operand : entry->operands()) entry_operands.push_back(operand.get());
  entry_operands[3] = replacement;
  entries->setOperand(0, MDNode::getDistinct(context, entry_operands));
  for (auto &site : sites) {
    IRBuilder<> builder(site.store);
    Value *resource;
    if (modern) {
      auto *binding = ConstantStruct::get(bind_type, {builder.getInt32(site.target), builder.getInt32(site.target),
          builder.getInt32(space), builder.getInt8(0)});
      resource = builder.CreateCall(create, {builder.getInt32(217), binding, builder.getInt32(site.target), builder.getFalse()});
      auto annotate = module.getOrInsertFunction("dx.op.annotateHandle",
          FunctionType::get(handle, {i32, handle, properties}, false));
      auto *property = ConstantStruct::get(properties, {builder.getInt32(2), builder.getInt32(5 | (4 << 8))});
      resource = builder.CreateCall(annotate, {builder.getInt32(216), resource, property});
    } else resource = builder.CreateCall(create, {builder.getInt32(57), builder.getInt8(0),
        builder.getInt32(ranges.at(site.target)), builder.getInt32(site.target), builder.getFalse()});
    auto *undef = UndefValue::get(i32);
    auto *fetched = builder.CreateCall(load, {builder.getInt32(66), resource, builder.getInt32(0),
        builder.getInt32(0), builder.getInt32(0), undef, undef, undef, undef});
    auto *destination = builder.CreateExtractValue(fetched, site.component);
    if (!LowerIntegerLogicOutput(*site.store, destination, operation, widths[site.target][site.component], error)) return false;
  }
  raw_string_ostream diagnostics(error);
  if (verifyModule(module, &diagnostics)) return false;
  framebuffer_space = space;
  return true;
}
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

#include "dxil_minmax.hpp"
#include <functional>
#include <array>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

namespace dxmt::dxil {
namespace {
constexpr unsigned SampleLevel = 62, GetDimensions = 72, Floor = 27, FMax = 35, FMin = 36;
constexpr unsigned MinLinear = 1, MagLinear = 2, MipLinear = 4, Maximum = 8;
bool IsFloat4Status(llvm::Type *type) {
  auto *structure = llvm::dyn_cast<llvm::StructType>(type);
  if (!structure || structure->isOpaque() || structure->getNumElements() != 5) return false;
  for (unsigned i = 0; i < 4; ++i)
    if (!structure->getElementType(i)->isFloatTy()) return false;
  return structure->getElementType(4)->isIntegerTy(32);
}

// Every optional footprint tap is control-dependent on its weight. A select
// after an unconditional sample would still access excluded texels.
using Components = std::array<llvm::Value *, 4>;
Components OptionalTap(llvm::IRBuilder<> &builder, llvm::Value *condition,
    const Components &previous, const std::function<Components(llvm::IRBuilder<> &)> &emit) {
  auto *before = builder.GetInsertBlock();
  auto *function = before->getParent();
  auto &context = before->getContext();
  auto *active = llvm::BasicBlock::Create(context, "dxmt.minmax.tap", function);
  auto *merge = llvm::BasicBlock::Create(context, "dxmt.minmax.tap.merge", function);
  builder.CreateCondBr(condition, active, merge);
  builder.SetInsertPoint(active);
  auto value = emit(builder);
  auto *active_end = builder.GetInsertBlock();
  builder.CreateBr(merge);
  builder.SetInsertPoint(merge);
  Components result;
  for (unsigned i = 0; i < 4; ++i) {
    auto *phi = builder.CreatePHI(previous[i]->getType(), 2);
    phi->addIncoming(previous[i], before);
    phi->addIncoming(value[i], active_end);
    result[i] = phi;
  }
  return result;
}
}

bool LowerReductionSampleLevel2D(llvm::CallInst &sample,
    const ReductionSampleState &state, std::string &error) {
  using namespace llvm;
  error.clear();
  auto reject = [&](const char *reason) { error = reason; return false; };
  auto *callee = sample.getCalledFunction();
  if (!callee || callee->getName() != "dx.op.sampleLevel.f32" || sample.arg_size() != 11 ||
      !IsFloat4Status(sample.getType())) return reject("expected float SampleLevel signature");
  auto *opcode = dyn_cast<ConstantInt>(sample.getArgOperand(0));
  if (!opcode || !opcode->getType()->isIntegerTy(32) || opcode->getZExtValue() != SampleLevel)
    return reject("invalid SampleLevel opcode");
  auto *handle_type = StructType::getTypeByName(sample.getContext(), "dx.types.Handle");
  if (!handle_type || sample.getArgOperand(1)->getType() != handle_type ||
      sample.getArgOperand(2)->getType() != handle_type)
    return reject("invalid texture/sampler handle types");
  for (unsigned i = 3; i < 7; ++i)
    if (!sample.getArgOperand(i)->getType()->isFloatTy()) return reject("invalid coordinates");
  for (unsigned i = 7; i < 10; ++i)
    if (!sample.getArgOperand(i)->getType()->isIntegerTy(32)) return reject("invalid offsets");
  if (!sample.getArgOperand(10)->getType()->isFloatTy()) return reject("invalid level");
  if (!state.flags || !state.flags->getType()->isIntegerTy(32) ||
      !state.default_components || !state.default_components->getType()->isIntegerTy(32) ||
      !state.min_lod || !state.min_lod->getType()->isFloatTy() ||
      !state.max_lod || !state.max_lod->getType()->isFloatTy() ||
      !state.resource_clamp || !state.resource_clamp->getType()->isFloatTy() ||
      !state.point_texture || state.point_texture->getType() != handle_type ||
      !state.address_u || !state.address_u->getType()->isIntegerTy(32) ||
      !state.address_v || !state.address_v->getType()->isIntegerTy(32))
    return reject("invalid reduction state types");
  DominatorTree dominance(*sample.getFunction());
  for (auto *value : {state.flags, state.default_components, state.min_lod, state.max_lod,
      state.resource_clamp, state.point_texture, state.address_u, state.address_v})
    if (&value->getContext() != &sample.getContext() ||
        (isa<Argument>(value) && cast<Argument>(value)->getParent() != sample.getFunction()) ||
        (isa<Instruction>(value) && (cast<Instruction>(value)->getFunction() != sample.getFunction() ||
          value == &sample || !dominance.dominates(value, &sample))))
      return reject("reduction state must dominate sample in the same function");
  for (unsigned i = 7; i < 9; ++i) {
    auto *offset = sample.getArgOperand(i);
    auto *constant = dyn_cast<ConstantInt>(offset);
    if (!isa<UndefValue>(offset) && (!constant || constant->getSExtValue() < -8 || constant->getSExtValue() > 7))
      return reject("expected constant texture offset in -8..7");
  }
  for (auto *user : sample.users()) {
    auto *extract = dyn_cast<ExtractValueInst>(user);
    if (!extract || extract->getNumIndices() != 1 || *extract->idx_begin() >= 4)
      return reject("feedback/status or aggregate flow requires further lowering");
  }
  auto &module = *sample.getModule();
  auto &context = module.getContext();
  IRBuilder<> types(context);
  auto *i32 = types.getInt32Ty();
  auto *f32 = types.getFloatTy();
  auto *dimensions_type = StructType::getTypeByName(context, "dx.types.Dimensions");
  if (dimensions_type && (dimensions_type->isOpaque() || dimensions_type->getNumElements() != 4))
    return reject("invalid dimensions type");
  if (dimensions_type) for (auto *type : dimensions_type->elements())
    if (type != i32) return reject("invalid dimensions components");
  auto *dimensions_fn = module.getFunction("dx.op.getDimensions");
  if (dimensions_fn && (!dimensions_type || dimensions_fn->getFunctionType() !=
      FunctionType::get(dimensions_type, {i32, handle_type, i32}, false)))
    return reject("invalid dimensions signature");
  for (auto name : {"dx.op.unary.f32", "dx.op.binary.f32"}) {
    auto *function = module.getFunction(name);
    auto *expected = name == StringRef("dx.op.unary.f32") ?
        FunctionType::get(f32, {i32, f32}, false) : FunctionType::get(f32, {i32, f32, f32}, false);
    if (function && function->getFunctionType() != expected) return reject("invalid numeric intrinsic signature");
  }
  // All rejection checks precede mutation. This helper is intentionally not
  // a handle-provenance parser: the calling module pass owns that qualification.
  if (!dimensions_type) dimensions_type = StructType::create(context, {i32, i32, i32, i32}, "dx.types.Dimensions");
  auto dimensions = module.getOrInsertFunction("dx.op.getDimensions", dimensions_type, i32, handle_type, i32);
  auto unary = module.getOrInsertFunction("dx.op.unary.f32", f32, i32, f32);
  auto binary = module.getOrInsertFunction("dx.op.binary.f32", f32, i32, f32, f32);
  auto *entry = sample.getParent();
  auto *merge = entry->splitBasicBlock(sample.getIterator(), "dxmt.minmax.result");
  entry->getTerminator()->eraseFromParent();
  auto *function = entry->getParent();
  auto *active = BasicBlock::Create(context, "dxmt.minmax.active", function, merge);
  auto *empty = BasicBlock::Create(context, "dxmt.minmax.empty", function, merge);
  IRBuilder<> builder(entry);
  auto number = [&](float value) { return ConstantFP::get(f32, value); };
  auto operation = [&](IRBuilder<> &b, unsigned op, Value *a, Value *c) -> Value * {
    return b.CreateCall(binary, {b.getInt32(op), a, c});
  };
  auto bit = [&](IRBuilder<> &b, unsigned mask) -> Value * {
    return b.CreateICmpNE(b.CreateAnd(state.flags, b.getInt32(mask)), b.getInt32(0));
  };
  auto *dim0 = builder.CreateCall(dimensions, {builder.getInt32(GetDimensions), state.point_texture, builder.getInt32(0)});
  auto *last = builder.CreateUIToFP(builder.CreateSub(builder.CreateExtractValue(dim0, 3), builder.getInt32(1)), f32);
  auto *sampler_lod = operation(builder, FMax, state.min_lod,
      operation(builder, FMin, state.max_lod, sample.getArgOperand(10)));
  auto *lod = operation(builder, FMax, sampler_lod, state.resource_clamp);
  builder.CreateCondBr(builder.CreateFCmpOGT(state.resource_clamp, last), empty, active);
  builder.SetInsertPoint(empty);
  Components defaults;
  for (unsigned component = 0; component < 4; ++component) {
    auto *one = builder.CreateICmpNE(builder.CreateAnd(state.default_components,
        builder.getInt32(1u << component)), builder.getInt32(0));
    defaults[component] = builder.CreateSelect(one, number(1), number(0));
  }
  builder.CreateBr(merge);
  builder.SetInsertPoint(active);
  auto *linear = builder.CreateSelect(builder.CreateFCmpOGT(lod, number(0)), bit(builder, MinLinear), bit(builder, MagLinear));
  auto *mip_linear = bit(builder, MipLinear);
  auto *maximum = bit(builder, Maximum);
  auto *bounded = operation(builder, FMax, number(0), operation(builder, FMin, lod, last));
  auto *rounded = builder.CreateCall(unary, {builder.getInt32(Floor),
      builder.CreateFAdd(bounded, builder.CreateSelect(mip_linear, number(0), number(0.5f)))});
  auto *lower = builder.CreateFPToUI(rounded, i32);
  auto reduce_values = [&](IRBuilder<> &b, const Components &a, const Components &c) -> Components {
    Components result;
    for (unsigned component = 0; component < 4; ++component) {
      result[component] = b.CreateSelect(maximum, operation(b, FMax, a[component], c[component]),
          operation(b, FMin, a[component], c[component]));
    }
    return result;
  };
  auto level = [&](IRBuilder<> &b, Value *mip) -> Components {
    auto *dims = b.CreateCall(dimensions, {b.getInt32(GetDimensions), state.point_texture, mip});
    Value *size[2], *base[2], *fraction[2];
    for (unsigned axis = 0; axis < 2; ++axis) {
      size[axis] = b.CreateUIToFP(b.CreateExtractValue(dims, axis), f32);
      auto *coordinate = sample.getArgOperand(3 + axis);
      auto *address = axis ? state.address_v : state.address_u;
      auto floor_value = [&](Value *value) { return b.CreateCall(unary, {b.getInt32(Floor), value}); };
      auto *wrapped = b.CreateFSub(coordinate, floor_value(coordinate));
      auto *period = b.CreateFSub(coordinate, b.CreateFMul(number(2), floor_value(b.CreateFMul(coordinate, number(0.5f)))));
      auto clamp = [&](Value *value, float minimum, float maximum) {
        return operation(b, FMax, number(minimum), operation(b, FMin, value, number(maximum)));
      };
      // Normalize before size multiplication. Border needs enough outside
      // texels to preserve every legal -8..7 offset even in a one-texel mip.
      // Preserve mirror phase/sign until the point sampler addresses the tap:
      // folding first would reverse the meaning of integer offsets.
      auto *normalized = b.CreateSelect(b.CreateICmpEQ(address, b.getInt32(1)), wrapped,
          b.CreateSelect(b.CreateICmpEQ(address, b.getInt32(2)), period,
          b.CreateSelect(b.CreateICmpEQ(address, b.getInt32(3)), clamp(coordinate, -16, 17),
          b.CreateSelect(b.CreateICmpEQ(address, b.getInt32(4)), clamp(coordinate, -16, 17),
              clamp(coordinate, -17, 17)))));
      auto *position = b.CreateFSub(b.CreateFMul(normalized, size[axis]),
          b.CreateSelect(linear, number(0.5f), number(0)));
      base[axis] = b.CreateCall(unary, {b.getInt32(Floor), position});
      fraction[axis] = b.CreateFSub(position, base[axis]);
    }
    auto tap = [&](IRBuilder<> &tap_builder, unsigned corner) -> Components {
      SmallVector<Value *, 11> arguments(sample.args());
      arguments[1] = state.point_texture;
      for (unsigned axis = 0; axis < 2; ++axis) {
        auto *offset = sample.getArgOperand(7 + axis);
        Value *offset_float = isa<UndefValue>(offset) ? number(0) : tap_builder.CreateSIToFP(offset, f32);
        auto *center = tap_builder.CreateFAdd(tap_builder.CreateFAdd(base[axis], offset_float),
            number(0.5f + float((corner >> axis) & 1u)));
        arguments[3 + axis] = tap_builder.CreateFDiv(center, size[axis]);
        arguments[7 + axis] = tap_builder.getInt32(0);
      }
      arguments[10] = tap_builder.CreateUIToFP(mip, f32);
      auto *value = tap_builder.CreateCall(callee, arguments);
      Components components;
      for (unsigned i = 0; i < 4; ++i) components[i] = tap_builder.CreateExtractValue(value, i);
      return components;
    };
    auto result = tap(b, 0);
    for (unsigned corner = 1; corner < 4; ++corner) {
      Value *contributes = linear;
      for (unsigned axis = 0; axis < 2; ++axis)
        if (corner & (1u << axis)) contributes = b.CreateAnd(contributes, b.CreateFCmpOGT(fraction[axis], number(0)));
      auto previous = result;
      result = OptionalTap(b, contributes, previous, [&](IRBuilder<> &tap_builder) {
        return reduce_values(tap_builder, previous, tap(tap_builder, corner));
      });
    }
    return result;
  };
  auto result = level(builder, lower);
  auto previous = result;
  result = OptionalTap(builder, builder.CreateAnd(mip_linear, builder.CreateFCmpOGT(bounded, rounded)), result,
      [&](IRBuilder<> &b) { return reduce_values(b, previous, level(b, b.CreateAdd(lower, b.getInt32(1)))); });
  auto *active_end = builder.GetInsertBlock();
  builder.CreateBr(merge);
  Components merged;
  for (unsigned i = 0; i < 4; ++i) {
    auto *phi = PHINode::Create(f32, 2, "dxmt.minmax.value", &*merge->begin());
    phi->addIncoming(defaults[i], empty);
    phi->addIncoming(result[i], active_end);
    merged[i] = phi;
  }
  while (!sample.use_empty()) {
    auto *extract = cast<ExtractValueInst>(*sample.user_begin());
    extract->replaceAllUsesWith(merged[*extract->idx_begin()]);
    extract->eraseFromParent();
  }
  sample.eraseFromParent();
  return true;
}
}

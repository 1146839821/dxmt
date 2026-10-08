#include "dxil_typed_origin.hpp"
#include "metalirconverter_native.h"
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <cstring>

static uint32_t Word(const unsigned char *bytes) {
  return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
int main(int argc, char **argv) {
  const bool shared = (argc == 3 && !std::strcmp(argv[2], "--shared")) ||
      (argc == 4 && !std::strcmp(argv[3], "--shared"));
  const bool array = argc >= 3 && !std::strcmp(argv[2], "--array");
  const bool dynamic_array = argc >= 3 && !std::strcmp(argv[2], "--dynamic-array");
  const bool dynamic_runtime = argc >= 3 && !std::strcmp(argv[2], "--dynamic-runtime");
  const bool modern = argc >= 3 && !std::strcmp(argv[2], "--modern");
  if (argc < 2 || argc > 4 || (argc >= 3 && !array && !dynamic_array && !dynamic_runtime && !modern && !shared) ||
      (argc == 4 && (!shared || (!array && !dynamic_array && !dynamic_runtime && !modern)))) return 1;
  const uint32_t record_offset = shared ? (array ? 6 : 3) : 0, record_count = shared ? 8 : 0;
  auto file = llvm::MemoryBuffer::getFileOrSTDIN(argv[1]);
  if (!file) return 1;
  auto input = (*file)->getBuffer();
  auto *bytes = reinterpret_cast<const unsigned char *>(input.data());
  if (input.size() < 32 || std::memcmp(bytes, "DXBC", 4) || Word(bytes + 24) != input.size()) return 1;
  const uint32_t parts = Word(bytes + 28);
  if (parts > (input.size() - 32) / 4) return 1;
  llvm::StringRef bitcode;
  for (uint32_t i = 0; i < parts; ++i) {
    const uint32_t offset = Word(bytes + 32 + i * 4);
    if (offset > input.size() - 8) return 1;
    const uint32_t length = Word(bytes + offset + 4);
    if (length > input.size() - offset - 8) return 1;
    if (std::memcmp(bytes + offset, "DXIL", 4)) continue;
    if (!bitcode.empty() || length < 24) return 1;
    auto *program = bytes + offset + 8;
    const uint32_t start = Word(program + 16), size = Word(program + 20);
    if (std::memcmp(program + 8, "DXIL", 4) || start < 16 || start > length - 8 || size > length - 8 - start)
      return 1;
    bitcode = llvm::StringRef(reinterpret_cast<const char *>(program + 8 + start), size);
  }
  if (bitcode.empty()) return 1;
  if (modern) for (unsigned negative = 0; negative < 3; ++negative) {
    llvm::LLVMContext context;
    context.setOpaquePointers(false);
    auto parsed = llvm::parseBitcodeFile(llvm::MemoryBufferRef(bitcode, argv[1]), context);
    if (!parsed) { llvm::consumeError(parsed.takeError()); return 1; }
    llvm::CallInst *annotation = nullptr, *binding = nullptr, *load = nullptr;
    for (auto &function : **parsed) for (auto &block : function) for (auto &instruction : block) {
      auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
      if (!call || !call->getCalledFunction()) continue;
      auto name = call->getCalledFunction()->getName();
      if (name == "dx.op.annotateHandle" && !annotation) annotation = call;
      if (name == "dx.op.createHandleFromBinding" && !binding) binding = call;
      if (name.startswith("dx.op.bufferLoad") && !load) load = call;
    }
    if (!annotation || !binding || !load || annotation->getArgOperand(1) != binding ||
        load->getArgOperand(1) != annotation) return 1;
    if (negative == 0) {
      auto *type = llvm::cast<llvm::StructType>(annotation->getArgOperand(2)->getType());
      annotation->setArgOperand(2, llvm::ConstantStruct::get(type,
          {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 10),
           llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 265)})); // float versus uint metadata
    } else if (negative == 1) load->setArgOperand(1, binding);
    else {
      auto *type = llvm::cast<llvm::StructType>(binding->getArgOperand(1)->getType());
      binding->setArgOperand(1, llvm::ConstantStruct::get(type,
          {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 3),
           llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 5),
           llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
           llvm::ConstantInt::get(llvm::Type::getInt8Ty(context), 0)}));
    }
    std::vector<dxmt::dxil::TypedOriginBinding> rejected;
    std::string error;
    if (llvm::verifyModule(**parsed, &llvm::errs()) ||
        dxmt::dxil::LowerTypedBufferOrigins(**parsed, rejected, error) || error.empty()) return 1;
    llvm::errs() << "modern-negative=" << negative << " rejected: " << error << '\n';
  }
  dxmt_msc_lower_typed_origins_params params = {};
  params.bitcode = uintptr_t(bitcode.data());
  params.bitcode_size = bitcode.size();
  const uint32_t layout = shared ? DXMT_MSC_TYPED_ORIGIN_LAYOUT_TAG | (record_count << 8) | record_offset : 0;
  params.reserved = layout;
  if (dxmt_msc_lower_typed_origins(&params) != DXMT_MSC_SUCCESS) return 1;
  std::vector<char> abi_ir(params.ir_size, '?');
  std::vector<dxmt_msc_typed_origin_binding> abi_bindings(params.binding_count, {UINT32_MAX, UINT32_MAX, UINT32_MAX});
  params.ir = uintptr_t(abi_ir.data());
  params.ir_capacity = abi_ir.size() - 1;
  params.bindings = uintptr_t(abi_bindings.data());
  params.binding_capacity = abi_bindings.size();
  if (dxmt_msc_lower_typed_origins(&params) != DXMT_MSC_ERROR_OUTPUT_TOO_SMALL ||
      abi_ir != std::vector<char>(abi_ir.size(), '?')) return 1;
  for (const auto &binding : abi_bindings)
    if (binding.resource_class != UINT32_MAX || binding.register_space != UINT32_MAX ||
        binding.shader_register != UINT32_MAX) return 1;
  params.ir_capacity = abi_ir.size();
  if (!abi_bindings.empty()) {
    params.binding_capacity = abi_bindings.size() - 1;
    if (dxmt_msc_lower_typed_origins(&params) != DXMT_MSC_ERROR_OUTPUT_TOO_SMALL ||
        abi_ir != std::vector<char>(abi_ir.size(), '?')) return 1;
    params.binding_capacity = abi_bindings.size();
    const uint64_t saved_bindings = params.bindings;
    params.bindings = params.ir;
    if (dxmt_msc_lower_typed_origins(&params) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
        params.ir_size || params.binding_count || abi_ir != std::vector<char>(abi_ir.size(), '?')) return 1;
    params.bindings = saved_bindings;
  }
  params.reserved = 1;
  params.ir_size = 123;
  params.binding_count = 123;
  if (dxmt_msc_lower_typed_origins(&params) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
      params.ir_size || params.binding_count) return 1;
  params.reserved = layout;
  if (dxmt_msc_lower_typed_origins(&params) != DXMT_MSC_SUCCESS) return 1;
  const auto published_ir = abi_ir;
  const auto published_bindings = abi_bindings;
  for (uint32_t invalid : {DXMT_MSC_TYPED_ORIGIN_LAYOUT_TAG,
       DXMT_MSC_TYPED_ORIGIN_LAYOUT_TAG | (65u << 8),
       DXMT_MSC_TYPED_ORIGIN_LAYOUT_TAG | (8u << 8) | 8u,
       DXMT_MSC_TYPED_ORIGIN_LAYOUT_TAG ^ 0x10000u}) {
    params.reserved = invalid;
    if (dxmt_msc_lower_typed_origins(&params) != DXMT_MSC_ERROR_INVALID_ARGUMENT ||
        params.ir_size || params.binding_count || abi_ir != published_ir ||
        std::memcmp(abi_bindings.data(), published_bindings.data(), abi_bindings.size() * sizeof(abi_bindings[0]))) return 1;
  }
  params.reserved = DXMT_MSC_TYPED_ORIGIN_LAYOUT_TAG | (8u << 8) | 7u;
  if (abi_bindings.size() > 1 &&
      (dxmt_msc_lower_typed_origins(&params) != DXMT_MSC_ERROR_UNSUPPORTED_SHADER ||
       params.ir_size || params.binding_count || abi_ir != published_ir)) return 1;
  params.reserved = layout;
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  auto parsed = llvm::parseBitcodeFile(llvm::MemoryBufferRef(bitcode, argv[1]), context);
  if (!parsed) { llvm::logAllUnhandledErrors(parsed.takeError(), llvm::errs()); return 1; }
  if (llvm::verifyModule(**parsed, &llvm::errs())) return 1;
  std::vector<dxmt::dxil::TypedOriginBinding> bindings;
  std::string error;
  if (!dxmt::dxil::LowerTypedBufferOrigins(**parsed, bindings, error, record_offset, record_count)) {
    llvm::errs() << error << '\n'; return 1;
  }
  {
    auto *origin_type = llvm::StructType::getTypeByName(context, "dxmt.TypedBufferOrigins");
    if (!origin_type || origin_type->getNumElements() != 1 ||
        llvm::cast<llvm::ArrayType>(origin_type->getElementType(0))->getNumElements() !=
            (shared ? record_count : bindings.size())) return 1;
    unsigned loads = 0;
    for (auto &function : **parsed) for (auto &block : function) for (auto &instruction : block) {
      auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
      if (!call || !call->getName().startswith("dxmt.origin.data")) continue;
      ++loads;
      auto *index = llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(2));
      if (index && (index->getZExtValue() < record_offset ||
          index->getZExtValue() >= record_offset + bindings.size())) return 1;
      if (!index) {
        auto *select = llvm::dyn_cast<llvm::SelectInst>(call->getArgOperand(2));
        auto *fallback = select ? llvm::dyn_cast<llvm::ConstantInt>(select->getFalseValue()) : nullptr;
        auto *sum = select ? llvm::dyn_cast<llvm::BinaryOperator>(select->getTrueValue()) : nullptr;
        auto *base = sum ? llvm::dyn_cast<llvm::ConstantInt>(sum->getOperand(1)) : nullptr;
        auto *predicate = select ? llvm::dyn_cast<llvm::ICmpInst>(select->getCondition()) : nullptr;
        auto *limit = predicate ? llvm::dyn_cast<llvm::ConstantInt>(predicate->getOperand(1)) : nullptr;
        if (!fallback || fallback->getZExtValue() != record_offset) return 1;
        if (shared) {
          if (!sum || sum->getOpcode() != llvm::Instruction::Add || !base || !predicate ||
              predicate->getPredicate() != llvm::ICmpInst::ICMP_ULT || !limit || !limit->getZExtValue() ||
              predicate->getOperand(0) != sum->getOperand(0) || base->getZExtValue() < record_offset ||
              base->getZExtValue() + limit->getZExtValue() > record_offset + bindings.size()) return 1;
          if (dynamic_runtime) {
            auto *relative = llvm::dyn_cast<llvm::BinaryOperator>(sum->getOperand(0));
            auto *register_base = relative ? llvm::dyn_cast<llvm::ConstantInt>(relative->getOperand(1)) : nullptr;
            if (!relative || relative->getOpcode() != llvm::Instruction::Sub || !register_base ||
                (register_base->getZExtValue() != 3 && register_base->getZExtValue() != 5) ||
                limit->getZExtValue() != 2 ||
                base->getZExtValue() != record_offset + (register_base->getZExtValue() == 3 ? 0 : 2)) return 1;
            const uint32_t first = register_base->getZExtValue();
            unsigned consumers = 0;
            for (auto *user : call->users()) {
              auto *origin = llvm::dyn_cast<llvm::ExtractValueInst>(user);
              if (!origin || origin->getNumIndices() != 1 || *origin->idx_begin() != 0) continue;
              for (auto *coordinate_user : origin->users()) {
                auto *coordinate = llvm::dyn_cast<llvm::BinaryOperator>(coordinate_user);
                if (!coordinate || coordinate->getOpcode() != llvm::Instruction::Add) continue;
                for (auto *access_user : coordinate->users()) {
                  auto *access = llvm::dyn_cast<llvm::CallInst>(access_user);
                  if (!access || !access->getCalledFunction() || access->arg_size() < 3 ||
                      access->getArgOperand(2) != coordinate) continue;
                  const auto name = access->getCalledFunction()->getName();
                  if (!name.startswith("dx.op.bufferLoad") && !name.startswith("dx.op.bufferStore")) continue;
                  auto *handle = llvm::dyn_cast<llvm::CallInst>(access->getArgOperand(1));
                  if (!handle || !handle->getCalledFunction()) return 1;
                  llvm::ConstantInt *resource_class = nullptr;
                  if (handle->getCalledFunction()->getName() == "dx.op.createHandle")
                    resource_class = llvm::dyn_cast<llvm::ConstantInt>(handle->getArgOperand(1));
                  else if (handle->getCalledFunction()->getName() == "dx.op.annotateHandle") {
                    auto *binding = llvm::dyn_cast<llvm::CallInst>(handle->getArgOperand(1));
                    auto *range = binding ? llvm::dyn_cast<llvm::Constant>(binding->getArgOperand(1)) : nullptr;
                    resource_class = range ? llvm::dyn_cast_or_null<llvm::ConstantInt>(range->getAggregateElement(3u)) : nullptr;
                  }
                  if (!resource_class || resource_class->getZExtValue() > 1 ||
                      first != (resource_class->getZExtValue() ? 5u : 3u)) return 1;
                  ++consumers;
                }
              }
            }
            // Bind the oracle to the actual SRV/UAV access, not to whichever
            // otherwise plausible interval the generated CBV index supplies.
            if (consumers != 1) return 1;
            for (uint32_t absolute : {first - 1, first, first + 1, first + 2, UINT32_MAX}) {
              const uint32_t relative_index = absolute - first;
              const uint64_t actual = relative_index < limit->getZExtValue() ?
                  relative_index + base->getZExtValue() : fallback->getZExtValue();
              const uint64_t expected_index = absolute == first || absolute == first + 1 ?
                  record_offset + (first == 3 ? 0 : 2) + absolute - first : record_offset;
              if (actual != expected_index) return 1;
            }
          }
          for (uint32_t relative : {0u, uint32_t(limit->getZExtValue() - 1), uint32_t(limit->getZExtValue()), UINT32_MAX}) {
            const bool valid = relative < limit->getZExtValue();
            const uint64_t selected = valid ? relative + base->getZExtValue() : fallback->getZExtValue();
            if (selected < record_offset || selected >= record_offset + bindings.size()) return 1;
          }
        }
      }
    }
    if (!loads) return 1;
    llvm::errs() << (shared ? "SHARED_ORIGIN" : "LOCAL_ORIGIN") << " offset=" << record_offset << " count=" <<
        (shared ? record_count : bindings.size()) << " verified loads=" << loads << '\n';
  }
  for (unsigned i = 0; i < bindings.size(); ++i)
    llvm::errs() << "record=" << i << " class=" << bindings[i].resource_class << " space=" <<
        bindings[i].register_space << " register=" << bindings[i].shader_register << '\n';
  if (array && (bindings.size() != 2 || bindings[0].resource_class != 0 ||
      bindings[0].register_space || bindings[0].shader_register != 4 ||
      bindings[1].resource_class != 1 || bindings[1].register_space || bindings[1].shader_register != 6)) return 1;
  if (dynamic_array && (bindings.size() != 3 || bindings[0].resource_class != 0 ||
      bindings[0].register_space || bindings[0].shader_register != 3 ||
      bindings[1].resource_class != 0 || bindings[1].register_space || bindings[1].shader_register != 4 ||
      bindings[2].resource_class != 1 || bindings[2].register_space || bindings[2].shader_register != 6)) return 1;
  if (dynamic_runtime) {
    const dxmt_msc_typed_origin_binding expected[] = {{0, 0, 3}, {0, 0, 4}, {1, 0, 5}, {1, 0, 6}};
    if (bindings.size() != 4) return 1;
    for (unsigned i = 0; i < 4; ++i)
      if (bindings[i].resource_class != expected[i].resource_class || bindings[i].register_space ||
          bindings[i].shader_register != expected[i].shader_register) return 1;
  }
  (*parsed)->setSourceFileName("");
  (*parsed)->setModuleIdentifier("");
  std::string expected;
  llvm::raw_string_ostream expected_stream(expected);
  (*parsed)->print(expected_stream, nullptr);
  expected_stream.flush();
  if (expected != std::string(abi_ir.begin(), abi_ir.end()) || bindings.size() != abi_bindings.size()) return 1;
  for (size_t i = 0; i < bindings.size(); ++i)
    if (bindings[i].resource_class != abi_bindings[i].resource_class ||
        bindings[i].register_space != abi_bindings[i].register_space ||
        bindings[i].shader_register != abi_bindings[i].shader_register) return 1;
  (*parsed)->print(llvm::outs(), nullptr);
  return 0;
}

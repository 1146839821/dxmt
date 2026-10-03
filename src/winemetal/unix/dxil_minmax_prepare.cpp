#include "dxil_minmax.hpp"
#include "metalirconverter_native.h"
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <cstring>
#include <new>

static int LowerReductionSamplers(dxmt_msc_lower_reduction_samplers_params *params) {
  if (!params) return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  if (dxmt_msc_reduction_params_alias(params)) return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  params->ir_size = 0;
  params->binding_count = 0;
  auto valid_address = [](uint64_t address, uint64_t size) {
    return address <= UINTPTR_MAX && size <= UINTPTR_MAX - address;
  };
  const uint64_t binding_bytes = uint64_t(params->binding_capacity) * sizeof(dxmt_msc_minmax_binding);
  const unsigned pair_offset = params->reserved & 0xffu;
  const unsigned pair_count = (params->reserved >> 8) & 0xffu;
  if ((params->reserved && ((params->reserved & 0xffff0000u) != DXMT_MSC_MINMAX_LAYOUT_TAG ||
          !pair_count || pair_count > 64 || pair_offset >= pair_count)) ||
      !params->bitcode || !params->bitcode_size || params->bitcode_size > 16 * 1024 * 1024 ||
      !valid_address(params->bitcode, params->bitcode_size) ||
      (!params->ir && params->ir_capacity) || (!params->bindings && params->binding_capacity) ||
      !valid_address(params->ir, params->ir_capacity) || !valid_address(params->bindings, binding_bytes))
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  auto overlaps = [](uint64_t a, uint64_t a_size, uint64_t b, uint64_t b_size) {
    return a_size && b_size && a < b + b_size && b < a + a_size;
  };
  const uint64_t parameter_address = uintptr_t(params);
  if (overlaps(params->ir, params->ir_capacity, params->bindings, binding_bytes) ||
      overlaps(params->ir, params->ir_capacity, params->bitcode, params->bitcode_size) ||
      overlaps(params->bindings, binding_bytes, params->bitcode, params->bitcode_size) ||
      overlaps(params->ir, params->ir_capacity, parameter_address, sizeof(*params)) ||
      overlaps(params->bindings, binding_bytes, parameter_address, sizeof(*params)) ||
      overlaps(params->bitcode, params->bitcode_size, parameter_address, sizeof(*params)))
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  llvm::StringRef input(reinterpret_cast<const char *>(uintptr_t(params->bitcode)), params->bitcode_size);
  auto module = llvm::parseBitcodeFile(llvm::MemoryBufferRef(input, "reduction-samplers"), context);
  if (!module) {
    llvm::consumeError(module.takeError());
    return DXMT_MSC_ERROR_INVALID_DXIL;
  }
  std::vector<dxmt_msc_minmax_binding> records;
  std::string error;
  if (!dxmt::dxil::LowerReductionSamplerBindings(**module, records, error, pair_offset, pair_count))
    return DXMT_MSC_ERROR_UNSUPPORTED_SHADER;
  // LLVM 15 and the selected older DXC assembler number unnamed SSA differently.
  for (auto &function : **module) for (auto &block : function) {
    if (!block.hasName()) block.setName("dxmt.block");
    for (auto &instruction : block)
      if (!instruction.getType()->isVoidTy() && !instruction.hasName()) instruction.setName("dxmt.value");
  }
  (*module)->setSourceFileName("");
  (*module)->setModuleIdentifier("");
  std::string ir;
  llvm::raw_string_ostream output(ir);
  (*module)->print(output, nullptr);
  output.flush();
  params->ir_size = ir.size();
  params->binding_count = records.size();
  if (!params->ir && !params->bindings) return DXMT_MSC_SUCCESS;
  if (!params->ir || params->ir_capacity < ir.size() || !params->bindings || params->binding_capacity < records.size())
    return DXMT_MSC_ERROR_OUTPUT_TOO_SMALL;
  std::memcpy(reinterpret_cast<void *>(uintptr_t(params->ir)), ir.data(), ir.size());
  std::memcpy(reinterpret_cast<void *>(uintptr_t(params->bindings)), records.data(), records.size() * sizeof(records[0]));
  return DXMT_MSC_SUCCESS;
}

extern "C" int dxmt_msc_lower_reduction_samplers(dxmt_msc_lower_reduction_samplers_params *params) {
  try {
    return LowerReductionSamplers(params);
  } catch (const std::bad_alloc &) {
    return DXMT_MSC_ERROR_OUT_OF_MEMORY;
  }
}

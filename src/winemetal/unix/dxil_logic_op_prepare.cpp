#include "dxil_logic_op.hpp"
#include "metalirconverter_native.h"
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <cstring>
#include <new>

static int LowerLogicOutputs(dxmt_msc_lower_logic_outputs_params *params) {
  if (!params || dxmt_msc_logic_params_alias(params)) return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  params->ir_size = 0;
  params->framebuffer_space = UINT32_MAX;
  auto valid = [](uint64_t address, uint64_t size) {
    return address <= UINTPTR_MAX && size <= UINTPTR_MAX - address;
  };
  if (params->reserved || params->operation > 15 || !params->bitcode || !params->bitcode_size ||
      params->bitcode_size > 16 * 1024 * 1024 || !valid(params->bitcode, params->bitcode_size) ||
      (!params->ir && params->ir_capacity) || !valid(params->ir, params->ir_capacity))
    return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  if (params->ir_capacity && params->ir < params->bitcode + params->bitcode_size &&
      params->bitcode < params->ir + params->ir_capacity) return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  std::array<std::array<uint32_t, 4>, 8> widths;
  for (unsigned target = 0; target < 8; ++target) for (unsigned component = 0; component < 4; ++component) {
    widths[target][component] = params->component_bits[target][component];
    if (widths[target][component] > 32) return DXMT_MSC_ERROR_INVALID_ARGUMENT;
  }
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  llvm::StringRef input(reinterpret_cast<const char *>(uintptr_t(params->bitcode)), params->bitcode_size);
  auto module = llvm::parseBitcodeFile(llvm::MemoryBufferRef(input, "logic-outputs"), context);
  if (!module) { llvm::consumeError(module.takeError()); return DXMT_MSC_ERROR_INVALID_DXIL; }
  uint32_t space;
  std::string error;
  if (!dxmt::dxil::LowerIntegerLogicOutputs(**module, params->operation, widths, space, error))
    return DXMT_MSC_ERROR_UNSUPPORTED_SHADER;
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
  if (params->ir && params->ir_capacity < ir.size()) return DXMT_MSC_ERROR_OUTPUT_TOO_SMALL;
  if (params->ir) std::memcpy(reinterpret_cast<void *>(uintptr_t(params->ir)), ir.data(), ir.size());
  params->framebuffer_space = space;
  return DXMT_MSC_SUCCESS;
}

extern "C" int dxmt_msc_lower_logic_outputs(dxmt_msc_lower_logic_outputs_params *params) {
  try { return LowerLogicOutputs(params); }
  catch (const std::bad_alloc &) { return DXMT_MSC_ERROR_OUT_OF_MEMORY; }
}

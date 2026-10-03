#include "dxil_typed_origin.hpp"
#include "metalirconverter_native.h"
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <cstring>

static uint32_t Word(const unsigned char *bytes) {
  return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
int main(int argc, char **argv) {
  const bool array = argc == 3 && !std::strcmp(argv[2], "--array");
  const bool dynamic_array = argc == 3 && !std::strcmp(argv[2], "--dynamic-array");
  if (argc != 2 && !array && !dynamic_array) return 1;
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
  dxmt_msc_lower_typed_origins_params params = {};
  params.bitcode = uintptr_t(bitcode.data());
  params.bitcode_size = bitcode.size();
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
  params.reserved = 0;
  if (dxmt_msc_lower_typed_origins(&params) != DXMT_MSC_SUCCESS) return 1;
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  auto parsed = llvm::parseBitcodeFile(llvm::MemoryBufferRef(bitcode, argv[1]), context);
  if (!parsed) { llvm::logAllUnhandledErrors(parsed.takeError(), llvm::errs()); return 1; }
  if (llvm::verifyModule(**parsed, &llvm::errs())) return 1;
  std::vector<dxmt::dxil::TypedOriginBinding> bindings;
  std::string error;
  if (!dxmt::dxil::LowerTypedBufferOrigins(**parsed, bindings, error)) {
    llvm::errs() << error << '\n'; return 1;
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

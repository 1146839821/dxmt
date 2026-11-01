#include "airconv_public.h"
#include "compiled_bitcode.hpp"
#include "metallib_writer.hpp"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  llvm::SMDiagnostic diagnostic;
  auto module = llvm::parseIRFile(argv[1], diagnostic, context);
  if (!module)
    return 1;
  llvm::SmallVector<char, 0> input;
  llvm::raw_svector_ostream stream(input);
  dxmt::metallib::MetallibWriter writer;
  writer.Write(*module, stream);
  SM50_MSC_TYPED_BUFFER_TABLE tables[] = {{0, 8}};
  sm50_bitcode_t output = nullptr;
  auto call = [&](uint32_t original, uint32_t extended,
                  const SM50_MSC_TYPED_BUFFER_TABLE *mapping, size_t count) {
    return SM50PatchMetalLibTypedBufferSidecar(input.data(), input.size(),
                                               original, extended, mapping,
                                               count, &output);
  };
  if (call(8, 16, tables, 1) != SM50_MSC_TYPED_BUFFER_PATCHED || !output)
    return 1;
  auto *compiled = static_cast<SM50CompiledBitcodeInternal *>(output);
  if (compiled->vec.size() < sizeof(dxmt::metallib::MTLBHeader))
    return 1;
  dxmt::metallib::MTLBHeader header;
  std::memcpy(&header, compiled->vec.data(), sizeof(header));
  if (header.BitcodeOffset > compiled->vec.size() ||
      header.BitcodeSize > compiled->vec.size() - header.BitcodeOffset)
    return 1;
  auto buffer = llvm::MemoryBuffer::getMemBufferCopy(llvm::StringRef(
      compiled->vec.data() + header.BitcodeOffset, header.BitcodeSize));
  auto parsed = llvm::parseBitcodeFile(buffer->getMemBufferRef(), context);
  if (!parsed || llvm::verifyModule(**parsed))
    return 1;
  std::string ir;
  llvm::raw_string_ostream text(ir);
  (*parsed)->print(text, nullptr);
  text.flush();
  if (ir.find("dxmt.sidecar") == std::string::npos)
    return 1;
  delete compiled;
  output = nullptr;
  for (const auto &bad :
       {SM50_MSC_TYPED_BUFFER_TABLE{0, 0}, SM50_MSC_TYPED_BUFFER_TABLE{1, 8},
        SM50_MSC_TYPED_BUFFER_TABLE{8, 8},
        SM50_MSC_TYPED_BUFFER_TABLE{0, 16}}) {
    output = reinterpret_cast<sm50_bitcode_t>(uintptr_t(1));
    if (call(8, 16, &bad, 1) != SM50_MSC_TYPED_BUFFER_INVALID_INPUT || output)
      return 1;
  }
  output = reinterpret_cast<sm50_bitcode_t>(uintptr_t(1));
  if (call(8, 16, nullptr, 1) != SM50_MSC_TYPED_BUFFER_INVALID_INPUT || output)
    return 1;
  SM50_MSC_TYPED_BUFFER_TABLE duplicates[] = {{0, 16}, {0, 24}};
  output = reinterpret_cast<sm50_bitcode_t>(uintptr_t(1));
  if (call(16, 32, duplicates, 2) != SM50_MSC_TYPED_BUFFER_INVALID_INPUT ||
      output)
    return 1;
  output = reinterpret_cast<sm50_bitcode_t>(uintptr_t(1));
  if (call(8, 16, tables, 65) != SM50_MSC_TYPED_BUFFER_INVALID_INPUT || output)
    return 1;
  output = reinterpret_cast<sm50_bitcode_t>(uintptr_t(1));
  if (SM50PatchMetalLibTypedBufferSidecar(input.data(), 4, 8, 16, tables, 1,
                                          &output) !=
          SM50_MSC_TYPED_BUFFER_INVALID_INPUT ||
      output)
    return 1;
  auto corrupt = input;
  corrupt[0] ^= 1;
  output = reinterpret_cast<sm50_bitcode_t>(uintptr_t(1));
  if (SM50PatchMetalLibTypedBufferSidecar(corrupt.data(), corrupt.size(), 8, 16,
                                          tables, 1, &output) !=
          SM50_MSC_TYPED_BUFFER_INVALID_INPUT ||
      output)
    return 1;
  SM50_MSC_TYPED_BUFFER_TABLE mismatched[] = {{8, 16}};
  output = reinterpret_cast<sm50_bitcode_t>(uintptr_t(1));
  if (call(16, 24, mismatched, 1) != SM50_MSC_TYPED_BUFFER_UNSUPPORTED ||
      output)
    return 1;
  std::cout << "typed-buffer metallib API success and failure paths passed\n";
  return 0;
}

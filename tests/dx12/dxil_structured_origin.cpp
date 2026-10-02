#include "dxil_typed_origin.hpp"
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
  if (argc != 2) return 1;
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
  (*parsed)->setSourceFileName("");
  (*parsed)->print(llvm::outs(), nullptr);
  return 0;
}

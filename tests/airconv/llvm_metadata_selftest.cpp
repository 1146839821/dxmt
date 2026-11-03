#include "llvm/AsmParser/Parser.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
#include <vector>

static void bits(std::vector<unsigned> &out, unsigned value, unsigned width) {
  for (unsigned i = 0; i < width; ++i)
    out.push_back((value >> i) & 1);
}
static void vbr(std::vector<unsigned> &out, unsigned value) {
  while (value >= 32) {
    bits(out, (value & 31) | 32, 6);
    value >>= 5;
  }
  bits(out, value, 6);
}
static bool mutate(llvm::SmallVectorImpl<char> &data, bool unknown) {
  std::vector<unsigned> pattern;
  bits(pattern, 3, 4); // unabbreviated record, four-bit code width
  vbr(pattern, 41);    // METADATA_STRING_TYPE
  vbr(pattern, 9);
  for (unsigned value : {0u, 18u, 1u, 0u, 0u, 0u, 64u, 0u, 0u})
    vbr(pattern, value);
  size_t found = 0, count = 0;
  for (size_t pos = 0; pos + pattern.size() <= data.size() * 8; ++pos) {
    bool equal = true;
    for (size_t i = 0; i < pattern.size(); ++i)
      if (((static_cast<unsigned char>(data[(pos + i) / 8]) >>
            ((pos + i) % 8)) &
           1) != pattern[i]) {
        equal = false;
        break;
      }
    if (equal) {
      found = pos;
      ++count;
    }
  }
  if (count != 1)
    return false;
  auto flip = [&](size_t bit) {
    data[bit / 8] ^= static_cast<char>(1u << (bit % 8));
  };
  if (unknown) {
    flip(found + 5);
    flip(found + 6);
  } // code 41 -> 47
  else
    flip(found + 4 + 12 + 6 + 1); // size-as-metadata bit
  return true;
}
static bool read(llvm::StringRef bitcode, bool should_succeed) {
  llvm::LLVMContext ctx;
  ctx.setOpaquePointers(false);
  auto buffer = llvm::MemoryBuffer::getMemBufferCopy(bitcode);
  auto module = llvm::parseBitcodeFile(buffer->getMemBufferRef(), ctx);
  if (!module) {
    llvm::consumeError(module.takeError());
    return !should_succeed;
  }
  return should_succeed && !llvm::verifyModule(**module, &llvm::errs());
}
int main() {
  llvm::LLVMContext ctx;
  ctx.setOpaquePointers(false);
  llvm::SMDiagnostic diagnostic;
  auto module = llvm::parseAssemblyString(
      "!fixture = !{!0}\n!0 = !DIStringType(name: \"s\", size: 64)\n",
      diagnostic, ctx);
  if (!module) {
    diagnostic.print("metadata-selftest", llvm::errs());
    return 1;
  }
  llvm::SmallVector<char, 0> data;
  llvm::raw_svector_ostream stream(data);
  llvm::WriteBitcodeToFile(*module, stream);
  if (!read(llvm::StringRef(data.data(), data.size()), true))
    return 2;
  for (bool unknown : {false, true}) {
    auto bad = data;
    if (!mutate(bad, unknown)) {
      std::fprintf(stderr, "unexpected bitstream shape\n");
      return 3;
    }
    if (!read(llvm::StringRef(bad.data(), bad.size()), false))
      return 4;
  }
  std::fprintf(stderr,
               "PASS valid string metadata and two rejected variants\n");
  return 0;
}

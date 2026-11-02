#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  auto bad = llvm::MemoryBuffer::getFile(argv[1]);
  auto good = llvm::MemoryBuffer::getFile(argv[2]);
  if (!bad || !good)
    return 3;
  for (int i = 0; i < 1000; i++) {
    llvm::LLVMContext ctx;
    ctx.setOpaquePointers(false);
    auto invalid = llvm::parseBitcodeFile((*bad)->getMemBufferRef(), ctx);
    if (invalid) {
      std::fprintf(stderr, "unexpected parse success at %d\n", i);
      return 4;
    }
    llvm::consumeError(invalid.takeError());
    auto valid = llvm::parseBitcodeFile((*good)->getMemBufferRef(), ctx);
    if (!valid) {
      llvm::logAllUnhandledErrors(valid.takeError(), llvm::errs(),
                                  "valid parse: ");
      return 5;
    }
    if (llvm::verifyModule(**valid, &llvm::errs()))
      return 6;
  }
  std::fprintf(stderr, "PASS 1000 iterations of error and successful parse\n");
  return 0;
}

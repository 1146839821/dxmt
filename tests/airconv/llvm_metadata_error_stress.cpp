#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 3 && (argc != 4 || std::strcmp(argv[3], "--concurrent")))
    return 2;
  auto bad = llvm::MemoryBuffer::getFile(argv[1]);
  auto good = llvm::MemoryBuffer::getFile(argv[2]);
  if (!bad || !good)
    return 3;
  const unsigned workers = argc == 4 ? 8 : 1;
  std::atomic<unsigned> failures{0};
  std::vector<std::thread> threads;
  for (unsigned worker = 0; worker < workers; ++worker) {
    threads.emplace_back([&] {
      for (int i = 0; i < 1000; i++) {
        llvm::LLVMContext ctx;
        ctx.setOpaquePointers(false);
        auto invalid = llvm::parseBitcodeFile((*bad)->getMemBufferRef(), ctx);
        if (invalid) {
          ++failures;
          return;
        }
        llvm::consumeError(invalid.takeError());
        auto valid = llvm::parseBitcodeFile((*good)->getMemBufferRef(), ctx);
        if (!valid) {
          llvm::consumeError(valid.takeError());
          ++failures;
          return;
        }
        if (llvm::verifyModule(**valid, &llvm::errs())) {
          ++failures;
          return;
        }
      }
    });
  }
  for (auto &thread : threads)
    thread.join();
  if (failures) {
    std::fprintf(stderr, "FAIL %u workers\n", failures.load());
    return 4;
  }
  std::fprintf(stderr,
               "PASS %u workers, 1000 error and successful parse pairs each\n",
               workers);
  return 0;
}

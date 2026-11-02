#include "airconv_public.h"
#include "compiled_bitcode.hpp"
#include "metallib_writer.hpp"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/FMF.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

struct Stats {
  unsigned truncations = 0;
  bool debug_info = false;
};

static bool makeDoubleFixture(const char *bytes, size_t size,
                              const char *path) {
  if (size < sizeof(dxmt::metallib::MTLBHeader))
    return false;
  dxmt::metallib::MTLBHeader header{};
  std::memcpy(&header, bytes, sizeof(header));
  if (header.Magic != dxmt::metallib::MTLB_Magic ||
      header.BitcodeOffset > size ||
      header.BitcodeSize > size - header.BitcodeOffset)
    return false;
  auto data = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(bytes + header.BitcodeOffset, header.BitcodeSize));
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  auto result = llvm::parseBitcodeFile(data->getMemBufferRef(), context);
  if (!result) {
    llvm::logAllUnhandledErrors(result.takeError(), llvm::errs(),
                                "PARSE_ERROR: ");
    return false;
  }
  llvm::Module &module = **result;
  llvm::StoreInst *target = nullptr;
  for (auto &function : module)
    for (auto &block : function)
      for (auto &inst : block)
        if (!target)
          if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&inst))
            if (store->getValueOperand()->getType()->isFloatTy())
              target = store;
  if (!target || !module.getNamedMetadata("llvm.dbg.cu"))
    return false;
  llvm::IRBuilder<> builder(target);
  builder.SetCurrentDebugLocation(target->getDebugLoc());
  builder.setFastMathFlags(llvm::FastMathFlags::getFast());
  auto *extended = builder.CreateFPExt(target->getValueOperand(),
                                       builder.getDoubleTy(), "fixture.ext");
  auto *product = builder.CreateFMul(
      extended, llvm::ConstantFP::get(builder.getDoubleTy(), 1.0),
      "fixture.mul");
  target->setOperand(
      0, builder.CreateFPTrunc(product, builder.getFloatTy(), "fixture.trunc"));
  if (llvm::verifyModule(module, &llvm::errs()))
    return false;
  llvm::SmallVector<char, 0> bytes_out;
  llvm::raw_svector_ostream stream(bytes_out);
  dxmt::metallib::MetallibWriter writer;
  writer.Write(module, stream);
  std::ofstream file_out(path, std::ios::binary | std::ios::trunc);
  file_out.write(bytes_out.data(), bytes_out.size());
  return file_out.good();
}

static bool inspect(const char *bytes, size_t size, const char *label,
                    Stats &stats) {
  if (size < sizeof(dxmt::metallib::MTLBHeader))
    return false;
  dxmt::metallib::MTLBHeader header{};
  std::memcpy(&header, bytes, sizeof(header));
  if (header.Magic != dxmt::metallib::MTLB_Magic ||
      header.BitcodeOffset > size ||
      header.BitcodeSize > size - header.BitcodeOffset)
    return false;
  auto data = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(bytes + header.BitcodeOffset, header.BitcodeSize));
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  auto result = llvm::parseBitcodeFile(data->getMemBufferRef(), context);
  if (!result) {
    llvm::logAllUnhandledErrors(result.takeError(), llvm::errs(),
                                "PARSE_ERROR: ");
    return false;
  }
  auto &module = **result;
  if (llvm::verifyModule(module, &llvm::errs()))
    return false;
  stats.debug_info = module.getNamedMetadata("llvm.dbg.cu") != nullptr;
  for (auto &function : module)
    for (auto &block : function)
      for (auto &inst : block)
        if (auto *trunc = llvm::dyn_cast<llvm::FPTruncInst>(&inst);
            trunc && trunc->getSrcTy()->isDoubleTy() &&
            trunc->getDestTy()->isFloatTy())
          ++stats.truncations;
  std::cerr << "PARSE_STATS " << label << " " << stats.truncations << " "
            << stats.debug_info << "\n";
  return true;
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 4)
    return 2;
  const bool expect_lowered =
      argc == 4 && std::strcmp(argv[3], "--expect-lowered") == 0;
  const bool make_double =
      argc == 4 && std::strcmp(argv[3], "--make-double") == 0;
  if (argc == 4 && !expect_lowered && !make_double)
    return 2;
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<char> input((std::istreambuf_iterator<char>(file)), {});
  if (input.empty())
    return 3;
  if (make_double)
    return makeDoubleFixture(input.data(), input.size(), argv[2]) ? 0 : 8;
  Stats before{};
  if (expect_lowered && !inspect(input.data(), input.size(), "input", before))
    return 6;
  sm50_bitcode_t output = nullptr;
  std::cerr << "DEBUG_METALLIB_PATCH_BEGIN " << input.size() << "\n";
  int result =
      SM50PatchMetalLibUnsupportedDouble(input.data(), input.size(), &output);
  std::cerr << "DEBUG_METALLIB_PATCH_END " << result << "\n";
  if (result || !output)
    return 4;
  auto *bitcode = static_cast<SM50CompiledBitcodeInternal *>(output);
  if (expect_lowered) {
    Stats after{};
    if (!inspect(bitcode->vec.data(), bitcode->vec.size(), "output", after) ||
        !before.debug_info || !after.debug_info || !before.truncations ||
        after.truncations >= before.truncations) {
      delete bitcode;
      return 7;
    }
  }
  if (argc >= 3) {
    std::ofstream file_out(argv[2], std::ios::binary | std::ios::trunc);
    file_out.write(bitcode->vec.data(), bitcode->vec.size());
    if (!file_out) {
      delete bitcode;
      return 5;
    }
  }
  delete bitcode;
  return 0;
}

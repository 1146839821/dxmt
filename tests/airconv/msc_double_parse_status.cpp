#include "airconv_public.h"
#include "compiled_bitcode.hpp"
#include "metallib_writer.hpp"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <vector>

static bool check(const std::vector<char> &input, uint32_t expected_parsed) {
  sm50_bitcode_t output = nullptr;
  uint32_t parsed = 9;
  if (SM50PatchMetalLibUnsupportedDoubleEx(input.data(), input.size(), &output,
                                           &parsed) ||
      !output)
    return false;
  auto *compiled = static_cast<SM50CompiledBitcodeInternal *>(output);
  bool ok = parsed == expected_parsed && compiled->vec.size() == input.size() &&
            std::memcmp(compiled->vec.data(), input.data(), input.size()) == 0;
  delete compiled;
  return ok;
}

static std::vector<char> wrap(const llvm::SmallVectorImpl<char> &bitcode) {
  dxmt::metallib::MTLBHeader header{};
  header.Magic = dxmt::metallib::MTLB_Magic;
  header.BitcodeOffset = sizeof(header);
  header.BitcodeSize = bitcode.size();
  std::vector<char> result(sizeof(header) + bitcode.size());
  std::memcpy(result.data(), &header, sizeof(header));
  std::memcpy(result.data() + sizeof(header), bitcode.data(), bitcode.size());
  return result;
}

int main() {
  sm50_bitcode_t output = reinterpret_cast<sm50_bitcode_t>(1);
  uint32_t parsed = 9;
  if (SM50PatchMetalLibUnsupportedDoubleEx(nullptr, 0, &output, &parsed) == 0 ||
      output || parsed)
    return 1;
  if (!check({'x'}, 0))
    return 2;
  llvm::SmallVector<char, 0> invalid{'b', 'a', 'd', '!'};
  if (!check(wrap(invalid), 0))
    return 3;
  llvm::LLVMContext context;
  llvm::Module module("empty", context);
  llvm::SmallVector<char, 0> bitcode;
  llvm::raw_svector_ostream stream(bitcode);
  llvm::WriteBitcodeToFile(module, stream);
  if (!check(wrap(bitcode), 1))
    return 4;
  return 0;
}

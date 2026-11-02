#include "airconv_public.h"
#include "compiled_bitcode.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<char> input((std::istreambuf_iterator<char>(file)), {});
  if (input.empty())
    return 3;
  sm50_bitcode_t output = nullptr;
  std::cerr << "DEBUG_METALLIB_PATCH_BEGIN " << input.size() << "\n";
  int result =
      SM50PatchMetalLibUnsupportedDouble(input.data(), input.size(), &output);
  std::cerr << "DEBUG_METALLIB_PATCH_END " << result << "\n";
  if (result || !output)
    return 4;
  delete static_cast<SM50CompiledBitcodeInternal *>(output);
  return 0;
}

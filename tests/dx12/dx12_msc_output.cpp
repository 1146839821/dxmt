#include "../../src/d3d12/d3d12_msc_output.hpp"
#include <iostream>

int main() {
  using dxmt::FinalizeMSCOutputBuffers;
  std::vector<uint8_t> metallib(8, 1), stage(5, 2);
  std::vector<char> entry = {'m', 'a', 'i', 'n', '\0', 'x'};
  std::string name;
  if (!FinalizeMSCOutputBuffers(metallib, stage, entry, 4, 3, 5, name) ||
      metallib.size() != 4 || stage.size() != 3 || name != "main")
    return 1;
  const auto reject = [&](size_t lib_size, size_t stage_size,
                          size_t name_size) {
    return !FinalizeMSCOutputBuffers(metallib, stage, entry, lib_size,
                                     stage_size, name_size, name) &&
           metallib.size() == 4 && stage.size() == 3 && name == "main";
  };
  if (!reject(0, 0, 5) || !reject(5, 0, 5) || !reject(4, 4, 5) ||
      !reject(4, 0, 0) || !reject(4, 0, 7) || !reject(4, 0, 4))
    return 2;
  std::vector<uint8_t> absent;
  if (FinalizeMSCOutputBuffers(metallib, absent, entry, 4, 1, 5, name) ||
      !absent.empty())
    return 3;
  if (!FinalizeMSCOutputBuffers(metallib, absent, entry, 4, 0, 5, name))
    return 4;
  std::cout << "MSC output sizes and entry point validated\n";
  return 0;
}

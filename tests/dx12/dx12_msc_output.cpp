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

  using dxmt::CompleteMSCOutputBuffers;
  using dxmt::MSCOutputCompletion;
  using dxmt::MSCOutputSizes;
  int calls = 0, result = -1;
  auto grows = [&](auto &lib, auto &stage_in, auto &out_entry,
                   MSCOutputSizes &sizes) {
    ++calls;
    sizes = {6, 2, 5};
    if (lib.size() < 6 || stage_in.size() < 2)
      return 10;
    out_entry[4] = '\0';
    return 0;
  };
  if (CompleteMSCOutputBuffers(metallib, absent, entry, name, 0, 10, result,
                               grows) != MSCOutputCompletion::Complete ||
      calls != 2 || metallib.size() != 6 || absent.size() != 2)
    return 5;
  absent.clear();
  calls = 0;
  auto query_on_null = [&](auto &, auto &stage_in, auto &,
                           MSCOutputSizes &sizes) {
    ++calls;
    sizes = {6, 2, 5};
    if (!stage_in.empty())
      stage_in[0] = 77;
    return 0;
  };
  if (CompleteMSCOutputBuffers(metallib, absent, entry, name, 0, 10, result,
                               query_on_null) !=
          MSCOutputCompletion::Complete ||
      calls != 2 || absent.size() != 2 || absent[0] != 77)
    return 9;
  calls = 0;
  auto staggered = [&](auto &, auto &, auto &, MSCOutputSizes &sizes) {
    ++calls;
    sizes = calls == 1   ? MSCOutputSizes{8, 0, 5}
            : calls == 2 ? MSCOutputSizes{0, 4, 5}
                         : MSCOutputSizes{8, 4, 5};
    return calls < 3 ? 10 : 0;
  };
  if (CompleteMSCOutputBuffers(metallib, absent, entry, name, 0, 10, result,
                               staggered) != MSCOutputCompletion::Complete ||
      calls != 3 || metallib.size() != 8 || absent.size() != 4)
    return 10;
  calls = 0;
  auto no_progress = [&](auto &, auto &, auto &, MSCOutputSizes &sizes) {
    ++calls;
    sizes = {1, 0, 1};
    return 10;
  };
  if (CompleteMSCOutputBuffers(metallib, absent, entry, name, 0, 10, result,
                               no_progress) != MSCOutputCompletion::Invalid ||
      calls != 1)
    return 6;
  calls = 0;
  auto changing = [&](auto &lib, auto &, auto &, MSCOutputSizes &sizes) {
    ++calls;
    sizes = {lib.size() + 1, 0, 1};
    return 10;
  };
  if (CompleteMSCOutputBuffers(metallib, absent, entry, name, 0, 10, result,
                               changing) != MSCOutputCompletion::Invalid ||
      calls != 3)
    return 7;
  auto failure = [&](auto &, auto &, auto &, MSCOutputSizes &) { return 42; };
  if (CompleteMSCOutputBuffers(metallib, absent, entry, name, 0, 10, result,
                               failure) != MSCOutputCompletion::CallFailed ||
      result != 42)
    return 8;
  std::cout << "MSC output sizes and entry point validated\n";
  return 0;
}

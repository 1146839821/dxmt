#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dxmt {

struct MSCOutputSizes {
  size_t metallib;
  size_t stage_in;
  size_t entry;
};

enum class MSCOutputCompletion { Complete, CallFailed, Invalid };

// The second conversion may produce sizes different from the sizing pass.
// Validate all reported lengths before inspecting or publishing any output.
inline bool
FinalizeMSCOutputBuffers(
    std::vector<uint8_t> &metallib, std::vector<uint8_t> &stage_in, const std::vector<char> &entry,
    size_t metallib_size, size_t stage_in_size, size_t entry_size, std::string &entry_point
) {
  if (!metallib_size || metallib_size > metallib.size() || stage_in_size > stage_in.size() || !entry_size ||
      entry_size > entry.size() || entry[entry_size - 1] != '\0')
    return false;
  entry_point.assign(entry.data(), entry_size - 1);
  metallib.resize(metallib_size);
  stage_in.resize(stage_in_size);
  return true;
}

// Retry only when the converter reports a larger requirement. Preserve prior
// capacities because a failed bridge call may not report every output size.
template <typename Compile>
MSCOutputCompletion
CompleteMSCOutputBuffers(
    std::vector<uint8_t> &metallib, std::vector<uint8_t> &stage_in, std::vector<char> &entry, std::string &entry_point,
    int success, int too_small, int &call_result, Compile compile
) {
  constexpr unsigned max_attempts = 3;
  for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
    MSCOutputSizes sizes{};
    call_result = compile(metallib, stage_in, entry, sizes);
    if (call_result != success && call_result != too_small)
      return MSCOutputCompletion::CallFailed;
    const bool grows =
        sizes.metallib > metallib.size() || sizes.stage_in > stage_in.size() || sizes.entry > entry.size();
    if (call_result == success && !grows)
      return FinalizeMSCOutputBuffers(
                 metallib, stage_in, entry, sizes.metallib, sizes.stage_in, sizes.entry, entry_point
             )
                 ? MSCOutputCompletion::Complete
                 : MSCOutputCompletion::Invalid;
    if (!grows || attempt + 1 == max_attempts)
      return MSCOutputCompletion::Invalid;
    if (sizes.metallib > metallib.size())
      metallib.resize(sizes.metallib);
    if (sizes.stage_in > stage_in.size())
      stage_in.resize(sizes.stage_in);
    if (sizes.entry > entry.size())
      entry.resize(sizes.entry);
  }
  return MSCOutputCompletion::Invalid;
}

} // namespace dxmt

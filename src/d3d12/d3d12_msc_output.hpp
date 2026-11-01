#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dxmt {

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

} // namespace dxmt

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace dxmt::air {

// UINT32_MAX identifies MSC's synthetic static-sampler table.
struct MSCSamplerSidecarTable {
  unsigned sampler_table_offset;
  unsigned sidecar_offset;
  unsigned parameter_index = 0;
};

// Builds a deterministic extension after the complete reflected TLAB. Input
// entries provide table offsets and root parameter identities; sidecar offsets
// in the input are ignored. On failure neither output is changed.
inline bool
buildMSCSamplerSidecarLayout(
    uint64_t reflected_size, const std::vector<MSCSamplerSidecarTable> &input,
    std::vector<MSCSamplerSidecarTable> &output, unsigned &extended_size
) {
  constexpr uint64_t max = std::numeric_limits<unsigned>::max();
  if (reflected_size > max - 7)
    return false;
  const uint64_t base = (reflected_size + 7) & ~uint64_t(7);
  auto tables = input;
  std::sort(tables.begin(), tables.end(), [](const auto &a, const auto &b) {
    return a.sampler_table_offset < b.sampler_table_offset;
  });
  for (std::size_t i = 0; i < tables.size(); ++i) {
    const uint64_t offset = tables[i].sampler_table_offset;
    if (offset % 8 || offset > reflected_size || 8 > reflected_size - offset ||
        (i && tables[i - 1].sampler_table_offset == offset))
      return false;
    if (i >= (max - base) / 8)
      return false;
    tables[i].sidecar_offset = static_cast<unsigned>(base + i * 8);
  }
  extended_size = static_cast<unsigned>(base + tables.size() * 8);
  output.swap(tables);
  return true;
}
} // namespace dxmt::air

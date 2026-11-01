#pragma once

#include "msc_lod_layout.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace dxmt::air {
struct MSCResourceSidecarTable {
  unsigned resource_table_offset;
  unsigned sidecar_offset;
  unsigned parameter_index;
};

// No output is published on failure. Layout is relative to the original MSC
// TLAB, and must not be combined with other extensions without coordination.
inline bool
buildMSCResourceSidecarLayout(
    uint64_t reflected_size, const std::vector<MSCResourceSidecarTable> &input,
    std::vector<MSCResourceSidecarTable> &output, unsigned &extended_size
) {
  std::vector<MSCSamplerSidecarTable> common;
  for (const auto &table : input) {
    if (table.parameter_index == UINT32_MAX || std::any_of(common.begin(), common.end(), [&](const auto &other) {
          return other.parameter_index == table.parameter_index;
        }))
      return false;
    common.push_back({table.resource_table_offset, 0, table.parameter_index});
  }
  std::vector<MSCSamplerSidecarTable> built;
  unsigned size = 0;
  if (!buildMSCSamplerSidecarLayout(reflected_size, common, built, size))
    return false;
  std::vector<MSCResourceSidecarTable> tables;
  for (const auto &table : built)
    tables.push_back({table.sampler_table_offset, table.sidecar_offset, table.parameter_index});
  output.swap(tables);
  extended_size = size;
  return true;
}
} // namespace dxmt::air

#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace dxmt::air {

// Geometry only. No mapped/status verdict is inferred from loaded values or
// CPU mapping state; the consumer must query the queue-ordered GPU sideband.
struct SparseBufferFootprint {
  std::array<uint64_t, 8> tiles{};
  uint32_t count = 0;
};

inline bool
BuildSparseBufferFootprint(uint64_t resource_size, uint64_t view_origin, uint64_t view_size,
                          uint64_t byte_offset, uint32_t component_mask, SparseBufferFootprint &output) {
  if (!component_mask || (component_mask & ~15u) || view_origin > resource_size ||
      view_size > resource_size - view_origin)
    return false;
  SparseBufferFootprint candidate;
  for (uint32_t component = 0; component < 4; ++component) {
    if (!(component_mask & (1u << component))) continue;
    const uint64_t displacement = uint64_t(component) * sizeof(uint32_t);
    if (byte_offset > std::numeric_limits<uint64_t>::max() - displacement) return false;
    const uint64_t first = byte_offset + displacement;
    if (first > view_size || sizeof(uint32_t) > view_size - first) return false;
    const uint64_t begin_tile = (view_origin + first) >> 16;
    const uint64_t end_tile = (view_origin + first + sizeof(uint32_t) - 1) >> 16;
    for (uint64_t tile = begin_tile; tile <= end_tile; ++tile) {
      bool found = false;
      for (uint32_t i = 0; i < candidate.count; ++i) found |= candidate.tiles[i] == tile;
      if (!found) candidate.tiles[candidate.count++] = tile;
    }
  }
  output = candidate;
  return true;
}

} // namespace dxmt::air

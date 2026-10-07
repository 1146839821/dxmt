#pragma once

#include <cstddef>
#include <cstdint>

namespace dxmt::air {

// AIR raw/structured descriptor word 3 is this immutable header's GPU VA.
// Word 2 remains the UAV counter. This is not an MSC descriptor extension.
struct SparseBufferFeedbackHeader {
  uint64_t resource_gpu_address;
  uint64_t resource_byte_size; // Actual D3D width, not sparse allocation padding.
  uint64_t mapping_gpu_address; // One GPU-written byte per resource 64KiB tile.
  uint64_t tile_count;
};
static_assert(sizeof(SparseBufferFeedbackHeader) == 32);
static_assert(offsetof(SparseBufferFeedbackHeader, mapping_gpu_address) == 16);
static_assert(offsetof(SparseBufferFeedbackHeader, tile_count) == 24);

} // namespace dxmt::air

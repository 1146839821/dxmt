#include "sparse_buffer_footprint.hpp"
#include <cstdio>

int main() {
  using dxmt::air::BuildSparseBufferFootprint;
  dxmt::air::SparseBufferFootprint result;
  if (!BuildSparseBufferFootprint(131072, 0, 131072, 65532, 15, result) ||
      result.count != 2 || result.tiles[0] != 0 || result.tiles[1] != 1) return 1;
  if (!BuildSparseBufferFootprint(131072, 65532, 16, 0, 2, result) ||
      result.count != 1 || result.tiles[0] != 1) return 1;
  if (!BuildSparseBufferFootprint(131072, 65535, 4, 0, 1, result) ||
      result.count != 2 || result.tiles[0] != 0 || result.tiles[1] != 1) return 1;
  for (uint32_t mask = 1; mask < 16; ++mask)
    if (!BuildSparseBufferFootprint(64, 16, 16, 0, mask, result) || result.count != 1 || result.tiles[0]) return 1;
  const auto saved = result;
  for (uint32_t mask : {0u, 16u, 0xffffffffu})
    if (BuildSparseBufferFootprint(64, 0, 64, 0, mask, result)) return 1;
  if (BuildSparseBufferFootprint(64, 65, 0, 0, 1, result) ||
      BuildSparseBufferFootprint(64, 16, 49, 0, 1, result) ||
      BuildSparseBufferFootprint(64, 16, 16, 13, 1, result) ||
      BuildSparseBufferFootprint(UINT64_MAX, 0, UINT64_MAX, UINT64_MAX, 8, result) ||
      result.count != saved.count || result.tiles != saved.tiles) return 1;
  std::puts("Sparse buffer byte footprint geometry PASS (not residency status)");
  return 0;
}

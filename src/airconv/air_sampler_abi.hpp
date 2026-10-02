#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

namespace dxmt::air {

// Bits consumed by air_minmax.metal. Minifying is computed from clamped LOD,
// not stored in the descriptor. Reduction is a reserved descriptor tag for
// dynamic dispatch; current eligibility still comes from the validated root.
enum SamplerReductionFlags : uint32_t {
  SamplerMinLinear = 1u,
  SamplerMagLinear = 2u,
  SamplerMipLinear = 4u,
  SamplerMaximum = 8u,
  SamplerMinifying = 16u,
  SamplerReduction = 32u,
  // Compiler-derived logical shape, never supplied by a sampler descriptor.
  SamplerLogical1D = 64u,
};

struct SamplerGPUStorage {
  uint64_t sampler;
  uint64_t cube_sampler;
  uint64_t metadata; // Bias in low 32 bits; reduction flags in high 32 bits.
  uint64_t lod_clamps; // MinLOD in low 32 bits; MaxLOD in high 32 bits.
};
static_assert(sizeof(SamplerGPUStorage) == 32);
static_assert(offsetof(SamplerGPUStorage, metadata) == 16);
static_assert(offsetof(SamplerGPUStorage, lod_clamps) == 24);

inline uint64_t PackSamplerMetadata(float bias, uint32_t flags) {
  return uint64_t(std::bit_cast<uint32_t>(bias)) | (uint64_t(flags) << 32);
}

inline uint64_t PackSamplerLODClamps(float min_lod, float max_lod) {
  return uint64_t(std::bit_cast<uint32_t>(min_lod)) | (uint64_t(std::bit_cast<uint32_t>(max_lod)) << 32);
}

} // namespace dxmt::air

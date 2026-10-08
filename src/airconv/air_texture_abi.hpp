#pragma once

#include <cstddef>
#include <cstdint>

namespace dxmt::air {

constexpr uint64_t TextureDefaultComponentsValid = 16u;

struct TextureGPUStorage {
  uint64_t resource_id;
  uint64_t metadata; // Existing array length and view-space resource clamp.
  uint64_t default_components; // Low four bits: one-valued OOB defaults; bit 4: valid.
  uint64_t padding;
};
static_assert(sizeof(TextureGPUStorage) == 32);
static_assert(offsetof(TextureGPUStorage, default_components) == 16);

constexpr uint64_t PackTextureDefaultComponents(uint32_t ones) {
  return TextureDefaultComponentsValid | (ones & 15u);
}

} // namespace dxmt::air

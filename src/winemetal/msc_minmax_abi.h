#pragma once
#include <stdint.h>

// Private compiler/submission contract. Not an MSC descriptor layout.
#define DXMT_MSC_MINMAX_SPACE 2u
#define DXMT_MSC_MINMAX_VERSION 3u
#define DXMT_MSC_MINMAX_ENABLED 32u
struct dxmt_msc_minmax_state {
  uint32_t flags;
  float min_lod;
  float max_lod;
  float resource_clamp;
  uint32_t default_components;
  uint32_t address_u;
  uint32_t address_v;
  float mip_lod_bias;
};
struct dxmt_msc_minmax_binding {
  uint32_t texture_space;
  uint32_t texture_register;
  uint32_t sampler_space;
  uint32_t sampler_register;
};

#pragma once
#include <stdint.h>

// Private compiler/submission contract. Not an MSC descriptor layout.
#define DXMT_MSC_MINMAX_SPACE 2u
#define DXMT_MSC_MINMAX_VERSION 5u
#define DXMT_MSC_MINMAX_ADDRESS_MASK 255u
#define DXMT_MSC_MINMAX_ADDRESS_W_SHIFT 8u
#define DXMT_MSC_MINMAX_ENABLED 32u
struct dxmt_msc_minmax_state {
  uint32_t flags;
  float min_lod;
  float max_lod;
  float resource_clamp;
  uint32_t default_components;
  uint32_t address_u;
  uint32_t address_vw; // AddressV in bits 0..7, AddressW in bits 8..15.
  float mip_lod_bias;
};
struct dxmt_msc_minmax_binding {
  uint32_t texture_space;
  uint32_t texture_register;
  uint32_t sampler_space;
  uint32_t sampler_register;
};

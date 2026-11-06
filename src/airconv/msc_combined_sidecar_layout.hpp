#pragma once

#include "msc_lod_layout.hpp"
#include "msc_resource_layout.hpp"
#include <limits>

namespace dxmt::air {

struct MSCCombinedSidecarLayout {
  std::vector<MSCSamplerSidecarTable> samplers;
  std::vector<MSCResourceSidecarTable> resources;
  unsigned buffer_size = 0;
};

// Preserve the established sampler ABI and append the resource sidecars.
// Publishing is transactional; invalid reflection or overflow leaves out intact.
inline bool
buildMSCCombinedSidecarLayout(
    uint64_t reflected_size, const std::vector<MSCSamplerSidecarTable> &samplers,
    const std::vector<MSCResourceSidecarTable> &resources, MSCCombinedSidecarLayout &out
) {
  MSCCombinedSidecarLayout candidate;
  unsigned sampler_size = 0, resource_size = 0;
  if (!buildMSCSamplerSidecarLayout(reflected_size, samplers, candidate.samplers, sampler_size) ||
      !buildMSCResourceSidecarLayout(reflected_size, resources, candidate.resources, resource_size))
    return false;
  for (const auto &sampler : candidate.samplers)
    for (const auto &resource : candidate.resources)
      if (sampler.sampler_table_offset == resource.resource_table_offset ||
          sampler.parameter_index == resource.parameter_index)
        return false;
  const auto max = std::numeric_limits<unsigned>::max();
  if (candidate.resources.size() > (max - sampler_size) / 8)
    return false;
  for (size_t i = 0; i < candidate.resources.size(); ++i)
    candidate.resources[i].sidecar_offset = sampler_size + static_cast<unsigned>(i * 8);
  candidate.buffer_size = sampler_size + static_cast<unsigned>(candidate.resources.size() * 8);
  out = std::move(candidate);
  return true;
}
} // namespace dxmt::air

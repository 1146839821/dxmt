#pragma once

#include "d3d12.h"
#include "Metal.hpp"
#include <vector>

namespace dxmt {

class MTLD3D12Device;

struct D3D12RaytracingDescriptor {
  WMTAccelerationStructureDescriptorInfo info = {};
  std::vector<WMT::Reference<WMT::Buffer>> buffers;
  std::vector<WMT::Reference<WMT::AccelerationStructure>> acceleration_structures;
  std::vector<uint32_t> instance_contributions;
};

bool ConvertD3D12RaytracingInputs(
    MTLD3D12Device *device, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *inputs,
    D3D12RaytracingDescriptor &descriptor, bool for_execution = false
);

bool CreateD3D12RaytracingAccelerationStructureHeader(
    MTLD3D12Device *device, const WMT::Reference<WMT::AccelerationStructure> &acceleration_structure,
    const std::vector<uint32_t> &instance_contributions, WMT::Reference<WMT::Buffer> &header,
    uint64_t &header_gpu_address
);

} // namespace dxmt

#pragma once

#include <cstddef>
#include <cstdint>

namespace dxmt {

struct D3D12RayDispatchRange {
  uint64_t start_address;
  uint64_t size_in_bytes;
};

struct D3D12RayDispatchRangeAndStride {
  uint64_t start_address;
  uint64_t size_in_bytes;
  uint64_t stride_in_bytes;
};

struct D3D12RayDispatchDescriptor {
  D3D12RayDispatchRange ray_generation_shader_record;
  D3D12RayDispatchRangeAndStride miss_shader_table;
  D3D12RayDispatchRangeAndStride hit_group_table;
  D3D12RayDispatchRangeAndStride callable_shader_table;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
};

struct D3D12RayDispatchArgument {
  D3D12RayDispatchDescriptor dispatch_rays_desc;
  uint64_t global_root_signature;
  uint64_t resource_descriptor_heap;
  uint64_t sampler_descriptor_heap;
  uint64_t visible_function_table;
  uint64_t intersection_function_table;
  uint64_t intersection_function_tables;
};

static_assert(sizeof(D3D12RayDispatchDescriptor) == 104);
static_assert(sizeof(D3D12RayDispatchArgument) == 152);

} // namespace dxmt

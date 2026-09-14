/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "d3d12_raytracing.hpp"

#include "d3d12_device.hpp"
#include <cstdint>
#include <limits>

namespace dxmt {

namespace {

constexpr uint64_t kD3D12AccelerationStructureAlignment = 256;
constexpr uint64_t kD3D12RaytracingInstanceDescriptorSize = 64;
constexpr uint64_t kD3D12RaytracingAABBSize = 24;

struct MetalUserIDInstanceDescriptor {
  float transformation[4][3];
  uint32_t options;
  uint32_t mask;
  uint32_t intersection_function_table_offset;
  uint32_t acceleration_structure_index;
  uint32_t user_id;
};
static_assert(sizeof(MetalUserIDInstanceDescriptor) == 68);

bool
MultiplyWithin(uint64_t left, uint64_t right, uint64_t &result) {
  if (right && left > std::numeric_limits<uint64_t>::max() / right)
    return false;
  result = left * right;
  return true;
}

bool
ResolveBuffer(
    MTLD3D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS address, uint64_t length,
    std::vector<WMT::Reference<WMT::Buffer>> &buffers, obj_handle_t &handle, uint64_t &offset
) {
  handle = NULL_OBJECT_HANDLE;
  offset = 0;
  if (!device || !address)
    return false;

  auto *allocation = device->LookupBufferByVA(address, &offset);
  if (!allocation || offset > allocation->length() || length > allocation->length() - offset)
    return false;

  WMT::Reference<WMT::Buffer> buffer(allocation->buffer());
  if (!buffer)
    return false;
  handle = buffer.handle;
  buffers.push_back(std::move(buffer));
  return true;
}

uint32_t
ConvertBuildFlags(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags) {
  uint32_t usage = WMTAccelerationStructureUsageNone;
  if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE)
    usage |= WMTAccelerationStructureUsageRefit;
  if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD)
    usage |= WMTAccelerationStructureUsagePreferFastBuild;
  if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE)
    usage |= WMTAccelerationStructureUsagePreferFastIntersection;
  if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY)
    usage |= WMTAccelerationStructureUsageMinimizeMemory;
  return usage;
}

bool
ConvertGeometry(
    MTLD3D12Device *device, const D3D12_RAYTRACING_GEOMETRY_DESC &source,
    WMTAccelerationStructureGeometryInfo &destination, std::vector<WMT::Reference<WMT::Buffer>> &buffers
) {
  destination = {};
  destination.opaque = (source.Flags & D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE) != 0;
  destination.allow_duplicate_intersection_function_invocation =
      (source.Flags & D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION) == 0;

  switch (source.Type) {
  case D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES: {
    const auto &triangles = source.Triangles;
    if (triangles.Transform3x4 || !triangles.VertexBuffer.StartAddress ||
        triangles.VertexBuffer.StrideInBytes < 12 || !triangles.VertexCount)
      return false;
    if (triangles.VertexFormat != DXGI_FORMAT_R32G32B32_FLOAT)
      return false;

    uint64_t vertex_length = 0;
    if (!MultiplyWithin(triangles.VertexBuffer.StrideInBytes, triangles.VertexCount, vertex_length))
      return false;
    if (!ResolveBuffer(
            device, triangles.VertexBuffer.StartAddress, vertex_length, buffers, destination.vertex_buffer,
            destination.vertex_buffer_offset
        ))
      return false;

    destination.type = WMTAccelerationStructureGeometryTriangle;
    destination.vertex_format = WMTAccelerationStructureVertexFormatFloat3;
    destination.vertex_stride = triangles.VertexBuffer.StrideInBytes;
    destination.index_buffer = NULL_OBJECT_HANDLE;
    destination.index_buffer_offset = 0;
    if (triangles.IndexFormat == DXGI_FORMAT_UNKNOWN) {
      if (triangles.IndexBuffer || triangles.IndexCount || triangles.VertexCount < 3)
        return false;
      destination.index_type = WMTAccelerationStructureIndexTypeNone;
      destination.triangle_count = triangles.VertexCount / 3;
    } else {
      const uint64_t index_size = triangles.IndexFormat == DXGI_FORMAT_R16_UINT
                                      ? sizeof(uint16_t)
                                      : triangles.IndexFormat == DXGI_FORMAT_R32_UINT ? sizeof(uint32_t) : 0;
      if (!index_size || !triangles.IndexBuffer || !triangles.IndexCount || triangles.IndexCount % 3)
        return false;
      uint64_t index_length = 0;
      if (!MultiplyWithin(index_size, triangles.IndexCount, index_length) ||
          !ResolveBuffer(
              device, triangles.IndexBuffer, index_length, buffers, destination.index_buffer,
              destination.index_buffer_offset
          ))
        return false;
      destination.index_type = triangles.IndexFormat == DXGI_FORMAT_R16_UINT
                                   ? WMTAccelerationStructureIndexTypeUInt16
                                   : WMTAccelerationStructureIndexTypeUInt32;
      destination.triangle_count = triangles.IndexCount / 3;
    }
    return destination.triangle_count != 0;
  }
  case D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS: {
    const auto &aabbs = source.AABBs;
    if (!aabbs.AABBs.StartAddress || !aabbs.AABBs.StrideInBytes || !aabbs.AABBCount ||
        aabbs.AABBs.StrideInBytes < kD3D12RaytracingAABBSize)
      return false;
    uint64_t aabb_length = 0;
    if (!MultiplyWithin(aabbs.AABBs.StrideInBytes, aabbs.AABBCount, aabb_length) ||
        !ResolveBuffer(
            device, aabbs.AABBs.StartAddress, aabb_length, buffers, destination.bounding_box_buffer,
            destination.bounding_box_buffer_offset
        ))
      return false;
    destination.type = WMTAccelerationStructureGeometryBoundingBox;
    destination.bounding_box_stride = aabbs.AABBs.StrideInBytes;
    destination.bounding_box_count = aabbs.AABBCount;
    destination.vertex_format = WMTAccelerationStructureVertexFormatFloat3;
    destination.index_type = WMTAccelerationStructureIndexTypeNone;
    return true;
  }
  default:
    return false;
  }
}

bool
ConvertInstanceDescriptors(
    MTLD3D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS address, UINT count,
    D3D12RaytracingDescriptor &descriptor
) {
  uint64_t instance_offset = 0;
  auto *instance_resource = device->LookupResourceByVA(address, &instance_offset);
  if (!instance_resource)
    return false;

  uint64_t instance_length = 0;
  if (!MultiplyWithin(sizeof(D3D12_RAYTRACING_INSTANCE_DESC), count, instance_length))
    return false;
  const auto instance_resource_desc = instance_resource->GetDesc();
  if (instance_offset > instance_resource_desc.Width ||
      instance_length > instance_resource_desc.Width - instance_offset)
    return false;

  D3D12_RANGE read_range = {instance_offset, instance_offset + instance_length};
  void *mapped = nullptr;
  if (FAILED(instance_resource->Map(0, &read_range, &mapped)) || !mapped)
    return false;

  std::vector<MetalUserIDInstanceDescriptor> converted(count);
  bool valid = true;
  const auto *source = reinterpret_cast<const D3D12_RAYTRACING_INSTANCE_DESC *>(
      static_cast<const uint8_t *>(mapped) + instance_offset
  );
  for (UINT i = 0; i < count; i++) {
    const auto &source_instance = source[i];
    auto &destination = converted[i];
    for (uint32_t row = 0; row < 3; row++)
      for (uint32_t column = 0; column < 4; column++)
        destination.transformation[column][row] = source_instance.Transform[row][column];

    const uint32_t flags = source_instance.Flags;
    if ((flags & ~0xfu) || (flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE &&
                             flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE)) {
      valid = false;
      break;
    }

    uint64_t acceleration_structure_offset = 0;
    auto *acceleration_structure_resource =
        device->LookupResourceByVA(source_instance.AccelerationStructure, &acceleration_structure_offset);
    if (!acceleration_structure_resource || acceleration_structure_offset ||
        !acceleration_structure_resource->acceleration_structure) {
      valid = false;
      break;
    }

    uint32_t acceleration_structure_index = 0;
    for (; acceleration_structure_index < descriptor.acceleration_structures.size(); acceleration_structure_index++) {
      if (descriptor.acceleration_structures[acceleration_structure_index].handle ==
          acceleration_structure_resource->acceleration_structure.handle)
        break;
    }
    if (acceleration_structure_index == descriptor.acceleration_structures.size()) {
      if (descriptor.acceleration_structures.size() >= WMT_MAX_ACCELERATION_STRUCTURE_INSTANCES) {
        valid = false;
        break;
      }
      descriptor.acceleration_structures.push_back(acceleration_structure_resource->acceleration_structure);
    }

    destination.options = flags;
    destination.mask = source_instance.InstanceMask;
    destination.intersection_function_table_offset = source_instance.InstanceContributionToHitGroupIndex;
    destination.acceleration_structure_index = acceleration_structure_index;
    destination.user_id = source_instance.InstanceID;
  }
  instance_resource->Unmap(0, nullptr);
  if (!valid)
    return false;

  WMTBufferInfo buffer_info = {};
  buffer_info.length = uint64_t(sizeof(MetalUserIDInstanceDescriptor)) * count;
  buffer_info.options = WMTResourceStorageModeShared | WMTResourceHazardTrackingModeUntracked;
  buffer_info.memory.set(nullptr);
  auto instance_descriptor_buffer = device->GetMTLDevice().newBuffer(buffer_info);
  if (!instance_descriptor_buffer)
    return false;
  instance_descriptor_buffer.updateContents(0, converted.data(), buffer_info.length);

  auto &instance = descriptor.info.data.instance;
  instance.instance_descriptor_buffer = instance_descriptor_buffer.handle;
  instance.instance_descriptor_buffer_offset = 0;
  instance.instance_descriptor_stride = sizeof(MetalUserIDInstanceDescriptor);
  instance.instance_count = count;
  instance.instance_descriptor_type = WMTAccelerationStructureInstanceDescriptorUserID;
  instance.instanced_acceleration_structure_count = descriptor.acceleration_structures.size();
  for (uint32_t i = 0; i < instance.instanced_acceleration_structure_count; i++)
    instance.instanced_acceleration_structures[i] = descriptor.acceleration_structures[i].handle;
  descriptor.buffers.push_back(std::move(instance_descriptor_buffer));
  return true;
}

} // namespace

bool
ConvertD3D12RaytracingInputs(
    MTLD3D12Device *device, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *inputs,
    D3D12RaytracingDescriptor &descriptor, bool for_execution
) {
  descriptor = {};
  if (!device || !inputs || !inputs->NumDescs)
    return false;

  descriptor.info.data.primitive.usage = ConvertBuildFlags(inputs->Flags);
  switch (inputs->Type) {
  case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL:
    if (inputs->NumDescs > WMT_MAX_ACCELERATION_STRUCTURE_GEOMETRIES ||
        inputs->DescsLayout != D3D12_ELEMENTS_LAYOUT_ARRAY || !inputs->pGeometryDescs)
      return false;
    descriptor.info.type = WMTAccelerationStructureDescriptorPrimitive;
    descriptor.info.data.primitive.geometry_count = inputs->NumDescs;
    for (UINT i = 0; i < inputs->NumDescs; i++) {
      if (!ConvertGeometry(device, inputs->pGeometryDescs[i], descriptor.info.data.primitive.geometries[i], descriptor.buffers))
        return false;
    }
    return true;
  case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL: {
    if (inputs->NumDescs > WMT_MAX_ACCELERATION_STRUCTURE_INSTANCES ||
        inputs->DescsLayout != D3D12_ELEMENTS_LAYOUT_ARRAY || !inputs->InstanceDescs)
      return false;
    uint64_t instance_length = 0;
    if (!MultiplyWithin(kD3D12RaytracingInstanceDescriptorSize, inputs->NumDescs, instance_length))
      return false;
    descriptor.info.type = WMTAccelerationStructureDescriptorInstance;
    auto &instance = descriptor.info.data.instance;
    instance.usage = ConvertBuildFlags(inputs->Flags);
    if (for_execution)
      return ConvertInstanceDescriptors(device, inputs->InstanceDescs, inputs->NumDescs, descriptor);
    if (!ResolveBuffer(
            device, inputs->InstanceDescs, instance_length, descriptor.buffers,
            instance.instance_descriptor_buffer, instance.instance_descriptor_buffer_offset
        ))
      return false;
    instance.instance_descriptor_stride = kD3D12RaytracingInstanceDescriptorSize;
    instance.instance_count = inputs->NumDescs;
    instance.instance_descriptor_type = WMTAccelerationStructureInstanceDescriptorDefault;
    return true;
  }
  default:
    return false;
  }
}

} // namespace dxmt

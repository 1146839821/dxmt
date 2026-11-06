#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <cstdint>
#include <iostream>
#include <utility>

#include "Metal.hpp"

namespace {

bool
CheckCommand(WMT::CommandBuffer command_buffer, const char *name) {
  if (command_buffer.status() != WMTCommandBufferStatusError)
    return true;

  std::cerr << name << " failed";
  auto error = command_buffer.error();
  if (error)
    std::cerr << ": " << error.description().getUTF8String();
  std::cerr << "\n";
  return false;
}

struct SharedBuffer {
  WMT::Reference<WMT::Buffer> buffer;
  void *contents;
};

SharedBuffer
NewSharedBuffer(WMT::Device device, uint64_t length, const void *initial_data = nullptr) {
  WMTBufferInfo info = {};
  info.length = length;
  info.options = WMTResourceStorageModeShared;
  info.memory.set(nullptr);
  auto buffer = device.newBuffer(info);
  if (buffer && initial_data)
    buffer.updateContents(0, initial_data, length);
  return {std::move(buffer), info.memory.get_accessible_or_null()};
}

} // namespace

int
main() {
  auto devices = WMT::CopyAllDevices();
  if (!devices || !devices.count()) {
    std::cerr << "Metal device enumeration failed\n";
    return 1;
  }

  WMT::Device device = devices.object(0);
  if (!device.supportsRaytracing()) {
    std::cout << "Metal acceleration structures unavailable on selected device\n";
    return 0;
  }

  const float vertices[] = {
      -1.0f, -1.0f, 0.0f,
      1.0f,  -1.0f, 0.0f,
      0.0f,  1.0f,  0.0f,
  };
  auto vertex_buffer = NewSharedBuffer(device, sizeof(vertices), vertices);
  if (!vertex_buffer.buffer) {
    std::cerr << "vertex buffer creation failed\n";
    return 1;
  }

  WMTAccelerationStructureDescriptorInfo descriptor = {};
  descriptor.type = WMTAccelerationStructureDescriptorPrimitive;
  auto &primitive = descriptor.data.primitive;
  primitive.geometry_count = 1;
  primitive.usage = WMTAccelerationStructureUsageRefit;
  auto &geometry = primitive.geometries[0];
  geometry.type = WMTAccelerationStructureGeometryTriangle;
  geometry.vertex_format = WMTAccelerationStructureVertexFormatFloat3;
  geometry.index_type = WMTAccelerationStructureIndexTypeNone;
  geometry.vertex_buffer = vertex_buffer.buffer.handle;
  geometry.vertex_stride = sizeof(float) * 3;
  geometry.triangle_count = 1;
  geometry.opaque = true;
  geometry.allow_duplicate_intersection_function_invocation = false;

  const auto sizes = device.accelerationStructureSizes(descriptor);
  if (!sizes.acceleration_structure_size || !sizes.build_scratch_buffer_size) {
    std::cerr << "Metal acceleration structure size query returned zero\n";
    return 1;
  }

  uint64_t acceleration_structure_id = 0;
  auto acceleration_structure =
      device.newAccelerationStructure(sizes.acceleration_structure_size, &acceleration_structure_id);
  auto scratch_buffer = NewSharedBuffer(device, sizes.build_scratch_buffer_size);
  if (!acceleration_structure || !scratch_buffer.buffer) {
    std::cerr << "Metal acceleration structure allocation failed\n";
    return 1;
  }

  uint32_t compacted_size = 0;
  auto compacted_size_buffer = NewSharedBuffer(device, sizeof(compacted_size));
  if (!compacted_size_buffer.buffer) {
    std::cerr << "compaction size buffer creation failed\n";
    return 1;
  }

  auto queue = device.newCommandQueue(4);
  if (!queue) {
    std::cerr << "command queue creation failed\n";
    return 1;
  }

  auto build_command_buffer = queue.commandBuffer();
  auto build_encoder = build_command_buffer.accelerationStructureCommandEncoder();
  if (!build_encoder ||
      !build_encoder.useResource(vertex_buffer.buffer, WMTResourceUsageRead) ||
      !build_encoder.useResource(scratch_buffer.buffer, static_cast<WMTResourceUsage>(WMTResourceUsageRead | WMTResourceUsageWrite)) ||
      !build_encoder.build(acceleration_structure, descriptor, scratch_buffer.buffer, 0) ||
      !build_encoder.writeCompactedSize(
          acceleration_structure, compacted_size_buffer.buffer, 0, WMTAccelerationStructureSizeDataTypeUInt32
      )) {
    std::cerr << "Metal acceleration structure build encoding failed\n";
    return 1;
  }
  build_encoder.endEncoding();
  build_command_buffer.commit();
  build_command_buffer.waitUntilCompleted();
  if (!CheckCommand(build_command_buffer, "acceleration structure build"))
    return 1;

  compacted_size = *static_cast<uint32_t *>(compacted_size_buffer.contents);
  if (!compacted_size || compacted_size > sizes.acceleration_structure_size) {
    std::cerr << "invalid compacted acceleration structure size: " << compacted_size << "\n";
    return 1;
  }

  uint64_t compacted_acceleration_structure_id = 0;
  auto compacted_acceleration_structure =
      device.newAccelerationStructure(compacted_size, &compacted_acceleration_structure_id);
  auto copied_acceleration_structure = device.newAccelerationStructure(sizes.acceleration_structure_size);
  if (!compacted_acceleration_structure || !copied_acceleration_structure) {
    std::cerr << "copy destination acceleration structure allocation failed\n";
    return 1;
  }

  auto copy_command_buffer = queue.commandBuffer();
  auto copy_encoder = copy_command_buffer.accelerationStructureCommandEncoder();
  if (!copy_encoder || !copy_encoder.copy(acceleration_structure, copied_acceleration_structure) ||
      !copy_encoder.copyAndCompact(acceleration_structure, compacted_acceleration_structure)) {
    std::cerr << "acceleration structure copy encoding failed\n";
    return 1;
  }
  copy_encoder.endEncoding();
  copy_command_buffer.commit();
  copy_command_buffer.waitUntilCompleted();
  if (!CheckCommand(copy_command_buffer, "acceleration structure copy"))
    return 1;

  auto refit_command_buffer = queue.commandBuffer();
  auto refit_encoder = refit_command_buffer.accelerationStructureCommandEncoder();
  if (!refit_encoder || !refit_encoder.refit(acceleration_structure, descriptor, scratch_buffer.buffer, 0)) {
    std::cerr << "acceleration structure refit encoding failed\n";
    return 1;
  }
  refit_encoder.endEncoding();
  refit_command_buffer.commit();
  refit_command_buffer.waitUntilCompleted();
  if (!CheckCommand(refit_command_buffer, "acceleration structure refit"))
    return 1;

  if (acceleration_structure.gpuResourceID() != acceleration_structure_id ||
      compacted_acceleration_structure.gpuResourceID() != compacted_acceleration_structure_id) {
    std::cerr << "acceleration structure resource ID query disagrees with allocation\n";
    return 1;
  }

  std::cout << "Metal acceleration structure foundation passed: build=" << sizes.acceleration_structure_size
            << ",scratch=" << sizes.build_scratch_buffer_size << ",compacted=" << compacted_size
            << ",resource_id=" << acceleration_structure_id << "\n";
  return 0;
}

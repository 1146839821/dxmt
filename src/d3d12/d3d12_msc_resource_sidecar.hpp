#pragma once

#include "d3d12_device.hpp"
#include <cstring>

namespace dxmt {

// Validate the whole mapping before changing an argument buffer. Null root
// tables write zero; non-null handles must belong to the bound resource heap.
inline bool
writeMSCResourceSidecars(
    const MTLD3D12RootSignature &root, MTLD3D12DescriptorHeap *heap, const uint64_t *staging, size_t staging_count,
    void *buffer, size_t buffer_size
) {
  const auto *layout = root.MSCCombinedSidecarLayout;
  if (!layout || !buffer || (!staging && staging_count) || buffer_size < layout->buffer_size)
    return false;
  for (const auto &table : layout->resources) {
    if (table.parameter_index >= root.ParameterSlots || table.sidecar_offset > buffer_size ||
        sizeof(uint64_t) > buffer_size - table.sidecar_offset)
      return false;
    const auto qword = root.Parameters[table.parameter_index].source_qword;
    if (qword >= root.UploadQwords || qword >= staging_count || !staging)
      return false;
    const D3D12_GPU_DESCRIPTOR_HANDLE handle{staging[qword]};
    if (handle.ptr && (!heap || !heap->GetMSCDescriptorTableAddress(handle)))
      return false;
  }
  for (const auto &table : layout->resources) {
    const auto value = staging[root.Parameters[table.parameter_index].source_qword];
    std::memcpy(static_cast<uint8_t *>(buffer) + table.sidecar_offset, &value, sizeof(value));
  }
  return true;
}
} // namespace dxmt

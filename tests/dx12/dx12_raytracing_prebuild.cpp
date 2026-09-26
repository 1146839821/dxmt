#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <iostream>

namespace {

template <typename T> struct Owned {
  T *ptr = nullptr;

  ~Owned() {
    if (ptr)
      ptr->Release();
  }

  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;

  T *operator->() const { return ptr; }
};

bool
CheckAligned(const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO &info) {
  constexpr UINT64 alignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
  return info.ResultDataMaxSizeInBytes && info.ScratchDataSizeInBytes &&
         !(info.ResultDataMaxSizeInBytes % alignment) && !(info.ScratchDataSizeInBytes % alignment) &&
         (!(info.UpdateScratchDataSizeInBytes % alignment));
}

D3D12_RESOURCE_DESC
BufferDesc(UINT64 width) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = width;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return desc;
}

} // namespace

int
main() {
  Owned<ID3D12Device> device;
  if (D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)) != S_OK) {
    std::cerr << "D3D12CreateDevice failed\n";
    return 1;
  }

  Owned<ID3D12Device5> device5;
  if (device->QueryInterface(IID_PPV_ARGS(&device5.ptr)) != S_OK || !device5.ptr) {
    std::cerr << "ID3D12Device5 query failed\n";
    return 1;
  }

  D3D12_HEAP_PROPERTIES upload_heap = {};
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_heap.CreationNodeMask = 1;
  upload_heap.VisibleNodeMask = 1;

  Owned<ID3D12Resource> vertices;
  const auto vertex_desc = BufferDesc(3 * sizeof(float) * 3);
  if (device->CreateCommittedResource(
          &upload_heap, D3D12_HEAP_FLAG_NONE, &vertex_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&vertices.ptr)
      ) != S_OK) {
    std::cerr << "vertex buffer creation failed\n";
    return 1;
  }

  D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
  geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  geometry.Triangles.VertexBuffer.StartAddress = vertices->GetGPUVirtualAddress();
  geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
  geometry.Triangles.VertexCount = 3;
  geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geometry.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  inputs.NumDescs = 1;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  inputs.pGeometryDescs = &geometry;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_info = {};
  device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &blas_info);
  if (!CheckAligned(blas_info) || blas_info.UpdateScratchDataSizeInBytes > blas_info.ScratchDataSizeInBytes) {
    std::cerr << "BLAS prebuild query returned invalid sizes\n";
    return 1;
  }

  const auto invalid_geometry = geometry;
  geometry.Triangles.VertexFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO invalid_info = {1, 2, 3};
  device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &invalid_info);
  if (invalid_info.ResultDataMaxSizeInBytes || invalid_info.ScratchDataSizeInBytes ||
      invalid_info.UpdateScratchDataSizeInBytes) {
    std::cerr << "invalid BLAS prebuild query returned sizes\n";
    return 1;
  }
  geometry = invalid_geometry;

  Owned<ID3D12Resource> instances;
  const auto instance_desc = BufferDesc(sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
  if (device->CreateCommittedResource(
          &upload_heap, D3D12_HEAP_FLAG_NONE, &instance_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&instances.ptr)
      ) != S_OK) {
    std::cerr << "instance buffer creation failed\n";
    return 1;
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs = {};
  tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  tlas_inputs.NumDescs = 1;
  tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlas_inputs.InstanceDescs = instances->GetGPUVirtualAddress();

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info = {};
  device5->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_info);
  if (!CheckAligned(tlas_info)) {
    std::cerr << "TLAS prebuild query returned invalid sizes\n";
    return 1;
  }

  std::cout << "D3D12 raytracing prebuild passed: blas=" << blas_info.ResultDataMaxSizeInBytes
            << ",scratch=" << blas_info.ScratchDataSizeInBytes << ",tlas="
            << tlas_info.ResultDataMaxSizeInBytes << "\n";
  return 0;
}

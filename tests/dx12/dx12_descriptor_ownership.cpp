#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include "d3d12_descriptor_heap.hpp"

#include <iostream>

namespace {

template <typename T>
void Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

bool CheckHR(const char *name, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

D3D12_HEAP_PROPERTIES DefaultHeap() {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC BufferDescription(UINT64 width, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = width;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
  return desc;
}

D3D12_RESOURCE_DESC RenderTargetDescription() {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 1;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  return desc;
}

bool IsDescriptorType(
    dxmt::MTLD3D12DescriptorHeap *heap, dxmt::ShaderVisibleDescriptorType expected, const char *name
) {
  if (heap->GetDescriptor(0).type == expected)
    return true;
  std::cerr << name << " descriptor type changed unexpectedly\n";
  return false;
}

} // namespace

int main() {
  ID3D12Device *device_a = nullptr;
  ID3D12Device *device_b = nullptr;
  ID3D12DescriptorHeap *shader_heap_a = nullptr;
  ID3D12DescriptorHeap *shader_heap_b = nullptr;
  ID3D12DescriptorHeap *rtv_heap_a = nullptr;
  ID3D12DescriptorHeap *rtv_heap_b = nullptr;
  ID3D12Resource *buffer_a = nullptr;
  ID3D12Resource *buffer_b = nullptr;
  ID3D12Resource *counter_a = nullptr;
  ID3D12Resource *counter_b = nullptr;
  ID3D12Resource *texture_a = nullptr;
  ID3D12Resource *texture_b = nullptr;
  bool passed = true;

  auto cleanup = [&] {
    Release(texture_b);
    Release(texture_a);
    Release(counter_b);
    Release(counter_a);
    Release(buffer_b);
    Release(buffer_a);
    Release(rtv_heap_b);
    Release(rtv_heap_a);
    Release(shader_heap_b);
    Release(shader_heap_a);
    Release(device_b);
    Release(device_a);
    return passed;
  };
  auto abort = [&] {
    passed = false;
    return cleanup() ? 0 : 1;
  };

  if (!CheckHR("D3D12CreateDevice A", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_a))) ||
      !CheckHR("D3D12CreateDevice B", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_b))))
    return abort();

  D3D12_DESCRIPTOR_HEAP_DESC shader_desc = {};
  shader_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  shader_desc.NumDescriptors = 1;
  shader_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (!CheckHR("CreateShaderHeap A", device_a->CreateDescriptorHeap(&shader_desc, IID_PPV_ARGS(&shader_heap_a))) ||
      !CheckHR("CreateShaderHeap B", device_b->CreateDescriptorHeap(&shader_desc, IID_PPV_ARGS(&shader_heap_b))))
    return abort();

  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_desc.NumDescriptors = 1;
  if (!CheckHR("CreateRTVHeap A", device_a->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap_a))) ||
      !CheckHR("CreateRTVHeap B", device_b->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap_b))))
    return abort();

  const auto default_heap = DefaultHeap();
  const auto buffer_desc = BufferDescription(256);
  const auto uav_buffer_desc = BufferDescription(256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  const auto texture_desc = RenderTargetDescription();
  if (!CheckHR("CreateBuffer A", device_a->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer_a))) ||
      !CheckHR("CreateBuffer B", device_b->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &uav_buffer_desc,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&buffer_b))) ||
      !CheckHR("CreateCounter A", device_a->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&counter_a))) ||
      !CheckHR("CreateCounter B", device_b->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&counter_b))) ||
      !CheckHR("CreateTexture A", device_a->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&texture_a))) ||
      !CheckHR("CreateTexture B", device_b->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&texture_b))))
    return abort();

  auto *shader_impl_a = static_cast<dxmt::MTLD3D12DescriptorHeap *>(shader_heap_a);
  auto *shader_impl_b = static_cast<dxmt::MTLD3D12DescriptorHeap *>(shader_heap_b);
  auto *rtv_impl_a = static_cast<dxmt::MTLD3D12RenderTargetDescriptorHeap *>(rtv_heap_a);
  auto *rtv_impl_b = static_cast<dxmt::MTLD3D12RenderTargetDescriptorHeap *>(rtv_heap_b);
  D3D12_CPU_DESCRIPTOR_HANDLE shader_cpu_a = {};
  D3D12_CPU_DESCRIPTOR_HANDLE shader_cpu_b = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_a = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_b = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = DXGI_FORMAT_UNKNOWN;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Buffer.NumElements = 64;
  srv_desc.Buffer.StructureByteStride = 4;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 64;
  uav_desc.Buffer.StructureByteStride = 4;
  D3D12_RENDER_TARGET_VIEW_DESC rtv_view = {};
  rtv_view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rtv_view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  rtv_view.Texture2D.MipSlice = 0;
  rtv_view.Texture2D.PlaneSlice = 0;
  shader_heap_a->GetCPUDescriptorHandleForHeapStart(&shader_cpu_a);
  shader_heap_b->GetCPUDescriptorHandleForHeapStart(&shader_cpu_b);
  rtv_heap_a->GetCPUDescriptorHandleForHeapStart(&rtv_cpu_a);
  rtv_heap_b->GetCPUDescriptorHandleForHeapStart(&rtv_cpu_b);

  device_a->CreateShaderResourceView(buffer_a, &srv_desc, shader_cpu_a);
  passed &= IsDescriptorType(shader_impl_a, dxmt::ShaderVisibleDescriptorType::SRVBuffer, "own SRV");
  passed &= IsDescriptorType(shader_impl_b, dxmt::ShaderVisibleDescriptorType::Null, "initial B");

  device_a->CreateShaderResourceView(buffer_a, &srv_desc, shader_cpu_b);
  passed &= IsDescriptorType(shader_impl_b, dxmt::ShaderVisibleDescriptorType::Null, "foreign heap SRV");

  device_b->CreateShaderResourceView(buffer_a, &srv_desc, shader_cpu_b);
  passed &= IsDescriptorType(shader_impl_b, dxmt::ShaderVisibleDescriptorType::Null, "foreign resource SRV");

  device_b->CreateUnorderedAccessView(buffer_b, counter_a, &uav_desc, shader_cpu_b);
  passed &= IsDescriptorType(shader_impl_b, dxmt::ShaderVisibleDescriptorType::Null, "foreign UAV counter");

  device_b->CreateUnorderedAccessView(texture_b, counter_b, nullptr, shader_cpu_b);
  passed &= IsDescriptorType(shader_impl_b, dxmt::ShaderVisibleDescriptorType::Null, "texture UAV counter");

  device_a->CreateRenderTargetView(texture_a, &rtv_view, rtv_cpu_a);
  passed &= rtv_impl_a->GetRenderTarget(0).Texture != nullptr;
  if (!passed)
    std::cerr << "own RTV descriptor was not created\n";
  device_a->CreateRenderTargetView(texture_a, &rtv_view, rtv_cpu_b);
  passed &= rtv_impl_b->GetRenderTarget(0).Texture == nullptr;
  if (!passed)
    std::cerr << "foreign RTV heap was modified\n";

  if (!passed)
    return abort();
  std::cout << "D3D12 descriptor and view ownership validation passed\n";
  return cleanup() ? 0 : 1;
}

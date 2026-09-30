#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

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
};

bool CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

bool ReadFile(const char *path, std::vector<char> &data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return false;
  const auto size = file.tellg();
  if (size <= 0)
    return false;
  file.seekg(0);
  data.resize(static_cast<size_t>(size));
  return file.read(data.data(), data.size()).good();
}

bool WaitForQueue(ID3D12CommandQueue *queue, ID3D12Device *device, ID3D12CommandList *list) {
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  bool result = false;
  ID3D12CommandList *lists[] = {list};
  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
      !CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event || !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  if (WaitForSingleObject(event, INFINITE) != WAIT_OBJECT_0)
    goto cleanup;
  result = true;

cleanup:
  if (event)
    CloseHandle(event);
  if (fence)
    fence->Release();
  return result;
}

D3D12_HEAP_PROPERTIES DefaultHeap() {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_HEAP_PROPERTIES UploadHeap() {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_UPLOAD;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_HEAP_PROPERTIES ReadbackHeap() {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_READBACK;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 width, D3D12_RESOURCE_FLAGS flags) {
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

bool CreateUploadedTexture(
    ID3D12Device *device, const D3D12_RESOURCE_DESC &desc, UINT value, Owned<ID3D12Resource> &texture,
    Owned<ID3D12Resource> &upload, std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> &footprints
) {
  const UINT subresource_count = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                     ? 1
                                     : static_cast<UINT>(desc.DepthOrArraySize);
  std::vector<UINT> row_counts(subresource_count);
  std::vector<UINT64> row_sizes(subresource_count);
  UINT64 total_size = 0;
  footprints.resize(subresource_count);
  device->GetCopyableFootprints(
      &desc, 0, subresource_count, 0, footprints.data(), row_counts.data(), row_sizes.data(), &total_size
  );

  const D3D12_HEAP_PROPERTIES default_heap = DefaultHeap();
  const D3D12_HEAP_PROPERTIES upload_heap = UploadHeap();
  if (!CheckHR("CreateTexture", device->CreateCommittedResource(
                                    &default_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture.ptr))))
    return false;

  const D3D12_RESOURCE_DESC upload_desc = BufferDesc(total_size ? total_size : 256, D3D12_RESOURCE_FLAG_NONE);
  if (!CheckHR("CreateTextureUpload", device->CreateCommittedResource(
                                         &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                         IID_PPV_ARGS(&upload.ptr))))
    return false;

  void *mapped = nullptr;
  if (!CheckHR("MapTextureUpload", upload.ptr->Map(0, nullptr, &mapped)))
    return false;
  std::memset(mapped, 0, static_cast<size_t>(upload_desc.Width));
  const UINT pixel = value | 0xff000000u;
  for (UINT subresource = 0; subresource < subresource_count; subresource++) {
    const auto &footprint = footprints[subresource].Footprint;
    BYTE *destination = static_cast<BYTE *>(mapped) + footprints[subresource].Offset;
    for (UINT depth = 0; depth < footprint.Depth; depth++) {
      BYTE *slice = destination + static_cast<size_t>(depth) * footprint.RowPitch * row_counts[subresource];
      for (UINT row = 0; row < row_counts[subresource]; row++)
        std::memcpy(slice + static_cast<size_t>(row) * footprint.RowPitch, &pixel, sizeof(pixel));
    }
  }
  upload.ptr->Unmap(0, nullptr);
  return true;
}

void SetSrvDesc(D3D12_SHADER_RESOURCE_VIEW_DESC &desc, UINT index) {
  desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  switch (index) {
  case 0:
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
    desc.Texture1D.MipLevels = 1;
    break;
  case 1:
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
    desc.Texture1DArray.MipLevels = 1;
    desc.Texture1DArray.ArraySize = 2;
    break;
  case 2:
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = 1;
    break;
  case 3:
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    desc.Texture2DArray.MipLevels = 1;
    desc.Texture2DArray.ArraySize = 2;
    break;
  case 4:
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    desc.TextureCube.MipLevels = 1;
    break;
  case 5:
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
    desc.TextureCubeArray.MipLevels = 1;
    desc.TextureCubeArray.NumCubes = 2;
    break;
  case 6:
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    break;
  case 7:
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
    desc.Texture2DMSArray.ArraySize = 2;
    break;
  default:
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    desc.Texture3D.MipLevels = 1;
    break;
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dx12_texture_dimensions <shader.cso>\n";
    return 2;
  }

  std::vector<char> shader;
  if (!ReadFile(argv[1], shader)) {
    std::cerr << "failed to read shader fixture\n";
    return 3;
  }

  Owned<ID3D12Device> device;
  Owned<ID3D12CommandQueue> queue;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12RootSignature> root_signature;
  Owned<ID3DBlob> root_blob;
  Owned<ID3DBlob> root_error;
  Owned<ID3D12DescriptorHeap> resource_heap;
  Owned<ID3D12DescriptorHeap> sampler_heap;
  Owned<ID3D12DescriptorHeap> rtv_heap;
  Owned<ID3D12Resource> textures[9];
  Owned<ID3D12Resource> uploads[9];
  Owned<ID3D12Resource> output;
  Owned<ID3D12Resource> readback;
  Owned<ID3D12PipelineState> pso;
  Owned<ID3D12GraphicsCommandList> list;
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints[9];
  D3D12_RESOURCE_DESC texture_descs[9] = {};
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_DESCRIPTOR_RANGE ranges[3] = {};
  D3D12_ROOT_PARAMETER root_parameters[3] = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC resource_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  D3D12_SAMPLER_DESC sampler_desc = {};
  D3D12_CLEAR_VALUE clear_value = {};
  D3D12_RESOURCE_BARRIER barrier = {};
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_CPU_DESCRIPTOR_HANDLE resource_cpu = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu = {};
  UINT descriptor_increment = 0;
  UINT rtv_increment = 0;
  BYTE *mapped = nullptr;
  bool result = false;

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr))))
    return 1;

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device.ptr->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue.ptr))) ||
      !CheckHR("CreateCommandAllocator", device.ptr->CreateCommandAllocator(
                                                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.ptr))))
    return 1;

  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 9, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0};
  ranges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
  for (UINT i = 0; i < 3; i++) {
    root_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[i].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    root_parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  root_desc.NumParameters = 3;
  root_desc.pParameters = root_parameters;
  if (!CheckHR("D3D12SerializeRootSignature", D3D12SerializeRootSignature(
                                                       &root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                       &root_blob.ptr, &root_error.ptr)) ||
      !CheckHR("CreateRootSignature", device.ptr->CreateRootSignature(
                                          0, root_blob.ptr->GetBufferPointer(), root_blob.ptr->GetBufferSize(),
                                          IID_PPV_ARGS(&root_signature.ptr))))
    return 1;

  resource_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  resource_heap_desc.NumDescriptors = 10;
  resource_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  sampler_heap_desc.NumDescriptors = 1;
  sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 2;
  if (!CheckHR("CreateResourceHeap", device.ptr->CreateDescriptorHeap(
                                          &resource_heap_desc, IID_PPV_ARGS(&resource_heap.ptr))) ||
      !CheckHR("CreateSamplerHeap", device.ptr->CreateDescriptorHeap(
                                         &sampler_heap_desc, IID_PPV_ARGS(&sampler_heap.ptr))) ||
      !CheckHR("CreateRTVHeap", device.ptr->CreateDescriptorHeap(
                                     &rtv_heap_desc, IID_PPV_ARGS(&rtv_heap.ptr))))
    return 1;
  descriptor_increment = device.ptr->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  rtv_increment = device.ptr->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  const D3D12_HEAP_PROPERTIES default_heap = DefaultHeap();
  const D3D12_HEAP_PROPERTIES readback_heap = ReadbackHeap();
  for (UINT i = 0; i < 9; i++) {
    texture_descs[i].Width = 1;
    texture_descs[i].Height = 1;
    texture_descs[i].DepthOrArraySize = 1;
    texture_descs[i].MipLevels = 1;
    texture_descs[i].Format = format;
    texture_descs[i].SampleDesc.Count = 1;
    texture_descs[i].Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  }
  texture_descs[0].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
  texture_descs[1].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
  texture_descs[1].DepthOrArraySize = 2;
  texture_descs[2].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_descs[3].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_descs[3].DepthOrArraySize = 2;
  texture_descs[4].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_descs[4].DepthOrArraySize = 6;
  texture_descs[5].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_descs[5].DepthOrArraySize = 12;
  texture_descs[6].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_descs[6].SampleDesc.Count = 4;
  texture_descs[6].Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  texture_descs[7].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_descs[7].DepthOrArraySize = 2;
  texture_descs[7].SampleDesc.Count = 4;
  texture_descs[7].Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  texture_descs[8].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  texture_descs[8].DepthOrArraySize = 2;

  for (UINT i = 0; i < 9; i++) {
    if (i == 6 || i == 7) {
      clear_value = {};
      clear_value.Format = format;
      clear_value.Color[3] = 1.0f;
      if (!CheckHR(i == 6 ? "CreateTextureMS" : "CreateTextureMSArray", device.ptr->CreateCommittedResource(
                                                        &default_heap, D3D12_HEAP_FLAG_NONE, &texture_descs[i],
                                                        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
                                                        IID_PPV_ARGS(&textures[i].ptr))))
        return 1;
    } else if (!CreateUploadedTexture(device.ptr, texture_descs[i], 11 + i * 11, textures[i], uploads[i], footprints[i])) {
      return 1;
    }
  }

  resource_cpu = resource_heap.ptr->GetCPUDescriptorHandleForHeapStart();
  for (UINT i = 0; i < 9; i++) {
    SetSrvDesc(srv_desc, i);
    auto descriptor = resource_cpu;
    descriptor.ptr += descriptor_increment * i;
    device.ptr->CreateShaderResourceView(textures[i].ptr, &srv_desc, descriptor);
  }
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 9;
  uav_desc.Buffer.StructureByteStride = sizeof(UINT);
  auto output_cpu = resource_cpu;
  output_cpu.ptr += descriptor_increment * 9;
  const D3D12_RESOURCE_DESC output_desc = BufferDesc(256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  if (!CheckHR("CreateOutput", device.ptr->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output.ptr))))
    return 1;
  device.ptr->CreateUnorderedAccessView(output.ptr, nullptr, &uav_desc, output_cpu);

  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  sampler_desc.MinLOD = 0;
  sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
  device.ptr->CreateSampler(&sampler_desc, sampler_heap.ptr->GetCPUDescriptorHandleForHeapStart());

  rtv_cpu = rtv_heap.ptr->GetCPUDescriptorHandleForHeapStart();
  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = format;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
  device.ptr->CreateRenderTargetView(textures[6].ptr, &rtv_desc, rtv_cpu);
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
  rtv_desc.Texture2DMSArray.ArraySize = 2;
  auto rtv_array = rtv_cpu;
  rtv_array.ptr += rtv_increment;
  device.ptr->CreateRenderTargetView(textures[7].ptr, &rtv_desc, rtv_array);

  const D3D12_RESOURCE_DESC readback_desc = BufferDesc(256, D3D12_RESOURCE_FLAG_NONE);
  if (!CheckHR("CreateReadback", device.ptr->CreateCommittedResource(
                                   &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback.ptr))))
    return 1;

  pso_desc.pRootSignature = root_signature.ptr;
  pso_desc.CS.pShaderBytecode = shader.data();
  pso_desc.CS.BytecodeLength = shader.size();
  if (!CheckHR("CreateComputePipelineState", device.ptr->CreateComputePipelineState(
                                                  &pso_desc, IID_PPV_ARGS(&pso.ptr))) ||
      !CheckHR("CreateCommandList", device.ptr->CreateCommandList(
                                      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.ptr, pso.ptr,
                                      IID_PPV_ARGS(&list.ptr))))
    return 1;

  for (UINT i = 0; i < 9; i++) {
    if (i == 6 || i == 7) {
      const float clear_color[] = {(i == 6 ? 77.0f : 88.0f) / 255.0f, 0.0f, 0.0f, 1.0f};
      auto rtv = rtv_cpu;
      rtv.ptr += rtv_increment * (i - 6);
      list.ptr->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
      barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = textures[i].ptr;
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      list.ptr->ResourceBarrier(1, &barrier);
      continue;
    }
    const UINT subresource_count = texture_descs[i].Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                       ? 1
                                       : static_cast<UINT>(texture_descs[i].DepthOrArraySize);
    for (UINT subresource = 0; subresource < subresource_count; subresource++) {
      D3D12_TEXTURE_COPY_LOCATION destination = {};
      destination.pResource = textures[i].ptr;
      destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      destination.SubresourceIndex = subresource;
      D3D12_TEXTURE_COPY_LOCATION source = {};
      source.pResource = uploads[i].ptr;
      source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      source.PlacedFootprint = footprints[i][subresource];
      list.ptr->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = textures[i].ptr;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list.ptr->ResourceBarrier(1, &barrier);
  }

  ID3D12DescriptorHeap *heaps[] = {resource_heap.ptr, sampler_heap.ptr};
  list.ptr->SetDescriptorHeaps(2, heaps);
  list.ptr->SetComputeRootSignature(root_signature.ptr);
  list.ptr->SetComputeRootDescriptorTable(0, resource_heap.ptr->GetGPUDescriptorHandleForHeapStart());
  list.ptr->SetComputeRootDescriptorTable(1, sampler_heap.ptr->GetGPUDescriptorHandleForHeapStart());
  auto output_gpu = resource_heap.ptr->GetGPUDescriptorHandleForHeapStart();
  output_gpu.ptr += descriptor_increment * 9;
  list.ptr->SetComputeRootDescriptorTable(2, output_gpu);
  list.ptr->Dispatch(1, 1, 1);
  barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = output.ptr;
  list.ptr->ResourceBarrier(1, &barrier);
  list.ptr->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, sizeof(UINT) * 9);
  if (!CheckHR("Close", list.ptr->Close()) || !WaitForQueue(queue.ptr, device.ptr, list.ptr))
    return 1;

  if (!CheckHR("MapReadback", readback.ptr->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    return 1;
  static const UINT expected[] = {11, 22, 33, 44, 55, 66, 77, 88, 99};
  for (UINT i = 0; i < 9; i++) {
    if (reinterpret_cast<UINT *>(mapped)[i] != expected[i]) {
      std::cerr << "texture dimension " << i << " readback mismatch: " << reinterpret_cast<UINT *>(mapped)[i]
                << " expected " << expected[i] << "\n";
      readback.ptr->Unmap(0, nullptr);
      return 1;
    }
  }
  readback.ptr->Unmap(0, nullptr);
  std::cout << "DXIL texture dimensions readback passed: 11,22,33,44,55,66,77,88,99\n";
  result = true;
  return result ? 0 : 1;
}

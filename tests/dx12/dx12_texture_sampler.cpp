#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

static bool
CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

int
main(int argc, char **argv) {
  if (argc < 2 || argc > 3)
    return 2;
  const bool static_sampler = argc == 3 && strcmp(argv[2], "--static-sampler") == 0;
  const bool direct_indexed = argc == 3 && strcmp(argv[2], "--direct-indexed") == 0;
  if (argc == 3 && !static_sampler && !direct_indexed)
    return 2;

  std::ifstream shader_file(argv[1], std::ios::binary | std::ios::ate);
  if (!shader_file)
    return 3;
  auto shader_size = shader_file.tellg();
  shader_file.seekg(0);
  std::vector<char> shader(static_cast<size_t>(shader_size));
  shader_file.read(shader.data(), shader.size());

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  ID3D12DescriptorHeap *resource_heap = nullptr;
  ID3D12DescriptorHeap *sampler_heap = nullptr;
  ID3D12Resource *texture = nullptr;
  ID3D12Resource *upload = nullptr;
  ID3D12Resource *output = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  ID3D12CommandList *lists[1] = {};
  UINT *mapped = nullptr;
  UINT value = 0;
  UINT descriptor_increment = 0;
  unsigned root_parameter_count = 0;
  HRESULT serialize_hr = E_FAIL;
  int result = 1;

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_DESCRIPTOR_RANGE ranges[3] = {};
  D3D12_ROOT_PARAMETER root_parameters[3] = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_desc = {};
  D3D12_STATIC_SAMPLER_DESC static_sampler_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC resource_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES upload_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC texture_desc = {};
  D3D12_RESOURCE_DESC buffer_desc = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  void *upload_data = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE resource_cpu = {};
  D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu = {};
  D3D12_GPU_DESCRIPTOR_HANDLE resource_gpu = {};
  ID3D12DescriptorHeap *heaps[2] = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  D3D12_SAMPLER_DESC sampler_desc = {};
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_TEXTURE_COPY_LOCATION texture_dst = {};
  D3D12_TEXTURE_COPY_LOCATION texture_src = {};

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    goto cleanup;
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    goto cleanup;
  if (!CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
    goto cleanup;

  if (direct_indexed) {
    versioned_root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned_root_desc.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
  } else {
    ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    ranges[1] = {static_sampler ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0};
    if (!static_sampler)
      ranges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
    root_parameter_count = static_sampler ? 2 : 3;
    for (unsigned i = 0; i < root_parameter_count; i++) {
      root_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[i].DescriptorTable.NumDescriptorRanges = 1;
      root_parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    root_desc.NumParameters = root_parameter_count;
    root_desc.pParameters = root_parameters;
  }
  if (static_sampler) {
    static_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    static_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_sampler_desc.MaxAnisotropy = 1;
    static_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    static_sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    static_sampler_desc.MinLOD = 0;
    static_sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    static_sampler_desc.ShaderRegister = 0;
    static_sampler_desc.RegisterSpace = 0;
    static_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_desc.NumStaticSamplers = 1;
    root_desc.pStaticSamplers = &static_sampler_desc;
  }
  serialize_hr = direct_indexed
                     ? D3D12SerializeVersionedRootSignature(&versioned_root_desc, &root_blob, &root_error)
                     : D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error);
  if (!CheckHR("D3D12SerializeRootSignature", serialize_hr))
    goto cleanup;
  if (!CheckHR(
          "CreateRootSignature",
          device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                       IID_PPV_ARGS(&root_signature))))
    goto cleanup;

  resource_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  resource_heap_desc.NumDescriptors = 2;
  resource_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  sampler_heap_desc.NumDescriptors = 1;
  sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (!CheckHR("CreateResourceHeap", device->CreateDescriptorHeap(&resource_heap_desc, IID_PPV_ARGS(&resource_heap))))
    goto cleanup;
  if (!static_sampler &&
      !CheckHR("CreateSamplerHeap", device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&sampler_heap))))
    goto cleanup;
  descriptor_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = 1;
  texture_desc.Height = 1;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  if (!CheckHR(
          "CreateTexture",
          device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))))
    goto cleanup;

  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_heap.CreationNodeMask = 1;
  upload_heap.VisibleNodeMask = 1;
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = 256;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!CheckHR(
          "CreateUploadBuffer",
          device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
    goto cleanup;
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size);
  if (!CheckHR("MapUpload", upload->Map(0, nullptr, &upload_data)))
    goto cleanup;
  static const UINT pixel = 0xff0000ff;
  memcpy(upload_data, &pixel, sizeof(pixel));
  upload->Unmap(0, nullptr);

  srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Texture2D.MipLevels = 1;
  resource_cpu = resource_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateShaderResourceView(texture, &srv_desc, resource_cpu);

  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 64;
  uav_desc.Buffer.StructureByteStride = sizeof(UINT);
  uav_cpu = resource_cpu;
  uav_cpu.ptr += descriptor_increment;
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!CheckHR(
          "CreateOutputBuffer",
          device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output))))
    goto cleanup;
  device->CreateUnorderedAccessView(output, nullptr, &uav_desc, uav_cpu);

  if (!static_sampler) {
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    device->CreateSampler(&sampler_desc, sampler_heap->GetCPUDescriptorHandleForHeapStart());
  }

  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  if (!CheckHR(
          "CreateReadbackBuffer",
          device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))))
    goto cleanup;

  pso_desc.pRootSignature = root_signature;
  pso_desc.CS.pShaderBytecode = shader.data();
  pso_desc.CS.BytecodeLength = shader.size();
  if (!CheckHR("CreateComputePipelineState", device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    goto cleanup;
  if (!CheckHR(
          "CreateCommandList",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso, IID_PPV_ARGS(&list))))
    goto cleanup;

  texture_dst.pResource = texture;
  texture_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  texture_src.pResource = upload;
  texture_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  texture_src.PlacedFootprint = footprint;
  list->CopyTextureRegion(&texture_dst, 0, 0, 0, &texture_src, nullptr);
  heaps[0] = resource_heap;
  if (!static_sampler)
    heaps[1] = sampler_heap;
  list->SetDescriptorHeaps(static_sampler ? 1 : 2, heaps);
  list->SetComputeRootSignature(root_signature);
  if (!direct_indexed) {
    list->SetComputeRootDescriptorTable(0, resource_heap->GetGPUDescriptorHandleForHeapStart());
    resource_gpu = resource_heap->GetGPUDescriptorHandleForHeapStart();
    resource_gpu.ptr += descriptor_increment;
    if (static_sampler) {
      list->SetComputeRootDescriptorTable(1, resource_gpu);
    } else {
      list->SetComputeRootDescriptorTable(1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
      list->SetComputeRootDescriptorTable(2, resource_gpu);
    }
  }
  list->Dispatch(1, 1, 1);
  list->CopyBufferRegion(readback, 0, output, 0, sizeof(UINT));
  if (!CheckHR("Close", list->Close()))
    goto cleanup;
  lists[0] = list;
  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
    goto cleanup;
  if (!CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event || !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  WaitForSingleObject(event, INFINITE);
  if (!CheckHR("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    goto cleanup;
  value = *mapped;
  readback->Unmap(0, nullptr);
  if (value != 255) {
    std::cerr << "texture sampler readback mismatch: " << value << "\n";
    goto cleanup;
  }
  std::cout << "DXIL " << (direct_indexed ? "direct indexed" : static_sampler ? "static" : "dynamic")
            << " texture sampler readback passed: " << value << "\n";
  result = 0;

cleanup:
  if (event)
    CloseHandle(event);
  if (fence)
    fence->Release();
  if (list)
    list->Release();
  if (pso)
    pso->Release();
  if (readback)
    readback->Release();
  if (output)
    output->Release();
  if (upload)
    upload->Release();
  if (texture)
    texture->Release();
  if (sampler_heap)
    sampler_heap->Release();
  if (resource_heap)
    resource_heap->Release();
  if (root_signature)
    root_signature->Release();
  if (root_blob)
    root_blob->Release();
  if (root_error)
    root_error->Release();
  if (allocator)
    allocator->Release();
  if (queue)
    queue->Release();
  if (device)
    device->Release();
  return result;
}

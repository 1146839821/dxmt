#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

bool
CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

} // namespace

int
main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: dx12_compute_sm6 <shader.cso> [--root-uav|--descriptor-uav]\n";
    return 2;
  }
  const bool root_uav = argc == 3 && strcmp(argv[2], "--root-uav") == 0;
  const bool descriptor_uav = argc == 3 && strcmp(argv[2], "--descriptor-uav") == 0;
  if (argc == 3 && !root_uav && !descriptor_uav) {
    std::cerr << "unknown test mode\n";
    return 2;
  }

  std::ifstream shader_file(argv[1], std::ios::binary | std::ios::ate);
  if (!shader_file) {
    std::cerr << "failed to open shader\n";
    return 3;
  }
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
  ID3D12DescriptorHeap *descriptor_heap = nullptr;
  ID3D12Resource *output_buffer = nullptr;
  ID3D12Resource *readback_buffer = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  ID3D12CommandList *lists[1] = {};
  UINT *mapped = nullptr;
  UINT output_value = 0;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_ROOT_PARAMETER root_parameter = {};
  D3D12_DESCRIPTOR_RANGE descriptor_range = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC output_desc = {};
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  int result = 1;

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    goto cleanup;

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    goto cleanup;
  if (!CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
    goto cleanup;

  if (root_uav || descriptor_uav) {
    if (root_uav) {
      root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
      root_parameter.Descriptor.ShaderRegister = 0;
      root_parameter.Descriptor.RegisterSpace = 0;
    } else {
      descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
      descriptor_range.NumDescriptors = 1;
      descriptor_range.BaseShaderRegister = 0;
      descriptor_range.RegisterSpace = 0;
      descriptor_range.OffsetInDescriptorsFromTableStart = 0;
      root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameter.DescriptorTable.NumDescriptorRanges = 1;
      root_parameter.DescriptorTable.pDescriptorRanges = &descriptor_range;
    }
    root_desc.NumParameters = 1;
    root_desc.pParameters = &root_parameter;
    if (!CheckHR(
            "D3D12SerializeRootSignature",
            D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error)))
      goto cleanup;
    if (!CheckHR(
            "CreateRootSignature",
            device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                         IID_PPV_ARGS(&root_signature))))
      goto cleanup;

    if (descriptor_uav) {
      D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
      heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      heap_desc.NumDescriptors = 1;
      heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      if (!CheckHR("CreateDescriptorHeap", device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&descriptor_heap))))
        goto cleanup;
    }

    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    default_heap.CreationNodeMask = 1;
    default_heap.VisibleNodeMask = 1;
    output_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    output_desc.Width = 256;
    output_desc.Height = 1;
    output_desc.DepthOrArraySize = 1;
    output_desc.MipLevels = 1;
    output_desc.SampleDesc.Count = 1;
    output_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    output_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!CheckHR(
            "CreateOutputBuffer",
            device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                             IID_PPV_ARGS(&output_buffer))))
      goto cleanup;

    if (descriptor_uav) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
      uav_desc.Format = DXGI_FORMAT_UNKNOWN;
      uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      uav_desc.Buffer.NumElements = 64;
      uav_desc.Buffer.StructureByteStride = sizeof(UINT);
      device->CreateUnorderedAccessView(
          output_buffer, nullptr, &uav_desc, descriptor_heap->GetCPUDescriptorHandleForHeapStart()
      );
    }

    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    readback_heap.CreationNodeMask = 1;
    readback_heap.VisibleNodeMask = 1;
    output_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (!CheckHR(
            "CreateReadbackBuffer",
            device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&readback_buffer))))
      goto cleanup;
  }

  pso_desc.pRootSignature = root_signature;
  pso_desc.CS.pShaderBytecode = shader.data();
  pso_desc.CS.BytecodeLength = shader.size();
  if (!CheckHR("CreateComputePipelineState", device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    goto cleanup;
  if (!CheckHR(
          "CreateCommandList",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso, IID_PPV_ARGS(&list))))
    goto cleanup;

  if (root_uav || descriptor_uav) {
    list->SetComputeRootSignature(root_signature);
    if (root_uav) {
      list->SetComputeRootUnorderedAccessView(0, output_buffer->GetGPUVirtualAddress());
    } else {
      ID3D12DescriptorHeap *heaps[] = {descriptor_heap};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootDescriptorTable(0, descriptor_heap->GetGPUDescriptorHandleForHeapStart());
    }
  }
  list->Dispatch(1, 1, 1);
  if (root_uav || descriptor_uav)
    list->CopyBufferRegion(readback_buffer, 0, output_buffer, 0, sizeof(UINT));
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

  if (root_uav || descriptor_uav) {
    if (!CheckHR("MapReadback", readback_buffer->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
      goto cleanup;
    output_value = *mapped;
    readback_buffer->Unmap(0, nullptr);
    if (output_value != 1234) {
      std::cerr << "root UAV readback mismatch: " << output_value << "\n";
      goto cleanup;
    }
    std::cout << "DXIL cs_6_0 root UAV readback passed: " << output_value << "\n";
  } else {
    std::cout << "DXIL cs_6_0 no-resource Dispatch passed\n";
  }
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
  if (readback_buffer)
    readback_buffer->Release();
  if (output_buffer)
    output_buffer->Release();
  if (descriptor_heap)
    descriptor_heap->Release();
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

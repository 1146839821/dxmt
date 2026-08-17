#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

bool CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex
              << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: dx12_compute_sm6 <shader.cso> "
                 "[--root-uav|--descriptor-uav|--descriptor-resources|--"
                 "root-cbv|--root-constants|--root-srv]\n";
    return 2;
  }
  const bool root_uav = argc == 3 && strcmp(argv[2], "--root-uav") == 0;
  const bool descriptor_uav =
      argc == 3 && strcmp(argv[2], "--descriptor-uav") == 0;
  const bool descriptor_resources =
      argc == 3 && strcmp(argv[2], "--descriptor-resources") == 0;
  const bool root_cbv = argc == 3 && strcmp(argv[2], "--root-cbv") == 0;
  const bool root_constants =
      argc == 3 && strcmp(argv[2], "--root-constants") == 0;
  const bool root_srv = argc == 3 && strcmp(argv[2], "--root-srv") == 0;
  if (argc == 3 && !root_uav && !descriptor_uav && !descriptor_resources &&
      !root_cbv && !root_constants && !root_srv) {
    std::cerr << "unknown test mode\n";
    return 2;
  }
  const bool needs_root_signature = root_uav || descriptor_uav ||
                                    descriptor_resources || root_cbv ||
                                    root_constants || root_srv;
  const bool needs_output = needs_root_signature;

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
  ID3D12Resource *input_buffer = nullptr;
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
  D3D12_ROOT_PARAMETER root_parameters[2] = {};
  D3D12_DESCRIPTOR_RANGE descriptor_ranges[3] = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES upload_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC output_desc = {};
  D3D12_RESOURCE_DESC input_desc = {};
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu = {};
  UINT descriptor_increment = 0;
  UINT input_value = 777;
  void *mapped_input = nullptr;
  int result = 1;

  if (!CheckHR("D3D12CreateDevice",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device))))
    goto cleanup;

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue",
               device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    goto cleanup;
  if (!CheckHR("CreateCommandAllocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator))))
    goto cleanup;

  if (needs_root_signature) {
    if (root_cbv || root_constants || root_srv) {
      if (root_cbv) {
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_parameters[0].Descriptor.ShaderRegister = 0;
        root_parameters[0].Descriptor.RegisterSpace = 0;
      } else if (root_srv) {
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        root_parameters[0].Descriptor.ShaderRegister = 0;
        root_parameters[0].Descriptor.RegisterSpace = 0;
      } else {
        root_parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[0].Constants.ShaderRegister = 0;
        root_parameters[0].Constants.RegisterSpace = 0;
        root_parameters[0].Constants.Num32BitValues = 1;
      }
      root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
      root_parameters[1].Descriptor.ShaderRegister = 0;
      root_parameters[1].Descriptor.RegisterSpace = 0;
      root_desc.NumParameters = 2;
      root_desc.pParameters = root_parameters;
    } else if (descriptor_resources) {
      descriptor_ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, 0};
      descriptor_ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 1};
      descriptor_ranges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 2};
      root_parameters[0].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[0].DescriptorTable.NumDescriptorRanges = 3;
      root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
      root_desc.NumParameters = 1;
      root_desc.pParameters = root_parameters;
    } else {
      if (root_uav) {
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        root_parameters[0].Descriptor.ShaderRegister = 0;
        root_parameters[0].Descriptor.RegisterSpace = 0;
      } else {
        descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptor_ranges[0].NumDescriptors = 1;
        descriptor_ranges[0].BaseShaderRegister = 0;
        descriptor_ranges[0].RegisterSpace = 0;
        descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
        root_parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
        root_parameters[0].DescriptorTable.pDescriptorRanges =
            descriptor_ranges;
      }
      root_desc.NumParameters = 1;
      root_desc.pParameters = root_parameters;
    }
    if (!CheckHR("D3D12SerializeRootSignature",
                 D3D12SerializeRootSignature(&root_desc,
                                             D3D_ROOT_SIGNATURE_VERSION_1,
                                             &root_blob, &root_error)))
      goto cleanup;
    if (!CheckHR("CreateRootSignature",
                 device->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                             root_blob->GetBufferSize(),
                                             IID_PPV_ARGS(&root_signature))))
      goto cleanup;

    if (descriptor_uav || descriptor_resources) {
      D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
      heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      heap_desc.NumDescriptors = descriptor_resources ? 3 : 1;
      heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      if (!CheckHR("CreateDescriptorHeap",
                   device->CreateDescriptorHeap(
                       &heap_desc, IID_PPV_ARGS(&descriptor_heap))))
        goto cleanup;
      descriptor_increment = device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    if (root_cbv || root_constants || root_srv || descriptor_resources) {
      upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
      upload_heap.CreationNodeMask = 1;
      upload_heap.VisibleNodeMask = 1;
      input_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      input_desc.Width = 256;
      input_desc.Height = 1;
      input_desc.DepthOrArraySize = 1;
      input_desc.MipLevels = 1;
      input_desc.SampleDesc.Count = 1;
      input_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      if (!CheckHR("CreateInputBuffer",
                   device->CreateCommittedResource(
                       &upload_heap, D3D12_HEAP_FLAG_NONE, &input_desc,
                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                       IID_PPV_ARGS(&input_buffer))))
        goto cleanup;
      if (!CheckHR("MapInputBuffer",
                   input_buffer->Map(0, nullptr, &mapped_input)))
        goto cleanup;
      memcpy(mapped_input, &input_value, sizeof(input_value));
      input_buffer->Unmap(0, nullptr);

      if (descriptor_resources) {
        descriptor_cpu = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
        cbv_desc.BufferLocation = input_buffer->GetGPUVirtualAddress();
        cbv_desc.SizeInBytes = 256;
        device->CreateConstantBufferView(&cbv_desc, descriptor_cpu);

        descriptor_cpu.ptr += descriptor_increment;
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.NumElements = 64;
        srv_desc.Buffer.StructureByteStride = sizeof(UINT);
        device->CreateShaderResourceView(input_buffer, &srv_desc,
                                         descriptor_cpu);
      }
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
    if (!CheckHR("CreateOutputBuffer",
                 device->CreateCommittedResource(
                     &default_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                     IID_PPV_ARGS(&output_buffer))))
      goto cleanup;

    if (descriptor_uav || descriptor_resources) {
      uav_desc.Format = DXGI_FORMAT_UNKNOWN;
      uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      uav_desc.Buffer.NumElements = 64;
      uav_desc.Buffer.StructureByteStride = sizeof(UINT);
      descriptor_cpu = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
      if (descriptor_resources)
        descriptor_cpu.ptr += descriptor_increment * 2;
      device->CreateUnorderedAccessView(output_buffer, nullptr, &uav_desc,
                                        descriptor_cpu);
    }

    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    readback_heap.CreationNodeMask = 1;
    readback_heap.VisibleNodeMask = 1;
    output_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (!CheckHR("CreateReadbackBuffer",
                 device->CreateCommittedResource(
                     &readback_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                     IID_PPV_ARGS(&readback_buffer))))
      goto cleanup;
  }

  pso_desc.pRootSignature = root_signature;
  pso_desc.CS.pShaderBytecode = shader.data();
  pso_desc.CS.BytecodeLength = shader.size();
  if (!CheckHR(
          "CreateComputePipelineState",
          device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    goto cleanup;
  if (!CheckHR("CreateCommandList",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator, pso, IID_PPV_ARGS(&list))))
    goto cleanup;

  if (needs_root_signature) {
    list->SetComputeRootSignature(root_signature);
    if (root_cbv) {
      list->SetComputeRootConstantBufferView(
          0, input_buffer->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(
          1, output_buffer->GetGPUVirtualAddress());
    } else if (root_constants) {
      list->SetComputeRoot32BitConstants(0, 1, &input_value, 0);
      list->SetComputeRootUnorderedAccessView(
          1, output_buffer->GetGPUVirtualAddress());
    } else if (root_srv) {
      list->SetComputeRootShaderResourceView(
          0, input_buffer->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(
          1, output_buffer->GetGPUVirtualAddress());
    } else if (descriptor_resources) {
      ID3D12DescriptorHeap *heaps[] = {descriptor_heap};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootDescriptorTable(
          0, descriptor_heap->GetGPUDescriptorHandleForHeapStart());
    } else if (root_uav) {
      list->SetComputeRootUnorderedAccessView(
          0, output_buffer->GetGPUVirtualAddress());
    } else {
      ID3D12DescriptorHeap *heaps[] = {descriptor_heap};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootDescriptorTable(
          0, descriptor_heap->GetGPUDescriptorHandleForHeapStart());
    }
  }
  list->Dispatch(1, 1, 1);
  if (needs_output)
    list->CopyBufferRegion(readback_buffer, 0, output_buffer, 0, sizeof(UINT));
  if (!CheckHR("Close", list->Close()))
    goto cleanup;

  lists[0] = list;
  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                  IID_PPV_ARGS(&fence))))
    goto cleanup;
  if (!CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event ||
      !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  WaitForSingleObject(event, INFINITE);

  if (needs_output) {
    if (!CheckHR("MapReadback",
                 readback_buffer->Map(0, nullptr,
                                      reinterpret_cast<void **>(&mapped))))
      goto cleanup;
    output_value = *mapped;
    readback_buffer->Unmap(0, nullptr);
    const UINT expected_value = descriptor_resources ? input_value * 2
                                : root_cbv || root_constants || root_srv
                                    ? input_value
                                    : 1234;
    if (output_value != expected_value) {
      std::cerr << "root parameter readback mismatch: " << output_value << "\n";
      goto cleanup;
    }
    std::cout << "DXIL cs_6_0 "
              << (root_cbv               ? "root CBV"
                  : root_constants       ? "root constants"
                  : root_srv             ? "root SRV"
                  : descriptor_resources ? "descriptor resources"
                                         : "root UAV")
              << " readback passed: " << output_value << "\n";
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
  if (input_buffer)
    input_buffer->Release();
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

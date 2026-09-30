#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <array>
#include <cstdint>
#include <cstring>
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

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = type;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC BufferDescription(UINT64 size) {
  D3D12_RESOURCE_DESC description = {};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Width = size;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return description;
}

} // namespace

int main() {
  static constexpr char source[] = R"HLSL(
RWStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID) {
  int scalar = -65536;
  int2 value2 = int2(-8, 8);
  int3 value3 = int3(-1, 0, 1);
  int4 value4 = int4(0x80000000, -65536, 65536, 0x7fffffff);
  int2 result2 = firstbithigh(value2);
  int3 result3 = firstbithigh(value3);
  int4 result4 = firstbithigh(value4);
  output[0] = asuint(firstbithigh(scalar));
  output[1] = asuint(result2.x);
  output[2] = asuint(result2.y);
  output[3] = asuint(result3.x);
  output[4] = asuint(result3.y);
  output[5] = asuint(result3.z);
  output[6] = asuint(result4.x);
  output[7] = asuint(result4.y);
  output[8] = asuint(result4.z);
  output[9] = asuint(result4.w);
}
)HLSL";
  static constexpr std::array<UINT, 10> expected = {
      15u, 2u, 3u, 0xffffffffu, 0xffffffffu, 0u, 30u, 15u, 16u, 30u,
  };

  HMODULE compiler = LoadLibraryA(D3DCOMPILER_DLL_A);
  if (!compiler) {
    std::cerr << "failed to load d3dcompiler_47.dll\n";
    return 1;
  }
  auto compile_shader = reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile_shader) {
    FreeLibrary(compiler);
    std::cerr << "failed to load D3DCompile\n";
    return 1;
  }
  ID3DBlob *shader_blob = nullptr;
  ID3DBlob *shader_errors = nullptr;
  HRESULT hr = compile_shader(
      source, std::strlen(source), "dx12_airconv_firstbit.hlsl", nullptr, nullptr, "cs_main", "cs_5_0",
      D3DCOMPILE_ENABLE_STRICTNESS, 0, &shader_blob, &shader_errors
  );
  if (FAILED(hr)) {
    if (shader_errors)
      std::cerr << static_cast<const char *>(shader_errors->GetBufferPointer()) << "\n";
    Release(shader_errors);
    Release(shader_blob);
    FreeLibrary(compiler);
    return 1;
  }
  Release(shader_errors);

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_errors = nullptr;
  ID3D12DescriptorHeap *descriptor_heap = nullptr;
  ID3D12Resource *output = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  UINT *mapped = nullptr;
  bool passed = false;
  D3D12_DESCRIPTOR_RANGE range = {};
  D3D12_ROOT_PARAMETER parameter = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC buffer_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc = {};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  ID3D12DescriptorHeap *heaps[1] = {};
  D3D12_RESOURCE_BARRIER barrier = {};
  ID3D12CommandList *command_lists[1] = {};

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    goto cleanup;

  range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  if (!CheckHR("D3D12SerializeRootSignature", D3D12SerializeRootSignature(
                                                   &root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                   &root_blob, &root_errors)))
    goto cleanup;
  if (!CheckHR("CreateRootSignature", device->CreateRootSignature(
                                       0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                       IID_PPV_ARGS(&root_signature))))
    goto cleanup;

  default_heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  readback_heap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
  buffer_desc = BufferDescription(sizeof(UINT) * expected.size());
  if (!CheckHR("CreateOutput", device->CreateCommittedResource(
                                  &default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output))) ||
      !CheckHR("CreateReadback", device->CreateCommittedResource(
                                    &readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))))
    goto cleanup;

  descriptor_desc = {};
  descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  descriptor_desc.NumDescriptors = 1;
  descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (!CheckHR("CreateDescriptorHeap", device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&descriptor_heap))))
    goto cleanup;
  uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = expected.size();
  uav_desc.Buffer.StructureByteStride = sizeof(UINT);
  device->CreateUnorderedAccessView(output, nullptr, &uav_desc, descriptor_heap->GetCPUDescriptorHandleForHeapStart());

  pipeline_desc = {};
  pipeline_desc.pRootSignature = root_signature;
  pipeline_desc.CS = {shader_blob->GetBufferPointer(), shader_blob->GetBufferSize()};
  if (!CheckHR("CreateComputePipelineState", device->CreateComputePipelineState(
                                                 &pipeline_desc, IID_PPV_ARGS(&pso))) ||
      !CheckHR("CreateCommandQueue", (queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))) ||
      !CheckHR("CreateCommandAllocator", device->CreateCommandAllocator(
                                                   D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
      !CheckHR("CreateCommandList", device->CreateCommandList(
                                               0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                               IID_PPV_ARGS(&list))))
    goto cleanup;

  list->SetComputeRootSignature(root_signature);
  heaps[0] = descriptor_heap;
  list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootDescriptorTable(0, descriptor_heap->GetGPUDescriptorHandleForHeapStart());
  list->Dispatch(1, 1, 1);
  barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = output;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
  list->CopyBufferRegion(readback, 0, output, 0, sizeof(UINT) * expected.size());
  if (!CheckHR("Close", list->Close()))
    goto cleanup;
  command_lists[0] = list;
  queue->ExecuteCommandLists(1, command_lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
      !CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event || !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  if (WaitForSingleObject(event, 30000) != WAIT_OBJECT_0)
    goto cleanup;
  if (!CheckHR("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    goto cleanup;
  passed = std::memcmp(mapped, expected.data(), sizeof(expected)) == 0;
  if (!passed) {
    std::cerr << "firstbit_shi results mismatch:";
    for (size_t i = 0; i < expected.size(); i++)
      std::cerr << " 0x" << std::hex << mapped[i];
    std::cerr << std::dec << "\n";
  }

cleanup:
  if (mapped)
    readback->Unmap(0, nullptr);
  if (event)
    CloseHandle(event);
  Release(fence);
  Release(pso);
  Release(readback);
  Release(output);
  Release(descriptor_heap);
  Release(list);
  Release(allocator);
  Release(queue);
  Release(root_errors);
  Release(root_blob);
  Release(root_signature);
  Release(device);
  Release(shader_errors);
  Release(shader_blob);
  FreeLibrary(compiler);
  std::cout << (passed ? "AIRCONV firstbit_shi vector test passed\n" : "AIRCONV firstbit_shi vector test failed\n");
  return passed ? 0 : 1;
}

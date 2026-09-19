#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

template <typename T> void
Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

bool
CheckHR(const char *name, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;
  std::cerr << name << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
  return false;
}

D3D12_HEAP_PROPERTIES
HeapProperties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = type;
  properties.CreationNodeMask = 1;
  properties.VisibleNodeMask = 1;
  return properties;
}

D3D12_RESOURCE_DESC
BufferDescription(UINT64 size) {
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

bool
WaitForQueue(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12CommandList *list) {
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  ID3D12CommandList *lists[] = {list};
  bool result = false;

  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
      !CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event || !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  result = WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0;

cleanup:
  if (event)
    CloseHandle(event);
  Release(fence);
  return result;
}

} // namespace

int
main() {
  static constexpr char shader_source[] = R"(
struct VSInput {
  float3 position : POSITION;
  float4 color : COLOR;
};

struct VSOutput {
  float4 position : SV_Position;
  float4 color : COLOR;
};

VSOutput main(VSInput input) {
  VSOutput output;
  output.position = float4(input.position, 1.0);
  output.color = input.color;
  return output;
}
)";

  struct Vertex {
    float position[3];
    float color[4];
  };
  static const Vertex vertices[] = {
      {{1.0f, 2.0f, 3.0f}, {10.0f, 20.0f, 30.0f, 40.0f}},
      {{4.0f, 5.0f, 6.0f}, {50.0f, 60.0f, 70.0f, 80.0f}},
      {{7.0f, 8.0f, 9.0f}, {90.0f, 100.0f, 110.0f, 120.0f}},
  };
  static const float expected_output[] = {
      1.0f, 2.0f, 3.0f, 20.0f, 30.0f, 40.0f,
      4.0f, 5.0f, 6.0f, 60.0f, 70.0f, 80.0f,
      7.0f, 8.0f, 9.0f, 100.0f, 110.0f, 120.0f,
  };

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12Resource *vertex_buffer = nullptr;
  ID3D12Resource *output_buffer = nullptr;
  ID3D12Resource *counter_buffer = nullptr;
  ID3D12Resource *counter_upload = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3DBlob *shader = nullptr;
  ID3DBlob *shader_errors = nullptr;
  HMODULE compiler = nullptr;
  void *mapped_vertex = nullptr;
  void *mapped_counter = nullptr;
  BYTE *mapped_readback = nullptr;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_INPUT_ELEMENT_DESC input_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  D3D12_SO_DECLARATION_ENTRY so_declaration[] = {
      {0, "SV_Position", 0, 0, 3, 0},
      {0, "COLOR", 0, 1, 3, 0},
  };
  UINT so_stride = 6 * sizeof(float);
  D3D12_STREAM_OUTPUT_BUFFER_VIEW so_view = {};
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  D3D12_RESOURCE_BARRIER barriers[2] = {};
  D3D12_VIEWPORT viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  D3D12_RECT scissor = {0, 0, 1, 1};
  D3D12_HEAP_PROPERTIES default_heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  D3D12_HEAP_PROPERTIES upload_heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  D3D12_HEAP_PROPERTIES readback_heap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
  D3D12_RESOURCE_DESC vertex_desc = BufferDescription(sizeof(vertices));
  D3D12_RESOURCE_DESC output_desc = BufferDescription(256);
  D3D12_RESOURCE_DESC counter_desc = BufferDescription(256);
  D3D12_RESOURCE_DESC readback_desc = BufferDescription(512);
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  const UINT output_offset = 16;
  const UINT output_size = sizeof(expected_output);
  const UINT counter_offset = sizeof(UINT);
  int result = 1;

  auto cleanup = [&] {
    if (mapped_readback)
      readback->Unmap(0, nullptr);
    if (mapped_counter)
      counter_upload->Unmap(0, nullptr);
    if (mapped_vertex)
      vertex_buffer->Unmap(0, nullptr);
    Release(shader_errors);
    Release(shader);
    if (compiler)
      FreeLibrary(compiler);
    Release(readback);
    Release(counter_upload);
    Release(counter_buffer);
    Release(output_buffer);
    Release(vertex_buffer);
    Release(pso);
    Release(list);
    Release(allocator);
    Release(queue);
    Release(device);
  };
  auto fail = [&](const char *message) {
    std::cerr << message << "\n";
    cleanup();
    return result;
  };

  compiler = LoadLibraryA(D3DCOMPILER_DLL_A);
  if (!compiler)
    return fail("failed to load d3dcompiler_47.dll");
  auto compile_shader = reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile_shader)
    return fail("failed to load D3DCompile");
  if (!CheckHR(
          "D3DCompile",
          compile_shader(
              shader_source, sizeof(shader_source) - 1, "dx12_stream_output.hlsl", nullptr, nullptr, "main", "vs_5_0",
              D3DCOMPILE_ENABLE_STRICTNESS, 0, &shader, &shader_errors
          )
      )) {
    if (shader_errors)
      std::cerr << static_cast<const char *>(shader_errors->GetBufferPointer()) << "\n";
    return fail("SM5 vertex shader compilation failed");
  }
  Release(shader_errors);

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return fail("device creation failed");
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))
      ))
    return fail("queue setup failed");

  pso_desc.VS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  pso_desc.InputLayout = {input_layout, 2};
  pso_desc.StreamOutput = {so_declaration, 2, &so_stride, 1, D3D12_SO_NO_RASTERIZED_STREAM};
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  if (!CheckHR("CreateGraphicsPipelineState", device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    return fail("Stream Output PSO creation failed");

  if (!CheckHR(
          "CreateVertexBuffer",
          device->CreateCommittedResource(
              &upload_heap, D3D12_HEAP_FLAG_NONE, &vertex_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
              IID_PPV_ARGS(&vertex_buffer)
          )
      ) ||
      !CheckHR("MapVertexBuffer", vertex_buffer->Map(0, nullptr, &mapped_vertex)))
    return fail("vertex buffer setup failed");
  memcpy(mapped_vertex, vertices, sizeof(vertices));
  vertex_buffer->Unmap(0, nullptr);
  mapped_vertex = nullptr;
  vertex_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = sizeof(vertices);
  vertex_view.StrideInBytes = sizeof(Vertex);

  if (!CheckHR(
          "CreateOutputBuffer",
          device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &output_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&output_buffer)
          )
      ) ||
      !CheckHR(
          "CreateCounterBuffer",
          device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &counter_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&counter_buffer)
          )
      ) ||
      !CheckHR(
          "CreateCounterUpload",
          device->CreateCommittedResource(
              &upload_heap, D3D12_HEAP_FLAG_NONE, &counter_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
              IID_PPV_ARGS(&counter_upload)
          )
      ) ||
      !CheckHR("MapCounterUpload", counter_upload->Map(0, nullptr, &mapped_counter)))
    return fail("Stream Output buffer setup failed");
  memset(mapped_counter, 0, counter_desc.Width);
  counter_upload->Unmap(0, nullptr);
  mapped_counter = nullptr;

  if (!CheckHR(
          "CreateReadback",
          device->CreateCommittedResource(
              &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&readback)
          )
      ) ||
      !CheckHR(
          "CreateCommandList",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso, IID_PPV_ARGS(&list))
      ))
      return fail("command list setup failed");

  list->CopyBufferRegion(counter_buffer, 0, counter_upload, 0, counter_desc.Width);
  barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[0].Transition.pResource = output_buffer;
  barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_STREAM_OUT;
  barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[1].Transition.pResource = counter_buffer;
  barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_STREAM_OUT;
  barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(2, barriers);

  so_view.BufferLocation = output_buffer->GetGPUVirtualAddress() + output_offset;
  so_view.SizeInBytes = output_size;
  so_view.BufferFilledSizeLocation = counter_buffer->GetGPUVirtualAddress() + counter_offset;
  list->SetPipelineState(pso);
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
  list->IASetVertexBuffers(0, 1, &vertex_view);
  list->SOSetTargets(0, 1, &so_view);
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  list->DrawInstanced(1, 1, 0, 0);
  list->DrawInstanced(2, 1, 1, 0);

  barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_STREAM_OUT;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_STREAM_OUT;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(2, barriers);
  list->CopyBufferRegion(readback, 0, output_buffer, output_offset, output_size);
  list->CopyBufferRegion(readback, 256, counter_buffer, counter_offset, sizeof(UINT));
  if (!CheckHR("Close", list->Close()) || !WaitForQueue(device, queue, list))
    return fail("Stream Output command execution failed");

  if (!CheckHR("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_readback))))
    return fail("readback mapping failed");
  auto *actual_output = reinterpret_cast<const float *>(mapped_readback);
  auto actual_counter = *reinterpret_cast<const UINT *>(mapped_readback + 256);
  if (actual_counter != output_size || memcmp(actual_output, expected_output, output_size) != 0)
    return fail("Stream Output readback mismatch");

  std::cout << "DXBC VS Stream Output readback passed: " << actual_counter << " bytes\n";
  result = 0;
  cleanup();
  return result;
}

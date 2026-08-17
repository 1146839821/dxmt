#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstring>
#include <fstream>
#include <iomanip>
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
  if (argc != 3)
    return 2;

  std::ifstream vertex_file(argv[1], std::ios::binary | std::ios::ate);
  std::ifstream pixel_file(argv[2], std::ios::binary | std::ios::ate);
  if (!vertex_file || !pixel_file)
    return 3;
  auto vertex_size = vertex_file.tellg();
  auto pixel_size = pixel_file.tellg();
  vertex_file.seekg(0);
  pixel_file.seekg(0);
  std::vector<char> vertex_shader(static_cast<size_t>(vertex_size));
  std::vector<char> pixel_shader(static_cast<size_t>(pixel_size));
  vertex_file.read(vertex_shader.data(), vertex_shader.size());
  pixel_file.read(pixel_shader.data(), pixel_shader.size());

  struct Vertex {
    float position[2];
    float color[4];
  };
  static const Vertex vertices[] = {
      {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{3.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{-1.0f, 3.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
  };

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  ID3D12Resource *render_target = nullptr;
  ID3D12Resource *vertex_buffer = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  ID3D12CommandList *lists[1] = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES upload_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC render_target_desc = {};
  D3D12_RESOURCE_DESC buffer_desc = {};
  D3D12_RESOURCE_DESC readback_desc = {};
  D3D12_CLEAR_VALUE clear_value = {};
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  D3D12_INPUT_ELEMENT_DESC input_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  D3D12_TEXTURE_COPY_LOCATION copy_dst = {};
  D3D12_TEXTURE_COPY_LOCATION copy_src = {};
  D3D12_RESOURCE_BARRIER barrier = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  void *mapped_upload = nullptr;
  BYTE *mapped_readback = nullptr;
  UINT pixel = 0;
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

  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;
  render_target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  render_target_desc.Width = 1;
  render_target_desc.Height = 1;
  render_target_desc.DepthOrArraySize = 1;
  render_target_desc.MipLevels = 1;
  render_target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  render_target_desc.SampleDesc.Count = 1;
  render_target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  clear_value.Format = render_target_desc.Format;
  clear_value.Color[0] = 0.0f;
  clear_value.Color[1] = 0.0f;
  clear_value.Color[2] = 0.0f;
  clear_value.Color[3] = 1.0f;
  if (!CheckHR("CreateRenderTarget",
               device->CreateCommittedResource(
                   &default_heap, D3D12_HEAP_FLAG_NONE, &render_target_desc,
                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
                   IID_PPV_ARGS(&render_target))))
    goto cleanup;

  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  if (!CheckHR("CreateRTVHeap", device->CreateDescriptorHeap(
                                    &rtv_heap_desc, IID_PPV_ARGS(&rtv_heap))))
    goto cleanup;
  rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(render_target, nullptr, rtv_handle);

  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_heap.CreationNodeMask = 1;
  upload_heap.VisibleNodeMask = 1;
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = sizeof(vertices);
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!CheckHR("CreateVertexBuffer",
               device->CreateCommittedResource(
                   &upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                   IID_PPV_ARGS(&vertex_buffer))))
    goto cleanup;
  if (!CheckHR("MapVertexBuffer",
               vertex_buffer->Map(0, nullptr, &mapped_upload)))
    goto cleanup;
  memcpy(mapped_upload, vertices, sizeof(vertices));
  vertex_buffer->Unmap(0, nullptr);
  vertex_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = sizeof(vertices);
  vertex_view.StrideInBytes = sizeof(Vertex);

  pso_desc.pRootSignature = root_signature;
  pso_desc.VS.pShaderBytecode = vertex_shader.data();
  pso_desc.VS.BytecodeLength = vertex_shader.size();
  pso_desc.PS.pShaderBytecode = pixel_shader.data();
  pso_desc.PS.BytecodeLength = pixel_shader.size();
  pso_desc.InputLayout.pInputElementDescs = input_layout;
  pso_desc.InputLayout.NumElements = 2;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  if (!CheckHR(
          "CreateGraphicsPipelineState",
          device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    goto cleanup;

  if (!CheckHR("CreateCommandList",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator, pso, IID_PPV_ARGS(&list))))
    goto cleanup;
  list->SetPipelineState(pso);
  list->SetGraphicsRootSignature(root_signature);
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->IASetVertexBuffers(0, 1, &vertex_view);
  list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
  list->DrawInstanced(3, 1, 0, 0);

  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = render_target;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);

  device->GetCopyableFootprints(&render_target_desc, 0, 1, 0, &footprint,
                                &row_count, &row_size, &total_size);
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;
  readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readback_desc.Width = total_size;
  readback_desc.Height = 1;
  readback_desc.DepthOrArraySize = 1;
  readback_desc.MipLevels = 1;
  readback_desc.SampleDesc.Count = 1;
  readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!CheckHR("CreateReadbackBuffer",
               device->CreateCommittedResource(
                   &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                   IID_PPV_ARGS(&readback))))
    goto cleanup;
  copy_dst.pResource = readback;
  copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  copy_dst.PlacedFootprint = footprint;
  copy_src.pResource = render_target;
  copy_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, nullptr);
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

  if (!CheckHR("MapReadback",
               readback->Map(0, nullptr,
                             reinterpret_cast<void **>(&mapped_readback))))
    goto cleanup;
  pixel = *reinterpret_cast<UINT *>(mapped_readback);
  readback->Unmap(0, nullptr);
  if ((pixel & 0x00ffffffu) != 0x000000ffu) {
    std::cerr << "graphics readback mismatch: 0x" << std::hex << pixel
              << std::dec << "\n";
    goto cleanup;
  }
  std::cout << "DXIL graphics readback passed: 0x" << std::hex << pixel
            << std::dec << "\n";
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
  if (vertex_buffer)
    vertex_buffer->Release();
  if (render_target)
    render_target->Release();
  if (rtv_heap)
    rtv_heap->Release();
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

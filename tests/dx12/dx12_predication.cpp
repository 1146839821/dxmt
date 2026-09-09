#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

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
  auto size = file.tellg();
  if (size <= 0)
    return false;
  file.seekg(0);
  data.resize(static_cast<size_t>(size));
  return file.read(data.data(), size).good();
}

void Transition(
    ID3D12GraphicsCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after
) {
  if (before == after)
    return;
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: dx12_predication <vs.cso> <ps.cso> <cs.cso>\n";
    return 2;
  }

  std::vector<char> vertex_shader;
  std::vector<char> pixel_shader;
  std::vector<char> compute_shader;
  if (!ReadFile(argv[1], vertex_shader) || !ReadFile(argv[2], pixel_shader) || !ReadFile(argv[3], compute_shader)) {
    std::cerr << "failed to read shader bytecode\n";
    return 3;
  }

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3D12RootSignature *graphics_root_signature = nullptr;
  ID3D12PipelineState *graphics_pso = nullptr;
  ID3D12PipelineState *compute_pso = nullptr;
  ID3D12CommandSignature *draw_signature = nullptr;
  ID3D12CommandSignature *dispatch_signature = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  ID3D12Resource *predicate = nullptr;
  ID3D12Resource *output = nullptr;
  ID3D12Resource *output_init = nullptr;
  ID3D12Resource *output_readback = nullptr;
  ID3D12Resource *render_target = nullptr;
  ID3D12Resource *render_readback = nullptr;
  ID3D12Resource *draw_args = nullptr;
  ID3D12Resource *dispatch_args = nullptr;
  ID3D12Resource *count_buffer = nullptr;
  ID3D12Resource *index_buffer = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;

  auto cleanup = [&]() {
    if (event)
      CloseHandle(event);
    if (fence)
      fence->Release();
    if (index_buffer)
      index_buffer->Release();
    if (count_buffer)
      count_buffer->Release();
    if (dispatch_args)
      dispatch_args->Release();
    if (draw_args)
      draw_args->Release();
    if (render_readback)
      render_readback->Release();
    if (render_target)
      render_target->Release();
    if (output_readback)
      output_readback->Release();
    if (output_init)
      output_init->Release();
    if (output)
      output->Release();
    if (predicate)
      predicate->Release();
    if (rtv_heap)
      rtv_heap->Release();
    if (dispatch_signature)
      dispatch_signature->Release();
    if (draw_signature)
      draw_signature->Release();
    if (compute_pso)
      compute_pso->Release();
    if (graphics_pso)
      graphics_pso->Release();
    if (graphics_root_signature)
      graphics_root_signature->Release();
    if (root_signature)
      root_signature->Release();
    if (list)
      list->Release();
    if (allocator)
      allocator->Release();
    if (queue)
      queue->Release();
    if (device)
      device->Release();
  };

  int result = 1;
  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
    cleanup();
    return result;
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !CheckHR("CreateCommandAllocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))) {
    cleanup();
    return result;
  }

  D3D12_ROOT_PARAMETER root_parameter = {};
  root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  root_parameter.Descriptor.ShaderRegister = 0;
  root_parameter.Descriptor.RegisterSpace = 0;
  root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &root_parameter;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  if (!CheckHR("D3D12SerializeRootSignature",
               D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error)) ||
      !CheckHR("CreateRootSignature",
               device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root_signature)))) {
    if (root_error)
      root_error->Release();
    if (root_blob)
      root_blob->Release();
    cleanup();
    return result;
  }
  if (root_error)
    root_error->Release();
  root_blob->Release();

  D3D12_ROOT_SIGNATURE_DESC graphics_root_desc = {};
  ID3DBlob *graphics_root_blob = nullptr;
  ID3DBlob *graphics_root_error = nullptr;
  if (!CheckHR("D3D12SerializeGraphicsRootSignature",
               D3D12SerializeRootSignature(&graphics_root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                            &graphics_root_blob, &graphics_root_error)) ||
      !CheckHR("CreateGraphicsRootSignature",
               device->CreateRootSignature(0, graphics_root_blob->GetBufferPointer(), graphics_root_blob->GetBufferSize(),
                                            IID_PPV_ARGS(&graphics_root_signature)))) {
    if (graphics_root_error)
      graphics_root_error->Release();
    if (graphics_root_blob)
      graphics_root_blob->Release();
    cleanup();
    return result;
  }
  if (graphics_root_error)
    graphics_root_error->Release();
  graphics_root_blob->Release();

  D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc = {};
  compute_desc.pRootSignature = root_signature;
  compute_desc.CS.pShaderBytecode = compute_shader.data();
  compute_desc.CS.BytecodeLength = compute_shader.size();
  if (!CheckHR("CreateComputePipelineState", device->CreateComputePipelineState(&compute_desc, IID_PPV_ARGS(&compute_pso)))) {
    cleanup();
    return result;
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_desc = {};
  graphics_desc.pRootSignature = graphics_root_signature;
  graphics_desc.VS.pShaderBytecode = vertex_shader.data();
  graphics_desc.VS.BytecodeLength = vertex_shader.size();
  graphics_desc.PS.pShaderBytecode = pixel_shader.data();
  graphics_desc.PS.BytecodeLength = pixel_shader.size();
  graphics_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  graphics_desc.NumRenderTargets = 1;
  graphics_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  graphics_desc.SampleDesc.Count = 1;
  graphics_desc.SampleMask = UINT_MAX;
  graphics_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  graphics_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  graphics_desc.RasterizerState.DepthClipEnable = TRUE;
  graphics_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  if (!CheckHR("CreateGraphicsPipelineState",
               device->CreateGraphicsPipelineState(&graphics_desc, IID_PPV_ARGS(&graphics_pso)))) {
    cleanup();
    return result;
  }

  D3D12_HEAP_PROPERTIES upload_heap = {};
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_heap.CreationNodeMask = 1;
  upload_heap.VisibleNodeMask = 1;
  D3D12_HEAP_PROPERTIES default_heap = {};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;
  D3D12_HEAP_PROPERTIES readback_heap = {};
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC buffer_desc = {};
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = 256;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!CheckHR("CreatePredicateBuffer",
               device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&predicate)))) {
    cleanup();
    return result;
  }
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!CheckHR("CreateOutputBuffer",
               device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&output)))) {
    cleanup();
    return result;
  }
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  if (!CheckHR("CreateOutputInit",
               device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&output_init)))) {
    cleanup();
    return result;
  }
  buffer_desc.Width = 256;
  if (!CheckHR("CreateOutputReadback",
               device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&output_readback)))) {
    cleanup();
    return result;
  }

  buffer_desc.Width = sizeof(D3D12_DRAW_ARGUMENTS);
  if (!CheckHR("CreateDrawArguments",
               device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&draw_args)))) {
    cleanup();
    return result;
  }
  buffer_desc.Width = sizeof(D3D12_DISPATCH_ARGUMENTS);
  if (!CheckHR("CreateDispatchArguments",
               device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&dispatch_args)))) {
    cleanup();
    return result;
  }
  buffer_desc.Width = sizeof(UINT);
  if (!CheckHR("CreateCountBuffer",
               device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&count_buffer)))) {
    cleanup();
    return result;
  }
  buffer_desc.Width = 256;
  if (!CheckHR("CreateIndexBuffer",
               device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&index_buffer)))) {
    cleanup();
    return result;
  }

  uint64_t *predicate_data = nullptr;
  UINT *output_init_data = nullptr;
  D3D12_DRAW_ARGUMENTS draw_data = {3, 1, 0, 0};
  D3D12_DISPATCH_ARGUMENTS dispatch_data = {1, 1, 1};
  UINT count_data = 1;
  uint16_t index_data[3] = {0, 1, 2};
  void *mapped = nullptr;
  if (!CheckHR("MapPredicate", predicate->Map(0, nullptr, reinterpret_cast<void **>(&predicate_data))) ||
      !CheckHR("MapOutputInit", output_init->Map(0, nullptr, reinterpret_cast<void **>(&output_init_data))) ||
      !CheckHR("MapDrawArguments", draw_args->Map(0, nullptr, &mapped))) {
    cleanup();
    return result;
  }
  memcpy(mapped, &draw_data, sizeof(draw_data));
  draw_args->Unmap(0, nullptr);
  if (!CheckHR("MapDispatchArguments", dispatch_args->Map(0, nullptr, &mapped))) {
    cleanup();
    return result;
  }
  memcpy(mapped, &dispatch_data, sizeof(dispatch_data));
  dispatch_args->Unmap(0, nullptr);
  if (!CheckHR("MapCountBuffer", count_buffer->Map(0, nullptr, &mapped))) {
    cleanup();
    return result;
  }
  memcpy(mapped, &count_data, sizeof(count_data));
  count_buffer->Unmap(0, nullptr);
  if (!CheckHR("MapIndexBuffer", index_buffer->Map(0, nullptr, &mapped))) {
    cleanup();
    return result;
  }
  memcpy(mapped, index_data, sizeof(index_data));
  index_buffer->Unmap(0, nullptr);
  *output_init_data = 0x13572468;
  output_init->Unmap(0, nullptr);

  D3D12_RESOURCE_DESC render_desc = {};
  render_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  render_desc.Width = 1;
  render_desc.Height = 1;
  render_desc.DepthOrArraySize = 1;
  render_desc.MipLevels = 1;
  render_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  render_desc.SampleDesc.Count = 1;
  render_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_CLEAR_VALUE clear_value = {};
  clear_value.Format = render_desc.Format;
  clear_value.Color[3] = 1.0f;
  if (!CheckHR("CreateRenderTarget",
               device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &render_desc,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
                                               IID_PPV_ARGS(&render_target)))) {
    cleanup();
    return result;
  }
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  if (!CheckHR("CreateRTVHeap", device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)))) {
    cleanup();
    return result;
  }
  auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(render_target, nullptr, rtv);
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  device->GetCopyableFootprints(&render_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size);
  buffer_desc.Width = total_size;
  if (!CheckHR("CreateRenderReadback",
               device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&render_readback)))) {
    cleanup();
    return result;
  }

  D3D12_INDIRECT_ARGUMENT_DESC draw_argument = {};
  draw_argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
  D3D12_COMMAND_SIGNATURE_DESC draw_signature_desc = {};
  draw_signature_desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
  draw_signature_desc.NumArgumentDescs = 1;
  draw_signature_desc.pArgumentDescs = &draw_argument;
  if (!CheckHR("CreateDrawSignature",
               device->CreateCommandSignature(&draw_signature_desc, graphics_root_signature,
                                              IID_PPV_ARGS(&draw_signature)))) {
    cleanup();
    return result;
  }
  D3D12_INDIRECT_ARGUMENT_DESC dispatch_argument = {};
  dispatch_argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
  D3D12_COMMAND_SIGNATURE_DESC dispatch_signature_desc = {};
  dispatch_signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
  dispatch_signature_desc.NumArgumentDescs = 1;
  dispatch_signature_desc.pArgumentDescs = &dispatch_argument;
  if (!CheckHR("CreateDispatchSignature",
               device->CreateCommandSignature(&dispatch_signature_desc, root_signature,
                                              IID_PPV_ARGS(&dispatch_signature)))) {
    cleanup();
    return result;
  }

  if (!CheckHR("CreateCommandList",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                         IID_PPV_ARGS(&list))) ||
      !CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
    cleanup();
    return result;
  }
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event) {
    cleanup();
    return result;
  }

  D3D12_RESOURCE_STATES output_state = D3D12_RESOURCE_STATE_COPY_DEST;
  D3D12_RESOURCE_STATES render_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
  UINT64 fence_value = 0;
  auto submit = [&]() -> bool {
    if (!CheckHR("Close", list->Close()))
      return false;
    ID3D12CommandList *lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    if (!CheckHR("Signal", queue->Signal(fence, ++fence_value)) ||
        !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(fence_value, event)))
      return false;
    WaitForSingleObject(event, INFINITE);
    HRESULT reset_hr = E_FAIL;
    for (unsigned attempt = 0; attempt < 100 && FAILED(reset_hr); attempt++) {
      reset_hr = allocator->Reset();
      if (FAILED(reset_hr))
        Sleep(1);
    }
    if (!CheckHR("ResetAllocator", reset_hr) ||
        !CheckHR("ResetCommandList", list->Reset(allocator, nullptr)))
      return false;
    return true;
  };

  auto set_predicate = [&](UINT64 value) {
    *predicate_data = value;
  };

  auto run_compute = [&](bool indirect, D3D12_PREDICATION_OP op, UINT64 predicate_value, UINT expected) -> bool {
    set_predicate(predicate_value);
    if (output_state != D3D12_RESOURCE_STATE_COPY_DEST)
      Transition(list, output, output_state, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(output, 0, output_init, 0, sizeof(UINT));
    Transition(list, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    output_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    list->SetComputeRootSignature(root_signature);
    list->SetPipelineState(compute_pso);
    list->SetComputeRootUnorderedAccessView(0, output->GetGPUVirtualAddress());
    list->SetPredication(predicate, 0, op);
    if (indirect)
      list->ExecuteIndirect(dispatch_signature, 1, dispatch_args, 0, count_buffer, 0);
    else
      list->Dispatch(1, 1, 1);
    Transition(list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    output_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->CopyBufferRegion(output_readback, 0, output, 0, sizeof(UINT));
    if (!submit())
      return false;
    UINT *readback = nullptr;
    if (!CheckHR("MapOutputReadback", output_readback->Map(0, nullptr, reinterpret_cast<void **>(&readback))))
      return false;
    UINT actual = *readback;
    output_readback->Unmap(0, nullptr);
    if (actual != expected) {
      std::cerr << "compute predication mismatch indirect=" << indirect << " op=" << op << " value=0x"
                << std::hex << predicate_value << " actual=0x" << actual << " expected=0x" << expected << std::dec
                << "\n";
      return false;
    }
    return true;
  };

  auto run_draw = [&](bool indexed, bool indirect, D3D12_PREDICATION_OP op, UINT64 predicate_value,
                      bool disable_predicate) -> bool {
    set_predicate(predicate_value);
    if (render_state != D3D12_RESOURCE_STATE_RENDER_TARGET)
      Transition(list, render_target, render_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    render_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    const float clear_color[4] = {0, 0, 0, 1};
    list->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    list->SetPipelineState(graphics_pso);
    list->SetGraphicsRootSignature(graphics_root_signature);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (indexed) {
      D3D12_INDEX_BUFFER_VIEW index_view = {};
      index_view.BufferLocation = index_buffer->GetGPUVirtualAddress();
      index_view.SizeInBytes = sizeof(index_data);
      index_view.Format = DXGI_FORMAT_R16_UINT;
      list->IASetIndexBuffer(&index_view);
    }
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport = {0, 0, 1, 1, 0, 1};
    D3D12_RECT scissor = {0, 0, 1, 1};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    if (disable_predicate)
      list->SetPredication(nullptr, 0, op);
    else
      list->SetPredication(predicate, 0, op);
    if (indirect)
      list->ExecuteIndirect(draw_signature, 1, draw_args, 0, count_buffer, 0);
    else if (indexed)
      list->DrawIndexedInstanced(3, 1, 0, 0, 0);
    else
      list->DrawInstanced(3, 1, 0, 0);
    Transition(list, render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    render_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = render_readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = render_target;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    if (!submit())
      return false;
    BYTE *readback = nullptr;
    if (!CheckHR("MapRenderReadback", render_readback->Map(0, nullptr, reinterpret_cast<void **>(&readback))))
      return false;
    UINT actual = *reinterpret_cast<UINT *>(readback);
    render_readback->Unmap(0, nullptr);
    const bool execute = disable_predicate ||
                         (op == D3D12_PREDICATION_OP_EQUAL_ZERO ? predicate_value == 0 : predicate_value != 0);
    const UINT expected = execute ? 0xff0000ffu : 0xff000000u;
    if ((actual & 0x00ffffffu) != (expected & 0x00ffffffu)) {
      std::cerr << "draw predication mismatch indexed=" << indexed << " indirect=" << indirect << " op=" << op
                << " value=0x" << std::hex << predicate_value << " actual=0x" << actual << " expected=0x" << expected
                << std::dec << "\n";
      return false;
    }
    return true;
  };

  bool ok = true;
  ok = ok && run_compute(false, D3D12_PREDICATION_OP_EQUAL_ZERO, 0, 0xc0def00d);
  ok = ok && run_compute(false, D3D12_PREDICATION_OP_EQUAL_ZERO, 0x100000001ull, 0x13572468);
  ok = ok && run_compute(false, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, 0x100000001ull, 0xc0def00d);
  ok = ok && run_compute(true, D3D12_PREDICATION_OP_EQUAL_ZERO, 0, 0xc0def00d);
  ok = ok && run_compute(true, D3D12_PREDICATION_OP_EQUAL_ZERO, 0x100000001ull, 0x13572468);
  ok = ok && run_compute(true, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, 0x100000001ull, 0xc0def00d);
  ok = ok && run_draw(false, false, D3D12_PREDICATION_OP_EQUAL_ZERO, 0, false);
  ok = ok && run_draw(false, false, D3D12_PREDICATION_OP_EQUAL_ZERO, 0x100000001ull, false);
  ok = ok && run_draw(false, false, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, 0x100000001ull, false);
  ok = ok && run_draw(true, false, D3D12_PREDICATION_OP_EQUAL_ZERO, 0, false);
  ok = ok && run_draw(true, false, D3D12_PREDICATION_OP_EQUAL_ZERO, 0x100000001ull, false);
  ok = ok && run_draw(false, true, D3D12_PREDICATION_OP_EQUAL_ZERO, 0, false);
  ok = ok && run_draw(false, true, D3D12_PREDICATION_OP_EQUAL_ZERO, 0x100000001ull, false);
  ok = ok && run_draw(true, true, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, 0x100000001ull, false);
  ok = ok && run_compute(false, D3D12_PREDICATION_OP_EQUAL_ZERO, 0x100000001ull, 0x13572468);
  // A null predicate disables filtering and must leave the command list usable.
  ok = ok && run_draw(false, false, D3D12_PREDICATION_OP_EQUAL_ZERO, 0x100000001ull, true);

  if (ok) {
    std::cout << "D3D12 predication draw, dispatch and ExecuteIndirect semantics passed\n";
    result = 0;
  }
  cleanup();
  return result;
}

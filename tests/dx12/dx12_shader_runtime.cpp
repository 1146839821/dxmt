#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

template <typename T>
class ComPtr {
public:
  ComPtr() = default;
  ComPtr(const ComPtr &) = delete;
  ComPtr &operator=(const ComPtr &) = delete;
  ~ComPtr() {
    if (value_)
      value_->Release();
  }

  T *get() const { return value_; }
  T **put() {
    if (value_)
      value_->Release();
    value_ = nullptr;
    return &value_;
  }

private:
  T *value_ = nullptr;
};

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

D3D12_RESOURCE_DESC BufferDescription(UINT64 size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC description = {};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Width = size;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  description.Flags = flags;
  return description;
}

bool CompileShader(
    pD3DCompile compile_shader, const char *source, const char *source_name, const char *entry,
    const char *target, std::vector<uint8_t> &bytecode
) {
  ID3DBlob *shader = nullptr;
  ID3DBlob *errors = nullptr;
  const HRESULT hr = compile_shader(
      source, std::strlen(source), source_name, nullptr, nullptr, entry, target,
      D3DCOMPILE_ENABLE_STRICTNESS, 0, &shader, &errors
  );
  if (FAILED(hr)) {
    if (errors)
      std::cerr << source_name << ": " << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
    if (errors)
      errors->Release();
    if (shader)
      shader->Release();
    return false;
  }

  const auto *data = static_cast<const uint8_t *>(shader->GetBufferPointer());
  bytecode.assign(data, data + shader->GetBufferSize());
  if (errors)
    errors->Release();
  shader->Release();
  return !bytecode.empty();
}

bool ReadFile(const char *path, std::vector<uint8_t> &data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return false;
  const auto size = file.tellg();
  if (size <= 0)
    return false;
  data.resize(static_cast<size_t>(size));
  file.seekg(0);
  return file.read(reinterpret_cast<char *>(data.data()), size).good();
}

bool CreateEmptyRootSignature(ID3D12Device *device, ID3D12RootSignature **root_signature) {
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  ComPtr<ID3DBlob> root_blob;
  ComPtr<ID3DBlob> root_error;
  if (!CheckHR(
          "D3D12SerializeRootSignature",
          D3D12SerializeRootSignature(
              &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, root_blob.put(), root_error.put()
          )
      )) {
    if (root_error.get())
      std::cerr << static_cast<const char *>(root_error.get()->GetBufferPointer()) << "\n";
    return false;
  }
  return CheckHR(
      "CreateRootSignature",
      device->CreateRootSignature(
          0, root_blob.get()->GetBufferPointer(), root_blob.get()->GetBufferSize(),
          IID_PPV_ARGS(root_signature)
      )
  );
}

bool WaitForQueue(ID3D12Device *device, ID3D12CommandQueue *queue) {
  ComPtr<ID3D12Fence> fence;
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()))))
    return false;
  if (!CheckHR("Signal", queue->Signal(fence.get(), 1)))
    return false;
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event) {
    std::cerr << "CreateEvent failed\n";
    return false;
  }
  const HRESULT event_hr = fence.get()->SetEventOnCompletion(1, event);
  const DWORD wait_result = event_hr == S_OK ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event);
  if (!CheckHR("SetEventOnCompletion", event_hr) || wait_result != WAIT_OBJECT_0) {
    std::cerr << "queue wait failed: " << wait_result << "\n";
    return false;
  }
  return CheckHR("GetDeviceRemovedReason", device->GetDeviceRemovedReason());
}

bool CreateVertexBuffer(
    ID3D12Device *device, ComPtr<ID3D12Resource> &vertex_buffer, D3D12_VERTEX_BUFFER_VIEW &vertex_view
) {
  struct Vertex {
    float position[2];
    float color[4];
  };
  static constexpr Vertex vertices[] = {
      {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{3.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
      {{-1.0f, 3.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
  };
  const auto heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const auto description = BufferDescription(sizeof(vertices));
  if (!CheckHR(
          "CreateVertexBuffer",
          device->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ,
              nullptr, IID_PPV_ARGS(vertex_buffer.put())
          )
      ))
    return false;
  void *mapped = nullptr;
  if (!CheckHR("MapVertexBuffer", vertex_buffer.get()->Map(0, nullptr, &mapped)))
    return false;
  std::memcpy(mapped, vertices, sizeof(vertices));
  vertex_buffer.get()->Unmap(0, nullptr);
  vertex_view.BufferLocation = vertex_buffer.get()->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = sizeof(vertices);
  vertex_view.StrideInBytes = sizeof(Vertex);
  return true;
}

bool RunCompute(
    ID3D12Device *device, ID3D12RootSignature *root_signature,
    const D3D12_SHADER_BYTECODE &shader, const char *label
) {
  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue.put()))))
    return false;

  ComPtr<ID3D12CommandAllocator> allocator;
  if (!CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put()))
      ))
    return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root_signature;
  pso_desc.CS = shader;
  ComPtr<ID3D12PipelineState> pso;
  if (!CheckHR("CreateComputePipelineState", device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(pso.put()))))
    return false;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (!CheckHR(
          "CreateCommandList",
          device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), pso.get(), IID_PPV_ARGS(list.put())
          )
      ))
    return false;

  list.get()->SetPipelineState(pso.get());
  list.get()->SetComputeRootSignature(root_signature);
  list.get()->Dispatch(1, 1, 1);
  if (!CheckHR("Close", list.get()->Close()))
    return false;
  ID3D12CommandList *lists[] = {list.get()};
  queue.get()->ExecuteCommandLists(1, lists);
  if (!WaitForQueue(device, queue.get()))
    return false;
  std::cout << label << " empty-root Dispatch passed\n";
  return true;
}

bool RunColorGraphics(
    ID3D12Device *device, ID3D12RootSignature *root_signature,
    const D3D12_SHADER_BYTECODE &vertex_shader, const D3D12_SHADER_BYTECODE &pixel_shader,
    const char *label
) {
  static const D3D12_INPUT_ELEMENT_DESC input_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue.put()))))
    return false;
  ComPtr<ID3D12CommandAllocator> allocator;
  if (!CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put()))
      ))
    return false;

  D3D12_RESOURCE_DESC render_target_desc = {};
  render_target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  render_target_desc.Width = 1;
  render_target_desc.Height = 1;
  render_target_desc.DepthOrArraySize = 1;
  render_target_desc.MipLevels = 1;
  render_target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  render_target_desc.SampleDesc.Count = 1;
  render_target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_CLEAR_VALUE clear_value = {};
  clear_value.Format = render_target_desc.Format;
  clear_value.Color[3] = 1.0f;
  ComPtr<ID3D12Resource> render_target;
  const auto default_heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  if (!CheckHR(
          "CreateRenderTarget",
          device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &render_target_desc,
              D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value, IID_PPV_ARGS(render_target.put())
          )
      ))
    return false;
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  if (!CheckHR("CreateRTVHeap", device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(rtv_heap.put()))))
    return false;
  const auto rtv = rtv_heap.get()->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(render_target.get(), nullptr, rtv);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root_signature;
  pso_desc.VS = vertex_shader;
  pso_desc.PS = pixel_shader;
  pso_desc.InputLayout = {input_layout, 2};
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = render_target_desc.Format;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  ComPtr<ID3D12PipelineState> pso;
  if (!CheckHR("CreateGraphicsPipelineState", device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(pso.put()))))
    return false;

  ComPtr<ID3D12Resource> vertex_buffer;
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  if (!CreateVertexBuffer(device, vertex_buffer, vertex_view))
    return false;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (!CheckHR(
          "CreateCommandList",
          device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), pso.get(), IID_PPV_ARGS(list.put())
          )
      ))
    return false;
  list.get()->SetPipelineState(pso.get());
  list.get()->SetGraphicsRootSignature(root_signature);
  list.get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list.get()->IASetVertexBuffers(0, 1, &vertex_view);
  list.get()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  list.get()->ClearRenderTargetView(rtv, clear_value.Color, 0, nullptr);
  D3D12_VIEWPORT viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  D3D12_RECT scissor = {0, 0, 1, 1};
  list.get()->RSSetViewports(1, &viewport);
  list.get()->RSSetScissorRects(1, &scissor);
  list.get()->DrawInstanced(3, 1, 0, 0);
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = render_target.get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list.get()->ResourceBarrier(1, &barrier);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  device->GetCopyableFootprints(
      &render_target_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size
  );
  const auto readback_heap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
  const auto readback_desc = BufferDescription(total_size);
  ComPtr<ID3D12Resource> readback;
  if (!CheckHR(
          "CreateReadbackBuffer",
          device->CreateCommittedResource(
              &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.put())
          )
      ))
    return false;
  D3D12_TEXTURE_COPY_LOCATION copy_dst = {};
  copy_dst.pResource = readback.get();
  copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  copy_dst.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION copy_src = {};
  copy_src.pResource = render_target.get();
  copy_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list.get()->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, nullptr);
  if (!CheckHR("Close", list.get()->Close()))
    return false;
  ID3D12CommandList *lists[] = {list.get()};
  queue.get()->ExecuteCommandLists(1, lists);
  if (!WaitForQueue(device, queue.get()))
    return false;
  void *mapped = nullptr;
  if (!CheckHR("MapReadback", readback.get()->Map(0, nullptr, &mapped)))
    return false;
  const auto pixel = *static_cast<const UINT *>(mapped);
  readback.get()->Unmap(0, nullptr);
  if (!pixel) {
    std::cerr << label << " DrawInstanced produced no color\n";
    return false;
  }
  std::cout << label << " empty-root DrawInstanced passed: 0x" << std::hex << pixel << std::dec << "\n";
  return true;
}

bool RunDepthOnlyGraphics(
    ID3D12Device *device, ID3D12RootSignature *root_signature,
    const D3D12_SHADER_BYTECODE &vertex_shader, const char *label
) {
  static const D3D12_INPUT_ELEMENT_DESC input_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue.put()))))
    return false;
  ComPtr<ID3D12CommandAllocator> allocator;
  if (!CheckHR(
          "CreateCommandAllocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put()))
      ))
    return false;
  D3D12_RESOURCE_DESC depth_desc = {};
  depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  depth_desc.Width = 1;
  depth_desc.Height = 1;
  depth_desc.DepthOrArraySize = 1;
  depth_desc.MipLevels = 1;
  depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE depth_clear = {};
  depth_clear.Format = depth_desc.Format;
  depth_clear.DepthStencil.Depth = 1.0f;
  ComPtr<ID3D12Resource> depth_stencil;
  const auto default_heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  if (!CheckHR(
          "CreateDepthStencil",
          device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
              D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear, IID_PPV_ARGS(depth_stencil.put())
          )
      ))
    return false;
  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
  dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsv_heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> dsv_heap;
  if (!CheckHR("CreateDSVHeap", device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(dsv_heap.put()))))
    return false;
  const auto dsv = dsv_heap.get()->GetCPUDescriptorHandleForHeapStart();
  device->CreateDepthStencilView(depth_stencil.get(), nullptr, dsv);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root_signature;
  pso_desc.VS = vertex_shader;
  pso_desc.InputLayout = {input_layout, 2};
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.DSVFormat = depth_desc.Format;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  pso_desc.DepthStencilState.DepthEnable = TRUE;
  pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  ComPtr<ID3D12PipelineState> pso;
  if (!CheckHR("CreateDepthOnlyPipelineState", device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(pso.put()))))
    return false;
  ComPtr<ID3D12Resource> vertex_buffer;
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  if (!CreateVertexBuffer(device, vertex_buffer, vertex_view))
    return false;
  D3D12_QUERY_HEAP_DESC query_desc = {};
  query_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  query_desc.Count = 1;
  ComPtr<ID3D12QueryHeap> query_heap;
  if (!CheckHR("CreateOcclusionQueryHeap", device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(query_heap.put()))))
    return false;
  const auto readback_heap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
  const auto readback_desc = BufferDescription(sizeof(UINT64));
  ComPtr<ID3D12Resource> readback;
  if (!CheckHR(
          "CreateQueryReadback",
          device->CreateCommittedResource(
              &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.put())
          )
      ))
    return false;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (!CheckHR(
          "CreateCommandList",
          device->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), pso.get(), IID_PPV_ARGS(list.put())
          )
      ))
    return false;
  list.get()->SetPipelineState(pso.get());
  list.get()->SetGraphicsRootSignature(root_signature);
  list.get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list.get()->IASetVertexBuffers(0, 1, &vertex_view);
  list.get()->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
  D3D12_VIEWPORT viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  D3D12_RECT scissor = {0, 0, 1, 1};
  list.get()->RSSetViewports(1, &viewport);
  list.get()->RSSetScissorRects(1, &scissor);
  list.get()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  list.get()->BeginQuery(query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
  list.get()->DrawInstanced(3, 1, 0, 0);
  list.get()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
  list.get()->ResolveQueryData(
      query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0, 1, readback.get(), 0
  );
  if (!CheckHR("Close", list.get()->Close()))
    return false;
  ID3D12CommandList *lists[] = {list.get()};
  queue.get()->ExecuteCommandLists(1, lists);
  if (!WaitForQueue(device, queue.get()))
    return false;
  void *mapped = nullptr;
  if (!CheckHR("MapQueryReadback", readback.get()->Map(0, nullptr, &mapped)))
    return false;
  const auto samples = *static_cast<const UINT64 *>(mapped);
  readback.get()->Unmap(0, nullptr);
  if (!samples) {
    std::cerr << label << " depth-only DrawInstanced produced zero samples\n";
    return false;
  }
  std::cout << label << " empty-root depth-only DrawInstanced passed: " << samples << " samples\n";
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: dx12_shader_runtime <compute.cso> <vertex.cso> <pixel.cso>\n";
    return 2;
  }

  static constexpr char legacy_compute_source[] = R"HLSL(
[numthreads(1, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID) {}
)HLSL";
  static constexpr char legacy_graphics_source[] = R"HLSL(
struct VSInput {
  float2 position : POSITION;
  float4 color : COLOR;
};
struct VSOutput {
  float4 position : SV_Position;
  float4 color : COLOR;
};
VSOutput vs_main(VSInput input) {
  VSOutput output;
  output.position = float4(input.position, 0.0, 1.0);
  output.color = input.color;
  return output;
}
float4 ps_main(VSOutput input) : SV_Target0 { return input.color; }
)HLSL";
  std::vector<uint8_t> legacy_compute;
  std::vector<uint8_t> legacy_vertex;
  std::vector<uint8_t> legacy_pixel;
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
  const bool compiled =
      CompileShader(
          compile_shader, legacy_compute_source, "legacy_cs.hlsl", "cs_main", "cs_5_0", legacy_compute
      ) && CompileShader(
          compile_shader, legacy_graphics_source, "legacy_vs.hlsl", "vs_main", "vs_5_0", legacy_vertex
      ) && CompileShader(
          compile_shader, legacy_graphics_source, "legacy_ps.hlsl", "ps_main", "ps_5_0", legacy_pixel
      );
  FreeLibrary(compiler);
  if (!compiled)
    return 1;

  std::vector<uint8_t> dxil_compute;
  std::vector<uint8_t> dxil_vertex;
  std::vector<uint8_t> dxil_pixel;
  if (!ReadFile(argv[1], dxil_compute) || !ReadFile(argv[2], dxil_vertex) || !ReadFile(argv[3], dxil_pixel)) {
    std::cerr << "failed to read one or more DXIL fixtures\n";
    return 1;
  }

  ComPtr<ID3D12Device> device;
  if (!CheckHR(
          "D3D12CreateDevice",
          D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.put()))
      ))
    return 1;
  ComPtr<ID3D12RootSignature> root_signature;
  if (!CreateEmptyRootSignature(device.get(), root_signature.put()))
    return 1;

  const D3D12_SHADER_BYTECODE legacy_cs = {legacy_compute.data(), legacy_compute.size()};
  const D3D12_SHADER_BYTECODE legacy_vs = {legacy_vertex.data(), legacy_vertex.size()};
  const D3D12_SHADER_BYTECODE legacy_ps = {legacy_pixel.data(), legacy_pixel.size()};
  const D3D12_SHADER_BYTECODE dxil_cs = {dxil_compute.data(), dxil_compute.size()};
  const D3D12_SHADER_BYTECODE dxil_vs = {dxil_vertex.data(), dxil_vertex.size()};
  const D3D12_SHADER_BYTECODE dxil_ps = {dxil_pixel.data(), dxil_pixel.size()};

  bool passed = true;
  passed = RunCompute(device.get(), root_signature.get(), legacy_cs, "AIRCONV") && passed;
  passed = RunColorGraphics(device.get(), root_signature.get(), legacy_vs, legacy_ps, "AIRCONV") && passed;
  passed = RunDepthOnlyGraphics(device.get(), root_signature.get(), legacy_vs, "AIRCONV") && passed;
  passed = RunCompute(device.get(), root_signature.get(), dxil_cs, "MSC") && passed;
  passed = RunColorGraphics(device.get(), root_signature.get(), dxil_vs, dxil_ps, "MSC") && passed;
  passed = RunDepthOnlyGraphics(device.get(), root_signature.get(), dxil_vs, "MSC") && passed;
  std::cout << (passed ? "D3D12 shader backend runtime coverage passed\n"
                       : "D3D12 shader backend runtime coverage failed\n");
  return passed ? 0 : 1;
}

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

D3D12_RESOURCE_DESC RenderTargetDescription() {
  D3D12_RESOURCE_DESC description = {};
  description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  description.Width = 1;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  return description;
}

bool CompileShader(pD3DCompile compile_shader, const char *source, const char *source_name, const char *entry,
                  const char *target, std::vector<uint8_t> &bytecode) {
  ID3DBlob *shader = nullptr;
  ID3DBlob *errors = nullptr;
  HRESULT hr = compile_shader(source, std::strlen(source), source_name, nullptr, nullptr, entry, target,
                              D3DCOMPILE_ENABLE_STRICTNESS, 0, &shader, &errors);
  if (FAILED(hr)) {
    if (errors)
      std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
    Release(errors);
    Release(shader);
    return false;
  }
  const auto *data = static_cast<const uint8_t *>(shader->GetBufferPointer());
  bytecode.assign(data, data + shader->GetBufferSize());
  Release(errors);
  Release(shader);
  return !bytecode.empty();
}

struct ShaderSet {
  std::vector<uint8_t> vertex;
  std::vector<uint8_t> no_input_vertex;
  std::vector<uint8_t> root_vertex;
  std::vector<uint8_t> geometry;
  std::vector<uint8_t> geometry_root_cbv;
  std::vector<uint8_t> geometry_root_srv_uav;
  std::vector<uint8_t> adjacency_geometry;
  std::vector<uint8_t> pixel;
  std::vector<uint8_t> pixel_query;
};

bool CompileShaders(pD3DCompile compile_shader, ShaderSet &shaders) {
  static constexpr char vertex_source[] = R"(
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
)";
  static constexpr char no_input_vertex_source[] = R"(
struct VSOutput {
  float4 position : SV_Position;
  float4 color : COLOR;
};

VSOutput vs_main(uint vertex_id : SV_VertexID) {
  VSOutput output;
  if (vertex_id == 0)
    output.position = float4(-1.0, -1.0, 0.0, 1.0);
  else if (vertex_id == 1)
    output.position = float4(3.0, -1.0, 0.0, 1.0);
  else
    output.position = float4(-1.0, 3.0, 0.0, 1.0);
  output.color = float4(1.0, 1.0, 1.0, 1.0);
  return output;
}
)";
  static constexpr char root_vertex_source[] = R"(
cbuffer RootData : register(b0) {
  float4 root_color;
};

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
  output.color = root_color;
  return output;
}
)";
  static constexpr char geometry_root_cbv_source[] = R"(
cbuffer RootData : register(b0) {
  float4 root_color;
};

struct VSOutput {
  float4 position : SV_Position;
  float4 color : COLOR;
};

[maxvertexcount(3)]
void gs_main(triangle VSOutput input[3], inout TriangleStream<VSOutput> output) {
  for (uint i = 0; i < 3; ++i) {
    VSOutput vertex = input[i];
    vertex.color = root_color;
    output.Append(vertex);
  }
}
)";
  static constexpr char geometry_root_srv_uav_source[] = R"(
StructuredBuffer<float4> root_input : register(t0);
RWStructuredBuffer<float4> root_output : register(u0);

struct VSOutput {
  float4 position : SV_Position;
  float4 color : COLOR;
};

[maxvertexcount(3)]
void gs_main(triangle VSOutput input[3], inout TriangleStream<VSOutput> output) {
  const float4 root_color = root_input[0];
  root_output[0] = root_color;
  for (uint i = 0; i < 3; ++i) {
    VSOutput vertex = input[i];
    vertex.color = root_color;
    output.Append(vertex);
  }
}
)";
  static constexpr char geometry_source[] = R"(
struct VSOutput {
  float4 position : SV_Position;
  float4 color : COLOR;
};

[maxvertexcount(3)]
void gs_main(triangle VSOutput input[3], inout TriangleStream<VSOutput> output) {
  for (uint i = 0; i < 3; ++i) {
    VSOutput vertex = input[i];
    output.Append(vertex);
  }
}
)";
  static constexpr char adjacency_geometry_source[] = R"(
struct VSOutput {
  float4 position : SV_Position;
  float4 color : COLOR;
};

[maxvertexcount(3)]
void gs_main(triangleadj VSOutput input[6], inout TriangleStream<VSOutput> output) {
  for (uint i = 0; i < 3; ++i) {
    VSOutput vertex = input[i * 2];
    output.Append(vertex);
  }
}
)";
  static constexpr char pixel_source[] = R"(
struct PSInput {
  float4 position : SV_Position;
  float4 color : COLOR;
};

float4 ps_main(PSInput input) : SV_Target {
  return input.color;
}
)";
  static constexpr char pixel_query_source[] = R"(
Texture2D<float4> input_texture : register(t0);

struct PSInput {
  float4 position : SV_Position;
  float4 color : COLOR;
};

float4 ps_main(PSInput input) : SV_Target {
  uint width;
  uint height;
  input_texture.GetDimensions(width, height);
  return width == 0 && height == 0 ? float4(1.0, 1.0, 1.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);
}
)";

  return CompileShader(compile_shader, vertex_source, "dx12_graphics_sm5_vs.hlsl", "vs_main", "vs_5_0",
                       shaders.vertex) &&
         CompileShader(compile_shader, no_input_vertex_source, "dx12_graphics_sm5_no_input_vs.hlsl", "vs_main",
                       "vs_5_0", shaders.no_input_vertex) &&
         CompileShader(compile_shader, root_vertex_source, "dx12_graphics_sm5_root_vs.hlsl", "vs_main", "vs_5_0",
                       shaders.root_vertex) &&
         CompileShader(compile_shader, geometry_source, "dx12_graphics_sm5_gs.hlsl", "gs_main", "gs_5_0",
                       shaders.geometry) &&
         CompileShader(compile_shader, geometry_root_cbv_source, "dx12_graphics_sm5_geometry_root_cbv_gs.hlsl", "gs_main",
                       "gs_5_0", shaders.geometry_root_cbv) &&
         CompileShader(compile_shader, geometry_root_srv_uav_source,
                       "dx12_graphics_sm5_geometry_root_srv_uav_gs.hlsl", "gs_main", "gs_5_0",
                       shaders.geometry_root_srv_uav) &&
         CompileShader(compile_shader, adjacency_geometry_source, "dx12_graphics_sm5_adj_gs.hlsl", "gs_main",
                       "gs_5_0", shaders.adjacency_geometry) &&
         CompileShader(compile_shader, pixel_source, "dx12_graphics_sm5_ps.hlsl", "ps_main", "ps_5_0", shaders.pixel) &&
         CompileShader(compile_shader, pixel_query_source, "dx12_graphics_sm5_null_query_ps.hlsl", "ps_main", "ps_5_0",
                       shaders.pixel_query);
}

struct TestCase {
  const char *name;
  D3D12_PRIMITIVE_TOPOLOGY topology;
  bool indexed;
  bool index32;
  bool adjacency;
  bool root_cbv;
  bool indirect;
  uint32_t expected_rgb;
  bool no_input = false;
  bool geometry_root_cbv = false;
  bool zero_index_view = false;
  bool geometry_root_srv_uav = false;
  bool null_texture_query = false;
};

bool RunCase(ID3D12Device *device, const ShaderSet &shaders, const TestCase &test) {
  struct Vertex {
    float position[2];
    float color[4];
  };
  static constexpr Vertex triangle_vertices[] = {
      {{0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
      {{-1.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
      {{3.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
      {{-1.0f, 3.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
  };
  static constexpr Vertex adjacency_vertices[] = {
      {{0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
      {{-1.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
      {{0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
      {{3.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
      {{0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
      {{-1.0f, 3.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
      {{0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
  };
  static constexpr uint16_t triangle_indices[] = {0xffff, 0, 1, 2};
  static constexpr uint16_t adjacency_indices[] = {0xffff, 0, 1, 2, 3, 4, 5};

  const Vertex *vertex_data = test.adjacency ? adjacency_vertices
                                             : (test.indexed ? triangle_vertices : triangle_vertices + 1);
  const UINT vertex_count = test.adjacency ? 7 : (test.indexed ? 4 : 3);
  const UINT index_count = test.adjacency ? 6 : 3;
  const UINT index_buffer_count = index_count + (test.indexed ? 1 : 0);
  std::vector<uint32_t> index32(index_buffer_count);
  if (test.index32) {
    const auto *source = test.adjacency ? adjacency_indices : triangle_indices;
    for (UINT i = 0; i < index_buffer_count; ++i)
      index32[i] = source[i];
  }

  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  ID3D12DescriptorHeap *shader_heap = nullptr;
  ID3D12Resource *render_target = nullptr;
  ID3D12Resource *vertex_buffer = nullptr;
  ID3D12Resource *index_buffer = nullptr;
  ID3D12Resource *root_data = nullptr;
  ID3D12Resource *root_uav_data = nullptr;
  ID3D12Resource *indirect_args = nullptr;
  ID3D12CommandSignature *command_signature = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  void *mapped_vertex = nullptr;
  void *mapped_index = nullptr;
  void *mapped_root = nullptr;
  void *mapped_indirect = nullptr;
  BYTE *mapped_readback = nullptr;
  ID3D12Resource *uav_readback = nullptr;
  void *mapped_uav_readback = nullptr;

  auto cleanup = [&] {
    if (mapped_readback)
      readback->Unmap(0, nullptr);
    if (mapped_indirect)
      indirect_args->Unmap(0, nullptr);
    if (mapped_root)
      root_data->Unmap(0, nullptr);
    if (mapped_uav_readback)
      uav_readback->Unmap(0, nullptr);
    if (mapped_index)
      index_buffer->Unmap(0, nullptr);
    if (mapped_vertex)
      vertex_buffer->Unmap(0, nullptr);
    if (event)
      CloseHandle(event);
    Release(fence);
    Release(readback);
    Release(command_signature);
    Release(indirect_args);
    Release(uav_readback);
    Release(root_uav_data);
    Release(root_data);
    Release(index_buffer);
    Release(vertex_buffer);
    Release(render_target);
    Release(shader_heap);
    Release(rtv_heap);
    Release(root_error);
    Release(root_blob);
    Release(root_signature);
    Release(pso);
    Release(list);
    Release(allocator);
    Release(queue);
  };
  auto fail = [&](const char *message) {
    std::cerr << "DXBC SM5 " << test.name << ": " << message << "\n";
    cleanup();
    return false;
  };

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !CheckHR("CreateCommandAllocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
    return fail("queue setup failed");

  D3D12_ROOT_PARAMETER root_parameters[2] = {};
  D3D12_DESCRIPTOR_RANGE descriptor_range = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  if (test.null_texture_query) {
    descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range.NumDescriptors = 1;
    descriptor_range.BaseShaderRegister = 0;
    descriptor_range.RegisterSpace = 0;
    descriptor_range.OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_range;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_desc.NumParameters = 1;
    root_desc.pParameters = root_parameters;
  } else if (test.root_cbv || test.geometry_root_cbv) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].ShaderVisibility = test.geometry_root_cbv ? D3D12_SHADER_VISIBILITY_GEOMETRY
                                                                   : D3D12_SHADER_VISIBILITY_VERTEX;
    root_desc.NumParameters = 1;
    root_desc.pParameters = root_parameters;
  } else if (test.geometry_root_srv_uav) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
    root_desc.NumParameters = 2;
    root_desc.pParameters = root_parameters;
  }
  if (root_desc.NumParameters) {
    if (!CheckHR("D3D12SerializeRootSignature",
                 D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error))) {
      if (root_error)
        std::cerr << static_cast<const char *>(root_error->GetBufferPointer()) << "\n";
      return fail("root signature serialization failed");
    }
    if (!CheckHR("CreateRootSignature",
                 device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                              IID_PPV_ARGS(&root_signature))))
      return fail("root signature creation failed");
  }

  const auto &vertex_shader = test.geometry_root_cbv || test.geometry_root_srv_uav ? shaders.vertex
                             : test.root_cbv           ? shaders.root_vertex
                             : test.no_input            ? shaders.no_input_vertex
                                                        : shaders.vertex;
  const auto &geometry_shader = test.geometry_root_cbv       ? shaders.geometry_root_cbv
                              : test.geometry_root_srv_uav ? shaders.geometry_root_srv_uav
                              : test.adjacency             ? shaders.adjacency_geometry
                                                           : shaders.geometry;
  D3D12_INPUT_ELEMENT_DESC input_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root_signature;
  pso_desc.VS = {vertex_shader.data(), vertex_shader.size()};
  pso_desc.GS = test.null_texture_query ? D3D12_SHADER_BYTECODE{}
                                         : D3D12_SHADER_BYTECODE{geometry_shader.data(), geometry_shader.size()};
  const auto &pixel_shader = test.null_texture_query ? shaders.pixel_query : shaders.pixel;
  pso_desc.PS = {pixel_shader.data(), pixel_shader.size()};
  pso_desc.InputLayout = test.no_input ? D3D12_INPUT_LAYOUT_DESC{} : D3D12_INPUT_LAYOUT_DESC{input_layout, 2};
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  if (!CheckHR("CreateGraphicsPipelineState",
               device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    return fail("graphics PSO creation failed");

  auto default_heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  auto upload_heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  auto readback_heap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
  auto render_target_desc = RenderTargetDescription();
  D3D12_CLEAR_VALUE clear_value = {};
  clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  clear_value.Color[2] = 1.0f;
  if (!CheckHR("CreateRenderTarget",
               device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &render_target_desc,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
                                               IID_PPV_ARGS(&render_target))))
    return fail("render target creation failed");

  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  if (!CheckHR("CreateRTVHeap", device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap))))
    return fail("RTV heap creation failed");
  auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(render_target, nullptr, rtv);

  if (test.null_texture_query) {
    D3D12_DESCRIPTOR_HEAP_DESC shader_heap_desc = {};
    shader_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    shader_heap_desc.NumDescriptors = 1;
    shader_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!CheckHR("CreateShaderHeap", device->CreateDescriptorHeap(&shader_heap_desc, IID_PPV_ARGS(&shader_heap))))
      return fail("shader heap creation failed");
    D3D12_SHADER_RESOURCE_VIEW_DESC null_srv = {};
    null_srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    null_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null_srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(nullptr, &null_srv,
                                     shader_heap->GetCPUDescriptorHandleForHeapStart());
  }

  auto vertex_desc = BufferDescription(sizeof(Vertex) * vertex_count);
  if (!CheckHR("CreateVertexBuffer",
               device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &vertex_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&vertex_buffer))) ||
      !CheckHR("MapVertexBuffer", vertex_buffer->Map(0, nullptr, &mapped_vertex)))
    return fail("vertex buffer setup failed");
  std::memcpy(mapped_vertex, vertex_data, sizeof(Vertex) * vertex_count);
  vertex_buffer->Unmap(0, nullptr);
  mapped_vertex = nullptr;
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  vertex_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = sizeof(Vertex) * vertex_count;
  vertex_view.StrideInBytes = sizeof(Vertex);

  D3D12_INDEX_BUFFER_VIEW index_view = {};
  if (test.indexed) {
    const size_t index_size = test.index32 ? sizeof(uint32_t) : sizeof(uint16_t);
    const void *index_data = test.index32 ? static_cast<const void *>(index32.data())
                                          : static_cast<const void *>(test.adjacency ? adjacency_indices : triangle_indices);
    auto index_desc = BufferDescription(index_size * index_buffer_count);
    if (!CheckHR("CreateIndexBuffer",
                 device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &index_desc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&index_buffer))) ||
        !CheckHR("MapIndexBuffer", index_buffer->Map(0, nullptr, &mapped_index)))
      return fail("index buffer setup failed");
    std::memcpy(mapped_index, index_data, index_size * index_buffer_count);
    index_buffer->Unmap(0, nullptr);
    mapped_index = nullptr;
    index_view.BufferLocation = index_buffer->GetGPUVirtualAddress();
    index_view.SizeInBytes = static_cast<UINT>(index_size * index_buffer_count);
    index_view.Format = test.index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
  }

  if (test.root_cbv || test.geometry_root_cbv || test.geometry_root_srv_uav) {
    auto root_desc_buffer = BufferDescription(256);
    if (!CheckHR("CreateRootData",
                 device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &root_desc_buffer,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&root_data))) ||
        !CheckHR("MapRootData", root_data->Map(0, nullptr, &mapped_root)))
      return fail("root data setup failed");
    static constexpr float root_color[] = {0.0f, 1.0f, 0.0f, 1.0f};
    std::memcpy(mapped_root, root_color, sizeof(root_color));
    root_data->Unmap(0, nullptr);
    mapped_root = nullptr;
  }

  if (test.geometry_root_srv_uav) {
    auto root_uav_desc = BufferDescription(256);
    root_uav_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!CheckHR("CreateRootUAVData",
                 device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &root_uav_desc,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                 IID_PPV_ARGS(&root_uav_data))))
      return fail("root UAV setup failed");
  }

  if (test.indirect) {
    const UINT64 argument_size = test.indexed ? sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) : sizeof(D3D12_DRAW_ARGUMENTS);
    auto indirect_desc = BufferDescription(argument_size);
    if (!CheckHR("CreateIndirectArgs",
                 device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &indirect_desc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&indirect_args))) ||
        !CheckHR("MapIndirectArgs", indirect_args->Map(0, nullptr, &mapped_indirect)))
      return fail("indirect argument setup failed");
    if (test.indexed) {
      auto *arguments = static_cast<D3D12_DRAW_INDEXED_ARGUMENTS *>(mapped_indirect);
      arguments->IndexCountPerInstance = index_count;
      arguments->InstanceCount = 1;
      arguments->StartIndexLocation = 1;
      arguments->BaseVertexLocation = 1;
      arguments->StartInstanceLocation = 0;
    } else {
      auto *arguments = static_cast<D3D12_DRAW_ARGUMENTS *>(mapped_indirect);
      arguments->VertexCountPerInstance = vertex_count;
      arguments->InstanceCount = 1;
      arguments->StartVertexLocation = 0;
      arguments->StartInstanceLocation = 0;
    }
    indirect_args->Unmap(0, nullptr);
    mapped_indirect = nullptr;

    D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
    argument_desc.Type = test.indexed ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
                                      : D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
    signature_desc.ByteStride = static_cast<UINT>(argument_size);
    signature_desc.NumArgumentDescs = 1;
    signature_desc.pArgumentDescs = &argument_desc;
    if (!CheckHR("CreateCommandSignature",
                 device->CreateCommandSignature(&signature_desc, nullptr, IID_PPV_ARGS(&command_signature))))
      return fail("command signature creation failed");
  }

  if (!CheckHR("CreateCommandList",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                         IID_PPV_ARGS(&list))))
    return fail("command list creation failed");

  list->SetPipelineState(pso);
  if (root_signature) {
    list->SetGraphicsRootSignature(root_signature);
    if (test.null_texture_query) {
      ID3D12DescriptorHeap *heaps[] = {shader_heap};
      list->SetDescriptorHeaps(1, heaps);
      list->SetGraphicsRootDescriptorTable(0, shader_heap->GetGPUDescriptorHandleForHeapStart());
    } else if (test.geometry_root_srv_uav) {
      list->SetGraphicsRootShaderResourceView(0, root_data->GetGPUVirtualAddress());
      list->SetGraphicsRootUnorderedAccessView(1, root_uav_data->GetGPUVirtualAddress());
    } else {
      list->SetGraphicsRootConstantBufferView(0, root_data->GetGPUVirtualAddress());
    }
  }
  list->IASetPrimitiveTopology(test.topology);
  if (!test.no_input)
    list->IASetVertexBuffers(0, 1, &vertex_view);
  if (test.indexed || test.zero_index_view)
    list->IASetIndexBuffer(&index_view);
  list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  list->ClearRenderTargetView(rtv, clear_value.Color, 0, nullptr);
  D3D12_VIEWPORT viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  D3D12_RECT scissor = {0, 0, 1, 1};
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  if (test.indirect) {
    list->ExecuteIndirect(command_signature, 1, indirect_args, 0, nullptr, 0);
  } else if (test.indexed) {
    list->DrawIndexedInstanced(index_count, 1, 1, 1, 0);
  } else {
    list->DrawInstanced(vertex_count, 1, 0, 0);
  }

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  device->GetCopyableFootprints(&render_target_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size);
  auto readback_desc = BufferDescription(total_size);
  if (!CheckHR("CreateReadback",
               device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&readback))))
    return fail("readback creation failed");

  if (test.geometry_root_srv_uav) {
    auto uav_readback_desc = BufferDescription(256);
    if (!CheckHR("CreateRootUAVReadback",
                 device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &uav_readback_desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&uav_readback))))
      return fail("root UAV readback setup failed");
    D3D12_RESOURCE_BARRIER uav_barrier = {};
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    uav_barrier.Transition.pResource = root_uav_data;
    uav_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    uav_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    uav_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &uav_barrier);
    list->CopyBufferRegion(uav_readback, 0, root_uav_data, 0, sizeof(float) * 4);
  }

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = render_target;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
  D3D12_TEXTURE_COPY_LOCATION copy_dst = {};
  copy_dst.pResource = readback;
  copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  copy_dst.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION copy_src = {};
  copy_src.pResource = render_target;
  copy_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, nullptr);
  if (!CheckHR("Close", list->Close()))
    return fail("command list close failed");

  ID3D12CommandList *command_lists[] = {list};
  queue->ExecuteCommandLists(1, command_lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
      !CheckHR("Signal", queue->Signal(fence, 1)))
    return fail("queue submission failed");
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event || !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    return fail("fence setup failed");
  if (WaitForSingleObject(event, INFINITE) != WAIT_OBJECT_0)
    return fail("queue wait failed");

  if (!CheckHR("MapReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_readback))))
    return fail("readback mapping failed");
  const UINT pixel = *reinterpret_cast<const UINT *>(mapped_readback);
  readback->Unmap(0, nullptr);
  mapped_readback = nullptr;
  if (test.geometry_root_srv_uav) {
    if (!CheckHR("MapRootUAVReadback", uav_readback->Map(0, nullptr, &mapped_uav_readback)))
      return fail("root UAV readback mapping failed");
    static constexpr uint32_t expected_root_uav[] = {0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u};
    if (std::memcmp(mapped_uav_readback, expected_root_uav, sizeof(expected_root_uav)) != 0) {
      uav_readback->Unmap(0, nullptr);
      mapped_uav_readback = nullptr;
      return fail("root UAV data mismatch");
    }
    uav_readback->Unmap(0, nullptr);
    mapped_uav_readback = nullptr;
  }
  if ((pixel & 0x00ffffffu) != test.expected_rgb) {
    std::cerr << "DXBC SM5 " << test.name << ": readback mismatch: 0x" << std::hex << pixel
              << " (expected 0x" << test.expected_rgb << ")" << std::dec << "\n";
    cleanup();
    return false;
  }

  std::cout << "DXBC SM5 " << test.name << " readback passed: 0x" << std::hex << pixel << std::dec << "\n";
  cleanup();
  return true;
}

} // namespace

int main(int argc, char **argv) {
  static constexpr TestCase all_cases[] = {
      {"list", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false, false, false, false, false, 0x00ffffffu},
      {"strip", D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, false, false, false, false, false, 0x00ffffffu},
      {"indexed16", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true, false, false, false, false, 0x00ffffffu},
      {"indexed32", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true, true, false, false, false, 0x00ffffffu},
      {"indexed-strip16", D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, true, false, false, false, false, 0x00ffffffu},
      {"indexed-strip32", D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, true, true, false, false, false, 0x00ffffffu},
      {"adj", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ, true, false, true, false, false, 0x00ffffffu},
      {"adj-strip", D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ, true, true, true, false, false, 0x00ffffffu},
      {"root-cbv", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false, false, false, true, false, 0x0000ff00u},
      {"indirect", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false, false, false, false, true, 0x00ffffffu},
      {"indirect-indexed32", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true, true, false, false, true, 0x00ffffffu},
      {"zero-index-view", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false, false, false, false, false, 0x00ffffffu,
       false, false, true},
      {"no-input-gs", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false, false, false, false, false, 0x00ffffffu, true,
       false},
      {"geometry-root-cbv", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false, false, false, false, false, 0x0000ff00u,
       false, true},
      {"geometry-root-srv-uav", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false, false, false, false, false,
       0x0000ff00u, false, false, false, true},
      {"null-texture-query", D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false, false, false, false, false,
       0x00ffffffu, false, false, false, false, true},
  };

  std::vector<const TestCase *> selected;
  if (argc == 1 || (argc == 2 && std::strcmp(argv[1], "--all") == 0)) {
    for (const auto &test : all_cases)
      selected.push_back(&test);
  } else if (argc >= 2 && std::strcmp(argv[1], "--help") == 0) {
    std::cout << "usage: dx12_graphics_sm5 [--all|case ...]\n";
    for (const auto &test : all_cases)
      std::cout << "  " << test.name << "\n";
    return 0;
  } else {
    for (int arg = 1; arg < argc; ++arg) {
      const TestCase *match = nullptr;
      for (const auto &test : all_cases) {
        if (std::strcmp(argv[arg], test.name) == 0) {
          match = &test;
          break;
        }
      }
      if (!match) {
        std::cerr << "unknown SM5 geometry test: " << argv[arg] << "\n";
        return 2;
      }
      selected.push_back(match);
    }
  }

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

  ShaderSet shaders;
  if (!CompileShaders(compile_shader, shaders)) {
    FreeLibrary(compiler);
    std::cerr << "SM5 shader compilation failed\n";
    return 1;
  }
  FreeLibrary(compiler);

  ID3D12Device *device = nullptr;
  if (!CheckHR("D3D12CreateDevice",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return 1;

  bool result = true;
  for (const auto *test : selected)
    result = RunCase(device, shaders, *test) && result;
  Release(device);
  return result ? 0 : 1;
}

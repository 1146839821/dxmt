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
  if (argc < 3 || argc > 4)
    return 2;
  const bool textured = argc == 4 && strcmp(argv[3], "--texture") == 0;
  const bool root_cbv = argc == 4 && strcmp(argv[3], "--root-cbv") == 0;
  const bool root_constants = argc == 4 && strcmp(argv[3], "--root-constants") == 0;
  const bool root_srv = argc == 4 && strcmp(argv[3], "--root-srv") == 0;
  const bool root_uav = argc == 4 && strcmp(argv[3], "--root-uav") == 0;
  const bool textured_root_cbv = argc == 4 && strcmp(argv[3], "--texture-root-cbv") == 0;
  if (argc == 4 && !textured && !root_cbv && !root_constants && !root_srv && !root_uav && !textured_root_cbv)
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
  static const float root_color[] = {0.0f, 1.0f, 0.0f, 1.0f};
  static const UINT root_color_bits[] = {0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u};

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  ID3D12DescriptorHeap *resource_heap = nullptr;
  ID3D12DescriptorHeap *sampler_heap = nullptr;
  ID3D12QueryHeap *query_heap = nullptr;
  ID3D12QueryHeap *timestamp_heap = nullptr;
  ID3D12Resource *render_target = nullptr;
  ID3D12Resource *vertex_buffer = nullptr;
  ID3D12Resource *root_data_buffer = nullptr;
  ID3D12Resource *root_uav_buffer = nullptr;
  ID3D12Resource *texture = nullptr;
  ID3D12Resource *texture_upload = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12Resource *query_readback = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  ID3D12CommandList *lists[1] = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_DESCRIPTOR_RANGE descriptor_ranges[2] = {};
  D3D12_ROOT_PARAMETER root_parameters[3] = {};
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES upload_heap = {};
  D3D12_HEAP_PROPERTIES texture_upload_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC render_target_desc = {};
  D3D12_RESOURCE_DESC buffer_desc = {};
  D3D12_RESOURCE_DESC texture_desc = {};
  D3D12_RESOURCE_DESC readback_desc = {};
  D3D12_RESOURCE_DESC query_readback_desc = {};
  D3D12_QUERY_HEAP_DESC query_heap_desc = {};
  D3D12_CLEAR_VALUE clear_value = {};
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC resource_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
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
  D3D12_TEXTURE_COPY_LOCATION texture_upload_dst = {};
  D3D12_TEXTURE_COPY_LOCATION texture_upload_src = {};
  D3D12_RESOURCE_BARRIER barrier = {};
  D3D12_RESOURCE_BARRIER root_uav_barrier = {};
  D3D12_RESOURCE_BARRIER texture_barrier = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  D3D12_SAMPLER_DESC sampler_desc = {};
  D3D12_CPU_DESCRIPTOR_HANDLE resource_cpu = {};
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu = {};
  ID3D12DescriptorHeap *descriptor_heaps[2] = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  void *mapped_upload = nullptr;
  void *mapped_root_data = nullptr;
  void *mapped_texture_upload = nullptr;
  BYTE *mapped_readback = nullptr;
  UINT64 *mapped_query_readback = nullptr;
  UINT pixel = 0;
  UINT expected_pixel = 0;
  UINT64 query_result = 0;
  UINT64 timestamp_frequency = 0;
  UINT64 timestamp_begin = 0;
  UINT64 timestamp_end = 0;
  UINT64 calibration_gpu = 0;
  UINT64 calibration_cpu = 0;
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
  query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  query_heap_desc.Count = 1;
  if (!CheckHR("CreateQueryHeap", device->CreateQueryHeap(&query_heap_desc, IID_PPV_ARGS(&query_heap))))
    goto cleanup;
  query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  query_heap_desc.Count = 2;
  if (!CheckHR("CreateTimestampQueryHeap", device->CreateQueryHeap(&query_heap_desc, IID_PPV_ARGS(&timestamp_heap))))
    goto cleanup;
  if (!CheckHR("GetTimestampFrequency", queue->GetTimestampFrequency(&timestamp_frequency)))
    goto cleanup;
  if (!timestamp_frequency) {
    std::cerr << "timestamp frequency returned zero\n";
    goto cleanup;
  }
  if (!CheckHR("GetClockCalibration", queue->GetClockCalibration(&calibration_gpu, &calibration_cpu)))
    goto cleanup;
  if (!calibration_gpu || !calibration_cpu) {
    std::cerr << "clock calibration returned zero\n";
    goto cleanup;
  }

  if (root_cbv) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_desc.NumParameters = 1;
    root_desc.pParameters = root_parameters;
  } else if (root_constants) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].Constants.Num32BitValues = 4;
    root_parameters[0].Constants.ShaderRegister = 0;
    root_parameters[0].Constants.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_desc.NumParameters = 1;
    root_desc.pParameters = root_parameters;
  } else if (root_srv) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_desc.NumParameters = 1;
    root_desc.pParameters = root_parameters;
  } else if (root_uav) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_desc.NumParameters = 1;
    root_desc.pParameters = root_parameters;
  } else if (textured_root_cbv) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    descriptor_ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    descriptor_ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0};
    for (unsigned i = 0; i < 2; i++) {
      root_parameters[i + 1].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[i + 1].DescriptorTable.NumDescriptorRanges = 1;
      root_parameters[i + 1].DescriptorTable.pDescriptorRanges =
          &descriptor_ranges[i];
    }
    root_desc.NumParameters = 3;
    root_desc.pParameters = root_parameters;
  } else if (textured) {
    descriptor_ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    descriptor_ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0};
    for (unsigned i = 0; i < 2; i++) {
      root_parameters[i].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[i].DescriptorTable.NumDescriptorRanges = 1;
      root_parameters[i].DescriptorTable.pDescriptorRanges =
          &descriptor_ranges[i];
    }
    root_desc.NumParameters = 2;
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

  if (textured || textured_root_cbv) {
    resource_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    resource_heap_desc.NumDescriptors = 1;
    resource_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_heap_desc.NumDescriptors = 1;
    sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!CheckHR("CreateResourceHeap",
                 device->CreateDescriptorHeap(&resource_heap_desc,
                                              IID_PPV_ARGS(&resource_heap))))
      goto cleanup;
    if (!CheckHR("CreateSamplerHeap",
                 device->CreateDescriptorHeap(&sampler_heap_desc,
                                              IID_PPV_ARGS(&sampler_heap))))
      goto cleanup;
    texture_upload_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    texture_upload_heap.CreationNodeMask = 1;
    texture_upload_heap.VisibleNodeMask = 1;
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = 1;
    texture_desc.Height = 1;
    texture_desc.DepthOrArraySize = 1;
    texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    if (!CheckHR("CreateTexture",
                 device->CreateCommittedResource(
                     &texture_upload_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                     IID_PPV_ARGS(&texture))))
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
    if (!CheckHR("CreateTextureUpload",
                 device->CreateCommittedResource(
                     &upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                     IID_PPV_ARGS(&texture_upload))))
      goto cleanup;
    device->GetCopyableFootprints(&texture_desc, 0, 1, 0,
                                  &texture_upload_dst.PlacedFootprint,
                                  &row_count, &row_size, &total_size);
    if (!CheckHR("MapTextureUpload",
                 texture_upload->Map(0, nullptr, &mapped_texture_upload)))
      goto cleanup;
    static const UINT texture_pixel = 0xff0000ff;
    memcpy(mapped_texture_upload, &texture_pixel, sizeof(texture_pixel));
    texture_upload->Unmap(0, nullptr);

    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    resource_cpu = resource_heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateShaderResourceView(texture, &srv_desc, resource_cpu);

    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    sampler_cpu = sampler_heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateSampler(&sampler_desc, sampler_cpu);
  }

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

  if (root_cbv || root_srv || textured_root_cbv) {
    buffer_desc.Width = 256;
    if (!CheckHR("CreateRootData",
                 device->CreateCommittedResource(
                     &upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                     IID_PPV_ARGS(&root_data_buffer))))
      goto cleanup;
    if (!CheckHR("MapRootData",
                 root_data_buffer->Map(0, nullptr, &mapped_root_data)))
      goto cleanup;
    memcpy(mapped_root_data, root_color, sizeof(root_color));
    root_data_buffer->Unmap(0, nullptr);
  }
  if (root_uav) {
    buffer_desc.Width = 256;
    buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!CheckHR("CreateRootUAV",
                 device->CreateCommittedResource(
                     &default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                     IID_PPV_ARGS(&root_uav_buffer))))
      goto cleanup;
  }

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
  if (textured || textured_root_cbv) {
    texture_upload_dst.pResource = texture;
    texture_upload_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    texture_upload_src.pResource = texture_upload;
    texture_upload_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    texture_upload_src.PlacedFootprint = texture_upload_dst.PlacedFootprint;
    list->CopyTextureRegion(&texture_upload_dst, 0, 0, 0, &texture_upload_src,
                            nullptr);
    texture_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    texture_barrier.Transition.pResource = texture;
    texture_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    texture_barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    texture_barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &texture_barrier);
  }
  list->SetPipelineState(pso);
  list->SetGraphicsRootSignature(root_signature);
  if (textured || textured_root_cbv) {
    descriptor_heaps[0] = resource_heap;
    descriptor_heaps[1] = sampler_heap;
    list->SetDescriptorHeaps(2, descriptor_heaps);
    const UINT resource_root_index = textured_root_cbv ? 1 : 0;
    const UINT sampler_root_index = textured_root_cbv ? 2 : 1;
    list->SetGraphicsRootDescriptorTable(
        resource_root_index, resource_heap->GetGPUDescriptorHandleForHeapStart());
    list->SetGraphicsRootDescriptorTable(
        sampler_root_index, sampler_heap->GetGPUDescriptorHandleForHeapStart());
  }
  if (root_cbv || textured_root_cbv)
    list->SetGraphicsRootConstantBufferView(0, root_data_buffer->GetGPUVirtualAddress());
  if (root_constants)
    list->SetGraphicsRoot32BitConstants(0, 4, root_color_bits, 0);
  if (root_srv)
    list->SetGraphicsRootShaderResourceView(0, root_data_buffer->GetGPUVirtualAddress());
  if (root_uav)
    list->SetGraphicsRootUnorderedAccessView(0, root_uav_buffer->GetGPUVirtualAddress());
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->IASetVertexBuffers(0, 1, &vertex_view);
  list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
  list->EndQuery(timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
  list->BeginQuery(query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0);
  list->DrawInstanced(3, 1, 0, 0);
  list->EndQuery(query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0);
  list->EndQuery(timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);

  if (root_uav) {
    root_uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    root_uav_barrier.Transition.pResource = root_uav_buffer;
    root_uav_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    root_uav_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    root_uav_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &root_uav_barrier);
    readback_desc.Width = 256;
  } else {
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = render_target;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);

    device->GetCopyableFootprints(&render_target_desc, 0, 1, 0, &footprint,
                                  &row_count, &row_size, &total_size);
    readback_desc.Width = total_size;
  }
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;
  readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readback_desc.Height = 1;
  readback_desc.DepthOrArraySize = 1;
  readback_desc.MipLevels = 1;
  readback_desc.SampleDesc.Count = 1;
  readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  query_readback_desc = readback_desc;
  query_readback_desc.Width = sizeof(UINT64) * 3;
  if (!CheckHR("CreateQueryReadback",
               device->CreateCommittedResource(
                   &readback_heap, D3D12_HEAP_FLAG_NONE, &query_readback_desc,
                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&query_readback))))
    goto cleanup;
  if (!CheckHR("CreateReadbackBuffer",
               device->CreateCommittedResource(
                   &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                   IID_PPV_ARGS(&readback))))
    goto cleanup;
  if (root_uav) {
    list->CopyBufferRegion(readback, 0, root_uav_buffer, 0, sizeof(UINT));
  } else {
    copy_dst.pResource = readback;
    copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    copy_dst.PlacedFootprint = footprint;
    copy_src.pResource = render_target;
    copy_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, nullptr);
  }
  list->ResolveQueryData(query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, query_readback, sizeof(UINT64) * 2);
  list->ResolveQueryData(timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, query_readback, 0);
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
  if (!CheckHR("MapQueryReadback",
               query_readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_query_readback))))
    goto cleanup;
  timestamp_begin = mapped_query_readback[0];
  timestamp_end = mapped_query_readback[1];
  query_result = mapped_query_readback[2];
  query_readback->Unmap(0, nullptr);
  if (!query_result) {
    std::cerr << "occlusion query returned zero\n";
    goto cleanup;
  }
  if (timestamp_end <= timestamp_begin) {
    std::cerr << "timestamp query did not advance: " << timestamp_begin << " -> " << timestamp_end << "\n";
    goto cleanup;
  }
  expected_pixel = (root_cbv || root_constants || root_srv || root_uav || textured_root_cbv) ? 0xff00ff00u
                                                                                         : 0xff0000ffu;
  if ((pixel & 0x00ffffffu) != (expected_pixel & 0x00ffffffu)) {
    std::cerr << "graphics readback mismatch: 0x" << std::hex << pixel
              << std::dec << "\n";
    goto cleanup;
  }
  std::cout << "DXIL "
            << (root_cbv ? "root CBV graphics"
                : root_constants ? "root constants graphics"
                : root_srv ? "root SRV graphics"
                : root_uav ? "root UAV graphics"
                : textured_root_cbv ? "root CBV textured graphics"
                : textured ? "textured graphics"
                           : "graphics")
            << " readback passed: 0x" << std::hex << pixel << std::dec << "\n";
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
  if (query_readback)
    query_readback->Release();
  if (texture_upload)
    texture_upload->Release();
  if (texture)
    texture->Release();
  if (root_uav_buffer)
    root_uav_buffer->Release();
  if (root_data_buffer)
    root_data_buffer->Release();
  if (vertex_buffer)
    vertex_buffer->Release();
  if (render_target)
    render_target->Release();
  if (rtv_heap)
    rtv_heap->Release();
  if (sampler_heap)
    sampler_heap->Release();
  if (resource_heap)
    resource_heap->Release();
  if (query_heap)
    query_heap->Release();
  if (timestamp_heap)
    timestamp_heap->Release();
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

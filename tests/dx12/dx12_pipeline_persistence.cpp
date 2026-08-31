#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

template <typename T> void
Release(T *&object) {
  if (object)
    object->Release();
  object = nullptr;
}

bool
ReadFile(const char *path, std::vector<char> &data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return false;
  auto size = file.tellg();
  if (size <= 0)
    return false;
  file.seekg(0);
  data.resize(static_cast<size_t>(size));
  return file.read(data.data(), data.size()).good();
}

bool
CheckHR(const char *name, HRESULT actual, HRESULT expected = S_OK) {
  if (actual == expected)
    return true;
  std::cerr << name << " returned 0x" << std::hex << static_cast<unsigned long>(actual) << ", expected 0x"
            << static_cast<unsigned long>(expected) << std::dec << "\n";
  return false;
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
  if (WaitForSingleObject(event, INFINITE) != WAIT_OBJECT_0)
    goto cleanup;
  result = true;

cleanup:
  if (event)
    CloseHandle(event);
  Release(fence);
  return result;
}

bool
RunCompute(ID3D12Device *device, ID3D12PipelineState *pipeline, ID3D12RootSignature *root_signature) {
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12Resource *output = nullptr;
  ID3D12Resource *readback = nullptr;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC output_desc = {};
  D3D12_RESOURCE_DESC readback_desc = {};
  D3D12_RESOURCE_BARRIER barrier = {};
  UINT *mapped = nullptr;
  bool result = false;

  if (!root_signature)
    goto cleanup;
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;
  output_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  output_desc.Width = 256;
  output_desc.Height = 1;
  output_desc.DepthOrArraySize = 1;
  output_desc.MipLevels = 1;
  output_desc.SampleDesc.Count = 1;
  output_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  output_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  readback_desc = output_desc;
  readback_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

  if (!CheckHR(
          "Create compute output",
          device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &output_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
              IID_PPV_ARGS(&output)
          )
      ) ||
      !CheckHR(
          "Create compute readback",
          device->CreateCommittedResource(
              &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&readback)
          )
      ) ||
      !CheckHR("Create compute queue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !CheckHR(
          "Create compute allocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))
      ) ||
      !CheckHR(
          "Create compute command list",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipeline, IID_PPV_ARGS(&list))
      ))
    goto cleanup;

  list->SetComputeRootSignature(root_signature);
  list->SetComputeRootUnorderedAccessView(0, output->GetGPUVirtualAddress());
  list->Dispatch(1, 1, 1);
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = output;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
  list->CopyBufferRegion(readback, 0, output, 0, sizeof(UINT));
  if (!CheckHR("Close compute command list", list->Close()) || !WaitForQueue(device, queue, list))
    goto cleanup;
  if (!CheckHR("Map compute readback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    goto cleanup;
  if (*mapped != 1234) {
    std::cerr << "compute readback mismatch: " << *mapped << "\n";
    readback->Unmap(0, nullptr);
    goto cleanup;
  }
  readback->Unmap(0, nullptr);
  result = true;

cleanup:
  Release(readback);
  Release(output);
  Release(list);
  Release(allocator);
  Release(queue);
  return result;
}

bool RunGraphics(ID3D12Device *device, ID3D12PipelineState *pipeline,
                 bool geometry) {
  struct Vertex {
    float position[2];
    float color[4];
  };
  static const Vertex vertices[] = {
      {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{3.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{-1.0f, 3.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
  };
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12Resource *render_target = nullptr;
  ID3D12Resource *vertex_buffer = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES upload_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC render_target_desc = {};
  D3D12_RESOURCE_DESC vertex_desc = {};
  D3D12_RESOURCE_DESC readback_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  D3D12_CLEAR_VALUE clear_value = {};
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
  D3D12_RESOURCE_BARRIER barrier = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  D3D12_TEXTURE_COPY_LOCATION copy_dst = {};
  D3D12_TEXTURE_COPY_LOCATION copy_src = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  D3D12_VIEWPORT viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  D3D12_RECT scissor = {0, 0, 1, 1};
  void *mapped_upload = nullptr;
  BYTE *mapped_readback = nullptr;
  UINT pixel = 0;
  UINT expected_pixel = 0;
  bool result = false;

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_heap.CreationNodeMask = 1;
  upload_heap.VisibleNodeMask = 1;
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;

  render_target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  render_target_desc.Width = 1;
  render_target_desc.Height = 1;
  render_target_desc.DepthOrArraySize = 1;
  render_target_desc.MipLevels = 1;
  render_target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  render_target_desc.SampleDesc.Count = 1;
  render_target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  clear_value.Format = render_target_desc.Format;
  clear_value.Color[3] = 1.0f;
  vertex_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  vertex_desc.Width = sizeof(vertices);
  vertex_desc.Height = 1;
  vertex_desc.DepthOrArraySize = 1;
  vertex_desc.MipLevels = 1;
  vertex_desc.SampleDesc.Count = 1;
  vertex_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;

  if (!CheckHR("Create graphics queue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !CheckHR(
          "Create graphics allocator",
          device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))
      ) ||
      !CheckHR(
          "Create render target",
          device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &render_target_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
              &clear_value, IID_PPV_ARGS(&render_target)
          )
      ) ||
      !CheckHR("Create RTV heap", device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap))) ||
      !CheckHR(
          "Create vertex buffer",
          device->CreateCommittedResource(
              &upload_heap, D3D12_HEAP_FLAG_NONE, &vertex_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
              IID_PPV_ARGS(&vertex_buffer)
          )
      ) ||
      !CheckHR("Map vertex buffer", vertex_buffer->Map(0, nullptr, &mapped_upload)))
    goto cleanup;
  memcpy(mapped_upload, vertices, sizeof(vertices));
  vertex_buffer->Unmap(0, nullptr);
  rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(render_target, nullptr, rtv);
  vertex_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = sizeof(vertices);
  vertex_view.StrideInBytes = sizeof(Vertex);
  device->GetCopyableFootprints(&render_target_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size);

  readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readback_desc.Width = total_size;
  readback_desc.Height = 1;
  readback_desc.DepthOrArraySize = 1;
  readback_desc.MipLevels = 1;
  readback_desc.SampleDesc.Count = 1;
  readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!CheckHR(
          "Create graphics readback",
          device->CreateCommittedResource(
              &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&readback)
          )
      ) ||
      !CheckHR(
          "Create graphics command list",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipeline, IID_PPV_ARGS(&list))
      ))
    goto cleanup;

  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->IASetVertexBuffers(0, 1, &vertex_view);
  list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  list->DrawInstanced(3, 1, 0, 0);
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = render_target;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
  copy_dst.pResource = readback;
  copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  copy_dst.PlacedFootprint = footprint;
  copy_src.pResource = render_target;
  copy_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, nullptr);
  if (!CheckHR("Close graphics command list", list->Close()) || !WaitForQueue(device, queue, list))
    goto cleanup;
  if (!CheckHR("Map graphics readback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_readback))))
    goto cleanup;
  pixel = *reinterpret_cast<const UINT *>(mapped_readback);
  readback->Unmap(0, nullptr);
  expected_pixel = geometry ? 0x0000ff00u : 0x000000ffu;
  if ((pixel & 0x00ffffffu) != expected_pixel) {
    std::cerr << "loaded graphics readback mismatch: 0x" << std::hex << pixel << std::dec << "\n";
    goto cleanup;
  }
  result = true;

cleanup:
  Release(readback);
  Release(rtv_heap);
  Release(vertex_buffer);
  Release(render_target);
  Release(list);
  Release(allocator);
  Release(queue);
  return result;
}

template <typename T, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type> struct alignas(void *) StreamSubobject {
  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
  T value = {};
};

struct ComputeStream {
  StreamSubobject<ID3D12RootSignature *, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE> root_signature;
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS> cs;
};

struct DuplicateComputeStream {
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS> first;
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS> second;
};

struct UnsupportedGraphicsStream {
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS> gs;
};

struct GraphicsStream {
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS> vs;
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> ps;
  StreamSubobject<D3D12_INPUT_LAYOUT_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT> input_layout;
  StreamSubobject<D3D12_PRIMITIVE_TOPOLOGY_TYPE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY> topology;
  StreamSubobject<D3D12_RT_FORMAT_ARRAY, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS> render_targets;
  StreamSubobject<DXGI_SAMPLE_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC> sample_desc;
  StreamSubobject<UINT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK> sample_mask;
  StreamSubobject<D3D12_RASTERIZER_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER> rasterizer;
  StreamSubobject<D3D12_BLEND_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> blend;
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS> gs;
};

struct InvalidTypeStream {
  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
  UINT value;
};

} // namespace

int
main(int argc, char **argv) {
  if (argc != 4 && argc != 5) {
    std::cerr << "usage: dx12_pipeline_persistence <compute_root_uav.cso> "
                 "<vertex.cso> <pixel.cso> [geometry.cso]\n";
    return 2;
  }

  std::vector<char> compute_shader;
  std::vector<char> vertex_shader;
  std::vector<char> pixel_shader;
  std::vector<char> geometry_shader;
  const bool geometry = argc == 5;
  if (!ReadFile(argv[1], compute_shader) || !ReadFile(argv[2], vertex_shader) ||
      !ReadFile(argv[3], pixel_shader) ||
      (geometry && !ReadFile(argv[4], geometry_shader))) {
    std::cerr << "failed to read shader fixture\n";
    return 3;
  }

  ID3D12Device *device = nullptr;
  ID3D12Device1 *device1 = nullptr;
  ID3D12Device2 *device2 = nullptr;
  ID3D12Device *other_device = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  ID3D12PipelineState *compute_pso = nullptr;
  ID3D12PipelineState *graphics_pso = nullptr;
  ID3D12PipelineState *compute_cached_pso = nullptr;
  ID3D12PipelineState *graphics_cached_pso = nullptr;
  ID3D12PipelineState *compute_stream_pso = nullptr;
  ID3D12PipelineState *graphics_stream_pso = nullptr;
  ID3D12PipelineState *other_compute_pso = nullptr;
  ID3D12PipelineState *malformed_token_pso = nullptr;
  ID3D12PipelineState *stale_token_pso = nullptr;
  ID3D12PipelineState *foreign_token_pso = nullptr;
  ID3D12PipelineState *invalid_descriptor_pso = nullptr;
  ID3D12PipelineState *canonical_disabled_pso = nullptr;
  ID3D12PipelineState *inactive_graphics_pso = nullptr;
  ID3D12PipelineState *inactive_graphics_changed_pso = nullptr;
  ID3D12PipelineState *invalid_stream_pso = nullptr;
  ID3D12PipelineState *loaded_compute = nullptr;
  ID3D12PipelineState *loaded_graphics = nullptr;
  ID3DBlob *compute_blob = nullptr;
  ID3DBlob *graphics_blob = nullptr;
  ID3DBlob *canonical_disabled_blob = nullptr;
  ID3DBlob *inactive_graphics_blob = nullptr;
  ID3DBlob *inactive_graphics_changed_blob = nullptr;
  ID3D12PipelineLibrary *library_base = nullptr;
  ID3D12PipelineLibrary1 *library = nullptr;
  ID3D12PipelineLibrary1 *rehydrated_library = nullptr;
  ID3D12PipelineLibrary1 *bad_library = nullptr;
  const D3D12_INPUT_ELEMENT_DESC input_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc = {};
  D3D12_ROOT_PARAMETER root_parameter = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_desc = {};
  D3D12_COMPUTE_PIPELINE_STATE_DESC mismatched_compute_desc = {};
  ComputeStream compute_stream = {};
  DuplicateComputeStream duplicate_compute_stream = {};
  UnsupportedGraphicsStream unsupported_graphics_stream = {};
  GraphicsStream graphics_stream = {};
  InvalidTypeStream invalid_type_stream = {};
  D3D12_PIPELINE_STATE_STREAM_DESC compute_stream_desc = {};
  D3D12_PIPELINE_STATE_STREAM_DESC duplicate_compute_stream_desc = {};
  D3D12_PIPELINE_STATE_STREAM_DESC unsupported_graphics_stream_desc = {};
  D3D12_PIPELINE_STATE_STREAM_DESC graphics_stream_desc = {};
  std::vector<BYTE> serialized;
  std::vector<BYTE> serialized_again;
  int result = 1;

  auto cleanup = [&] {
    Release(bad_library);
    Release(rehydrated_library);
    Release(library_base);
    Release(library);
    Release(other_compute_pso);
    Release(loaded_graphics);
    Release(loaded_compute);
    Release(invalid_stream_pso);
    Release(inactive_graphics_changed_pso);
    Release(inactive_graphics_pso);
    Release(canonical_disabled_pso);
    Release(invalid_descriptor_pso);
    Release(foreign_token_pso);
    Release(stale_token_pso);
    Release(malformed_token_pso);
    Release(graphics_stream_pso);
    Release(compute_stream_pso);
    Release(graphics_cached_pso);
    Release(compute_cached_pso);
    Release(graphics_pso);
    Release(compute_pso);
    Release(graphics_blob);
    Release(compute_blob);
    Release(canonical_disabled_blob);
    Release(inactive_graphics_changed_blob);
    Release(inactive_graphics_blob);
    Release(other_device);
    Release(root_signature);
    Release(root_error);
    Release(root_blob);
    Release(device2);
    Release(device1);
    Release(device);
  };
  auto fail = [&](const char *message) {
    std::cerr << message << "\n";
    cleanup();
    return result;
  };

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    return fail("device creation failed");
  if (!CheckHR("Query ID3D12Device1", device->QueryInterface(IID_PPV_ARGS(&device1))) ||
      !CheckHR("Query ID3D12Device2", device->QueryInterface(IID_PPV_ARGS(&device2))))
    return fail("device interface query failed");

  root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  root_parameter.Descriptor.ShaderRegister = 0;
  root_desc.NumParameters = 1;
  root_desc.pParameters = &root_parameter;
  if (!CheckHR(
          "D3D12SerializeRootSignature",
          D3D12SerializeRootSignature(
              &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error
          )
      ) ||
      !CheckHR(
          "CreateRootSignature",
          device->CreateRootSignature(
              0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)
          )
      ))
    return fail("root signature creation failed");

  compute_desc.CS = {compute_shader.data(), compute_shader.size()};
  compute_desc.pRootSignature = root_signature;
  if (!CheckHR(
          "CreateComputePipelineState",
          device->CreateComputePipelineState(&compute_desc, IID_PPV_ARGS(&compute_pso))
      ))
    return fail("compute PSO creation failed");
  if (!CheckHR("Compute GetCachedBlob", compute_pso->GetCachedBlob(&compute_blob)) || !compute_blob)
    return fail("compute cache blob failed");

  auto compute_cached_desc = compute_desc;
  compute_cached_desc.CachedPSO = {compute_blob->GetBufferPointer(), compute_blob->GetBufferSize()};
  if (!CheckHR(
          "CreateComputePipelineState from cache token",
          device->CreateComputePipelineState(&compute_cached_desc, IID_PPV_ARGS(&compute_cached_pso))
      ))
    return fail("compute cache-token creation failed");

  std::vector<BYTE> malformed_token(compute_blob->GetBufferSize());
  memcpy(malformed_token.data(), compute_blob->GetBufferPointer(), malformed_token.size());
  malformed_token[0] ^= 0xff;
  auto malformed_compute_desc = compute_desc;
  malformed_compute_desc.CachedPSO = {malformed_token.data(), malformed_token.size()};
  if (!CheckHR(
          "CreateComputePipelineState from malformed token",
          device->CreateComputePipelineState(&malformed_compute_desc, IID_PPV_ARGS(&malformed_token_pso))
      ))
    return fail("malformed token was not treated as a cache miss");

  std::vector<BYTE> stale_token(compute_blob->GetBufferSize());
  memcpy(stale_token.data(), compute_blob->GetBufferPointer(), stale_token.size());
  stale_token[sizeof("DXMTPCH1") - 1 + sizeof(uint32_t) * 2]++;
  auto stale_compute_desc = compute_desc;
  stale_compute_desc.CachedPSO = {stale_token.data(), stale_token.size()};
  if (!CheckHR(
          "CreateComputePipelineState from stale token",
          device->CreateComputePipelineState(&stale_compute_desc, IID_PPV_ARGS(&stale_token_pso))
      ))
    return fail("stale token was not treated as a cache miss");

  graphics_desc.VS = {vertex_shader.data(), vertex_shader.size()};
  graphics_desc.PS = {pixel_shader.data(), pixel_shader.size()};
  if (geometry)
    graphics_desc.GS = {geometry_shader.data(), geometry_shader.size()};
  graphics_desc.InputLayout = {input_layout, 2};
  graphics_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  graphics_desc.NumRenderTargets = 1;
  graphics_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  graphics_desc.SampleDesc.Count = 1;
  graphics_desc.SampleMask = UINT_MAX;
  graphics_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  graphics_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  graphics_desc.RasterizerState.DepthClipEnable = TRUE;
  graphics_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  if (!CheckHR(
          "CreateGraphicsPipelineState",
          device->CreateGraphicsPipelineState(&graphics_desc, IID_PPV_ARGS(&graphics_pso))
      ))
    return fail("graphics PSO creation failed");
  if (!CheckHR("Graphics GetCachedBlob", graphics_pso->GetCachedBlob(&graphics_blob)) || !graphics_blob)
    return fail("graphics cache blob failed");

  auto canonical_disabled_desc = graphics_desc;
  canonical_disabled_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  canonical_disabled_desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_CLEAR;
  canonical_disabled_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  canonical_disabled_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  canonical_disabled_desc.DepthStencilState.StencilReadMask = 1;
  canonical_disabled_desc.DepthStencilState.StencilWriteMask = 1;
  canonical_disabled_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  canonical_disabled_desc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  if (!CheckHR(
          "CreateGraphicsPipelineState with ignored blend fields",
          device->CreateGraphicsPipelineState(&canonical_disabled_desc, IID_PPV_ARGS(&canonical_disabled_pso))
      ) ||
      !CheckHR("Ignored blend fields GetCachedBlob", canonical_disabled_pso->GetCachedBlob(&canonical_disabled_blob)) ||
      !canonical_disabled_blob ||
      canonical_disabled_blob->GetBufferSize() != graphics_blob->GetBufferSize() ||
      memcmp(
          canonical_disabled_blob->GetBufferPointer(), graphics_blob->GetBufferPointer(), graphics_blob->GetBufferSize()
      ))
    return fail("ignored blend fields changed the persistence key");

  auto foreign_compute_desc = compute_desc;
  foreign_compute_desc.CachedPSO = {graphics_blob->GetBufferPointer(), graphics_blob->GetBufferSize()};
  if (!CheckHR(
          "CreateComputePipelineState from foreign token",
          device->CreateComputePipelineState(&foreign_compute_desc, IID_PPV_ARGS(&foreign_token_pso))
      ))
    return fail("foreign token was not treated as a cache miss");

  auto invalid_graphics_desc = graphics_desc;
  invalid_graphics_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xff;
  if (!CheckHR(
          "CreateGraphicsPipelineState with invalid write mask",
          device->CreateGraphicsPipelineState(&invalid_graphics_desc, IID_PPV_ARGS(&invalid_descriptor_pso)), E_INVALIDARG
      ))
    return fail("invalid graphics descriptor was accepted");

  auto invalid_sample_desc = graphics_desc;
  invalid_sample_desc.SampleDesc.Quality = 1;
  if (!CheckHR(
          "CreateGraphicsPipelineState with invalid sample quality",
          device->CreateGraphicsPipelineState(&invalid_sample_desc, IID_PPV_ARGS(&invalid_descriptor_pso)), E_INVALIDARG
      ))
    return fail("invalid sample quality was accepted");

  auto invalid_node_mask_desc = graphics_desc;
  invalid_node_mask_desc.NodeMask = 2;
  if (!CheckHR(
          "CreateGraphicsPipelineState with invalid node mask",
          device->CreateGraphicsPipelineState(&invalid_node_mask_desc, IID_PPV_ARGS(&invalid_descriptor_pso)), E_INVALIDARG
      ))
    return fail("invalid node mask was accepted");

  D3D12_INPUT_ELEMENT_DESC invalid_input_element = input_layout[0];
  invalid_input_element.Format = DXGI_FORMAT_UNKNOWN;
  auto invalid_input_desc = graphics_desc;
  invalid_input_desc.InputLayout = {&invalid_input_element, 1};
  if (!CheckHR(
          "CreateGraphicsPipelineState with invalid input format",
          device->CreateGraphicsPipelineState(&invalid_input_desc, IID_PPV_ARGS(&invalid_descriptor_pso)), E_INVALIDARG
      ))
    return fail("invalid input format was accepted");

  auto unsupported_logic_desc = graphics_desc;
  unsupported_logic_desc.BlendState.RenderTarget[0].LogicOpEnable = TRUE;
  unsupported_logic_desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_COPY;
  if (!CheckHR(
          "CreateGraphicsPipelineState with unsupported logic op",
          device->CreateGraphicsPipelineState(&unsupported_logic_desc, IID_PPV_ARGS(&invalid_descriptor_pso)), E_NOTIMPL
      ))
    return fail("unsupported logic op was accepted");

  auto unsupported_forced_sample_desc = graphics_desc;
  unsupported_forced_sample_desc.RasterizerState.ForcedSampleCount = 1;
  if (!CheckHR(
          "CreateGraphicsPipelineState with unsupported forced sample count",
          device->CreateGraphicsPipelineState(
              &unsupported_forced_sample_desc, IID_PPV_ARGS(&invalid_descriptor_pso)
          ),
          E_NOTIMPL
      ))
    return fail("unsupported forced sample count was accepted");

  auto inactive_graphics_desc = graphics_desc;
  inactive_graphics_desc.BlendState.IndependentBlendEnable = TRUE;
  inactive_graphics_desc.BlendState.RenderTarget[1].RenderTargetWriteMask = 0;
  if (!CheckHR(
          "CreateGraphicsPipelineState with inactive blend state",
          device->CreateGraphicsPipelineState(&inactive_graphics_desc, IID_PPV_ARGS(&inactive_graphics_pso))
      ) ||
      !CheckHR("Inactive graphics GetCachedBlob", inactive_graphics_pso->GetCachedBlob(&inactive_graphics_blob)) ||
      !inactive_graphics_blob)
    return fail("inactive graphics state creation failed");

  auto inactive_graphics_changed_desc = inactive_graphics_desc;
  inactive_graphics_changed_desc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  if (!CheckHR(
          "CreateGraphicsPipelineState with changed inactive blend state",
          device->CreateGraphicsPipelineState(
              &inactive_graphics_changed_desc, IID_PPV_ARGS(&inactive_graphics_changed_pso)
          )
      ) ||
      !CheckHR(
          "Changed inactive graphics GetCachedBlob",
          inactive_graphics_changed_pso->GetCachedBlob(&inactive_graphics_changed_blob)
      ) ||
      !inactive_graphics_changed_blob ||
      inactive_graphics_blob->GetBufferSize() != graphics_blob->GetBufferSize() ||
      memcmp(
          inactive_graphics_blob->GetBufferPointer(), graphics_blob->GetBufferPointer(), graphics_blob->GetBufferSize()
      ) ||
      inactive_graphics_blob->GetBufferSize() != inactive_graphics_changed_blob->GetBufferSize() ||
      memcmp(
          inactive_graphics_blob->GetBufferPointer(), inactive_graphics_changed_blob->GetBufferPointer(),
          inactive_graphics_blob->GetBufferSize()
      ))
    return fail("inactive graphics state changed the persistence key");

  auto graphics_cached_desc = graphics_desc;
  graphics_cached_desc.CachedPSO = {graphics_blob->GetBufferPointer(), graphics_blob->GetBufferSize()};
  if (!CheckHR(
          "CreateGraphicsPipelineState from cache token",
          device->CreateGraphicsPipelineState(&graphics_cached_desc, IID_PPV_ARGS(&graphics_cached_pso))
      ))
    return fail("graphics cache-token creation failed");

  compute_stream.root_signature.value = root_signature;
  compute_stream_desc.SizeInBytes = sizeof(compute_stream);
  compute_stream_desc.pPipelineStateSubobjectStream = &compute_stream;
  {
    std::vector<BYTE> transient_stream_shader(compute_shader.begin(), compute_shader.end());
    compute_stream.cs.value = {transient_stream_shader.data(), transient_stream_shader.size()};
    if (!CheckHR(
            "CreatePipelineState compute stream",
            device2->CreatePipelineState(&compute_stream_desc, IID_PPV_ARGS(&compute_stream_pso))
        ))
      return fail("compute stream creation failed");
  }
  compute_stream.cs.value = compute_desc.CS;
  if (!RunCompute(device, compute_stream_pso, root_signature))
    return fail("compute stream caller-lifetime execution failed");

  auto truncated_stream_desc = compute_stream_desc;
  truncated_stream_desc.SizeInBytes--;
  if (!CheckHR(
          "CreatePipelineState truncated stream",
          device2->CreatePipelineState(&truncated_stream_desc, IID_PPV_ARGS(&invalid_stream_pso)), E_INVALIDARG
      ))
    return fail("truncated stream was accepted");

  duplicate_compute_stream.first.value = compute_desc.CS;
  duplicate_compute_stream.second.value = compute_desc.CS;
  duplicate_compute_stream_desc.SizeInBytes = sizeof(duplicate_compute_stream);
  duplicate_compute_stream_desc.pPipelineStateSubobjectStream = &duplicate_compute_stream;
  if (!CheckHR(
          "CreatePipelineState duplicate stream",
          device2->CreatePipelineState(&duplicate_compute_stream_desc, IID_PPV_ARGS(&invalid_stream_pso)), E_INVALIDARG
      ))
    return fail("duplicate stream subobject was accepted");

  invalid_type_stream.type = static_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE>(0x17);
  invalid_type_stream.value = 0;
  D3D12_PIPELINE_STATE_STREAM_DESC invalid_type_stream_desc = {
      sizeof(invalid_type_stream), &invalid_type_stream
  };
  if (!CheckHR(
          "CreatePipelineState invalid stream type",
          device2->CreatePipelineState(&invalid_type_stream_desc, IID_PPV_ARGS(&invalid_stream_pso)), E_INVALIDARG
      ))
    return fail("invalid stream type was accepted");

  compute_stream.cs.value.pShaderBytecode = nullptr;
  if (!CheckHR(
          "CreatePipelineState null shader stream",
          device2->CreatePipelineState(&compute_stream_desc, IID_PPV_ARGS(&invalid_stream_pso)), E_INVALIDARG
      ))
    return fail("null shader stream was accepted");
  compute_stream.cs.value.pShaderBytecode = compute_shader.data();

  unsupported_graphics_stream.gs.value = graphics_desc.GS;
  unsupported_graphics_stream_desc.SizeInBytes = sizeof(unsupported_graphics_stream);
  unsupported_graphics_stream_desc.pPipelineStateSubobjectStream = &unsupported_graphics_stream;
  if (!CheckHR(
          "CreatePipelineState unsupported stream",
          device2->CreatePipelineState(&unsupported_graphics_stream_desc, IID_PPV_ARGS(&invalid_stream_pso)), E_INVALIDARG
      ))
    return fail("incomplete graphics stream was accepted");

  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE truncated_unsupported_type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS;
  D3D12_PIPELINE_STATE_STREAM_DESC truncated_unsupported_stream_desc = {
      sizeof(truncated_unsupported_type), &truncated_unsupported_type
  };
  if (!CheckHR(
          "CreatePipelineState truncated unsupported stream",
          device2->CreatePipelineState(
              &truncated_unsupported_stream_desc, IID_PPV_ARGS(&invalid_stream_pso)
          ),
          E_INVALIDARG
      ))
    return fail("truncated unsupported stream was accepted");

  graphics_stream.vs.value = graphics_desc.VS;
  graphics_stream.ps.value = graphics_desc.PS;
  graphics_stream.input_layout.value = graphics_desc.InputLayout;
  graphics_stream.topology.value = graphics_desc.PrimitiveTopologyType;
  graphics_stream.render_targets.value.NumRenderTargets = graphics_desc.NumRenderTargets;
  graphics_stream.render_targets.value.RTFormats[0] = graphics_desc.RTVFormats[0];
  graphics_stream.sample_desc.value = graphics_desc.SampleDesc;
  graphics_stream.sample_mask.value = graphics_desc.SampleMask;
  graphics_stream.rasterizer.value = graphics_desc.RasterizerState;
  graphics_stream.blend.value = graphics_desc.BlendState;
  graphics_stream.gs.value = graphics_desc.GS;
  graphics_stream_desc.SizeInBytes = offsetof(GraphicsStream, gs) +
                                     (geometry ? sizeof(graphics_stream.gs) : 0);
  graphics_stream_desc.pPipelineStateSubobjectStream = &graphics_stream;
  if (!CheckHR(
          "CreatePipelineState graphics stream",
          device2->CreatePipelineState(&graphics_stream_desc, IID_PPV_ARGS(&graphics_stream_pso))
      ))
    return fail("graphics stream creation failed");

  graphics_stream.blend.value.RenderTarget[0].RenderTargetWriteMask = 0xff;
  if (!CheckHR(
          "CreatePipelineState invalid blend stream",
          device2->CreatePipelineState(&graphics_stream_desc, IID_PPV_ARGS(&invalid_stream_pso)), E_INVALIDARG
      ))
    return fail("invalid blend stream was accepted");
  graphics_stream.blend.value.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

  if (!CheckHR(
          "CreatePipelineLibrary",
          device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library))
      ))
    return fail("empty library creation failed");
  if (!CheckHR("Query ID3D12PipelineLibrary", library->QueryInterface(IID_PPV_ARGS(&library_base))))
    return fail("pipeline library base interface query failed");
  Release(library_base);
  if (!CheckHR("Store compute pipeline", library->StorePipeline(L"compute", compute_pso)))
    return fail("compute store failed");
  if (!CheckHR("Store graphics pipeline", library->StorePipeline(L"graphics", graphics_pso)))
    return fail("graphics store failed");
  if (!CheckHR("Store duplicate pipeline", library->StorePipeline(L"compute", compute_pso), E_INVALIDARG))
    return fail("duplicate store accepted");

  if (!CheckHR(
          "Load compute pipeline",
          library->LoadComputePipeline(L"compute", &compute_desc, IID_PPV_ARGS(&loaded_compute))
      ) ||
      !CheckHR(
          "Load graphics pipeline",
          library->LoadGraphicsPipeline(L"graphics", &graphics_desc, IID_PPV_ARGS(&loaded_graphics))
      )) {
    Release(loaded_graphics);
    Release(loaded_compute);
    return fail("live library load failed");
  }
  if (!RunCompute(device, loaded_compute, root_signature) ||
      !RunGraphics(device, loaded_graphics, geometry)) {
    Release(loaded_graphics);
    Release(loaded_compute);
    return fail("live library pipeline execution failed");
  }
  Release(loaded_graphics);
  Release(loaded_compute);
  if (!CheckHR("Load missing pipeline", library->LoadComputePipeline(L"missing", &compute_desc, IID_PPV_ARGS(&loaded_compute)), E_INVALIDARG))
    return fail("missing pipeline did not fail");
  if (!CheckHR("Load stream pipeline", library->LoadPipeline(L"compute", &compute_stream_desc, IID_PPV_ARGS(&loaded_compute))))
    return fail("stream library load failed");
  Release(loaded_compute);
  if (!CheckHR("Load graphics stream pipeline",
               library->LoadPipeline(L"graphics", &graphics_stream_desc,
                                     IID_PPV_ARGS(&loaded_graphics))))
    return fail("graphics stream library load failed");
  Release(loaded_graphics);

  mismatched_compute_desc = compute_desc;
  mismatched_compute_desc.NodeMask = 1;
  if (!CheckHR(
          "Load mismatched pipeline",
          library->LoadComputePipeline(L"compute", &mismatched_compute_desc, IID_PPV_ARGS(&loaded_compute)), E_INVALIDARG
      ))
    return fail("mismatched pipeline was accepted");

  auto serialized_size = library->GetSerializedSize();
  if (!serialized_size)
    return fail("library serialized size was zero");
  serialized.resize(serialized_size);
  if (!CheckHR("Serialize library", library->Serialize(serialized.data(), serialized.size())))
    return fail("library serialization failed");
  serialized_again.resize(serialized_size);
  if (!CheckHR("Serialize library again", library->Serialize(serialized_again.data(), serialized_again.size())) ||
      serialized != serialized_again)
    return fail("library serialization was not deterministic");
  if (!CheckHR("Serialize undersized library", library->Serialize(serialized.data(), serialized.size() - 1), E_INVALIDARG))
    return fail("undersized serialization was accepted");

  if (!CheckHR(
          "Create rehydrated library",
          device1->CreatePipelineLibrary(serialized.data(), serialized.size(), IID_PPV_ARGS(&rehydrated_library))
      ))
    return fail("rehydrated library creation failed");
  if (!CheckHR(
          "Load rehydrated compute pipeline",
          rehydrated_library->LoadComputePipeline(L"compute", &compute_desc, IID_PPV_ARGS(&loaded_compute))
      ) ||
      !CheckHR(
          "Load rehydrated graphics pipeline",
          rehydrated_library->LoadGraphicsPipeline(L"graphics", &graphics_desc, IID_PPV_ARGS(&loaded_graphics))
      )) {
    Release(loaded_graphics);
    Release(loaded_compute);
    return fail("rehydrated library load failed");
  }
  if (!RunCompute(device, loaded_compute, root_signature) ||
      !RunGraphics(device, loaded_graphics, geometry)) {
    Release(loaded_graphics);
    Release(loaded_compute);
    return fail("rehydrated pipeline execution failed");
  }
  Release(loaded_graphics);
  Release(loaded_compute);

  if (!CheckHR(
          "Load rehydrated stream pipeline",
          rehydrated_library->LoadPipeline(L"compute", &compute_stream_desc, IID_PPV_ARGS(&loaded_compute))
      ))
    return fail("rehydrated stream library load failed");
  Release(loaded_compute);
  if (!CheckHR("Load rehydrated graphics stream pipeline",
               rehydrated_library->LoadPipeline(L"graphics",
                                                &graphics_stream_desc,
                                                IID_PPV_ARGS(&loaded_graphics))))
    return fail("rehydrated graphics stream library load failed");
  Release(loaded_graphics);

  if (!CheckHR(
          "Create truncated library",
          device1->CreatePipelineLibrary(serialized.data(), serialized.size() - 1, IID_PPV_ARGS(&bad_library)), E_INVALIDARG
      ))
    return fail("truncated library was accepted");
  if (!CheckHR(
          "Create malformed library",
          device1->CreatePipelineLibrary(compute_blob->GetBufferPointer(), compute_blob->GetBufferSize(), IID_PPV_ARGS(&bad_library)),
          E_INVALIDARG
      ))
    return fail("malformed library was accepted");

  if (!CheckHR("D3D12CreateDevice second", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&other_device))))
    return fail("second device creation failed");
  auto other_compute_desc = compute_desc;
  other_compute_desc.pRootSignature = nullptr;
  if (!CheckHR(
          "Create second-device compute pipeline",
          other_device->CreateComputePipelineState(&other_compute_desc, IID_PPV_ARGS(&other_compute_pso))
      ))
    return fail("second-device PSO creation failed");
  if (!CheckHR("Store wrong-device pipeline", library->StorePipeline(L"wrong-device", other_compute_pso), E_INVALIDARG))
    return fail("wrong-device pipeline was accepted");

  result = 0;
  cleanup();
  return result;
}

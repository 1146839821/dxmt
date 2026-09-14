#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
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
  const auto size = file.tellg();
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

template <typename T, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type> struct alignas(void *) StreamSubobject {
  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
  T value = {};
};

struct MeshStream {
  StreamSubobject<ID3D12RootSignature *, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE> root_signature;
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> mesh_shader;
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> pixel_shader;
  StreamSubobject<D3D12_RT_FORMAT_ARRAY, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS> render_targets;
  StreamSubobject<D3D12_RASTERIZER_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER> rasterizer;
  StreamSubobject<D3D12_BLEND_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> blend;
  StreamSubobject<D3D12_DEPTH_STENCIL_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL> depth_stencil;
  StreamSubobject<DXGI_SAMPLE_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC> sample_desc;
};

bool
WaitForQueue(ID3D12CommandQueue *queue, ID3D12Device *device, ID3D12CommandList *list) {
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  bool result = false;

  ID3D12CommandList *lists[] = {list};
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
RunMesh(const std::vector<char> &mesh_shader, const std::vector<char> &pixel_shader) {
  ID3D12Device *device = nullptr;
  ID3D12Device2 *device2 = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  ID3D12PipelineState *pipeline = nullptr;
  ID3D12PipelineState *invalid_pipeline = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList6 *list = nullptr;
  ID3D12Resource *render_target = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC render_target_desc = {};
  D3D12_RESOURCE_DESC readback_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  D3D12_CLEAR_VALUE clear_value = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
  D3D12_VIEWPORT viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  D3D12_RECT scissor = {0, 0, 1, 1};
  D3D12_RESOURCE_BARRIER barrier = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  D3D12_TEXTURE_COPY_LOCATION copy_destination = {};
  D3D12_TEXTURE_COPY_LOCATION copy_source = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  BYTE *mapped = nullptr;
  UINT pixel = 0;
  MeshStream stream = {};
  D3D12_PIPELINE_STATE_STREAM_DESC stream_desc = {};
  bool result = false;

  if (!CheckHR("D3D12CreateDevice", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
      !CheckHR("CheckFeatureSupport", device->CheckFeatureSupport(
                                         D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))) ||
      options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED) {
    if (options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
      std::cerr << "mesh shader feature query was promoted unexpectedly\n";
    goto cleanup;
  }
  if (!CheckHR("Query ID3D12Device2", device->QueryInterface(IID_PPV_ARGS(&device2))))
    goto cleanup;

  if (!CheckHR(
          "D3D12SerializeRootSignature",
          D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_error)
      ) ||
      !CheckHR(
          "CreateRootSignature",
          device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                      IID_PPV_ARGS(&root_signature))
      ))
    goto cleanup;

  stream.root_signature.value = root_signature;
  stream.mesh_shader.value.pShaderBytecode = mesh_shader.data();
  stream.mesh_shader.value.BytecodeLength = mesh_shader.size();
  stream.pixel_shader.value.pShaderBytecode = pixel_shader.data();
  stream.pixel_shader.value.BytecodeLength = pixel_shader.size();
  stream.render_targets.value.NumRenderTargets = 1;
  stream.render_targets.value.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  stream.rasterizer.value.FillMode = D3D12_FILL_MODE_SOLID;
  stream.rasterizer.value.CullMode = D3D12_CULL_MODE_NONE;
  stream.rasterizer.value.DepthClipEnable = TRUE;
  stream.blend.value.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  stream.sample_desc.value.Count = 1;
  stream_desc.pPipelineStateSubobjectStream = &stream;
  stream_desc.SizeInBytes = sizeof(stream);

  if (!CheckHR("Create mesh pipeline state", device2->CreatePipelineState(&stream_desc, IID_PPV_ARGS(&pipeline))))
    goto cleanup;

  stream.sample_desc.value.Quality = 1;
  if (!CheckHR(
          "Reject invalid mesh sample description",
          device2->CreatePipelineState(&stream_desc, IID_PPV_ARGS(&invalid_pipeline)), E_INVALIDARG
      ))
    goto cleanup;
  stream.sample_desc.value.Quality = 0;

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;
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
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readback_desc.Height = 1;
  readback_desc.DepthOrArraySize = 1;
  readback_desc.MipLevels = 1;
  readback_desc.SampleDesc.Count = 1;
  readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  if (!CheckHR("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      !CheckHR("CreateCommandAllocator", device->CreateCommandAllocator(
                                             D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
      !CheckHR(
          "CreateRenderTarget",
          device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &render_target_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
              &clear_value, IID_PPV_ARGS(&render_target)
          )
      ) ||
      !CheckHR("CreateRTVHeap", device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap))))
    goto cleanup;
  rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(render_target, nullptr, rtv);

  device->GetCopyableFootprints(&render_target_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size);
  readback_desc.Width = total_size;
  if (!CheckHR(
          "CreateReadback",
          device->CreateCommittedResource(
              &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&readback)
          )
      ) ||
      !CheckHR(
          "CreateMeshCommandList",
          device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipeline, IID_PPV_ARGS(&list))
      ))
    goto cleanup;

  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  list->ClearRenderTargetView(rtv, clear_value.Color, 0, nullptr);
  list->SetGraphicsRootSignature(root_signature);
  list->DispatchMesh(1, 1, 1);
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = render_target;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
  copy_destination.pResource = readback;
  copy_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  copy_destination.PlacedFootprint = footprint;
  copy_source.pResource = render_target;
  copy_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list->CopyTextureRegion(&copy_destination, 0, 0, 0, &copy_source, nullptr);
  if (!CheckHR("CloseMeshCommandList", list->Close()) ||
      !WaitForQueue(queue, device, static_cast<ID3D12CommandList *>(list)))
    goto cleanup;

  if (!CheckHR("MapMeshReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    goto cleanup;
  pixel = *reinterpret_cast<const UINT *>(mapped + footprint.Offset);
  readback->Unmap(0, nullptr);
  if ((pixel & 0x00ffffffu) != 0x000000ffu) {
    std::cerr << "mesh readback mismatch: 0x" << std::hex << pixel << std::dec << "\n";
    goto cleanup;
  }
  result = true;

cleanup:
  Release(rtv_heap);
  Release(readback);
  Release(render_target);
  Release(list);
  Release(allocator);
  Release(queue);
  Release(invalid_pipeline);
  Release(pipeline);
  Release(root_error);
  Release(root_blob);
  Release(root_signature);
  Release(device2);
  Release(device);
  return result;
}

} // namespace

int
main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: dx12_mesh_sm6 <mesh.cso> <pixel.cso>\n";
    return 2;
  }

  std::vector<char> mesh_shader;
  std::vector<char> pixel_shader;
  if (!ReadFile(argv[1], mesh_shader) || !ReadFile(argv[2], pixel_shader)) {
    std::cerr << "failed to read shader fixture\n";
    return 3;
  }
  return RunMesh(mesh_shader, pixel_shader) ? 0 : 1;
}

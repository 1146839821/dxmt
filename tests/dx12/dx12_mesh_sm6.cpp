#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>

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

struct MeshStreamWithAS {
  StreamSubobject<ID3D12RootSignature *, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE> root_signature;
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> mesh_shader;
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> pixel_shader;
  StreamSubobject<D3D12_RT_FORMAT_ARRAY, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS> render_targets;
  StreamSubobject<D3D12_RASTERIZER_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER> rasterizer;
  StreamSubobject<D3D12_BLEND_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> blend;
  StreamSubobject<D3D12_DEPTH_STENCIL_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL> depth_stencil;
  StreamSubobject<DXGI_SAMPLE_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC> sample_desc;
  StreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS> amplification_shader;
};

template <typename Stream> void
InitializeMeshStream(
    Stream &target, ID3D12RootSignature *root_signature, const std::vector<char> &mesh_shader,
    const std::vector<char> &pixel_shader
) {
  target.root_signature.value = root_signature;
  target.mesh_shader.value.pShaderBytecode = mesh_shader.data();
  target.mesh_shader.value.BytecodeLength = mesh_shader.size();
  target.pixel_shader.value.pShaderBytecode = pixel_shader.data();
  target.pixel_shader.value.BytecodeLength = pixel_shader.size();
  target.render_targets.value.NumRenderTargets = 1;
  target.render_targets.value.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  target.rasterizer.value.FillMode = D3D12_FILL_MODE_SOLID;
  target.rasterizer.value.CullMode = D3D12_CULL_MODE_NONE;
  target.rasterizer.value.DepthClipEnable = TRUE;
  target.blend.value.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  target.sample_desc.value.Count = 1;
}

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
RunMesh(
    const std::vector<char> &amplification_shader, const std::vector<char> &mesh_shader,
    const std::vector<char> &pixel_shader, bool cull_primitive
) {
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
  ID3D12Resource *mesh_output = nullptr;
  ID3D12Resource *mesh_constants = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12Resource *mesh_output_readback = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  ID3D12DescriptorHeap *uav_heap = nullptr;
  ID3D12DescriptorHeap *descriptor_heaps[1] = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_ROOT_PARAMETER root_parameters[2] = {};
  D3D12_DESCRIPTOR_RANGE mesh_output_range = {};
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES upload_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC render_target_desc = {};
  D3D12_RESOURCE_DESC readback_desc = {};
  D3D12_RESOURCE_DESC mesh_output_desc = {};
  D3D12_RESOURCE_DESC mesh_constants_desc = {};
  D3D12_RESOURCE_DESC mesh_output_readback_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC uav_heap_desc = {};
  D3D12_UNORDERED_ACCESS_VIEW_DESC mesh_output_uav_desc = {};
  D3D12_CLEAR_VALUE clear_value = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
  D3D12_VIEWPORT viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  D3D12_RECT scissor = {0, 0, 1, 1};
  D3D12_RESOURCE_BARRIER barrier = {};
  D3D12_RESOURCE_BARRIER output_barrier = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  D3D12_TEXTURE_COPY_LOCATION copy_destination = {};
  D3D12_TEXTURE_COPY_LOCATION copy_source = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  BYTE *mapped = nullptr;
  UINT *mapped_mesh_output = nullptr;
  UINT *mapped_mesh_constants = nullptr;
  UINT pixel = 0;
  const UINT expected_pixel = cull_primitive ? 0u : 0x000000ffu;
  const UINT expected_mesh_output = cull_primitive ? 0u : 42u;
  MeshStream stream = {};
  MeshStreamWithAS stream_with_as = {};
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

  mesh_output_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  mesh_output_range.NumDescriptors = 1;
  mesh_output_range.BaseShaderRegister = 0;
  mesh_output_range.RegisterSpace = 0;
  mesh_output_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  root_parameters[0].DescriptorTable.pDescriptorRanges = &mesh_output_range;
  root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_parameters[1].Descriptor.ShaderRegister = 0;
  root_parameters[1].Descriptor.RegisterSpace = 0;
  root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_desc.NumParameters = 2;
  root_desc.pParameters = root_parameters;

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

  InitializeMeshStream(stream, root_signature, mesh_shader, pixel_shader);
  InitializeMeshStream(stream_with_as, root_signature, mesh_shader, pixel_shader);
  stream_with_as.amplification_shader.value.pShaderBytecode = amplification_shader.data();
  stream_with_as.amplification_shader.value.BytecodeLength = amplification_shader.size();
  stream_desc.pPipelineStateSubobjectStream = amplification_shader.empty() ? static_cast<void *>(&stream)
                                                                            : static_cast<void *>(&stream_with_as);
  stream_desc.SizeInBytes = amplification_shader.empty() ? sizeof(stream) : sizeof(stream_with_as);

  if (!CheckHR("Create mesh pipeline state", device2->CreatePipelineState(&stream_desc, IID_PPV_ARGS(&pipeline))))
    goto cleanup;

  if (amplification_shader.empty())
    stream.sample_desc.value.Quality = 1;
  else
    stream_with_as.sample_desc.value.Quality = 1;
  if (!CheckHR(
          "Reject invalid mesh sample description",
          device2->CreatePipelineState(&stream_desc, IID_PPV_ARGS(&invalid_pipeline)), E_INVALIDARG
      ))
    goto cleanup;
  if (amplification_shader.empty())
    stream.sample_desc.value.Quality = 0;
  else
    stream_with_as.sample_desc.value.Quality = 0;

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
  mesh_output_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  mesh_output_desc.Width = 256;
  mesh_output_desc.Height = 1;
  mesh_output_desc.DepthOrArraySize = 1;
  mesh_output_desc.MipLevels = 1;
  mesh_output_desc.SampleDesc.Count = 1;
  mesh_output_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  mesh_output_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  mesh_constants_desc = mesh_output_desc;
  mesh_constants_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  mesh_output_readback_desc = mesh_constants_desc;
  clear_value.Format = render_target_desc.Format;
  clear_value.Color[3] = 1.0f;
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  uav_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  uav_heap_desc.NumDescriptors = 1;
  uav_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
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
      !CheckHR(
          "CreateMeshOutputUAV",
          device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &mesh_output_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
              nullptr, IID_PPV_ARGS(&mesh_output)
          )
      ) ||
      !CheckHR(
          "CreateMeshConstants",
          device->CreateCommittedResource(
              &upload_heap, D3D12_HEAP_FLAG_NONE, &mesh_constants_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
              nullptr, IID_PPV_ARGS(&mesh_constants)
          )
      ) ||
      !CheckHR("CreateUAVHeap", device->CreateDescriptorHeap(&uav_heap_desc, IID_PPV_ARGS(&uav_heap))) ||
      !CheckHR("CreateRTVHeap", device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap))))
    goto cleanup;
  if (!CheckHR("MapMeshConstants", mesh_constants->Map(0, nullptr, reinterpret_cast<void **>(&mapped_mesh_constants))))
    goto cleanup;
  *mapped_mesh_constants = 41;
  mesh_constants->Unmap(0, nullptr);
  rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(render_target, nullptr, rtv);
  mesh_output_uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  mesh_output_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  mesh_output_uav_desc.Buffer.NumElements = 64;
  mesh_output_uav_desc.Buffer.StructureByteStride = sizeof(UINT);
  device->CreateUnorderedAccessView(
      mesh_output, nullptr, &mesh_output_uav_desc, uav_heap->GetCPUDescriptorHandleForHeapStart()
  );

  device->GetCopyableFootprints(&render_target_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size);
  readback_desc.Width = total_size;
  mesh_output_readback_desc.Width = 256;
  if (!CheckHR(
          "CreateReadback",
          device->CreateCommittedResource(
              &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&readback)
          )
      ) ||
      !CheckHR(
          "CreateMeshOutputReadback",
          device->CreateCommittedResource(
              &readback_heap, D3D12_HEAP_FLAG_NONE, &mesh_output_readback_desc, D3D12_RESOURCE_STATE_COPY_DEST,
              nullptr, IID_PPV_ARGS(&mesh_output_readback)
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
  descriptor_heaps[0] = uav_heap;
  list->SetDescriptorHeaps(1, descriptor_heaps);
  list->SetGraphicsRootDescriptorTable(0, uav_heap->GetGPUDescriptorHandleForHeapStart());
  list->SetGraphicsRootConstantBufferView(1, mesh_constants->GetGPUVirtualAddress());
  list->DispatchMesh(1, 1, 1);
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = render_target;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
  output_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  output_barrier.Transition.pResource = mesh_output;
  output_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  output_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  output_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &output_barrier);
  copy_destination.pResource = readback;
  copy_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  copy_destination.PlacedFootprint = footprint;
  copy_source.pResource = render_target;
  copy_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list->CopyTextureRegion(&copy_destination, 0, 0, 0, &copy_source, nullptr);
  list->CopyBufferRegion(mesh_output_readback, 0, mesh_output, 0, sizeof(UINT));
  if (!CheckHR("CloseMeshCommandList", list->Close()) ||
      !WaitForQueue(queue, device, static_cast<ID3D12CommandList *>(list)))
    goto cleanup;

  if (!CheckHR("MapMeshReadback", readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
    goto cleanup;
  pixel = *reinterpret_cast<const UINT *>(mapped + footprint.Offset);
  readback->Unmap(0, nullptr);
  if ((pixel & 0x00ffffffu) != expected_pixel) {
    std::cerr << "mesh readback mismatch: 0x" << std::hex << pixel << std::dec << "\n";
    goto cleanup;
  }
  if (!cull_primitive) {
    if (!CheckHR(
            "MapMeshOutputReadback",
            mesh_output_readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_mesh_output))
        ))
      goto cleanup;
    if (*mapped_mesh_output != expected_mesh_output) {
      std::cerr << "mesh resource readback mismatch: " << *mapped_mesh_output << "\n";
      mesh_output_readback->Unmap(0, nullptr);
      goto cleanup;
    }
    mesh_output_readback->Unmap(0, nullptr);
  }
  std::cout << "DXIL " << (cull_primitive ? "SV_CullPrimitive mesh" : "mesh")
            << " readback passed: 0x" << std::hex << pixel << std::dec
            << (cull_primitive ? "" : ", root-CBV/descriptor-UAV=42") << "\n";
  result = true;

cleanup:
  Release(rtv_heap);
  Release(uav_heap);
  Release(mesh_output_readback);
  Release(readback);
  Release(mesh_constants);
  Release(mesh_output);
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
  if (argc != 3 && argc != 4) {
    std::cerr << "usage: dx12_mesh_sm6 [<amplification.cso>] <mesh.cso> <pixel.cso>\n"
              << "       dx12_mesh_sm6 --cull-primitive <mesh.cso> <pixel.cso>\n";
    return 2;
  }

  const bool cull_primitive = argc == 4 && std::strcmp(argv[1], "--cull-primitive") == 0;
  std::vector<char> amplification_shader;
  std::vector<char> mesh_shader;
  std::vector<char> pixel_shader;
  const int mesh_index = argc == 4 ? 2 : 1;
  const int pixel_index = argc == 4 ? 3 : 2;
  if ((argc == 4 && !cull_primitive && !ReadFile(argv[1], amplification_shader)) ||
      !ReadFile(argv[mesh_index], mesh_shader) || !ReadFile(argv[pixel_index], pixel_shader)) {
    std::cerr << "failed to read shader fixture\n";
    return 3;
  }
  return RunMesh(amplification_shader, mesh_shader, pixel_shader, cull_primitive) ? 0 : 1;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

template <typename T> struct ReleaseCOM { void operator()(T *p) const { if (p) p->Release(); } };
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;

static bool Check(HRESULT hr, const char *name) {
  if (FAILED(hr)) std::printf("%s failed %08lx\n", name, (unsigned long)hr);
  return SUCCEEDED(hr);
}

static void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list->ResourceBarrier(1, &barrier);
}

int wmain(int argc, wchar_t **argv) {
  if (argc != 3 || !SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", argv[2])) return 1;
  SetEnvironmentVariableW(L"DXMT_ENABLE_AIR_MINMAX", nullptr);
  SetEnvironmentVariableW(L"DXMT_ENABLE_AIR_MINMAX_DYNAMIC", nullptr);
  SetEnvironmentVariableW(L"DXMT_TYPED_ORIGIN_DXC_DIRECTORY", nullptr);
  HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 1;
  LARGE_INTEGER size = {}; DWORD read = 0;
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 32 * 1024 * 1024) {
    CloseHandle(file); return 1;
  }
  std::vector<unsigned char> shader(size.QuadPart);
  const bool loaded = ReadFile(file, shader.data(), shader.size(), &read, nullptr) && read == shader.size();
  CloseHandle(file);
  if (!loaded) return 1;
  ID3D12Device *raw_device = nullptr;
  if (!Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)), "device")) return 1;
  OwnedCOM<ID3D12Device> device(raw_device);
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  if (!Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)), "options")) return 1;
  std::printf("MINMAX_COMPARISON_GPU requested_FL=11_0 "
      "TiledResourcesTier=%u TypedUAVAdditionalFormats=%u LogicOp=%u\n",
      UINT(options.TiledResourcesTier),
      UINT(options.TypedUAVLoadAdditionalFormats), UINT(options.OutputMergerLogicOp));
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  ID3D12CommandQueue *raw_queue = nullptr;
  if (!Check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&raw_queue)), "queue")) return 1;
  OwnedCOM<ID3D12CommandQueue> queue(raw_queue);
  ID3D12Fence *raw_fence = nullptr;
  if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_fence)), "fence")) return 1;
  OwnedCOM<ID3D12Fence> fence(raw_fence);
  UINT64 serial = 0;
  const auto execute = [&](ID3D12GraphicsCommandList *list) {
    ID3D12CommandList *lists[] = {list}; queue->ExecuteCommandLists(1, lists);
    if (!Check(queue->Signal(fence.get(), ++serial), "signal")) return false;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return false;
    const bool done = Check(fence->SetEventOnCompletion(serial, event), "completion") &&
        WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
    CloseHandle(event); return done;
  };
  D3D12_HEAP_PROPERTIES defaults = {}; defaults.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_HEAP_PROPERTIES uploads = {}; uploads.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_HEAP_PROPERTIES readbacks = {}; readbacks.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = desc.Height = 2; desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  ID3D12Resource *raw_color = nullptr, *raw_depth = nullptr;
  if (!Check(device->CreateCommittedResource(&defaults, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw_color)), "color")) return 1;
  OwnedCOM<ID3D12Resource> color(raw_color);
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {}; UINT64 total = 0;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
  desc.Format = DXGI_FORMAT_R32_TYPELESS; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE clear = {}; clear.Format = DXGI_FORMAT_D32_FLOAT; clear.DepthStencil.Depth = 0.25f;
  if (!Check(device->CreateCommittedResource(&defaults, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&raw_depth)), "depth")) return 1;
  OwnedCOM<ID3D12Resource> depth(raw_depth);
  desc = {}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = total + 16;
  desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ID3D12Resource *raw_upload = nullptr, *raw_output = nullptr, *raw_readback = nullptr;
  if (!Check(device->CreateCommittedResource(&uploads, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&raw_upload)), "upload")) return 1;
  OwnedCOM<ID3D12Resource> upload(raw_upload);
  void *mapped = nullptr;
  if (!Check(upload->Map(0, nullptr, &mapped), "upload map")) return 1;
  std::memset(mapped, 0, desc.Width);
  const UINT pixels[] = {0xff000010, 0xff000040, 0xff0000c0, 0xff0000f0};
  std::memcpy(static_cast<unsigned char *>(mapped) + footprint.Offset, pixels, 8);
  std::memcpy(static_cast<unsigned char *>(mapped) + footprint.Offset + footprint.Footprint.RowPitch, pixels + 2, 8);
  const UINT sentinel[] = {0x6d5a4b3c, 0x6d5a4b3c, 0x6d5a4b3c, 0x6d5a4b3c};
  std::memcpy(static_cast<unsigned char *>(mapped) + total, sentinel, sizeof(sentinel));
  upload->Unmap(0, nullptr);
  desc.Width = 16; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (!Check(device->CreateCommittedResource(&defaults, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw_output)), "output")) return 1;
  OwnedCOM<ID3D12Resource> output(raw_output);
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  if (!Check(device->CreateCommittedResource(&readbacks, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw_readback)), "readback")) return 1;
  OwnedCOM<ID3D12Resource> readback(raw_readback);
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3,
      D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  ID3D12DescriptorHeap *raw_resources = nullptr, *raw_samplers = nullptr, *raw_dsv = nullptr;
  if (!Check(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&raw_resources)), "resource heap")) return 1;
  OwnedCOM<ID3D12DescriptorHeap> resources(raw_resources);
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; heap_desc.NumDescriptors = 2;
  if (!Check(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&raw_samplers)), "sampler heap")) return 1;
  OwnedCOM<ID3D12DescriptorHeap> samplers(raw_samplers);
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; heap_desc.NumDescriptors = 1;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (!Check(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&raw_dsv)), "DSV heap")) return 1;
  OwnedCOM<ID3D12DescriptorHeap> dsv(raw_dsv);
  auto resource_cpu = resources->GetCPUDescriptorHandleForHeapStart();
  const auto resource_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(color.get(), &srv, resource_cpu);
  resource_cpu.ptr += resource_stride; srv.Format = DXGI_FORMAT_R32_FLOAT;
  device->CreateShaderResourceView(depth.get(), &srv, resource_cpu);
  resource_cpu.ptr += resource_stride;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements = 4; uav.Buffer.StructureByteStride = 4;
  device->CreateUnorderedAccessView(output.get(), nullptr, &uav, resource_cpu);
  D3D12_DEPTH_STENCIL_VIEW_DESC depth_view = {};
  depth_view.Format = DXGI_FORMAT_D32_FLOAT; depth_view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  device->CreateDepthStencilView(depth.get(), &depth_view, dsv->GetCPUDescriptorHandleForHeapStart());
  D3D12_DESCRIPTOR_RANGE1 ranges[] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0},
      {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 2},
      {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0}};
  D3D12_ROOT_PARAMETER1 parameters[2] = {};
  parameters[0].ParameterType = parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable = {2, ranges}; parameters[1].DescriptorTable = {1, ranges + 2};
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1; root_desc.Desc_1_1.NumParameters = 2;
  root_desc.Desc_1_1.pParameters = parameters;
  ID3DBlob *raw_blob = nullptr;
  if (!Check(D3D12SerializeVersionedRootSignature(&root_desc, &raw_blob, nullptr), "serialize")) return 1;
  OwnedCOM<ID3DBlob> blob(raw_blob);
  ID3D12RootSignature *raw_root = nullptr;
  if (!Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
      IID_PPV_ARGS(&raw_root)), "root")) return 1;
  OwnedCOM<ID3D12RootSignature> root(raw_root);
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root.get(); pso_desc.CS = {shader.data(), shader.size()};
  ID3D12PipelineState *raw_pso = nullptr;
  if (!Check(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&raw_pso)), "PSO")) return 1;
  OwnedCOM<ID3D12PipelineState> pso(raw_pso);
  ID3D12CommandAllocator *raw_allocator = nullptr;
  ID3D12GraphicsCommandList *raw_list = nullptr;
  if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "allocator")) return 1;
  OwnedCOM<ID3D12CommandAllocator> allocator(raw_allocator);
  if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
      IID_PPV_ARGS(&raw_list)), "list")) return 1;
  OwnedCOM<ID3D12GraphicsCommandList> list(raw_list);
  D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
  dst.pResource = color.get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.pResource = upload.get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprint;
  list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  list->ClearDepthStencilView(dsv->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 0.25f, 0, 0, nullptr);
  Transition(list.get(), color.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(list.get(), depth.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  list->CopyBufferRegion(output.get(), 0, upload.get(), total, 16);
  Transition(list.get(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!Check(list->Close(), "upload close") || !execute(list.get())) return 1;
  // Keep upload storage separate: allocator reset is a different contract from
  // comparison/reduction coexistence and is not qualified by this fixture.
  OwnedCOM<ID3D12CommandAllocator> upload_allocator(std::move(allocator));
  OwnedCOM<ID3D12GraphicsCommandList> upload_list(std::move(list));
  raw_allocator = nullptr; raw_list = nullptr;
  if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "dispatch allocator")) return 1;
  allocator.reset(raw_allocator);
  if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), pso.get(),
      IID_PPV_ARGS(&raw_list)), "dispatch list")) return 1;
  list.reset(raw_list);
  // Record once with initialized descriptors; volatile samplers are replaced only
  // after completed submissions, leaving this closed list unchanged.
  const auto set_samplers = [&](D3D12_FILTER filter, D3D12_COMPARISON_FUNC comparison) {
    D3D12_SAMPLER_DESC sampler = {}; sampler.Filter = filter;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX; sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    auto cpu = samplers->GetCPUDescriptorHandleForHeapStart();
    device->CreateSampler(&sampler, cpu);
    cpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    sampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR; sampler.ComparisonFunc = comparison;
    device->CreateSampler(&sampler, cpu);
  };
  set_samplers(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR, D3D12_COMPARISON_FUNC_LESS_EQUAL);
  ID3D12DescriptorHeap *heaps[] = {resources.get(), samplers.get()};
  list->SetDescriptorHeaps(2, heaps); list->SetComputeRootSignature(root.get());
  list->SetComputeRootDescriptorTable(0, resources->GetGPUDescriptorHandleForHeapStart());
  list->SetComputeRootDescriptorTable(1, samplers->GetGPUDescriptorHandleForHeapStart());
  // Reset the written words each submission so stale results cannot pass.
  Transition(list.get(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
  list->CopyBufferRegion(output.get(), 0, upload.get(), total, 16);
  Transition(list.get(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  list->Dispatch(1, 1, 1);
  Transition(list.get(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.get(), 0, output.get(), 0, 16);
  Transition(list.get(), output.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!Check(list->Close(), "dispatch close")) return 1;
  const D3D12_FILTER filters[] = {D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR,
      D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MIN_MAG_MIP_LINEAR};
  const UINT expected_red[] = {16, 240, 128};
  OwnedCOM<ID3D12CommandAllocator> depth_update_allocator;
  OwnedCOM<ID3D12GraphicsCommandList> depth_update_list;
  for (unsigned iteration = 0; iteration < 12; ++iteration) {
    if (iteration == 6) {
      // Test both sides of the reference. A zero or stale depth binding cannot
      // reproduce the expected switch, even if sampler comparison itself works.
      raw_allocator = nullptr; raw_list = nullptr;
      if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "depth update allocator")) return 1;
      depth_update_allocator.reset(raw_allocator);
      if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, depth_update_allocator.get(), nullptr,
          IID_PPV_ARGS(&raw_list)), "depth update list")) return 1;
      depth_update_list.reset(raw_list);
      Transition(depth_update_list.get(), depth.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
      depth_update_list->ClearDepthStencilView(dsv->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 0.75f, 0, 0, nullptr);
      Transition(depth_update_list.get(), depth.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      if (!Check(depth_update_list->Close(), "depth update close") || !execute(depth_update_list.get())) return 1;
    }
    const unsigned reduction = iteration % 3;
    const bool greater = iteration % 6 >= 3;
    const bool high_depth = iteration >= 6;
    const UINT expected_comparison = greater != high_depth ? 0x3f800000 : 0;
    set_samplers(filters[reduction], greater ? D3D12_COMPARISON_FUNC_GREATER_EQUAL : D3D12_COMPARISON_FUNC_LESS_EQUAL);
    if (!execute(list.get())) return 1;
    D3D12_RANGE range = {0, 16};
    if (!Check(readback->Map(0, &range, &mapped), "readback map")) return 1;
    UINT values[4]; std::memcpy(values, mapped, sizeof(values));
    D3D12_RANGE empty = {}; readback->Unmap(0, &empty);
    const bool ok = values[0] == expected_red[reduction] && values[1] == expected_comparison &&
        values[2] == sentinel[2] && values[3] == sentinel[3];
    std::printf("MINMAX_COMPARISON_GPU iteration=%u red=%u comparison_bits=%08x expected=%u,%08x %s\n",
        iteration, values[0], values[1], expected_red[reduction], expected_comparison, ok ? "PASS" : "FAIL");
    if (!ok) return 1;
  }
  std::printf("MINMAX_COMPARISON_GPU classification=DXMT_LOCAL_PASS groups=12\n");
  return 0;
}

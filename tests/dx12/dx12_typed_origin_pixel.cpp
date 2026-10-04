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
static bool Load(const wchar_t *path, std::vector<unsigned char> &bytes) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {}; DWORD read = 0;
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 32 * 1024 * 1024;
  if (ok) { bytes.resize(size.QuadPart); ok = ReadFile(file, bytes.data(), bytes.size(), &read, nullptr) && read == bytes.size(); }
  CloseHandle(file); return ok;
}

enum class DrawMode { Typed, RejectTypedVS, Ordinary, RejectMinMaxSwitch };

static bool Run(ID3D12Device *device, const std::vector<unsigned char> &vs,
    const std::vector<unsigned char> &ps, const std::vector<unsigned char> &ordinary,
    bool live, bool pixel_visibility, DrawMode mode = DrawMode::Typed) {
  const bool reject_typed_vs = mode == DrawMode::RejectTypedVS;
  const bool reject_minmax_switch = mode == DrawMode::RejectMinMaxSwitch;
  const bool ordinary_only = mode == DrawMode::Ordinary;
  D3D12_COMMAND_QUEUE_DESC qd = {}; ID3D12CommandQueue *raw_queue = nullptr;
  if (!Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&raw_queue)), "queue")) return false;
  OwnedCOM<ID3D12CommandQueue> queue(raw_queue);
  ID3D12Fence *raw_fence = nullptr;
  if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_fence)), "fence")) return false;
  OwnedCOM<ID3D12Fence> fence(raw_fence);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) return false;
  struct CloseEvent { HANDLE event; ~CloseEvent() { CloseHandle(event); } } close_event{event};
  auto execute = [&](ID3D12GraphicsCommandList *list, UINT64 serial) {
    ID3D12CommandList *lists[] = {list}; queue->ExecuteCommandLists(1, lists);
    return Check(queue->Signal(fence.get(), serial), "signal") &&
        Check(fence->SetEventOnCompletion(serial, event), "completion") &&
        WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
  };
  D3D12_RESOURCE_DESC bd = {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = 1024;
  bd.Height = bd.DepthOrArraySize = bd.MipLevels = bd.SampleDesc.Count = 1;
  bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  auto buffer = [&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state, bool uav) {
    D3D12_HEAP_PROPERTIES props = {}; props.Type = type;
    auto desc = bd; if (uav) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource *raw = nullptr;
    Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
        IID_PPV_ARGS(&raw)), "buffer");
    return OwnedCOM<ID3D12Resource>(raw);
  };
  auto first = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false);
  auto second = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false);
  auto initial = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false);
  auto output = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_SOURCE, true);
  auto readback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, false);
  if (!first || !second || !initial || !output || !readback) return false;
  const UINT a[] = {0xdeadbeef, 17, 29, 0xbadc0ffe}, b[] = {0xdeadbeef, 37, 49, 0xbadc0ffe};
  const UINT init[] = {0x10203040, 3, 7, 0x50607080};
  auto fill = [&](ID3D12Resource *resource, const UINT *values) {
    void *mapped = nullptr;
    if (!Check(resource->Map(0, nullptr, &mapped), "map")) return false;
    std::memset(mapped, 0xcd, 512); std::memcpy(mapped, values, 16); resource->Unmap(0, nullptr); return true;
  };
  if (!fill(first.get(), a) || !fill(second.get(), b) || !fill(initial.get(), init)) return false;
  D3D12_RESOURCE_DESC td = {}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  td.Width = 2; td.Height = td.DepthOrArraySize = td.MipLevels = td.SampleDesc.Count = 1;
  td.Format = DXGI_FORMAT_R32_UINT; td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_HEAP_PROPERTIES props = {}; props.Type = D3D12_HEAP_TYPE_DEFAULT;
  ID3D12Resource *raw_target = nullptr;
  if (!Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &td,
      D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&raw_target)), "target")) return false;
  OwnedCOM<ID3D12Resource> target(raw_target);
  auto heap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool visible) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {}; desc.Type = type; desc.NumDescriptors = count;
    if (visible) desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap *raw = nullptr;
    Check(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&raw)), "heap");
    return OwnedCOM<ID3D12DescriptorHeap>(raw);
  };
  auto resources = heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  auto rtvs = heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  if (!resources || !rtvs) return false;
  const auto cpu = resources->GetCPUDescriptorHandleForHeapStart();
  auto uav_cpu = cpu; uav_cpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {}; srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.FirstElement = 1; srv.Buffer.NumElements = 2;
  device->CreateShaderResourceView(first.get(), &srv, cpu);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {}; uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.FirstElement = 1; uav.Buffer.NumElements = 2;
  device->CreateUnorderedAccessView(output.get(), nullptr, &uav, uav_cpu);
  auto rtv = rtvs->GetCPUDescriptorHandleForHeapStart(); device->CreateRenderTargetView(target.get(), nullptr, rtv);
  const auto flags = static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE |
      (live ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE : 0));
  D3D12_DESCRIPTOR_RANGE1 ranges[] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, flags, 0},
      {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, flags, 1}};
  D3D12_ROOT_PARAMETER1 parameters[2] = {}; parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable = {2, ranges};
  parameters[0].ShaderVisibility = pixel_visibility ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants = {3, 0, 1}; parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC rd = {}; rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  rd.Desc_1_1.NumParameters = 2; rd.Desc_1_1.pParameters = parameters;
  if (ordinary_only) { rd.Desc_1_1.NumParameters = 1; rd.Desc_1_1.pParameters = parameters + 1; }
  ID3DBlob *raw_blob = nullptr;
  if (!Check(D3D12SerializeVersionedRootSignature(&rd, &raw_blob, nullptr), "serialize")) return false;
  OwnedCOM<ID3DBlob> blob(raw_blob); ID3D12RootSignature *raw_root = nullptr;
  if (!Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
      IID_PPV_ARGS(&raw_root)), "root")) return false;
  OwnedCOM<ID3D12RootSignature> root(raw_root);
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = root.get();
  pd.VS = {vs.data(), vs.size()}; pd.PS = {ps.data(), ps.size()};
  pd.SampleMask = UINT_MAX; pd.SampleDesc.Count = 1; pd.NumRenderTargets = 1;
  pd.RTVFormats[0] = DXGI_FORMAT_R32_UINT; pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pd.RasterizerState.DepthClipEnable = TRUE; pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  ID3D12PipelineState *raw_pso = nullptr;
  if (!Check(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&raw_pso)), "pso")) return false;
  OwnedCOM<ID3D12PipelineState> pso(raw_pso);
  auto ordinary_rd = rd; ordinary_rd.Desc_1_1.NumParameters = 1; ordinary_rd.Desc_1_1.pParameters = parameters + 1;
  raw_blob = nullptr;
  if (!Check(D3D12SerializeVersionedRootSignature(&ordinary_rd, &raw_blob, nullptr), "ordinary serialize")) return false;
  OwnedCOM<ID3DBlob> ordinary_blob(raw_blob); raw_root = nullptr;
  if (!Check(device->CreateRootSignature(0, ordinary_blob->GetBufferPointer(), ordinary_blob->GetBufferSize(),
      IID_PPV_ARGS(&raw_root)), "ordinary root")) return false;
  OwnedCOM<ID3D12RootSignature> ordinary_root(raw_root); pd.pRootSignature = ordinary_root.get();
  pd.PS = {ordinary.data(), ordinary.size()}; raw_pso = nullptr;
  if (!reject_typed_vs && !Check(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&raw_pso)), "ordinary pso")) return false;
  OwnedCOM<ID3D12PipelineState> ordinary_pso(raw_pso);
  ID3D12CommandAllocator *raw_allocator = nullptr; ID3D12GraphicsCommandList *raw_list = nullptr;
  if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "allocator")) return false;
  OwnedCOM<ID3D12CommandAllocator> allocator(raw_allocator);
  if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), pso.get(),
      IID_PPV_ARGS(&raw_list)), "list")) return false;
  OwnedCOM<ID3D12GraphicsCommandList> list(raw_list);
  auto transition = [&](ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &barrier);
  };
  transition(output.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  list->CopyBufferRegion(output.get(), 0, initial.get(), 0, 512);
  transition(output.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  transition(target.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  list->SetGraphicsRootSignature(root.get()); ID3D12DescriptorHeap *heaps[] = {resources.get()};
  list->SetDescriptorHeaps(1, heaps);
  if (!ordinary_only) list->SetGraphicsRootDescriptorTable(0, resources->GetGPUDescriptorHandleForHeapStart());
  list->SetGraphicsRoot32BitConstant(ordinary_only ? 0 : 1, 0xabc123, 0);
  list->OMSetRenderTargets(1, &rtv, FALSE, nullptr); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_VIEWPORT viewport = {0, 0, 2, 1, 0, 1}; D3D12_RECT rect = {0, 0, 2, 1};
  list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &rect);
  if (reject_minmax_switch && !SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", L"Z:\\not-used-after-combination-rejection"))
    return false;
  list->DrawInstanced(3, 1, 0, 0);
  if (reject_typed_vs || reject_minmax_switch) {
    const bool rejected = FAILED(list->Close());
    if (rejected) std::puts(reject_typed_vs ?
        "PIXEL_ORIGIN typed VS + ordinary PS rejected PASS (no GPU submission)" :
        "PIXEL_ORIGIN late MinMax override rejected PASS (no GPU submission)");
    return rejected;
  }
  // Same encoder: restore the ordinary application PSO/TLAB after the private draw.
  rect.left = 1; list->RSSetScissorRects(1, &rect); list->SetPipelineState(ordinary_pso.get());
  list->SetGraphicsRootSignature(ordinary_root.get());
  list->SetGraphicsRoot32BitConstant(0, 0xabc123, 0);
  list->DrawInstanced(3, 1, 0, 0);
  transition(output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  transition(target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.get(), 0, output.get(), 0, 16);
  D3D12_TEXTURE_COPY_LOCATION src = {}; src.pResource = target.get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION dst = {}; dst.pResource = readback.get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint.Offset = 512; dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_UINT, 2, 1, 1, 256};
  list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  if (!Check(list->Close(), "close")) return false;
  for (unsigned pass = 0; pass < 4; ++pass) {
    const bool replacement = live && (pass % 2 == 0);
    if (live) device->CreateShaderResourceView(replacement ? second.get() : first.get(), &srv, cpu);
    const UINT x = ordinary_only ? 77 : replacement ? 40 : 20, y = ordinary_only ? 77 : replacement ? 56 : 36;
    if (!execute(list.get(), pass + 1)) return false;
    void *mapped = nullptr;
    if (!Check(readback->Map(0, nullptr, &mapped), "readback")) return false;
    const auto *words = static_cast<const UINT *>(mapped);
    const bool ok = words[0] == init[0] && words[1] == (ordinary_only ? init[1] : x + 100) &&
        words[2] == (ordinary_only ? init[2] : y + 100) &&
        words[3] == init[3] && words[128] == x && words[129] == 77;
    if (!ok) std::printf("PIXEL_ORIGIN mismatch static=%u pixel=%u pass=%u values=%08x,%u,%u,%08x rt=%u,%u\n",
        !live, pixel_visibility, pass, words[0], words[1], words[2], words[3], words[128], words[129]);
    readback->Unmap(0, nullptr); if (!ok) return false;
    std::printf("PIXEL_ORIGIN PASS live=%u pixel=%u pass=%u values=%u,%u guards=preserved\n", live, pixel_visibility, pass, x, y);
  }
  return true;
}

int wmain(int argc, wchar_t **argv) {
  const bool minmax_switch = argc == 7 && !wcscmp(argv[6], L"--minmax-switch");
  const bool automatic = argc == 7 && (!wcscmp(argv[6], L"--auto") || minmax_switch);
  const bool ordinary_only = argc == 7 && !wcscmp(argv[6], L"--ordinary");
  if (argc != 6 && !automatic && !ordinary_only) return 1;
  std::vector<unsigned char> vs, ps, ordinary, typed_vs;
  if (!Load(argv[1], vs) || !Load(argv[2], ps) || !Load(argv[3], ordinary) ||
      !Load(argv[4], typed_vs) || !SetEnvironmentVariableW(L"DXMT_TYPED_ORIGIN_DXC_DIRECTORY",
          automatic || ordinary_only ? nullptr : argv[5]) ||
      !SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", nullptr)) return 1;
  std::printf("PIXEL_ORIGIN selection=%s\n", automatic ? "deployed" : ordinary_only ? "ordinary" : "override");
  ID3D12Device *raw = nullptr;
  if (!Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw)), "device")) return 1;
  OwnedCOM<ID3D12Device> device(raw);
  if (minmax_switch) return Run(device.get(), vs, ps, ordinary, false, false, DrawMode::RejectMinMaxSwitch) ? 0 : 1;
  if (ordinary_only) {
    if (!Run(device.get(), vs, ordinary, ordinary, false, false, DrawMode::Ordinary)) return 1;
    std::puts("ordinary graphics without origin override GPU PASS"); return 0;
  }
  for (bool live : {false, true}) for (bool pixel : {false, true})
    if (!Run(device.get(), vs, ps, ordinary, live, pixel)) return 1;
  if (!Run(device.get(), typed_vs, ordinary, ordinary, false, false, DrawMode::RejectTypedVS)) return 1;
  std::puts("typed-origin production pixel GPU PASS (16 draws)"); return 0;
}

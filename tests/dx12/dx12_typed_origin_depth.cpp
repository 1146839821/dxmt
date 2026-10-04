#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cmath>
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

enum class DepthMode { Typed, Ordinary, DenyVertex };
enum class RootVersion { RS10, RS11 };
struct DepthCase {
  RootVersion root_version;
  bool live;
  D3D12_SHADER_VISIBILITY table_visibility;
  bool indexed;
  DepthMode mode = DepthMode::Typed;
};

static bool Run(ID3D12Device *device, const std::vector<unsigned char> &typed,
    const std::vector<unsigned char> &ordinary, const DepthCase &test) {
  const bool legacy = test.root_version == RootVersion::RS10;
  const bool live = test.live;
  const bool indexed = test.indexed;
  const bool vertex_visibility = test.table_visibility == D3D12_SHADER_VISIBILITY_VERTEX;
  const bool ordinary_only = test.mode == DepthMode::Ordinary;
  const bool deny_vertex = test.mode == DepthMode::DenyVertex;
  D3D12_COMMAND_QUEUE_DESC qd = {}; ID3D12CommandQueue *raw_queue = nullptr;
  if (!Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&raw_queue)), "queue")) return false;
  OwnedCOM<ID3D12CommandQueue> queue(raw_queue);
  ID3D12Fence *raw_fence = nullptr;
  if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_fence)), "fence")) return false;
  OwnedCOM<ID3D12Fence> fence(raw_fence);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) return false;
  struct CloseEvent { HANDLE value; ~CloseEvent() { CloseHandle(value); } } close_event{event};
  auto execute = [&](ID3D12GraphicsCommandList *list, UINT64 serial) {
    ID3D12CommandList *lists[] = {list}; queue->ExecuteCommandLists(1, lists);
    return Check(queue->Signal(fence.get(), serial), "signal") &&
        Check(fence->SetEventOnCompletion(serial, event), "completion") &&
        WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
  };
  auto buffer = [&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
    D3D12_RESOURCE_DESC desc = {}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 1024; desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES props = {}; props.Type = type;
    ID3D12Resource *raw = nullptr;
    Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
        IID_PPV_ARGS(&raw)), "buffer");
    return OwnedCOM<ID3D12Resource>(raw);
  };
  auto first = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
  auto second = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
  auto indices = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
  auto readback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
  if (!first || !second || !indices || !readback) return false;
  const UINT a[] = {0xdeadbeef, 8, 16, 32, 0xbadc0ffe};
  const UINT b[] = {0xdeadbeef, 48, 64, 96, 0xbadc0ffe};
  auto fill = [&](ID3D12Resource *resource, const void *values, size_t bytes) {
    void *mapped = nullptr;
    if (!Check(resource->Map(0, nullptr, &mapped), "map")) return false;
    std::memset(mapped, 0xcd, 1024); std::memcpy(mapped, values, bytes); resource->Unmap(0, nullptr); return true;
  };
  const unsigned short order[] = {0xffff, 0, 1, 2, 0xffff};
  if (!fill(first.get(), a, sizeof(a)) || !fill(second.get(), b, sizeof(b)) ||
      !fill(indices.get(), order, sizeof(order))) return false;
  auto heap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, bool visible) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {}; desc.Type = type; desc.NumDescriptors = 1;
    if (visible) desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap *raw = nullptr;
    Check(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&raw)), "heap");
    return OwnedCOM<ID3D12DescriptorHeap>(raw);
  };
  auto resources = heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
  auto dsvs = heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false);
  if (!resources || !dsvs) return false;
  const auto cpu = resources->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {}; srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.FirstElement = 1; srv.Buffer.NumElements = 2;
  device->CreateShaderResourceView(first.get(), &srv, cpu);
  D3D12_RESOURCE_DESC td = {}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  td.Width = 2; td.Height = td.DepthOrArraySize = td.MipLevels = td.SampleDesc.Count = 1;
  td.Format = DXGI_FORMAT_D32_FLOAT; td.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_HEAP_PROPERTIES props = {}; props.Type = D3D12_HEAP_TYPE_DEFAULT;
  ID3D12Resource *raw_target = nullptr;
  if (!Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &td,
      D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&raw_target)), "depth")) return false;
  OwnedCOM<ID3D12Resource> target(raw_target);
  auto dsv = dsvs->GetCPUDescriptorHandleForHeapStart(); device->CreateDepthStencilView(target.get(), nullptr, dsv);
  const auto flags = static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE |
      (live ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE : 0));
  D3D12_DESCRIPTOR_RANGE1 range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, flags, 0};
  D3D12_ROOT_PARAMETER1 parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable = {1, &range};
  parameters[0].ShaderVisibility = test.table_visibility;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants = {3, 0, 1}; parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  const auto root_flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
      (deny_vertex ? D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS : 0));
  auto make_root = [&](bool constants_only) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rd = {}; rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rd.Desc_1_1 = {constants_only ? 1u : 2u, parameters + (constants_only ? 1 : 0), 0, nullptr, root_flags};
    D3D12_DESCRIPTOR_RANGE range0 = {range.RangeType, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameters0[2] = {};
    parameters0[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters0[0].DescriptorTable = {1, &range0}; parameters0[0].ShaderVisibility = parameters[0].ShaderVisibility;
    parameters0[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters0[1].Constants = parameters[1].Constants; parameters0[1].ShaderVisibility = parameters[1].ShaderVisibility;
    if (legacy) {
      rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
      rd.Desc_1_0 = {constants_only ? 1u : 2u, parameters0 + (constants_only ? 1 : 0), 0, nullptr, root_flags};
    }
    ID3DBlob *raw_blob = nullptr;
    Check(D3D12SerializeVersionedRootSignature(&rd, &raw_blob, nullptr), "serialize");
    OwnedCOM<ID3DBlob> blob(raw_blob); ID3D12RootSignature *raw = nullptr;
    if (blob) Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&raw)), "root");
    return OwnedCOM<ID3D12RootSignature>(raw);
  };
  auto root = make_root(ordinary_only), ordinary_root = make_root(true);
  if (!root || !ordinary_root) return false;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = root.get();
  const auto &shader = ordinary_only ? ordinary : typed;
  pd.VS = {shader.data(), shader.size()}; // PS deliberately absent, no color attachments.
  pd.SampleMask = UINT_MAX; pd.SampleDesc.Count = 1; pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pd.RasterizerState.DepthClipEnable = TRUE;
  pd.DepthStencilState.DepthEnable = TRUE; pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  ID3D12PipelineState *raw_pso = nullptr;
  const auto hr = device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&raw_pso));
  OwnedCOM<ID3D12PipelineState> pso(raw_pso);
  if (deny_vertex && (hr == E_NOTIMPL || hr == E_INVALIDARG)) {
    std::puts("DEPTH_ORIGIN denied vertex root rejected at PSO PASS (no GPU submission)"); return true;
  }
  if (!Check(hr, "pso")) return false;
  OwnedCOM<ID3D12PipelineState> ordinary_pso;
  if (!deny_vertex) {
    pd.pRootSignature = ordinary_root.get(); pd.VS = {ordinary.data(), ordinary.size()}; raw_pso = nullptr;
    if (!Check(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&raw_pso)), "ordinary pso")) return false;
    ordinary_pso.reset(raw_pso);
  }
  ID3D12CommandAllocator *raw_allocator = nullptr; ID3D12GraphicsCommandList *raw_list = nullptr;
  if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "allocator")) return false;
  OwnedCOM<ID3D12CommandAllocator> allocator(raw_allocator);
  if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), pso.get(),
      IID_PPV_ARGS(&raw_list)), "list")) return false;
  OwnedCOM<ID3D12GraphicsCommandList> list(raw_list);
  auto transition = [&](D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {target.get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after}; list->ResourceBarrier(1, &barrier);
  };
  transition(D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  list->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
  list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  list->SetGraphicsRootSignature(root.get()); ID3D12DescriptorHeap *heaps[] = {resources.get()};
  list->SetDescriptorHeaps(1, heaps);
  if (!ordinary_only) list->SetGraphicsRootDescriptorTable(0, resources->GetGPUDescriptorHandleForHeapStart());
  list->SetGraphicsRoot32BitConstant(ordinary_only ? 0 : 1, 0xabc123, 0);
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_INDEX_BUFFER_VIEW ibv = {indices->GetGPUVirtualAddress(), sizeof(order), DXGI_FORMAT_R16_UINT};
  if (indexed) list->IASetIndexBuffer(&ibv);
  D3D12_VIEWPORT viewport = {0, 0, 2, 1, 0, 1}; D3D12_RECT rect = {0, 0, 2, 1};
  list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &rect);
  auto draw = [&] {
    if (indexed) list->DrawIndexedInstanced(3, 1, 1, 0, 0); else list->DrawInstanced(3, 1, 0, 0);
  };
  draw();
  if (deny_vertex) {
    const bool rejected = FAILED(list->Close());
    if (rejected) std::puts("DEPTH_ORIGIN denied vertex root rejected at Close PASS (no GPU submission)");
    return rejected;
  }
  rect.left = 1; list->RSSetScissorRects(1, &rect);
  list->SetPipelineState(ordinary_pso.get()); list->SetGraphicsRootSignature(ordinary_root.get());
  list->SetGraphicsRoot32BitConstant(0, 0xabc123, 0); draw();
  transition(D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION src = {}; src.pResource = target.get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION dst = {}; dst.pResource = readback.get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint.Offset = 512; dst.PlacedFootprint.Footprint = {DXGI_FORMAT_D32_FLOAT, 2, 1, 1, 256};
  list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  if (!Check(list->Close(), "close")) return false;
  for (unsigned pass = 0; pass < 4; ++pass) {
    const bool replacement = live && pass % 2 == 0;
    auto current = srv;
    if (replacement) { current.Buffer.FirstElement = 2; current.Buffer.NumElements = 1; }
    if (live) device->CreateShaderResourceView(replacement ? second.get() : first.get(), &current, cpu);
    if (!execute(list.get(), pass + 1)) return false;
    void *mapped = nullptr;
    if (!Check(readback->Map(0, nullptr, &mapped), "readback")) return false;
    const float *values = reinterpret_cast<const float *>(static_cast<const unsigned char *>(mapped) + 512);
    const float expected = ordinary_only ? 0.75f : replacement ? 0.25f : 0.15625f;
    const bool ok = std::isfinite(values[0]) && std::isfinite(values[1]) &&
        std::fabs(values[0] - expected) < 0.000001f && std::fabs(values[1] - 0.75f) < 0.000001f;
    std::printf("DEPTH_ORIGIN %s RS=%s live=%u vertex=%u indexed=%u ordinary=%u pass=%u depth=%g,%g expected=%g,0.75\n",
        ok ? "PASS" : "FAIL", legacy ? "1.0" : "1.1", live, vertex_visibility, indexed, ordinary_only,
        pass, values[0], values[1], expected);
    readback->Unmap(0, nullptr); if (!ok) return false;
  }
  return true;
}

int wmain(int argc, wchar_t **argv) {
  const bool automatic = argc == 5 && !wcscmp(argv[4], L"--auto");
  const bool ordinary_only = argc == 5 && !wcscmp(argv[4], L"--ordinary");
  if (argc != 4 && !automatic && !ordinary_only) return 1;
  std::vector<unsigned char> typed, ordinary;
  if (!Load(argv[1], typed) || !Load(argv[2], ordinary) ||
      !SetEnvironmentVariableW(L"DXMT_TYPED_ORIGIN_DXC_DIRECTORY", automatic || ordinary_only ? nullptr : argv[3]) ||
      !SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", nullptr)) return 1;
  ID3D12Device *raw = nullptr;
  if (!Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw)), "device")) return 1;
  OwnedCOM<ID3D12Device> device(raw);
  if (ordinary_only) {
    for (bool indexed : {false, true})
      if (!Run(device.get(), typed, ordinary,
          {RootVersion::RS11, false, D3D12_SHADER_VISIBILITY_ALL, indexed, DepthMode::Ordinary})) return 1;
    std::puts("ordinary depth-only GPU PASS (8 draws)"); return 0;
  }
  for (bool indexed : {false, true}) for (auto visibility : {D3D12_SHADER_VISIBILITY_ALL, D3D12_SHADER_VISIBILITY_VERTEX}) {
    for (bool live : {false, true})
      if (!Run(device.get(), typed, ordinary, {RootVersion::RS11, live, visibility, indexed})) return 1;
    if (!Run(device.get(), typed, ordinary, {RootVersion::RS10, true, visibility, indexed})) return 1;
  }
  DepthCase deny_pair = {RootVersion::RS11, false, D3D12_SHADER_VISIBILITY_VERTEX, false};
  std::puts("DEPTH_ORIGIN DenyVS pair: positive control (same shader/data/draw/root except deny flag)");
  if (!Run(device.get(), typed, ordinary, deny_pair)) return 1;
  deny_pair.mode = DepthMode::DenyVertex;
  if (!Run(device.get(), typed, ordinary, deny_pair)) return 1;
  std::puts("typed-origin native depth-only GPU PASS (52 draws)"); return 0;
}

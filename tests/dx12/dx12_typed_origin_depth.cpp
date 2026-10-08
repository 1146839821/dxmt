#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <cstdlib>
#include <cwchar>
#include "../../src/d3d12/d3d12_device.hpp"
#include "../../src/d3d12/d3d12_typed_origin_pipeline.hpp"

static UINT fixture_sample_mask = UINT_MAX;
static const wchar_t *fixture_compiler_directory = nullptr;

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

enum class DepthMode { Typed, Ordinary, DenyVertex, DenyGeometry, DenyHull, DenyDomain };
enum class RootVersion { RS10, RS11 };
struct DepthCase {
  RootVersion root_version;
  bool live;
  D3D12_SHADER_VISIBILITY table_visibility;
  bool indexed;
  DepthMode mode = DepthMode::Typed;
  UINT structured_stage = UINT_MAX;
};

enum class FixtureKind { Native, Geometry, Tessellation };
struct DepthShaders {
  FixtureKind kind = FixtureKind::Native;
  struct Stages { std::vector<unsigned char> vertex, geometry, hull, domain; } typed, ordinary, structured;
};

static bool Run(ID3D12Device *device, const DepthShaders &shaders, const DepthCase &test) {
  const auto &typed = shaders.typed.vertex, &ordinary = shaders.ordinary.vertex;
  const bool geometry_fixture = shaders.kind == FixtureKind::Geometry;
  const bool tessellation_fixture = shaders.kind == FixtureKind::Tessellation;
  const bool legacy = test.root_version == RootVersion::RS10;
  const bool live = test.live;
  const bool indexed = test.indexed;
  const bool vertex_visibility = test.table_visibility == D3D12_SHADER_VISIBILITY_VERTEX;
  const bool ordinary_only = test.mode == DepthMode::Ordinary;
  const bool deny_root = test.mode != DepthMode::Typed && test.mode != DepthMode::Ordinary;
  const bool emulation = geometry_fixture || tessellation_fixture;
  const UINT table_count = tessellation_fixture ? 3 : geometry_fixture ? 2 : 1;
  const D3D12_SHADER_VISIBILITY stages[] = {D3D12_SHADER_VISIBILITY_VERTEX,
      geometry_fixture ? D3D12_SHADER_VISIBILITY_GEOMETRY : D3D12_SHADER_VISIBILITY_HULL,
      D3D12_SHADER_VISIBILITY_DOMAIN};
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
  OwnedCOM<ID3D12Resource> extra_first[2], extra_second[2];
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
  for (UINT s = 1; s < table_count; ++s) {
    extra_first[s - 1] = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    extra_second[s - 1] = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    const UINT av[] = {0xdeadbeef, 8 + 4 * s, 16 + 4 * s, 32 + 4 * s, 48 + 4 * s, 60 + 4 * s, 0xbadc0ffe};
    const UINT bv[] = {0xdeadbeef, 48, 64 + 8 * s, 24 + 4 * s, 32 + 4 * s, 40 + 4 * s, 0xbadc0ffe};
    if (!extra_first[s - 1] || !extra_second[s - 1] ||
        !fill(extra_first[s - 1].get(), av, sizeof(av)) ||
        !fill(extra_second[s - 1].get(), bv, sizeof(bv))) return false;
  }
  auto heap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, bool visible) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {}; desc.Type = type; desc.NumDescriptors = visible ? table_count : 1;
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
  const auto descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  for (UINT s = 1; s < table_count; ++s) {
    auto stage_srv = srv;
    stage_srv.Buffer.FirstElement = 1 + s;
    stage_srv.Buffer.NumElements = s == 1 ? 3 : 2;
    if (test.structured_stage == s) { stage_srv.Format = DXGI_FORMAT_UNKNOWN; stage_srv.Buffer.StructureByteStride = 4; }
    device->CreateShaderResourceView(extra_first[s - 1].get(), &stage_srv, {cpu.ptr + s * descriptor_size});
  }
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
  D3D12_ROOT_PARAMETER1 parameters[4] = {};
  for (UINT s = 0; s < table_count; ++s) {
    parameters[s].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[s].DescriptorTable = {1, &range};
    parameters[s].ShaderVisibility = emulation ? stages[s] : test.table_visibility;
  }
  parameters[table_count].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[table_count].Constants = {3, 0, 1};
  parameters[table_count].ShaderVisibility = emulation ? D3D12_SHADER_VISIBILITY_ALL : D3D12_SHADER_VISIBILITY_VERTEX;
  const auto root_flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
      (test.mode == DepthMode::DenyVertex ? D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS : 0) |
      (test.mode == DepthMode::DenyGeometry ? D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS : 0) |
      (test.mode == DepthMode::DenyHull ? D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS : 0) |
      (test.mode == DepthMode::DenyDomain ? D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS : 0));
  auto make_root = [&](bool constants_only) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rd = {}; rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rd.Desc_1_1 = {constants_only ? 1u : table_count + 1, parameters + (constants_only ? table_count : 0), 0, nullptr, root_flags};
    D3D12_DESCRIPTOR_RANGE range0 = {range.RangeType, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameters0[4] = {};
    for (UINT s = 0; s < table_count; ++s) {
      parameters0[s].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters0[s].DescriptorTable = {1, &range0}; parameters0[s].ShaderVisibility = parameters[s].ShaderVisibility;
    }
    parameters0[table_count].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters0[table_count].Constants = parameters[table_count].Constants;
    parameters0[table_count].ShaderVisibility = parameters[table_count].ShaderVisibility;
    if (legacy) {
      rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
      rd.Desc_1_0 = {constants_only ? 1u : table_count + 1, parameters0 + (constants_only ? table_count : 0), 0, nullptr, root_flags};
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
  const auto set_emulation = [&](bool ordinary_stages) {
    const auto &selected = ordinary_stages ? shaders.ordinary : shaders.typed;
    const auto &gs = !ordinary_stages && test.structured_stage == 1 ? shaders.structured.geometry : selected.geometry;
    const auto &hs = !ordinary_stages && test.structured_stage == 1 ? shaders.structured.hull : selected.hull;
    const auto &ds = !ordinary_stages && test.structured_stage == 2 ? shaders.structured.domain : selected.domain;
    if (geometry_fixture) pd.GS = {gs.data(), gs.size()};
    if (tessellation_fixture) { pd.HS = {hs.data(), hs.size()}; pd.DS = {ds.data(), ds.size()}; }
  };
  set_emulation(ordinary_only);
  pd.SampleMask = fixture_sample_mask; pd.SampleDesc.Count = 1; pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pd.PrimitiveTopologyType = tessellation_fixture ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pd.RasterizerState.DepthClipEnable = TRUE;
  pd.DepthStencilState.DepthEnable = TRUE; pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  ID3D12PipelineState *raw_pso = nullptr;
  const auto hr = device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&raw_pso));
  OwnedCOM<ID3D12PipelineState> pso(raw_pso);
  if (deny_root && (hr == E_NOTIMPL || hr == E_INVALIDARG)) {
    std::printf("DEPTH_ORIGIN denied root mode=%u rejected at PSO PASS (no GPU submission)\n", unsigned(test.mode)); return true;
  }
  if (!Check(hr, "pso")) return false;
  if (!deny_root && !ordinary_only && fixture_sample_mask != UINT_MAX) {
    const dxmt::D3D12TypedOriginGraphicsVariant *variant = nullptr;
    if (!Check(static_cast<dxmt::MTLD3D12GraphicsPipelineState *>(pso.get())->GetTypedOriginVariant(
            fixture_compiler_directory, &variant), "private variant") || !variant ||
        !variant->pso || variant->bindings.empty()) return false;
    std::printf("DEPTH_ORIGIN private variant bindings=%zu mask=%08x PASS\n", variant->bindings.size(), fixture_sample_mask);
  }
  OwnedCOM<ID3D12PipelineState> ordinary_pso;
  if (!deny_root) {
    pd.SampleMask = UINT_MAX;
    pd.pRootSignature = ordinary_root.get(); pd.VS = {ordinary.data(), ordinary.size()}; raw_pso = nullptr;
    set_emulation(true);
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
  if (!ordinary_only) for (UINT s = 0; s < table_count; ++s)
    list->SetGraphicsRootDescriptorTable(s, {resources->GetGPUDescriptorHandleForHeapStart().ptr + s * descriptor_size});
  list->SetGraphicsRoot32BitConstant(ordinary_only ? 0 : table_count, 0xabc123, 0);
  list->IASetPrimitiveTopology(tessellation_fixture ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST :
      D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_INDEX_BUFFER_VIEW ibv = {indices->GetGPUVirtualAddress(), sizeof(order), DXGI_FORMAT_R16_UINT};
  if (indexed) list->IASetIndexBuffer(&ibv);
  D3D12_VIEWPORT viewport = {0, 0, 2, 1, 0, 1}; D3D12_RECT rect = {0, 0, 2, 1};
  list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &rect);
  auto draw = [&] {
    if (indexed) list->DrawIndexedInstanced(3, 1, 1, 0, 0); else list->DrawInstanced(3, 1, 0, 0);
  };
  draw();
  if (deny_root) {
    const bool rejected = FAILED(list->Close());
    if (rejected) std::printf("DEPTH_ORIGIN denied root mode=%u rejected at Close PASS (no GPU submission)\n", unsigned(test.mode));
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
    if (live) for (UINT s = 1; s < table_count; ++s) {
      auto stage_srv = srv;
      stage_srv.Buffer.FirstElement = replacement ? 2 + s : 1 + s;
      stage_srv.Buffer.NumElements = replacement ? (s == 1 ? 2 : 1) : (s == 1 ? 3 : 2);
      if (test.structured_stage == s) { stage_srv.Format = DXGI_FORMAT_UNKNOWN; stage_srv.Buffer.StructureByteStride = 4; }
      device->CreateShaderResourceView(replacement ? extra_second[s - 1].get() : extra_first[s - 1].get(),
          &stage_srv, {cpu.ptr + s * descriptor_size});
    }
    if (!execute(list.get(), pass + 1)) return false;
    void *mapped = nullptr;
    if (!Check(readback->Map(0, nullptr, &mapped), "readback")) return false;
    const float *values = reinterpret_cast<const float *>(static_cast<const unsigned char *>(mapped) + 512);
    float expected = ordinary_only ? 0.75f : replacement ? 0.25f : 0.15625f;
    if (emulation && !ordinary_only) {
      // Independent hand-calculated stage contributions, including OOB zero:
      // initial VS=40, GS/HS=2*300, DS=4*152; live VS=64,
      // GS/HS=2*100, DS=4*40. Every stage has a distinct origin/count.
      expected = (replacement ? (tessellation_fixture ? 424 : 264) :
          (tessellation_fixture ? 1248 : 640)) / 4096.0f;
    }
    if (!(fixture_sample_mask & 1u)) expected = 1.0f;
    const bool ok = std::isfinite(values[0]) && std::isfinite(values[1]) &&
        std::fabs(values[0] - expected) < 0.000001f && std::fabs(values[1] - 0.75f) < 0.000001f;
    std::printf("DEPTH_ORIGIN %s RS=%s live=%u vertex=%u indexed=%u ordinary=%u structured=%u pass=%u depth=%g,%g expected=%g,0.75\n",
        ok ? "PASS" : "FAIL", legacy ? "1.0" : "1.1", live, vertex_visibility, indexed, ordinary_only,
        test.structured_stage, pass, values[0], values[1], expected);
    readback->Unmap(0, nullptr); if (!ok) return false;
  }
  return true;
}

int wmain(int argc, wchar_t **argv) {
  if (argc > 4 && !std::wcsncmp(argv[argc - 1], L"--sample-mask=", 14)) {
    wchar_t *end = nullptr;
    const auto value = std::wcstoull(argv[argc - 1] + 14, &end, 0);
    if (!argv[argc - 1][14] || argv[argc - 1][14] == L'-' || *end || value > UINT_MAX) return 2;
    fixture_sample_mask = static_cast<UINT>(value); --argc;
  }
  std::vector<wchar_t> compiler_directory(32768);
  if (argc >= 4) {
    const auto length = GetFullPathNameW(argv[3], compiler_directory.size(), compiler_directory.data(), nullptr);
    if (!length || length >= compiler_directory.size()) return 1;
    fixture_compiler_directory = compiler_directory.data();
  }
  DepthShaders shaders;
  const wchar_t *mode = argc >= 5 ? argv[4] : L"";
  const bool geometry_fixture = (argc == 7 || argc == 8) && (!wcscmp(mode, L"--geometry") || !wcscmp(mode, L"--geometry-auto") ||
      !wcscmp(mode, L"--geometry-ordinary"));
  const bool tessellation_fixture = (argc == 9 || argc == 11) && (!wcscmp(mode, L"--tessellation") || !wcscmp(mode, L"--tessellation-auto") ||
      !wcscmp(mode, L"--tessellation-ordinary"));
  const bool automatic = (argc == 5 && !wcscmp(mode, L"--auto")) ||
      (geometry_fixture && !wcscmp(mode, L"--geometry-auto")) ||
      (tessellation_fixture && !wcscmp(mode, L"--tessellation-auto"));
  const bool ordinary_only = (argc == 5 && !wcscmp(mode, L"--ordinary")) ||
      (geometry_fixture && !wcscmp(mode, L"--geometry-ordinary")) ||
      (tessellation_fixture && !wcscmp(mode, L"--tessellation-ordinary"));
  if (argc != 4 && !automatic && !ordinary_only && !geometry_fixture && !tessellation_fixture) return 1;
  shaders.kind = tessellation_fixture ? FixtureKind::Tessellation : geometry_fixture ? FixtureKind::Geometry : FixtureKind::Native;
  if (geometry_fixture && (!Load(argv[5], shaders.typed.geometry) || !Load(argv[6], shaders.ordinary.geometry))) return 1;
  if (tessellation_fixture && (!Load(argv[5], shaders.typed.hull) || !Load(argv[6], shaders.typed.domain) ||
      !Load(argv[7], shaders.ordinary.hull) || !Load(argv[8], shaders.ordinary.domain))) return 1;
  const bool mixed_stages = (geometry_fixture && argc == 8) || (tessellation_fixture && argc == 11);
  if (geometry_fixture && mixed_stages && !Load(argv[7], shaders.structured.geometry)) return 1;
  if (tessellation_fixture && mixed_stages &&
      (!Load(argv[9], shaders.structured.hull) || !Load(argv[10], shaders.structured.domain))) return 1;
  auto &typed = shaders.typed.vertex, &ordinary = shaders.ordinary.vertex;
  if (!Load(argv[1], typed) || !Load(argv[2], ordinary) ||
      !SetEnvironmentVariableW(L"DXMT_TYPED_ORIGIN_DXC_DIRECTORY", automatic || ordinary_only ? nullptr : fixture_compiler_directory) ||
      !SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", nullptr)) return 1;
  ID3D12Device *raw = nullptr;
  if (!Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw)), "device")) return 1;
  OwnedCOM<ID3D12Device> device(raw);
  if (ordinary_only) {
    for (bool indexed : {false, true})
      if (!Run(device.get(), shaders,
          {RootVersion::RS11, false, D3D12_SHADER_VISIBILITY_ALL, indexed, DepthMode::Ordinary})) return 1;
    std::puts("ordinary depth-only GPU PASS (8 draws)"); return 0;
  }
  if (geometry_fixture || tessellation_fixture) {
    for (bool indexed : {false, true}) {
      for (bool live : {false, true})
        if (!Run(device.get(), shaders, {RootVersion::RS11, live, D3D12_SHADER_VISIBILITY_VERTEX, indexed})) return 1;
      if (!Run(device.get(), shaders, {RootVersion::RS10, true, D3D12_SHADER_VISIBILITY_VERTEX, indexed})) return 1;
    }
    for (auto mode : geometry_fixture ? std::vector<DepthMode>{DepthMode::DenyGeometry} :
        std::vector<DepthMode>{DepthMode::DenyHull, DepthMode::DenyDomain}) {
      DepthCase pair = {RootVersion::RS11, false, D3D12_SHADER_VISIBILITY_VERTEX, false};
      if (!Run(device.get(), shaders, pair)) return 1;
      pair.mode = mode;
      if (!Run(device.get(), shaders, pair)) return 1;
    }
    if (mixed_stages) for (UINT s = 1; s < (tessellation_fixture ? 3u : 2u); ++s) for (bool live : {false, true}) {
      DepthCase mixed = {RootVersion::RS11, live, D3D12_SHADER_VISIBILITY_VERTEX, true};
      mixed.structured_stage = s;
      if (!Run(device.get(), shaders, mixed)) return 1;
    }
    std::printf("typed-origin %s depth GPU PASS (direct/indexed, deny pairs, mixed=%u, ordinary restoration)\n",
        geometry_fixture ? "geometry" : "tessellation", mixed_stages);
    return 0;
  }
  for (bool indexed : {false, true}) for (auto visibility : {D3D12_SHADER_VISIBILITY_ALL, D3D12_SHADER_VISIBILITY_VERTEX}) {
    for (bool live : {false, true})
      if (!Run(device.get(), shaders, {RootVersion::RS11, live, visibility, indexed})) return 1;
    if (!Run(device.get(), shaders, {RootVersion::RS10, true, visibility, indexed})) return 1;
  }
  DepthCase deny_pair = {RootVersion::RS11, false, D3D12_SHADER_VISIBILITY_VERTEX, false};
  std::puts("DEPTH_ORIGIN DenyVS pair: positive control (same shader/data/draw/root except deny flag)");
  if (!Run(device.get(), shaders, deny_pair)) return 1;
  deny_pair.mode = DepthMode::DenyVertex;
  if (!Run(device.get(), shaders, deny_pair)) return 1;
  std::puts("typed-origin native depth-only GPU PASS (52 draws)"); return 0;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

template <typename T> struct ReleaseCOM { void operator()(T *p) const { if (p) p->Release(); } };
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;
static bool Check(HRESULT hr, const char *name) {
  if (FAILED(hr)) std::printf("%s failed %08lx\n", name, (unsigned long)hr);
  return SUCCEEDED(hr);
}
static bool Load(const char *path, std::vector<unsigned char> &bytes) {
  HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {}; DWORD read = 0;
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 32 * 1024 * 1024;
  if (ok) { bytes.resize(size.QuadPart); ok = ReadFile(file, bytes.data(), bytes.size(), &read, nullptr) && read == bytes.size(); }
  CloseHandle(file); return ok;
}
int main(int argc, char **argv) {
  const bool experimental = argc == 7 && !std::strcmp(argv[6], "--experimental");
  if ((argc != 6 && !experimental) || !*argv[5]) return 2;
  if (experimental) {
    if (!SetEnvironmentVariableW(L"DXMT_EXPERIMENTAL_LOGIC_OP_MSAA", L"1")) return 1;
    std::puts("D3D12_MSAA experimental admission requested");
  }
  unsigned op = 0;
  for (const char *p = argv[5]; *p; ++p) {
    if (*p < '0' || *p > '9' || op > 15) return 2;
    op = op * 10 + *p - '0';
  }
  if (op > 15) return 2;
  std::vector<unsigned char> vs, seed, source, read;
  if (!Load(argv[1], vs) || !Load(argv[2], seed) || !Load(argv[3], source) || !Load(argv[4], read)) return 1;
  ID3D12Device *raw_device = nullptr;
  if (!Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)), "device")) return 1;
  OwnedCOM<ID3D12Device> device(raw_device);
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {}; support.Format = DXGI_FORMAT_R32_UINT;
  if (!Check(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)), "format support")) return 1;
  const UINT required = D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD;
  if ((support.Support1 & required) != required || (support.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE)) {
    std::puts("R32_UINT MSAA/load without resolve capability mismatch"); return 1;
  }
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
  quality.Format = DXGI_FORMAT_R32_UINT; quality.SampleCount = 4;
  if (!Check(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality, sizeof(quality)), "sample quality") ||
      !quality.NumQualityLevels) return 1;
  std::puts("D3D12_MSAA R32_UINT render/load without resolve capability PASS");
  auto root = [&](const D3D12_ROOT_SIGNATURE_DESC &desc) {
    ID3DBlob *raw_blob = nullptr; ID3D12RootSignature *raw = nullptr;
    if (!Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &raw_blob, nullptr), "serialize"))
      return OwnedCOM<ID3D12RootSignature>{};
    OwnedCOM<ID3DBlob> blob(raw_blob);
    Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&raw)), "root");
    return OwnedCOM<ID3D12RootSignature>(raw);
  };
  D3D12_ROOT_SIGNATURE_DESC empty = {};
  auto graphics_root = root(empty);
  D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable = {1, &range};
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  D3D12_ROOT_SIGNATURE_DESC compute_desc = {2, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  auto compute_root = root(compute_desc);
  if (!graphics_root || !compute_root) return 1;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
  pd.pRootSignature = graphics_root.get(); pd.VS = {vs.data(), vs.size()}; pd.PS = {seed.data(), seed.size()};
  pd.SampleMask = UINT_MAX; pd.SampleDesc.Count = 4; pd.NumRenderTargets = 1;
  pd.RTVFormats[0] = DXGI_FORMAT_R32_UINT; pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pd.RasterizerState.DepthClipEnable = TRUE;
  pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  ID3D12PipelineState *raw = nullptr;
  if (!Check(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&raw)), "seed PSO")) return 1;
  OwnedCOM<ID3D12PipelineState> seed_pso(raw);
  pd.PS = {source.data(), source.size()}; pd.BlendState.RenderTarget[0].LogicOpEnable = TRUE;
  pd.BlendState.RenderTarget[0].LogicOp = static_cast<D3D12_LOGIC_OP>(op); raw = nullptr;
  if (!Check(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&raw)), "logic PSO")) return 1;
  OwnedCOM<ID3D12PipelineState> logic_pso(raw);
  D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {}; cd.pRootSignature = compute_root.get(); cd.CS = {read.data(), read.size()}; raw = nullptr;
  if (!Check(device->CreateComputePipelineState(&cd, IID_PPV_ARGS(&raw)), "read PSO")) return 1;
  OwnedCOM<ID3D12PipelineState> read_pso(raw);
  D3D12_RESOURCE_DESC td = {}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  td.Width = td.Height = td.DepthOrArraySize = td.MipLevels = 1; td.SampleDesc.Count = 4;
  td.Format = DXGI_FORMAT_R32_UINT; td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_HEAP_PROPERTIES props = {}; props.Type = D3D12_HEAP_TYPE_DEFAULT;
  ID3D12Resource *resource = nullptr;
  if (!Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET,
      nullptr, IID_PPV_ARGS(&resource)), "MSAA target")) return 1;
  OwnedCOM<ID3D12Resource> target(resource);
  D3D12_RESOURCE_DESC bd = {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = 256;
  bd.Height = bd.DepthOrArraySize = bd.MipLevels = bd.SampleDesc.Count = 1;
  bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  resource = nullptr;
  if (!Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      nullptr, IID_PPV_ARGS(&resource)), "output")) return 1;
  OwnedCOM<ID3D12Resource> output(resource); bd.Flags = D3D12_RESOURCE_FLAG_NONE; props.Type = D3D12_HEAP_TYPE_READBACK; resource = nullptr;
  if (!Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, IID_PPV_ARGS(&resource)), "readback")) return 1;
  OwnedCOM<ID3D12Resource> readback(resource);
  auto heap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, bool visible) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {}; desc.Type = type; desc.NumDescriptors = 1;
    if (visible) desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap *raw_heap = nullptr;
    Check(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&raw_heap)), "heap");
    return OwnedCOM<ID3D12DescriptorHeap>(raw_heap);
  };
  auto rtvs = heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false), resources = heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
  if (!rtvs || !resources) return 1;
  D3D12_RENDER_TARGET_VIEW_DESC rtv = {}; rtv.Format = td.Format; rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
  const auto rtv_handle = rtvs->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(target.get(), &rtv, rtv_handle);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {}; srv.Format = td.Format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  device->CreateShaderResourceView(target.get(), &srv, resources->GetCPUDescriptorHandleForHeapStart());
  ID3D12CommandQueue *raw_queue = nullptr; D3D12_COMMAND_QUEUE_DESC qd = {};
  if (!Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&raw_queue)), "queue")) return 1;
  OwnedCOM<ID3D12CommandQueue> queue(raw_queue);
  ID3D12CommandAllocator *raw_allocator = nullptr;
  if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "allocator")) return 1;
  OwnedCOM<ID3D12CommandAllocator> allocator(raw_allocator);
  ID3D12GraphicsCommandList *raw_list = nullptr;
  if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), seed_pso.get(), IID_PPV_ARGS(&raw_list)), "list")) return 1;
  OwnedCOM<ID3D12GraphicsCommandList> list(raw_list);
  list->SetGraphicsRootSignature(graphics_root.get()); list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
  D3D12_VIEWPORT viewport = {0, 0, 1, 1, 0, 1}; D3D12_RECT rect = {0, 0, 1, 1};
  list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &rect); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->DrawInstanced(3, 1, 0, 0); list->SetPipelineState(logic_pso.get()); list->DrawInstanced(3, 1, 0, 0);
  auto transition = [&](ID3D12Resource *r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after}; list->ResourceBarrier(1, &barrier);
  };
  transition(target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  list->SetPipelineState(read_pso.get()); list->SetComputeRootSignature(compute_root.get());
  ID3D12DescriptorHeap *heaps[] = {resources.get()}; list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootDescriptorTable(0, resources->GetGPUDescriptorHandleForHeapStart());
  list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress()); list->Dispatch(1, 1, 1);
  transition(output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(readback.get(), 0, output.get(), 0, 16);
  if (!Check(list->Close(), "close")) return 1;
  ID3D12CommandList *lists[] = {list.get()}; queue->ExecuteCommandLists(1, lists);
  ID3D12Fence *raw_fence = nullptr;
  if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_fence)), "fence")) return 1;
  OwnedCOM<ID3D12Fence> fence(raw_fence);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) return 1;
  const bool finished = Check(queue->Signal(fence.get(), 1), "signal") &&
      Check(fence->SetEventOnCompletion(1, event), "completion") && WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
  CloseHandle(event); if (!finished) return 1;
  void *mapped = nullptr;
  if (!Check(readback->Map(0, nullptr, &mapped), "map")) return 1;
  const auto *values = static_cast<const unsigned *>(mapped);
  bool ok = true;
  for (unsigned sample = 0; sample < 4; ++sample) {
    const unsigned src = 240, dst = 0x12340000u + sample * 0x101u;
    const unsigned expected[] = {0, UINT_MAX, src, ~src, dst, ~dst, src & dst, ~(src & dst),
        src | dst, ~(src | dst), src ^ dst, ~(src ^ dst), src & ~dst, ~src & dst, src | ~dst, ~src | dst};
    std::printf("D3D12_MSAA op=%u sample=%u observed=%08x expected=%08x\n", op, sample, values[sample], expected[op]);
    ok &= values[sample] == expected[op];
  }
  readback->Unmap(0, nullptr);
  if (!ok) return 1;
  std::puts("D3D12_MSAA 4x R32_UINT raw-sample LogicOp PASS"); return 0;
}

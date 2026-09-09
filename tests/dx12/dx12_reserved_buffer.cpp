#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr UINT TileBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT A = 0x12345678, B = 0x76543210, Written = 0xabcdef01;

template <typename T> struct Owned {
  T *ptr = nullptr;
  ~Owned() { if (ptr) ptr->Release(); }
  T *operator->() const { return ptr; }
  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
};
void Check(const char *operation, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << operation << ": 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    throw std::runtime_error(operation);
  }
}
D3D12_HEAP_PROPERTIES Properties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES p = {};
  p.Type = type; p.CreationNodeMask = p.VisibleNodeMask = 1;
  return p;
}
D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC d = {};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes;
  d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
  return d;
}
void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b = {};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list->ResourceBarrier(1, &b);
}
std::vector<char> ReadShader(const char *path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f || f.tellg() <= 0) throw std::runtime_error("cannot open shader");
  std::vector<char> data(static_cast<size_t>(f.tellg()));
  f.seekg(0); f.read(data.data(), data.size());
  if (!f) throw std::runtime_error("cannot read shader");
  return data;
}
void Wait(ID3D12Fence *fence, UINT64 value) {
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event) throw std::runtime_error("CreateEvent");
  HRESULT hr = fence->SetEventOnCompletion(value, event);
  DWORD status = SUCCEEDED(hr) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event);
  if (status != WAIT_OBJECT_0) {
    // Do not destroy allocations still referenced by GPU work after a timeout.
    std::cerr << "GPU completion failed or timed out at " << value << std::endl;
    ExitProcess(1);
  }
}
int Run(const char *read_path, const char *write_path) {
  auto read_shader = ReadShader(read_path), write_shader = ReadShader(write_path);
  Owned<ID3D12Device> device;
  Check("device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.ptr)));
  Owned<ID3D12CommandQueue> direct, compute;
  D3D12_COMMAND_QUEUE_DESC q = {};
  q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Check("direct queue", device->CreateCommandQueue(&q, IID_PPV_ARGS(&direct.ptr)));
  q.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  Check("compute queue", device->CreateCommandQueue(&q, IID_PPV_ARGS(&compute.ptr)));
  Owned<ID3D12Fence> mapping_done, compute_done, gate;
  Check("mapping fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mapping_done.ptr)));
  Check("compute fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&compute_done.ptr)));
  Check("gate fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate.ptr)));
  Owned<ID3D12Heap> heap;
  D3D12_HEAP_DESC hd = {};
  hd.SizeInBytes = 2 * TileBytes; hd.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  Check("tile heap", device->CreateHeap(&hd, IID_PPV_ARGS(&heap.ptr)));
  Owned<ID3D12Resource> sparse, upload, output, readback;
  auto d = BufferDesc(2 * TileBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  Check("reserved buffer", device->CreateReservedResource(&d, D3D12_RESOURCE_STATE_COMMON,
                                                          nullptr, IID_PPV_ARGS(&sparse.ptr)));
  if (!sparse->GetGPUVirtualAddress()) {
    std::cout << "SKIP: reserved buffer has no shader-visible backing\n";
    return 77;
  }
  const auto stable_va = sparse->GetGPUVirtualAddress();
  auto create = [&](Owned<ID3D12Resource> &r, UINT bytes, D3D12_HEAP_TYPE type,
                    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    auto props = Properties(type); auto desc = BufferDesc(bytes, flags);
    Check("committed buffer", device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE,
          &desc, state, nullptr, IID_PPV_ARGS(&r.ptr)));
  };
  create(upload, 2 * TileBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
  create(output, 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
  create(readback, TileBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  UINT *data = nullptr;
  Check("upload map", upload->Map(0, nullptr, reinterpret_cast<void **>(&data)));
  for (UINT i = 0; i < 2 * TileBytes / sizeof(UINT); ++i) data[i] = i < TileBytes / sizeof(UINT) ? A : B;
  upload->Unmap(0, nullptr);

  // Create all descriptors before mapping. Never rewrite them during any phase.
  Owned<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC dh = {};
  dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; dh.NumDescriptors = 3;
  dh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check("descriptor heap", device->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&descriptors.ptr)));
  auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
  const UINT stride = device->GetDescriptorHandleIncrementSize(dh.Type);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = TileBytes / 4; srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device->CreateShaderResourceView(sparse.ptr, &srv, cpu);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 64; uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  cpu.ptr += stride; device->CreateUnorderedAccessView(output.ptr, nullptr, &uav, cpu);
  uav.Buffer.NumElements = TileBytes / 4;
  cpu.ptr += stride; device->CreateUnorderedAccessView(sparse.ptr, nullptr, &uav, cpu);
  D3D12_DESCRIPTOR_RANGE ranges[2] = {
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 1}};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  D3D12_ROOT_SIGNATURE_DESC rs = {}; rs.NumParameters = 1; rs.pParameters = &parameter;
  Owned<ID3DBlob> blob, errors;
  Check("serialize root signature", D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob.ptr, &errors.ptr));
  Owned<ID3D12RootSignature> root;
  Check("root signature", device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root.ptr)));
  Owned<ID3D12PipelineState> read_pso, write_pso;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = root.ptr;
  pd.CS = {read_shader.data(), read_shader.size()};
  Check("read PSO", device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&read_pso.ptr)));
  pd.CS = {write_shader.data(), write_shader.size()};
  Check("write PSO", device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&write_pso.ptr)));
  Owned<ID3D12CommandAllocator> init_allocator;
  Owned<ID3D12GraphicsCommandList> init;
  Check("init allocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&init_allocator.ptr)));
  Check("init list", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, init_allocator.ptr, nullptr, IID_PPV_ARGS(&init.ptr)));
  D3D12_TILED_RESOURCE_COORDINATE coord = {};
  D3D12_TILE_REGION_SIZE region = {}; region.NumTiles = 2;
  UINT offset = 0;
  direct->UpdateTileMappings(sparse.ptr, 1, &coord, &region, heap.ptr, 1, nullptr, &offset, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
  Transition(init.ptr, sparse.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
  init->CopyTiles(sparse.ptr, &coord, &region, upload.ptr, 0, D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
  Transition(init.ptr, sparse.ptr, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
  Check("init close", init->Close());
  ID3D12CommandList *initial_lists[] = {init.ptr};
  direct->ExecuteCommandLists(1, initial_lists);
  Check("init signal", direct->Signal(mapping_done.ptr, 1));
  UINT64 serial = 1;
  region.NumTiles = 1;
  auto phase = [&](const char *name, int tile, bool write, UINT expected, bool delayed = false) {
    if (serial > 1) Check("direct waits previous compute", direct->Wait(compute_done.ptr, serial - 1));
    if (delayed) Check("hold mapping queue", direct->Wait(gate.ptr, 1));
    D3D12_TILE_RANGE_FLAGS flags = tile < 0 ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE;
    UINT heap_tile = tile < 0 ? 0 : static_cast<UINT>(tile);
    direct->UpdateTileMappings(sparse.ptr, 1, &coord, &region, tile < 0 ? nullptr : heap.ptr,
                              1, &flags, &heap_tile, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
    Check("mapping signal", direct->Signal(mapping_done.ptr, serial + 1));
    Check("compute waits mapping", compute->Wait(mapping_done.ptr, serial + 1));
    // Use a fresh allocator/list for every phase.  The queue fence signals
    // submission ordering, while DXMT retires command allocators on the
    // Metal command-buffer completion callback; reusing one allocator here
    // can race that callback and produce a spurious E_FAIL from Reset().
    Owned<ID3D12CommandAllocator> phase_allocator;
    Owned<ID3D12GraphicsCommandList> phase_list;
    Check("phase allocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                                              IID_PPV_ARGS(&phase_allocator.ptr)));
    Check("phase list", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                                    phase_allocator.ptr,
                                                    write ? write_pso.ptr : read_pso.ptr,
                                                    IID_PPV_ARGS(&phase_list.ptr)));
    phase_list->SetComputeRootSignature(root.ptr);
    ID3D12DescriptorHeap *heaps[] = {descriptors.ptr};
    phase_list->SetDescriptorHeaps(1, heaps);
    phase_list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
    const auto access = write ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Transition(phase_list.ptr, sparse.ptr, D3D12_RESOURCE_STATE_COMMON, access);
    if (!write) Transition(phase_list.ptr, output.ptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    phase_list->Dispatch(1, 1, 1);
    if (!write) {
      Transition(phase_list.ptr, output.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      phase_list->CopyBufferRegion(readback.ptr, 0, output.ptr, 0, 2 * sizeof(UINT));
      Transition(phase_list.ptr, output.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    } else if (tile >= 0) {
      Transition(phase_list.ptr, sparse.ptr, access, D3D12_RESOURCE_STATE_COPY_SOURCE);
      phase_list->CopyTiles(sparse.ptr, &coord, &region, readback.ptr, 0, D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
    }
    Transition(phase_list.ptr, sparse.ptr, write && tile >= 0 ? D3D12_RESOURCE_STATE_COPY_SOURCE : access, D3D12_RESOURCE_STATE_COMMON);
    Check("phase close", phase_list->Close());
    ID3D12CommandList *lists[] = {phase_list.ptr};
    compute->ExecuteCommandLists(1, lists);
    Check("compute signal", compute->Signal(compute_done.ptr, serial));
    if (delayed) Check("release mapping queue", gate->Signal(1));
    Wait(compute_done.ptr, serial++);
    if (!write || tile >= 0) {
      Check("readback map", readback->Map(0, nullptr, reinterpret_cast<void **>(&data)));
      UINT first = data[0], last = data[write ? TileBytes / sizeof(UINT) - 1 : 1];
      readback->Unmap(0, nullptr);
      if (first != expected || last != expected) {
        std::cerr << name << " expected 0x" << std::hex << expected << ", got 0x" << first << "/0x" << last << std::dec << "\n";
        throw std::runtime_error("shader result mismatch");
      }
    }
    if (sparse->GetGPUVirtualAddress() != stable_va) throw std::runtime_error("VA changed on remap");
    std::cout << name << " passed\n";
  };
  phase("nonzero backing A / descriptors before map", 0, false, A + 1);
  phase("remap A to B / delayed direct-to-compute wait", 1, false, B + 1, true);
  phase("remap back to A preserves contents", 0, false, A + 1);
  phase("NULL SRV reads zero", -1, false, 1);
  phase("NULL UAV write completes", -1, true, 0);
  phase("NULL SRV remains zero after write", -1, false, 1);
  phase("NULL write leaves backing A intact", 0, false, A + 1);
  phase("NULL write leaves backing B intact", 1, false, B + 1);
  phase("mapped UAV write and CopyTiles readback", 1, true, Written);
  phase("SRV sees mapped UAV write", 1, false, Written + 1);
  phase("mapped write leaves other backing intact", 0, false, A + 1);
  std::cout << "Reserved buffer shader, remap, NULL and cross-queue tests passed\n";
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: dx12_reserved_buffer <probe_read.cso> <probe_write.cso>\n";
    return 2;
  }
  try { return Run(argv[1], argv[2]); }
  catch (const std::exception &e) { std::cerr << e.what() << "\n"; return 1; }
}

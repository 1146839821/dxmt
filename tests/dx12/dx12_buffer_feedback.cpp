#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
template <typename T> struct Owned {
  T *p = nullptr;
  ~Owned() { if (p) p->Release(); }
};
void Check(HRESULT hr) { if (FAILED(hr)) { std::printf("HRESULT %08lx\n", (unsigned long)hr); throw std::runtime_error("API failed"); } }
D3D12_RESOURCE_DESC Buffer(UINT64 size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC d = {};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = size;
  d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
  return d;
}
D3D12_HEAP_PROPERTIES Properties(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES p = {}; p.Type = type; p.CreationNodeMask = p.VisibleNodeMask = 1; return p;
}
void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list->ResourceBarrier(1, &b);
}
int Run(pD3DCompile compile, bool uav_source, bool root_source, bool indirect, bool pixel, bool vertex, bool geometry) {
  const bool graphics_stage = pixel || vertex || geometry;
  // Equal zero payloads in mapped and NULL tiles force status to be independent
  // of payload. Both descriptor views start at the last word of tile zero.
  const char *hlsl = R"(
#ifdef UAV_SOURCE
RWByteAddressBuffer raw : register(u1);
RWStructuredBuffer<uint> structured : register(u2);
#else
ByteAddressBuffer raw : register(t0);
StructuredBuffer<uint> structured : register(t1);
#endif
RWStructuredBuffer<uint> output : register(u0);
#ifdef GEOMETRY_SOURCE
struct GeometryVertex { float4 position : SV_Position; };
[maxvertexcount(3)] void main(triangle GeometryVertex positions[3],
                            inout TriangleStream<GeometryVertex> stream) {
#elif defined(VERTEX_SOURCE)
float4 main(uint id : SV_VertexID) : SV_Position {
  if (id == 0) {
#elif defined(PIXEL_SOURCE)
void main(float4 position : SV_Position) {
#else
[numthreads(1,1,1)] void main() {
#endif
  uint a, b, c, d, e;
  uint x = raw.Load(0, a);
  uint y = raw.Load(4, b);
  uint2 z = raw.Load2(0, c);
  uint s = structured.Load(0, d);
  uint t = structured.Load(1, e);
  output[0]=x; output[1]=CheckAccessFullyMapped(a);
  output[2]=y; output[3]=CheckAccessFullyMapped(b);
  output[4]=z.x; output[5]=z.y; output[6]=CheckAccessFullyMapped(c);
  output[7]=s; output[8]=CheckAccessFullyMapped(d);
  output[9]=t; output[10]=CheckAccessFullyMapped(e);
  output[11]=0x1234u;
#ifdef VERTEX_SOURCE
  }
  return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);
#endif
#ifdef GEOMETRY_SOURCE
  for (uint i = 0; i < 3; ++i) stream.Append(positions[i]);
  stream.RestartStrip();
#endif
})";
  Owned<ID3DBlob> shader, errors;
  D3D_SHADER_MACRO macros[5] = {}; UINT macro_count = 0;
  if (uav_source) macros[macro_count++] = {"UAV_SOURCE", "1"};
  if (pixel) macros[macro_count++] = {"PIXEL_SOURCE", "1"};
  if (vertex) macros[macro_count++] = {"VERTEX_SOURCE", "1"};
  if (geometry) macros[macro_count++] = {"GEOMETRY_SOURCE", "1"};
  auto hr = compile(hlsl, std::strlen(hlsl), "buffer-feedback", macros, nullptr, "main", geometry ? "gs_5_0" : vertex ? "vs_5_0" : pixel ? "ps_5_0" : "cs_5_0",
                    D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &shader.p, &errors.p);
  if (errors.p) std::printf("%s\n", (const char *)errors.p->GetBufferPointer());
  Check(hr);
  auto disassemble = reinterpret_cast<pD3DDisassemble>(
      GetProcAddress(GetModuleHandleA("d3dcompiler_47.dll"), "D3DDisassemble"));
  if (!disassemble) throw std::runtime_error("disassembler unavailable");
  Owned<ID3DBlob> assembly;
  Check(disassemble(shader.p->GetBufferPointer(), shader.p->GetBufferSize(), 0, nullptr, &assembly.p));
  auto text = static_cast<const char *>(assembly.p->GetBufferPointer());
  std::printf("%s\n", text);
  if (!std::strstr(text, "ld_raw_s") || !std::strstr(text, "ld_structured_s") ||
      !std::strstr(text, "check_access_fully_mapped"))
    throw std::runtime_error("feedback opcodes missing from compiled DXBC");
  Owned<ID3D12Device> device;
  Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)));
  Owned<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC qd = {};
  Check(device.p->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue.p)));
  Owned<ID3D12CommandAllocator> allocator;
  Check(device.p->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.p)));
  Owned<ID3D12Fence> fence;
  Check(device.p->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.p)));
  Owned<ID3D12Resource> source, upload, output, readback;
  auto source_desc = Buffer(131072, uav_source ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
  Check(device.p->CreateReservedResource(&source_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&source.p)));
  auto create = [&](Owned<ID3D12Resource> &resource, UINT64 size, D3D12_HEAP_TYPE type,
                    D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
    auto properties = Properties(type); auto desc = Buffer(size, flags);
    Check(device.p->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                            IID_PPV_ARGS(&resource.p)));
  };
  create(upload, 65536, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  void *mapped = nullptr; Check(upload.p->Map(0, nullptr, &mapped)); std::memset(mapped, 0, 65536); upload.p->Unmap(0, nullptr);
  create(output, 48, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  create(readback, 48, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
  Owned<ID3D12Heap> tiles;
  D3D12_HEAP_DESC hd = {}; hd.SizeInBytes = 65536; hd.Properties = Properties(D3D12_HEAP_TYPE_DEFAULT);
  hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  Check(device.p->CreateHeap(&hd, IID_PPV_ARGS(&tiles.p)));
  Owned<ID3D12DescriptorHeap> descriptors;
  D3D12_DESCRIPTOR_HEAP_DESC dd = {}; dd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  dd.NumDescriptors = 3; dd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check(device.p->CreateDescriptorHeap(&dd, IID_PPV_ARGS(&descriptors.p)));
  auto cpu = descriptors.p->GetCPUDescriptorHandleForHeapStart();
  auto stride = device.p->GetDescriptorHandleIncrementSize(dd.Type);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {}; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Format = DXGI_FORMAT_R32_TYPELESS; srv.Buffer.FirstElement = 16383; srv.Buffer.NumElements = 2;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  if (uav_source) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC raw = {}; raw.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    raw.Format = DXGI_FORMAT_R32_TYPELESS; raw.Buffer.FirstElement = 16383; raw.Buffer.NumElements = 2;
    raw.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device.p->CreateUnorderedAccessView(source.p, nullptr, &raw, cpu);
  } else device.p->CreateShaderResourceView(source.p, &srv, cpu);
  cpu.ptr += stride;
  srv.Format = DXGI_FORMAT_UNKNOWN; srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE; srv.Buffer.StructureByteStride = 4;
  if (uav_source) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC structured = {}; structured.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    structured.Buffer.FirstElement = 16383; structured.Buffer.NumElements = 2; structured.Buffer.StructureByteStride = 4;
    device.p->CreateUnorderedAccessView(source.p, nullptr, &structured, cpu);
  } else device.p->CreateShaderResourceView(source.p, &srv, cpu);
  cpu.ptr += stride;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {}; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 12; uav.Buffer.StructureByteStride = 4;
  device.p->CreateUnorderedAccessView(output.p, nullptr, &uav, cpu);
  D3D12_DESCRIPTOR_RANGE ranges[] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0},
                                    {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 2}};
  if (uav_source) { ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[0].BaseShaderRegister = 1; }
  D3D12_ROOT_PARAMETER parameter = {}; parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  D3D12_ROOT_SIGNATURE_DESC rd = {}; rd.NumParameters = 1; rd.pParameters = &parameter;
  D3D12_ROOT_PARAMETER root_parameters[3] = {};
  if (root_source) {
    for (UINT i = 0; i < 2; ++i) {
      root_parameters[i].ParameterType = uav_source ? D3D12_ROOT_PARAMETER_TYPE_UAV : D3D12_ROOT_PARAMETER_TYPE_SRV;
      root_parameters[i].Descriptor.ShaderRegister = uav_source ? i + 1 : i;
    }
    root_parameters[2] = parameter;
    root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
    // Table starts at the heap base; output remains at descriptor offset 2.
    rd.NumParameters = 3; rd.pParameters = root_parameters;
  }
  Owned<ID3DBlob> root_blob, root_error;
  Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob.p, &root_error.p));
  Owned<ID3D12RootSignature> root;
  Check(device.p->CreateRootSignature(0, root_blob.p->GetBufferPointer(), root_blob.p->GetBufferSize(), IID_PPV_ARGS(&root.p)));
  Owned<ID3D12PipelineState> pipeline;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = root.p;
  pd.CS = {shader.p->GetBufferPointer(), shader.p->GetBufferSize()};
  if (graphics_stage) {
    const char *vertex_source = "float4 main(uint id : SV_VertexID) : SV_Position { return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1); }";
    Owned<ID3DBlob> vs;
    if (!vertex) Check(compile(vertex_source, std::strlen(vertex_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs.p, nullptr));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics = {};
    graphics.pRootSignature = root.p;
    graphics.VS = vertex ? pd.CS : D3D12_SHADER_BYTECODE{vs.p->GetBufferPointer(), vs.p->GetBufferSize()};
    if (pixel) graphics.PS = pd.CS;
    if (geometry) graphics.GS = pd.CS;
    graphics.SampleMask = UINT_MAX; graphics.SampleDesc.Count = 1;
    graphics.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    graphics.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Check(device.p->CreateGraphicsPipelineState(&graphics, IID_PPV_ARGS(&pipeline.p)));
  } else Check(device.p->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipeline.p)));
  Owned<ID3D12GraphicsCommandList> list;
  Check(device.p->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.p, pipeline.p, IID_PPV_ARGS(&list.p)));
  Check(list.p->Close());
  Owned<ID3D12CommandSignature> signature;
  Owned<ID3D12Resource> arguments;
  if (indirect) {
    D3D12_INDIRECT_ARGUMENT_DESC updates[3] = {};
    for (UINT i = 0; i < 2; ++i) {
      updates[i].Type = uav_source ? D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW : D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
      if (uav_source) updates[i].UnorderedAccessView.RootParameterIndex = i;
      else updates[i].ShaderResourceView.RootParameterIndex = i;
    }
    updates[2].Type = graphics_stage ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW : D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC sd = {}; sd.ByteStride = graphics_stage ? 32 : 28; sd.NumArgumentDescs = 3; sd.pArgumentDescs = updates;
    Check(device.p->CreateCommandSignature(&sd, root.p, IID_PPV_ARGS(&signature.p)));
    create(arguments, sd.ByteStride, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    Check(arguments.p->Map(0, nullptr, &mapped));
    const UINT64 address = source.p->GetGPUVirtualAddress() + 65532;
    std::memcpy(mapped, &address, 8);
    std::memcpy(static_cast<char *>(mapped) + 8, &address, 8);
    const UINT draw[4] = {3, 1, 0, 0}, dispatch[3] = {1, 1, 1};
    std::memcpy(static_cast<char *>(mapped) + 16, graphics_stage ? draw : dispatch, graphics_stage ? 16 : 12);
    arguments.p->Unmap(0, nullptr);
  }
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event) throw std::runtime_error("event creation");
  const auto source_state = uav_source ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS :
      pixel ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  for (UINT phase = 0; phase < 4; ++phase) {
    D3D12_TILED_RESOURCE_COORDINATE origin = {};
    D3D12_TILE_REGION_SIZE region = {2, FALSE, 0, 0, 0};
    D3D12_TILE_RANGE_FLAGS flags[] = {D3D12_TILE_RANGE_FLAG_NULL, D3D12_TILE_RANGE_FLAG_NULL};
    flags[phase & 1] = D3D12_TILE_RANGE_FLAG_NONE;
    UINT offsets[] = {0, 0}, counts[] = {1, 1};
    queue.p->UpdateTileMappings(source.p, 1, &origin, &region, tiles.p, 2, flags, offsets, counts, D3D12_TILE_MAPPING_FLAG_NONE);
    Check(allocator.p->Reset()); Check(list.p->Reset(allocator.p, pipeline.p));
    Transition(list.p, source.p, phase ? source_state : D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_DEST);
    list.p->CopyBufferRegion(source.p, UINT64(phase & 1) * 65536, upload.p, 0, 65536);
    Transition(list.p, source.p, D3D12_RESOURCE_STATE_COPY_DEST, source_state);
    if (phase) Transition(list.p, output.p, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (graphics_stage) list.p->SetGraphicsRootSignature(root.p);
    else list.p->SetComputeRootSignature(root.p);
    ID3D12DescriptorHeap *heaps[] = {descriptors.p}; list.p->SetDescriptorHeaps(1, heaps);
    if (root_source) {
      for (UINT i = 0; i < 2; ++i) {
        // Deliberately different from the indirect stream's VA: the oracle
        // must fail if the resolver forgets to apply its root updates.
        const auto initial = source.p->GetGPUVirtualAddress() + (indirect ? 65536 : 65532);
        if (graphics_stage) {
          if (uav_source) list.p->SetGraphicsRootUnorderedAccessView(i, initial);
          else list.p->SetGraphicsRootShaderResourceView(i, initial);
        } else if (uav_source) list.p->SetComputeRootUnorderedAccessView(i, initial);
        else list.p->SetComputeRootShaderResourceView(i, initial);
      }
    }
    if (graphics_stage) {
      list.p->SetGraphicsRootDescriptorTable(root_source ? 2 : 0, descriptors.p->GetGPUDescriptorHandleForHeapStart());
      D3D12_VIEWPORT viewport = {0, 0, 1, 1, 0, 1}; D3D12_RECT scissor = {0, 0, 1, 1};
      list.p->RSSetViewports(1, &viewport); list.p->RSSetScissorRects(1, &scissor);
      list.p->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      if (indirect) list.p->ExecuteIndirect(signature.p, 1, arguments.p, 0, nullptr, 0);
      else list.p->DrawInstanced(3, 1, 0, 0);
    } else {
      list.p->SetComputeRootDescriptorTable(root_source ? 2 : 0, descriptors.p->GetGPUDescriptorHandleForHeapStart());
      if (indirect) list.p->ExecuteIndirect(signature.p, 1, arguments.p, 0, nullptr, 0);
      else list.p->Dispatch(1, 1, 1);
    }
    Transition(list.p, output.p, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list.p->CopyBufferRegion(readback.p, 0, output.p, 0, 48); Check(list.p->Close());
    ID3D12CommandList *lists[] = {list.p}; queue.p->ExecuteCommandLists(1, lists);
    Check(queue.p->Signal(fence.p, phase + 1)); Check(fence.p->SetEventOnCompletion(phase + 1, event));
    if (WaitForSingleObject(event, 30000) != WAIT_OBJECT_0) { CloseHandle(event); throw std::runtime_error("GPU timeout"); }
    Check(readback.p->Map(0, nullptr, &mapped));
    auto actual = static_cast<const UINT *>(mapped);
    bool passed = true;
    for (UINT i = 0; i < 12; ++i) {
      UINT expected = 0;
      if (i == 1 || i == 8) expected = (phase & 1) == 0;
      if (i == 3 || i == 10) expected = phase & 1;
      if (i == 11) expected = 0x1234;
      if (actual[i] != expected) { std::printf("phase %u output %u expected %u actual %u\n", phase, i, expected, actual[i]); passed = false; }
    }
    readback.p->Unmap(0, nullptr);
    if (!passed) { CloseHandle(event); return 1; }
  }
  CloseHandle(event);
  std::printf("BUFFER_FEEDBACK %s %s %s raw/structured zero-payload, boundary and alternating remap PASS\n",
              geometry ? "geometry" : vertex ? (indirect ? "indirect-vertex" : "vertex") : pixel ? (indirect ? "indirect-pixel" : "pixel") : indirect ? "indirect" : "direct", root_source ? "root" : "table", uav_source ? "UAV" : "SRV");
  return 0;
}
}
int main(int argc, char **argv) {
  bool uav_source = false, root_source = false, indirect = false, pixel = false, vertex = false, geometry = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--uav")) uav_source = true;
    else if (!std::strcmp(argv[i], "--root")) root_source = true;
    else if (!std::strcmp(argv[i], "--indirect")) { indirect = true; root_source = true; }
    else if (!std::strcmp(argv[i], "--pixel")) { pixel = true; root_source = true; }
    else if (!std::strcmp(argv[i], "--vertex")) { vertex = true; root_source = true; }
    else if (!std::strcmp(argv[i], "--geometry")) { geometry = true; root_source = true; }
    else return 2;
  }
  if (unsigned(pixel) + unsigned(vertex) + unsigned(geometry) > 1 || (geometry && indirect)) return 2;
  auto library = LoadLibraryA("d3dcompiler_47.dll");
  if (!library) return 77;
  auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(library, "D3DCompile"));
  int result = 1;
  try { if (compile) result = Run(compile, uav_source, root_source, indirect, pixel, vertex, geometry); } catch (const std::exception &e) { std::puts(e.what()); }
  FreeLibrary(library); return result;
}

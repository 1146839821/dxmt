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
int Run(pD3DCompile compile, bool uav_source, bool root_source, bool indirect, bool pixel, bool vertex, bool geometry, bool hull, bool domain, bool counted, bool predicated, bool root_updates, bool multi, bool vb_updates, bool vb_only) {
  const UINT command_count = multi ? 3 : 1;
  const UINT output_words = 12 * command_count;
  const bool tessellation = hull || domain;
  const bool graphics_stage = pixel || vertex || geometry || tessellation;
  const bool update_roots = indirect && (!counted || root_updates);
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
#ifdef MULTI_SOURCE
cbuffer Control : register(b0) { uint command_id; };
#endif
#if defined(HULL_SOURCE) || defined(DOMAIN_SOURCE)
struct TessVertex { float4 position : SV_Position; };
struct TessFactors { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
TessFactors patch_constants(InputPatch<TessVertex, 3> patch) {
  TessFactors factors = {{1, 1, 1}, 1}; return factors;
}
#endif
#ifdef HULL_SOURCE
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("patch_constants")]
TessVertex main(InputPatch<TessVertex, 3> patch, uint id : SV_OutputControlPointID) {
  if (id == 0) {
#elif defined(DOMAIN_SOURCE)
[domain("tri")]
TessVertex main(TessFactors factors, const OutputPatch<TessVertex, 3> patch, float3 coord : SV_DomainLocation) {
  if (coord.x == 1 && coord.y == 0 && coord.z == 0) {
#elif defined(GEOMETRY_SOURCE)
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
  uint base = 0;
#ifdef MULTI_SOURCE
  base = command_id * 12;
#endif
#ifdef VB_SOURCE
#ifdef GEOMETRY_SOURCE
  x = asuint(positions[0].position.w) + asuint(positions[1].position.w) + asuint(positions[2].position.w);
#else
  x = asuint(patch[0].position.w) + asuint(patch[1].position.w) + asuint(patch[2].position.w);
#endif
#endif
  output[base + 0]=x; output[base + 1]=CheckAccessFullyMapped(a);
  output[base + 2]=y; output[base + 3]=CheckAccessFullyMapped(b);
  output[base + 4]=z.x; output[base + 5]=z.y; output[base + 6]=CheckAccessFullyMapped(c);
  output[base + 7]=s; output[base + 8]=CheckAccessFullyMapped(d);
  output[base + 9]=t; output[base + 10]=CheckAccessFullyMapped(e);
  output[base + 11]=0x1234u;
#ifdef HULL_SOURCE
  }
  return patch[id];
#elif defined(DOMAIN_SOURCE)
  }
  TessVertex result;
  result.position = patch[0].position * coord.x + patch[1].position * coord.y + patch[2].position * coord.z;
  return result;
#endif
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
  D3D_SHADER_MACRO macros[9] = {}; UINT macro_count = 0;
  if (vb_updates) macros[macro_count++] = {"VB_SOURCE", "1"};
  if (multi) macros[macro_count++] = {"MULTI_SOURCE", "1"};
  if (uav_source) macros[macro_count++] = {"UAV_SOURCE", "1"};
  if (pixel) macros[macro_count++] = {"PIXEL_SOURCE", "1"};
  if (vertex) macros[macro_count++] = {"VERTEX_SOURCE", "1"};
  if (geometry) macros[macro_count++] = {"GEOMETRY_SOURCE", "1"};
  if (hull) macros[macro_count++] = {"HULL_SOURCE", "1"};
  if (domain) macros[macro_count++] = {"DOMAIN_SOURCE", "1"};
  auto hr = compile(hlsl, std::strlen(hlsl), "buffer-feedback", macros, nullptr, "main", hull ? "hs_5_0" : domain ? "ds_5_0" : geometry ? "gs_5_0" : vertex ? "vs_5_0" : pixel ? "ps_5_0" : "cs_5_0",
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
  create(output, output_words * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  create(readback, output_words * 4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
  Owned<ID3D12Resource> indirect_vertices, initial_vertices;
  if (vb_updates) {
    create(indirect_vertices, 72, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    create(initial_vertices, 24, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    Check(indirect_vertices.p->Map(0, nullptr, &mapped));
    auto values = static_cast<UINT *>(mapped);
    for (UINT row = 0; row < 3; ++row)
      for (UINT vertex_id = 0; vertex_id < 3; ++vertex_id) {
        values[row * 6 + vertex_id * 2] = 100 + row;
        values[row * 6 + vertex_id * 2 + 1] = 0xdeadbeef;
      }
    indirect_vertices.p->Unmap(0, nullptr);
    Check(initial_vertices.p->Map(0, nullptr, &mapped));
    const UINT initial[] = {999, 999, 999, 700, 700, 700};
    std::memcpy(mapped, initial, sizeof(initial)); initial_vertices.p->Unmap(0, nullptr);
  }
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
  uav.Buffer.NumElements = output_words; uav.Buffer.StructureByteStride = 4;
  device.p->CreateUnorderedAccessView(output.p, nullptr, &uav, cpu);
  D3D12_DESCRIPTOR_RANGE ranges[] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0},
                                    {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 2}};
  if (uav_source) { ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[0].BaseShaderRegister = 1; }
  D3D12_ROOT_PARAMETER parameter = {}; parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  D3D12_ROOT_SIGNATURE_DESC rd = {}; rd.NumParameters = 1; rd.pParameters = &parameter;
  D3D12_ROOT_PARAMETER root_parameters[4] = {};
  if (root_source) {
    for (UINT i = 0; i < 2; ++i) {
      root_parameters[i].ParameterType = uav_source ? D3D12_ROOT_PARAMETER_TYPE_UAV : D3D12_ROOT_PARAMETER_TYPE_SRV;
      root_parameters[i].Descriptor.ShaderRegister = uav_source ? i + 1 : i;
    }
    root_parameters[2] = parameter;
    root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
    // Table starts at the heap base; output remains at descriptor offset 2.
    rd.NumParameters = multi ? 4 : 3; rd.pParameters = root_parameters;
    if (multi) {
      root_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      root_parameters[3].Constants = {0, 0, 1};
    }
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
    if (vb_updates) vertex_source =
        "float4 main(uint marker : VALUE0, uint kept : VALUE1, uint id : SV_VertexID) : SV_Position { "
        "return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, asfloat(marker + kept)); }";
    Owned<ID3DBlob> vs;
    if (!vertex) Check(compile(vertex_source, std::strlen(vertex_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs.p, nullptr));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics = {};
    D3D12_INPUT_ELEMENT_DESC elements[] = {
        {"VALUE", 0, DXGI_FORMAT_R32_UINT, 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"VALUE", 1, DXGI_FORMAT_R32_UINT, 7, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    if (vb_updates) graphics.InputLayout = {elements, 2};
    graphics.pRootSignature = root.p;
    graphics.VS = vertex ? pd.CS : D3D12_SHADER_BYTECODE{vs.p->GetBufferPointer(), vs.p->GetBufferSize()};
    if (pixel) graphics.PS = pd.CS;
    if (geometry) graphics.GS = pd.CS;
    graphics.SampleMask = UINT_MAX; graphics.SampleDesc.Count = 1;
    graphics.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    graphics.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Owned<ID3DBlob> companion;
    if (tessellation) {
      const char *helper = R"(
struct TessVertex { float4 position : SV_Position; };
struct TessFactors { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
TessFactors patch_constants(InputPatch<TessVertex, 3> patch) {
  TessFactors factors = {{1, 1, 1}, 1}; return factors;
}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("patch_constants")]
TessVertex hs_main(InputPatch<TessVertex, 3> patch, uint id : SV_OutputControlPointID) { return patch[id]; }
[domain("tri")]
TessVertex ds_main(TessFactors factors, const OutputPatch<TessVertex, 3> patch, float3 coord : SV_DomainLocation) {
  TessVertex result;
  result.position = patch[0].position * coord.x + patch[1].position * coord.y + patch[2].position * coord.z;
  return result;
})";
      Check(compile(helper, std::strlen(helper), nullptr, nullptr, nullptr, hull ? "ds_main" : "hs_main",
                    hull ? "ds_5_0" : "hs_5_0", 0, 0, &companion.p, nullptr));
      D3D12_SHADER_BYTECODE other = {companion.p->GetBufferPointer(), companion.p->GetBufferSize()};
      graphics.HS = hull ? pd.CS : other; graphics.DS = domain ? pd.CS : other;
      graphics.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    }
    Check(device.p->CreateGraphicsPipelineState(&graphics, IID_PPV_ARGS(&pipeline.p)));
  } else Check(device.p->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipeline.p)));
  Owned<ID3D12GraphicsCommandList> list;
  Check(device.p->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.p, pipeline.p, IID_PPV_ARGS(&list.p)));
  Check(list.p->Close());
  Owned<ID3D12CommandSignature> signature;
  Owned<ID3D12Resource> arguments;
  Owned<ID3D12Resource> count_buffer;
  if (counted) create(count_buffer, 16, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  if (indirect) {
    D3D12_INDIRECT_ARGUMENT_DESC updates[5] = {};
    for (UINT i = 0; i < 2; ++i) {
      updates[i].Type = uav_source ? D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW : D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
      if (uav_source) updates[i].UnorderedAccessView.RootParameterIndex = i;
      else updates[i].ShaderResourceView.RootParameterIndex = i;
    }
    updates[2].Type = graphics_stage ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW : D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC sd = {}; sd.ByteStride = graphics_stage ? 32 : 28; sd.NumArgumentDescs = 3; sd.pArgumentDescs = updates;
    if (multi) {
      updates[3] = updates[2];
      updates[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
      updates[2].Constant = {3, 0, 1};
      sd.ByteStride = 40; sd.NumArgumentDescs = 4;
      if (vb_updates) {
        updates[4] = updates[3];
        updates[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
        updates[3].VertexBuffer.Slot = 3;
        sd.ByteStride = 56; sd.NumArgumentDescs = 5;
      }
    }
    if (!update_roots) { sd.ByteStride = 16; sd.NumArgumentDescs = 1; sd.pArgumentDescs = &updates[2]; }
    if (vb_only) {
      updates[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
      updates[0].VertexBuffer.Slot = 3;
      updates[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
      sd.ByteStride = 32; sd.NumArgumentDescs = 2; sd.pArgumentDescs = updates;
    }
    Check(device.p->CreateCommandSignature(&sd, update_roots ? root.p : nullptr, IID_PPV_ARGS(&signature.p)));
    create(arguments, sd.ByteStride * command_count, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    Check(arguments.p->Map(0, nullptr, &mapped));
    const UINT64 address = source.p->GetGPUVirtualAddress() + 65532;
    std::memset(mapped, 0, sd.ByteStride * command_count);
    for (UINT row = 0; row < command_count; ++row) {
      auto stream = static_cast<char *>(mapped) + row * sd.ByteStride;
      if (vb_only) {
        const D3D12_VERTEX_BUFFER_VIEW vb = {indirect_vertices.p->GetGPUVirtualAddress(), 24, 8};
        const UINT draw[4] = {3, 1, 0, 0};
        std::memcpy(stream, &vb, sizeof(vb)); std::memcpy(stream + 16, draw, sizeof(draw));
        continue;
      }
      if (update_roots) {
        std::memcpy(stream, &address, 8);
        std::memcpy(stream + 8, &address, 8);
      }
      if (multi) std::memcpy(stream + 16, &row, 4);
      if (vb_updates) {
        const D3D12_VERTEX_BUFFER_VIEW vb = {indirect_vertices.p->GetGPUVirtualAddress() + row * 24, 24, 8};
        std::memcpy(stream + 20, &vb, sizeof(vb));
      }
      const UINT draw[4] = {3, 1, 0, 0}, dispatch[3] = {1, 1, 1};
      std::memcpy(stream + (update_roots ? (multi ? (vb_updates ? 36 : 20) : 16) : 0), graphics_stage ? draw : dispatch, graphics_stage ? 16 : 12);
    }
    arguments.p->Unmap(0, nullptr);
  }
  HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event) throw std::runtime_error("event creation");
  const auto source_state = uav_source ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS :
      pixel ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  for (UINT phase = 0; phase < 4; ++phase) {
    const bool should_execute = !counted || (predicated ? phase == 2 : (phase & 1) != 0);
    if (counted) {
      Check(count_buffer.p->Map(0, nullptr, &mapped));
      const UINT count = multi && !predicated ? (phase == 3 ? UINT_MAX : phase) : predicated ? (phase == 1 ? 0 : phase == 3 ? UINT_MAX : 1) : (phase & 1 ? (phase == 3 ? UINT_MAX : 1) : 0);
      const UINT64 predicate = phase == 0 || phase == 3 ? 1 : 0;
      std::memcpy(mapped, &count, 4); std::memcpy(static_cast<char *>(mapped) + 8, &predicate, 8);
      count_buffer.p->Unmap(0, nullptr);
    }
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
    if (counted) {
      Transition(list.p, output.p, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
      list.p->CopyBufferRegion(output.p, 0, upload.p, 0, output_words * 4);
      Transition(list.p, output.p, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (graphics_stage) list.p->SetGraphicsRootSignature(root.p);
    else list.p->SetComputeRootSignature(root.p);
    ID3D12DescriptorHeap *heaps[] = {descriptors.p}; list.p->SetDescriptorHeaps(1, heaps);
    if (root_source) {
      for (UINT i = 0; i < 2; ++i) {
        // Deliberately different from the indirect stream's VA: the oracle
        // must fail if the resolver forgets to apply its root updates.
        const auto initial = source.p->GetGPUVirtualAddress() + (update_roots ? 65536 : 65532);
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
      list.p->IASetPrimitiveTopology(tessellation ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      if (vb_updates) {
        const D3D12_VERTEX_BUFFER_VIEW wrong = {initial_vertices.p->GetGPUVirtualAddress(), 12, 4};
        const D3D12_VERTEX_BUFFER_VIEW kept = {initial_vertices.p->GetGPUVirtualAddress() + 12, 12, 4};
        list.p->IASetVertexBuffers(3, 1, &wrong); list.p->IASetVertexBuffers(7, 1, &kept);
      }
      if (multi) list.p->SetGraphicsRoot32BitConstant(3, command_count, 0);
      if (predicated) list.p->SetPredication(count_buffer.p, 8, D3D12_PREDICATION_OP_EQUAL_ZERO);
      if (indirect) list.p->ExecuteIndirect(signature.p, command_count, arguments.p, 0, count_buffer.p, 0);
      else list.p->DrawInstanced(3, 1, 0, 0);
      if (predicated) list.p->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    } else {
      list.p->SetComputeRootDescriptorTable(root_source ? 2 : 0, descriptors.p->GetGPUDescriptorHandleForHeapStart());
      if (indirect) list.p->ExecuteIndirect(signature.p, 1, arguments.p, 0, nullptr, 0);
      else list.p->Dispatch(1, 1, 1);
    }
    Transition(list.p, output.p, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list.p->CopyBufferRegion(readback.p, 0, output.p, 0, output_words * 4); Check(list.p->Close());
    ID3D12CommandList *lists[] = {list.p}; queue.p->ExecuteCommandLists(1, lists);
    Check(queue.p->Signal(fence.p, phase + 1)); Check(fence.p->SetEventOnCompletion(phase + 1, event));
    if (WaitForSingleObject(event, 30000) != WAIT_OBJECT_0) { CloseHandle(event); throw std::runtime_error("GPU timeout"); }
    Check(readback.p->Map(0, nullptr, &mapped));
    auto actual = static_cast<const UINT *>(mapped);
    bool passed = true;
    for (UINT word = 0; word < output_words; ++word) {
      const UINT i = word % 12;
      const bool active = multi ? (predicated ? phase == 2 && word / 12 == 0 : word / 12 < (phase == 3 ? command_count : phase)) : should_execute;
      UINT expected = active && vb_updates && i == 0 ? 3 * (800 + word / 12) : 0;
      if (active && (i == 1 || i == 8)) expected = (phase & 1) == 0;
      if (active && (i == 3 || i == 10)) expected = phase & 1;
      if (active && i == 11) expected = 0x1234;
      if (actual[word] != expected) { std::printf("phase %u output %u expected %u actual %u\n", phase, word, expected, actual[word]); passed = false; }
    }
    readback.p->Unmap(0, nullptr);
    if (!passed) { CloseHandle(event); return 1; }
  }
  CloseHandle(event);
  if (vb_updates) std::puts("INDIRECT_VB slots 3/7, updated/seeded records PASS");
  if (multi) std::puts("MULTI_COMMAND separate-output-slots PASS");
  if (counted) std::printf("GPU count/predication gate predicated=%u PASS\n", predicated);
  std::printf("BUFFER_FEEDBACK %s %s %s raw/structured zero-payload, boundary and alternating remap PASS\n",
              hull ? "hull" : domain ? "domain" : geometry ? "geometry" : vertex ? (indirect ? "indirect-vertex" : "vertex") : pixel ? (indirect ? "indirect-pixel" : "pixel") : indirect ? "indirect" : "direct", root_source ? "root" : "table", uav_source ? "UAV" : "SRV");
  return 0;
}
}
int main(int argc, char **argv) {
  bool uav_source = false, root_source = false, indirect = false, pixel = false, vertex = false, geometry = false, hull = false, domain = false;
  bool counted = false, predicated = false;
  bool root_updates = false, multi = false, vb_updates = false, vb_only = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--uav")) uav_source = true;
    else if (!std::strcmp(argv[i], "--root")) root_source = true;
    else if (!std::strcmp(argv[i], "--indirect")) { indirect = true; root_source = true; }
    else if (!std::strcmp(argv[i], "--pixel")) { pixel = true; root_source = true; }
    else if (!std::strcmp(argv[i], "--vertex")) { vertex = true; root_source = true; }
    else if (!std::strcmp(argv[i], "--geometry")) { geometry = true; root_source = true; }
    else if (!std::strcmp(argv[i], "--hull")) { hull = true; root_source = true; }
    else if (!std::strcmp(argv[i], "--domain")) { domain = true; root_source = true; }
    else if (!std::strcmp(argv[i], "--counted")) { counted = true; indirect = true; root_source = true; }
    else if (!std::strcmp(argv[i], "--predicated")) { predicated = counted = indirect = root_source = true; }
    else if (!std::strcmp(argv[i], "--vb-updates")) { vb_updates = multi = root_updates = counted = indirect = root_source = true; }
    else if (!std::strcmp(argv[i], "--vb-only")) { vb_only = vb_updates = counted = indirect = root_source = true; }
    else if (!std::strcmp(argv[i], "--multi")) { multi = root_updates = counted = indirect = root_source = true; }
    else if (!std::strcmp(argv[i], "--root-updates")) { root_updates = indirect = root_source = true; }
    else return 2;
  }
  if (vb_only) { multi = false; root_updates = false; }
  if (unsigned(pixel) + unsigned(vertex) + unsigned(geometry) + unsigned(hull) + unsigned(domain) > 1 ||
      (counted && !(geometry || hull || domain))) return 2;
  auto library = LoadLibraryA("d3dcompiler_47.dll");
  if (!library) return 77;
  auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(library, "D3DCompile"));
  int result = 1;
  try { if (compile) result = Run(compile, uav_source, root_source, indirect, pixel, vertex, geometry, hull, domain, counted, predicated, root_updates, multi, vb_updates, vb_only); } catch (const std::exception &e) { std::puts(e.what()); }
  FreeLibrary(library); return result;
}

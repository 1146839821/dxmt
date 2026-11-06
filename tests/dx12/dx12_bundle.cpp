#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../../src/util/com/com_pointer.hpp"

using dxmt::Com;
namespace {

void Check(bool condition, const char *label) {
  if (!condition)
    throw std::runtime_error(label);
}
void HR(HRESULT actual, const char *label, HRESULT expected = S_OK) {
  if (actual != expected) {
    std::cerr << label << ": actual=0x" << std::hex << unsigned(actual)
              << " expected=0x" << unsigned(expected) << std::dec << '\n';
    throw std::runtime_error(label);
  }
}

constexpr char shader[] = R"hlsl(
cbuffer Parameters : register(b0) { uint value; uint output_index; };
RWStructuredBuffer<uint> output_buffer : register(u0);
float4 vs(uint vertex : SV_VertexID) : SV_Position {
  float2 p = float2(vertex == 2 ? 3 : -1, vertex == 1 ? 3 : -1);
  return float4(p,0,1);
}
float4 vs_input(float2 position : POSITION) : SV_Position { return float4(position,0,1); }
uint ps() : SV_Target { return value; }
uint ps_alt() : SV_Target { return value+100; }
[numthreads(1,1,1)] void cs() { output_buffer[output_index] = value; }
)hlsl";

std::vector<char> Shader(const char *entry, const char *target) {
  char directory[1024]{};
  if (GetEnvironmentVariableA("BUNDLE_SHADER_DIR", directory,
                              sizeof(directory))) {
    const std::string path = std::string(directory) + "/" + entry + ".cso";
    std::ifstream input(path, std::ios::binary);
    Check(bool(input), path.c_str());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
  }
  static auto compiler = LoadLibraryA("d3dcompiler_47.dll");
  Check(compiler != nullptr, "load d3dcompiler_47");
  auto compile =
      reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
  Check(compile != nullptr, "find D3DCompile");
  Com<ID3DBlob> blob, errors;
  const HRESULT result =
      compile(shader, sizeof(shader) - 1, "bundle.hlsl", nullptr, nullptr,
              entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
  if (errors)
    std::cerr.write(static_cast<const char *>(errors->GetBufferPointer()),
                    errors->GetBufferSize());
  HR(result, entry);
  const auto bytes = static_cast<const char *>(blob->GetBufferPointer());
  return {bytes, bytes + blob->GetBufferSize()};
}

struct Recording {
  Com<ID3D12CommandAllocator> allocator;
  Com<ID3D12GraphicsCommandList4> list;
};
Recording Record(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type,
                 ID3D12PipelineState *initial = nullptr) {
  Recording result;
  HR(device->CreateCommandAllocator(type, IID_PPV_ARGS(&result.allocator)),
     "CreateCommandAllocator");
  HR(device->CreateCommandList(0, type, result.allocator.ptr(), initial,
                               IID_PPV_ARGS(&result.list)),
     "CreateCommandList");
  return result;
}
Com<ID3D12Resource>
Buffer(ID3D12Device *device, D3D12_HEAP_TYPE heap, UINT64 size,
       D3D12_RESOURCE_STATES state,
       D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_HEAP_PROPERTIES properties{};
  properties.Type = heap;
  properties.CreationNodeMask = properties.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
  Com<ID3D12Resource> result;
  HR(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc,
                                     state, nullptr, IID_PPV_ARGS(&result)),
     "CreateBuffer");
  return result;
}
void Upload(ID3D12Resource *resource, const void *data, size_t size) {
  void *mapped = nullptr;
  const D3D12_RANGE read{0, 0};
  HR(resource->Map(0, &read, &mapped), "Map upload");
  memcpy(mapped, data, size);
  resource->Unmap(0, nullptr);
}
void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        before, after};
  list->ResourceBarrier(1, &barrier);
}
void Wait(ID3D12Device *device, ID3D12CommandQueue *queue) {
  Com<ID3D12Fence> fence;
  HR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
     "Create completion fence");
  HR(queue->Signal(fence.ptr(), 1), "Queue signal");
  const HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  Check(event != nullptr, "Create completion event");
  const HRESULT hr = fence->SetEventOnCompletion(1, event);
  const DWORD status =
      SUCCEEDED(hr) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
  CloseHandle(event);
  HR(hr, "SetEventOnCompletion");
  Check(status == WAIT_OBJECT_0, "GPU completion timed out");
  HR(device->GetDeviceRemovedReason(), "Device remains healthy");
}
void Submit(ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *list) {
  ID3D12CommandList *lists[]{list};
  queue->ExecuteCommandLists(1, lists);
}

Com<ID3D12RootSignature> Root(ID3D12Device *device) {
  D3D12_ROOT_PARAMETER parameters[2]{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants = {0, 0, 2};
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[1].Descriptor = {0, 0};
  D3D12_ROOT_SIGNATURE_DESC desc{};
  desc.NumParameters = 2;
  desc.pParameters = parameters;
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  Com<ID3DBlob> blob, error;
  HR(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob,
                                 &error),
     "SerializeRootSignature");
  Com<ID3D12RootSignature> root;
  HR(device->CreateRootSignature(0, blob->GetBufferPointer(),
                                 blob->GetBufferSize(), IID_PPV_ARGS(&root)),
     "CreateRootSignature");
  return root;
}
Com<ID3D12PipelineState> Graphics(ID3D12Device *device,
                                  ID3D12RootSignature *root, bool input = false,
                                  bool alternate = false,
                                  DXGI_FORMAT format = DXGI_FORMAT_R32_UINT) {
  auto vs = Shader(input ? "vs_input" : "vs", "vs_5_1");
  auto ps = Shader(alternate ? "ps_alt" : "ps", "ps_5_1");
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
  desc.pRootSignature = root;
  desc.VS = {vs.data(), vs.size()};
  desc.PS = {ps.data(), ps.size()};
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  desc.SampleMask = UINT_MAX;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = format;
  desc.SampleDesc.Count = 1;
  const D3D12_INPUT_ELEMENT_DESC element{
      "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,
      3,          0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
      0};
  if (input)
    desc.InputLayout = {&element, 1};
  Com<ID3D12PipelineState> result;
  HR(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&result)),
     "Create graphics PSO");
  return result;
}

void Contracts(ID3D12Device *device, ID3D12PipelineState *pso,
               ID3D12PipelineState *other_format) {
  auto bundle = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE, pso);
  auto parent = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  parent.list->ExecuteBundle(bundle.list.ptr());
  HR(parent.list->Close(), "open bundle is invalid", E_INVALIDARG);
  HR(bundle.list->Close(), "Close empty bundle");
  parent = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  parent.list->ExecuteBundle(bundle.list.ptr());
  HR(parent.list->Close(), "empty bundle is valid");
  auto wrong = Record(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
  wrong.list->ExecuteBundle(bundle.list.ptr());
  HR(wrong.list->Close(), "compute caller is invalid", E_INVALIDARG);
  auto direct = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  HR(direct.list->Close(), "Close direct callee");
  parent = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  parent.list->ExecuteBundle(direct.list.ptr());
  HR(parent.list->Close(), "direct callee is invalid", E_INVALIDARG);
  parent = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  parent.list->ExecuteBundle(nullptr);
  HR(parent.list->Close(), "null callee is invalid", E_INVALIDARG);
  HR(bundle.allocator->Reset(), "Reset bundle allocator");
  parent = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  parent.list->ExecuteBundle(bundle.list.ptr());
  HR(parent.list->Close(), "reset bundle allocator is invalid", E_INVALIDARG);

  bundle = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE, pso);
  bundle.list->SetPipelineState(other_format);
  HR(bundle.list->Close(), "mixed bundle RTV formats are invalid",
     E_INVALIDARG);

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 1;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Com<ID3D12DescriptorHeap> heap;
  HR(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)),
     "Create bundle heap");
  ID3D12DescriptorHeap *heaps[]{heap.ptr()};
  bundle = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE);
  bundle.list->SetDescriptorHeaps(1, heaps);
  HR(bundle.list->Close(), "Close bundle with heaps");
  parent = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  parent.list->ExecuteBundle(bundle.list.ptr());
  HR(parent.list->Close(), "mismatched bundle heaps are invalid", E_INVALIDARG);
  parent = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  parent.list->SetDescriptorHeaps(1, heaps);
  parent.list->ExecuteBundle(bundle.list.ptr());
  HR(parent.list->Close(), "matching bundle heaps are valid");
  auto indirect_buffer = Buffer(device, D3D12_HEAP_TYPE_UPLOAD, 64,
                                D3D12_RESOURCE_STATE_GENERIC_READ);
  D3D12_INDIRECT_ARGUMENT_DESC indirect_args[2]{};
  indirect_args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
  D3D12_COMMAND_SIGNATURE_DESC signature_desc{sizeof(D3D12_DRAW_ARGUMENTS), 1,
                                              indirect_args, 0};
  Com<ID3D12CommandSignature> signature;
  HR(device->CreateCommandSignature(&signature_desc, nullptr,
                                    IID_PPV_ARGS(&signature)),
     "Create bundle indirect signature");
  bundle = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE);
  bundle.list->ExecuteIndirect(signature.ptr(), 1, indirect_buffer.ptr(), 0,
                               indirect_buffer.ptr(), 0);
  HR(bundle.list->Close(), "bundle indirect count buffer is invalid",
     E_INVALIDARG);
  indirect_args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
  indirect_args[0].VertexBuffer.Slot = 3;
  indirect_args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
  signature_desc.ByteStride =
      sizeof(D3D12_VERTEX_BUFFER_VIEW) + sizeof(D3D12_DRAW_ARGUMENTS);
  signature_desc.NumArgumentDescs = 2;
  signature = nullptr;
  HR(device->CreateCommandSignature(&signature_desc, nullptr,
                                    IID_PPV_ARGS(&signature)),
     "Create state-modifying indirect signature");
  bundle = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE);
  bundle.list->ExecuteIndirect(signature.ptr(), 1, indirect_buffer.ptr(), 0,
                               nullptr, 0);
  HR(bundle.list->Close(), "bundle indirect state changes are invalid",
     E_INVALIDARG);

  bundle = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE);
  bundle.list->AtomicCopyBufferUINT(nullptr, 0, nullptr, 0, 0, nullptr,
                                    nullptr);
  bundle.list->AtomicCopyBufferUINT64(nullptr, 0, nullptr, 0, 0, nullptr,
                                      nullptr);
  bundle.list->ResolveSubresourceRegion(nullptr, 0, 0, 0, nullptr, 0, nullptr,
                                        DXGI_FORMAT_UNKNOWN,
                                        D3D12_RESOLVE_MODE_AVERAGE);
  bundle.list->InitializeMetaCommand(nullptr, nullptr, 0);
  bundle.list->ExecuteMetaCommand(nullptr, nullptr, 0);
  bundle.list->SetProtectedResourceSession(nullptr);
  HR(bundle.list->Close(), "forbidden bundle commands are dropped");
  std::cout << "PASS bundle type/closed/reset/PSO/heap contracts and forbidden "
               "command drops\n";
}

void GraphicsReadback(ID3D12Device *device, ID3D12CommandQueue *queue,
                      ID3D12RootSignature *root, ID3D12PipelineState *pso,
                      ID3D12PipelineState *alternate,
                      ID3D12PipelineState *input, bool bad_oracle) {
  constexpr UINT width = 8;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  Com<ID3D12Resource> target;
  HR(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                     D3D12_RESOURCE_STATE_RENDER_TARGET,
                                     nullptr, IID_PPV_ARGS(&target)),
     "Create target");
  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_desc.NumDescriptors = 1;
  Com<ID3D12DescriptorHeap> rtvs;
  HR(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtvs)),
     "Create RTV heap");
  const auto rtv = rtvs->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(target.ptr(), nullptr, rtv);
  auto readback = Buffer(device, D3D12_HEAP_TYPE_READBACK, 512,
                         D3D12_RESOURCE_STATE_COPY_DEST);
  auto bundle = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE, pso);
  bundle.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  bundle.list->DrawInstanced(3, 1, 0, 0);
  bundle.list->SetGraphicsRoot32BitConstant(0, 42, 0);
  HR(bundle.list->Close(), "Close inherited bundle");
  auto parent = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT, alternate);
  const D3D12_VIEWPORT viewport{0, 0, float(width), 1, 0, 1};
  auto setup = [&](ID3D12GraphicsCommandList *list) {
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list->RSSetViewports(1, &viewport);
    list->SetGraphicsRootSignature(root);
    const UINT data[]{7, 0};
    list->SetGraphicsRoot32BitConstants(0, 2, data, 0);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  };
  auto column = [](ID3D12GraphicsCommandList *list, UINT x) {
    const D3D12_RECT rect{LONG(x), 0, LONG(x + 1), 1};
    list->RSSetScissorRects(1, &rect);
  };
  setup(parent.list.ptr());
  const float clear[4]{};
  parent.list->ClearRenderTargetView(rtv, clear, 0, nullptr);
  column(parent.list.ptr(), 0);
  parent.list->DrawInstanced(3, 1, 0, 0); // alternate PSO => 107
  column(parent.list.ptr(), 1);
  parent.list->ExecuteBundle(bundle.list.ptr()); // initial bundle PSO => 7
  column(parent.list.ptr(), 2);
  parent.list->DrawInstanced(
      3, 1, 0, 0); // bundle PSO, topology and root change persist => 42
  column(parent.list.ptr(), 3);
  parent.list->SetGraphicsRoot32BitConstant(0, 9, 0);
  parent.list->ExecuteBundle(bundle.list.ptr()); // repeat => 9

  const float vertices[3][4]{
      {-1, -1, 99, 99}, {-1, 3, 99, 99}, {3, -1, 99, 99}};
  const UINT16 indices[]{0, 1, 2};
  auto vb = Buffer(device, D3D12_HEAP_TYPE_UPLOAD, sizeof(vertices),
                   D3D12_RESOURCE_STATE_GENERIC_READ);
  auto ib = Buffer(device, D3D12_HEAP_TYPE_UPLOAD, sizeof(indices),
                   D3D12_RESOURCE_STATE_GENERIC_READ);
  Upload(vb.ptr(), vertices, sizeof(vertices));
  Upload(ib.ptr(), indices, sizeof(indices));
  D3D12_VERTEX_BUFFER_VIEW vbv{vb->GetGPUVirtualAddress(), sizeof(vertices),
                               sizeof(vertices[0])};
  D3D12_INDEX_BUFFER_VIEW ibv{ib->GetGPUVirtualAddress(), sizeof(indices),
                              DXGI_FORMAT_R16_UINT};
  auto indexed = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE, pso);
  indexed.list->SetPipelineState(input);
  indexed.list->SetGraphicsRootSignature(root);
  indexed.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  indexed.list->IASetVertexBuffers(3, 1, &vbv);
  indexed.list->IASetIndexBuffer(&ibv);
  UINT constants[]{55, 0};
  indexed.list->SetGraphicsRoot32BitConstants(0, 2, constants, 0);
  vbv = {};
  ibv = {};
  constants[0] = 999;
  indexed.list->DrawIndexedInstanced(3, 1, 0, 0, 0);
  HR(indexed.list->Close(), "Close indexed bundle");
  column(parent.list.ptr(), 4);
  parent.list->ExecuteBundle(indexed.list.ptr());

  const D3D12_DRAW_ARGUMENTS args{3, 1, 0, 0};
  auto arguments = Buffer(device, D3D12_HEAP_TYPE_UPLOAD, sizeof(args),
                          D3D12_RESOURCE_STATE_GENERIC_READ);
  Upload(arguments.ptr(), &args, sizeof(args));
  D3D12_INDIRECT_ARGUMENT_DESC argument{};
  argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
  D3D12_COMMAND_SIGNATURE_DESC signature_desc{sizeof(args), 1, &argument, 0};
  Com<ID3D12CommandSignature> signature;
  HR(device->CreateCommandSignature(&signature_desc, nullptr,
                                    IID_PPV_ARGS(&signature)),
     "Create indirect signature");
  auto indirect = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE, pso);
  indirect.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  indirect.list->SetGraphicsRoot32BitConstant(0, 66, 0);
  indirect.list->ExecuteIndirect(signature.ptr(), 1, arguments.ptr(), 0,
                                 nullptr, 0);
  HR(indirect.list->Close(), "Close indirect bundle");
  column(parent.list.ptr(), 5);
  parent.list->ExecuteBundle(indirect.list.ptr());
  HR(parent.list->Close(), "Close graphics parent");

  auto second = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT, alternate);
  setup(second.list.ptr());
  second.list->SetGraphicsRoot32BitConstant(0, 11, 0);
  column(second.list.ptr(), 6);
  second.list->ExecuteBundle(bundle.list.ptr());
  column(second.list.ptr(), 7);
  second.list->DrawInstanced(
      3, 1, 0, 0); // root change persists in second parent too => 42
  Transition(second.list.ptr(), target.ptr(),
             D3D12_RESOURCE_STATE_RENDER_TARGET,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
  dst.pResource = readback.ptr();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_UINT, width, 1, 1, 256};
  src.pResource = target.ptr();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  second.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  HR(second.list->Close(), "Close second parent");

  // Re-record the same bundle object using another allocator. Already recorded
  // parents depend on the old allocator generation and on their own value
  // copies.
  Com<ID3D12CommandAllocator> replacement;
  HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE,
                                    IID_PPV_ARGS(&replacement)),
     "Replacement allocator");
  HR(bundle.list->Reset(replacement.ptr(), alternate), "Re-record bundle");
  bundle.list->SetGraphicsRoot32BitConstant(0, 999, 0);
  HR(bundle.list->Close(), "Close replacement bundle");
  Com<ID3D12Fence> gate;
  HR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)),
     "Create submission gate");
  HR(queue->Wait(gate.ptr(), 1), "Queue wait gate");
  Submit(queue, parent.list.ptr());
  Submit(queue, second.list.ptr());
  const HRESULT pending_reset = bundle.allocator->Reset();
  HR(gate->Signal(1),
     "Release submission gate"); // release even if reset check fails
  HR(pending_reset, "bundle allocator remains in flight", E_FAIL);
  Wait(device, queue);
  void *mapped = nullptr;
  D3D12_RANGE range{0, width * sizeof(UINT)};
  HR(readback->Map(0, &range, &mapped), "Map graphics readback");
  std::array<UINT, width> expected{107, 7, 42, 9, 55, 66, 11, 42};
  if (bad_oracle)
    ++expected[4];
  bool matches = true;
  for (UINT i = 0; i < width; ++i) {
    const UINT actual = static_cast<UINT *>(mapped)[i];
    if (actual != expected[i]) {
      matches = false;
      std::cerr << "pixel " << i << ": actual=" << actual
                << " expected=" << expected[i] << '\n';
    }
  }
  readback->Unmap(0, nullptr);
  Check(matches, "bundle graphics readback");
  std::cout
      << "PASS bundle graphics: 8 exact pixels; initial "
         "PSO/inheritance/repeat/state/copies/indirect/multiple parents\n";
}

void ComputeReadback(ID3D12Device *device, ID3D12CommandQueue *queue,
                     ID3D12RootSignature *root) {
  auto cs = Shader("cs", "cs_5_1");
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
  desc.pRootSignature = root;
  desc.CS = {cs.data(), cs.size()};
  Com<ID3D12PipelineState> pipeline;
  HR(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)),
     "Create compute PSO");
  auto upload = Buffer(device, D3D12_HEAP_TYPE_UPLOAD, 16,
                       D3D12_RESOURCE_STATE_GENERIC_READ);
  auto output = Buffer(device, D3D12_HEAP_TYPE_DEFAULT, 16,
                       D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto readback = Buffer(device, D3D12_HEAP_TYPE_READBACK, 16,
                         D3D12_RESOURCE_STATE_COPY_DEST);
  const UINT zero[4]{};
  Upload(upload.ptr(), zero, sizeof(zero));
  auto bundle = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE, pipeline.ptr());
  bundle.list->SetComputeRootSignature(root);
  bundle.list->SetComputeRoot32BitConstant(0, 77, 0);
  bundle.list->Dispatch(1, 1, 1);
  HR(bundle.list->Close(), "Close compute bundle");
  auto parent = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  parent.list->CopyBufferRegion(output.ptr(), 0, upload.ptr(), 0, 16);
  Transition(parent.list.ptr(), output.ptr(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  parent.list->SetComputeRootSignature(root);
  const UINT constants[]{1, 0};
  parent.list->SetComputeRoot32BitConstants(0, 2, constants, 0);
  parent.list->SetComputeRootUnorderedAccessView(
      1, output->GetGPUVirtualAddress());
  parent.list->ExecuteBundle(bundle.list.ptr());
  const UINT next[]{78, 1};
  parent.list->SetComputeRoot32BitConstants(0, 2, next, 0);
  parent.list->Dispatch(1, 1, 1);
  Transition(parent.list.ptr(), output.ptr(),
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_DEST);
  auto immediate = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE);
  D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param{output->GetGPUVirtualAddress() + 8,
                                             99};
  immediate.list->WriteBufferImmediate(1, &param, nullptr);
  param = {output->GetGPUVirtualAddress() + 12, 999};
  HR(immediate.list->Close(), "Close WBI bundle");
  parent.list->ExecuteBundle(immediate.list.ptr());
  Transition(parent.list.ptr(), output.ptr(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  parent.list->CopyBufferRegion(readback.ptr(), 0, output.ptr(), 0, 16);
  HR(parent.list->Close(), "Close compute parent");
  Submit(queue, parent.list.ptr());
  Wait(device, queue);
  void *mapped = nullptr;
  HR(readback->Map(0, nullptr, &mapped), "Map compute readback");
  const UINT expected[]{77, 78, 99, 0};
  const bool matches = memcmp(mapped, expected, sizeof(expected)) == 0;
  if (!matches) {
    const auto values = static_cast<const UINT *>(mapped);
    std::cerr << "compute/WBI: " << values[0] << ',' << values[1] << ','
              << values[2] << ',' << values[3] << '\n';
  }
  readback->Unmap(0, nullptr);
  Check(matches, "compute/WBI readback");
  std::cout
      << "PASS bundle compute/WBI: 4 exact words including untouched guard\n";
}

void SubmissionReset(ID3D12Device *device, ID3D12CommandQueue *queue) {
  auto bundle = Record(device, D3D12_COMMAND_LIST_TYPE_BUNDLE);
  HR(bundle.list->Close(), "Close reset-test bundle");
  auto parent = Record(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  parent.list->ExecuteBundle(bundle.list.ptr());
  HR(parent.list->Close(), "Close reset-test parent");
  HR(bundle.allocator->Reset(), "Reset dependency after parent recording");
  Submit(queue, parent.list.ptr());
  Check(FAILED(device->GetDeviceRemovedReason()),
        "submission must reject reset bundle allocator");
  std::cout << "PASS bundle submission rejects stale allocator generation\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    Com<ID3D12Device> device;
    HR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                         IID_PPV_ARGS(&device)),
       "CreateDevice");
    D3D12_COMMAND_QUEUE_DESC desc{};
    Com<ID3D12CommandQueue> queue;
    HR(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "CreateQueue");
    if (argc > 1 && !strcmp(argv[1], "--submit-reset")) {
      SubmissionReset(device.ptr(), queue.ptr());
      return 0;
    }
    auto root = Root(device.ptr());
    auto pso = Graphics(device.ptr(), root.ptr());
    auto alternate = Graphics(device.ptr(), root.ptr(), false, true);
    auto input = Graphics(device.ptr(), root.ptr(), true);
    auto different_format =
        Graphics(device.ptr(), root.ptr(), false, false, DXGI_FORMAT_R16_UINT);
    Contracts(device.ptr(), pso.ptr(), different_format.ptr());
    GraphicsReadback(device.ptr(), queue.ptr(), root.ptr(), pso.ptr(),
                     alternate.ptr(), input.ptr(),
                     argc > 1 && !strcmp(argv[1], "--bad-oracle"));
    ComputeReadback(device.ptr(), queue.ptr(), root.ptr());
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL bundle: " << error.what() << '\n';
    return 1;
  }
}

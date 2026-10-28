#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include "d3d12_device.hpp"
#include <utility>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

bool CheckHR(const char *name, HRESULT hr) {
  if (FAILED(hr)) {
    std::cerr << name << " failed: 0x" << std::hex
              << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
  }
  return true;
}

// Compare every integer pixel, including suppressed primitives and strip gaps.
bool RunGeometryDifferential(const char *vs_path, const char *ps_path,
                             const char *gs_path, const char *kind,
                             const char *index_mode, const char *option) {
  D3D_PRIMITIVE_TOPOLOGY topology;
  UINT arity = 0, step = 0;
  bool strip = false;
  const bool msc_point_output = !strcmp(kind, "msc-point-output") ||
                                !strcmp(kind, "msc-point-output-instanced");
  const bool point_output = !strcmp(kind, "point-output") ||
                            !strcmp(kind, "point-output-instanced") ||
                            msc_point_output;
  const bool geometry_instanced = !strcmp(kind, "triangle-instanced") ||
                                  !strcmp(kind, "triangle-instanced-2") ||
                                  !strcmp(kind, "triangle-instanced-32");
  const bool forced_three_instances =
      option && !strcmp(option, "--gs-instances-3");
  const UINT gs_instances =
      forced_three_instances                   ? 3
      : !strcmp(kind, "triangle-instanced-2")  ? 2
      : !strcmp(kind, "triangle-instanced-32") ? 32
      : !strcmp(kind, "point-output-instanced") ||
              !strcmp(kind, "msc-point-output-instanced") || geometry_instanced
          ? 3
          : 1;
  if (point_output || !strcmp(kind, "point")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    arity = step = 1;
  } else if (!strcmp(kind, "line")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    arity = step = 2;
  } else if (!strcmp(kind, "line-strip")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    arity = 2;
    step = 1;
  } else if (!strcmp(kind, "line-adj")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
    arity = step = 4;
  } else if (!strcmp(kind, "line-strip-adj")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
    arity = 4;
    step = 1;
  } else if (!strcmp(kind, "triangle") || geometry_instanced) {
    topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    arity = step = 3;
  } else if (!strcmp(kind, "triangle-strip")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    arity = 3;
    step = 1;
    strip = true;
  } else if (!strcmp(kind, "triangle-adj")) {
    topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
    arity = step = 6;
  } else
    return false;
  const bool indexed = strcmp(index_mode, "none") != 0;
  const bool index32 = !strcmp(index_mode, "u32");
  if (indexed && !index32 && strcmp(index_mode, "u16"))
    return false;
  const bool performance = option && !strcmp(option, "--performance");
  const bool wrong = option && !strcmp(option, "--wrong-expected");
  const bool wrong_count =
      option && !strcmp(option, "--invocation-count-wrong");
  const bool invocation_count =
      option && (!strcmp(option, "--invocation-count") ||
                 !strcmp(option, "--invocation-count-offset") ||
                 !strcmp(option, "--invocation-count-single") || wrong_count);
  const bool single_draw_instance =
      option && (!strcmp(option, "--draw-one-instance") ||
                 !strcmp(option, "--invocation-count-single"));
  const UINT draw_instances = single_draw_instance ? 1 : 2;
  const UINT draw_start =
      option && (!strcmp(option, "--draw-offset") ||
                 !strcmp(option, "--invocation-count-offset"))
          ? 2
          : 0;
  const bool reject_so = option && !strcmp(option, "--stream-output-rejection");
  const bool reject_topology =
      option && !strcmp(option, "--topology-rejection");
  const bool reject_instancing =
      option && !strcmp(option, "--instancing-rejection");
  if (option && !performance && !wrong && !reject_so && !reject_topology &&
      !reject_instancing && !draw_start && !single_draw_instance &&
      !forced_three_instances && !invocation_count)
    return false;
  auto read = [](const char *path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f || f.tellg() <= 0 || f.tellg() > 16 * 1024 * 1024)
      return std::vector<char>{};
    std::vector<char> data(static_cast<size_t>(f.tellg()));
    f.seekg(0);
    if (!f.read(data.data(), data.size()))
      data.clear();
    return data;
  };
  const auto vs = read(vs_path), ps = read(ps_path), gs = read(gs_path);
  if (vs.empty() || ps.empty() || gs.empty())
    return false;
  struct Objects {
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    ID3D12RootSignature *root = nullptr;
    ID3D12PipelineState *pso = nullptr;
    ID3D12DescriptorHeap *rtvs = nullptr;
    ID3D12Resource *vb = nullptr, *ib = nullptr, *target = nullptr,
                   *readback = nullptr, *counter = nullptr,
                   *counter_zero = nullptr, *counter_readback = nullptr;
    ID3D12Fence *fence = nullptr, *gate = nullptr;
    HANDLE event = nullptr;
    void release_recording() {
      if (list) {
        list->Release();
        list = nullptr;
      }
      if (allocator) {
        allocator->Release();
        allocator = nullptr;
      }
      if (pso) {
        pso->Release();
        pso = nullptr;
      }
      if (root) {
        root->Release();
        root = nullptr;
      }
      if (rtvs) {
        rtvs->Release();
        rtvs = nullptr;
      }
      if (vb) {
        vb->Release();
        vb = nullptr;
      }
      if (ib) {
        ib->Release();
        ib = nullptr;
      }
      if (target) {
        target->Release();
        target = nullptr;
      }
      if (counter) {
        counter->Release();
        counter = nullptr;
      }
      if (counter_zero) {
        counter_zero->Release();
        counter_zero = nullptr;
      }
    }
    ~Objects() {
      if (gate)
        gate->Signal(1);
      release_recording();
      if (readback)
        readback->Release();
      if (counter_readback)
        counter_readback->Release();
      if (queue)
        queue->Release();
      if (fence)
        fence->Release();
      if (gate)
        gate->Release();
      if (event)
        CloseHandle(event);
      if (device)
        device->Release();
    }
  } o;
  if (!CheckHR("GS device", D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                              IID_PPV_ARGS(&o.device))))
    return false;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
  parameters[0].Constants = {0, 0, 1};
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
  parameters[1].Descriptor = {0, 0};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {
      invocation_count ? 2u : 1u, parameters, 0, nullptr,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  if (reject_so)
    root_desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
  ID3DBlob *blob = nullptr;
  if (!CheckHR("GS serialize root",
               D3D12SerializeRootSignature(
                   &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr)))
    return false;
  HRESULT hr = o.device->CreateRootSignature(0, blob->GetBufferPointer(),
                                             blob->GetBufferSize(),
                                             IID_PPV_ARGS(&o.root));
  blob->Release();
  if (!CheckHR("GS root", hr))
    return false;
  const D3D12_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = o.root;
  desc.VS = {vs.data(), vs.size()};
  desc.PS = {ps.data(), ps.size()};
  desc.GS = {gs.data(), gs.size()};
  desc.InputLayout = {layout, 2};
  desc.PrimitiveTopologyType = arity == 1 ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT
                               : (arity == 2 || arity == 4)
                                   ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
                                   : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  if (reject_topology)
    desc.PrimitiveTopologyType = arity == 1
                                     ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE
                                     : D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UINT;
  desc.SampleDesc.Count = 1;
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  const D3D12_SO_DECLARATION_ENTRY so = {0, "COLOR", 0, 0, 4, 0};
  const UINT so_stride = 16;
  if (reject_so)
    desc.StreamOutput = {&so, 1, &so_stride, 1, D3D12_SO_NO_RASTERIZED_STREAM};
  const auto pso_start = std::chrono::steady_clock::now();
  hr = o.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&o.pso));
  if (point_output)
    std::cout << "PointStream PSO creation us="
              << std::chrono::duration<double, std::micro>(
                     std::chrono::steady_clock::now() - pso_start)
                     .count()
              << "\n";
  if (reject_so || reject_topology || reject_instancing) {
    bool correct =
        hr == (reject_so || reject_instancing ? E_NOTIMPL : E_INVALIDARG) &&
        !o.pso && o.device->GetDeviceRemovedReason() == S_OK;
    std::cout << "GS rejection kind=" << kind << " option=" << option
              << " hr=0x" << std::hex << UINT(hr) << std::dec
              << " correct=" << correct << " GPU_NOT_RUN\n";
    return correct;
  }
  if (!CheckHR("GS differential PSO", hr))
    return false;
  auto *pipeline = static_cast<dxmt::MTLD3D12GraphicsPipelineState *>(o.pso);
  const auto config = pipeline->msc_geometry_config;
  if (point_output && !msc_point_output) {
    if (pipeline->shader_backend != dxmt::D3D12ShaderBackend::Airconv ||
        !pipeline->airconv_geometry)
      return false;
    std::cout << "GS AIRCONV point output instances=" << gs_instances << "\n";
  } else {
    if (pipeline->shader_backend !=
            dxmt::D3D12ShaderBackend::MetalShaderConverter ||
        !pipeline->msc_geometry || !config.gs_vertex_size_in_bytes ||
        !config.gs_max_input_primitives_per_mesh_threadgroup ||
        config.gs_instance_count != gs_instances)
      return false;
    std::cout << "GS MSC reflection vertex_bytes="
              << config.gs_vertex_size_in_bytes << " max_primitives="
              << config.gs_max_input_primitives_per_mesh_threadgroup
              << " instances=" << config.gs_instance_count << "\n";
  }
  const UINT count = arity + step * 65;
  constexpr UINT vertex_offset = 32, index_offset = 16;
  const UINT height = point_output ? 3 : 3 * gs_instances;
  const UINT width = point_output ? 512 : 256;
  struct Vertex {
    float position[2];
    float color[4];
  };
  static_assert(sizeof(Vertex) == 24);
  std::vector<Vertex> vertices(512);
  for (UINT i = 0; i < vertices.size(); ++i)
    vertices[i] = {{0, 0}, {float((i * i * 13 + 7 * i + 11) % 251), 0, 0, 1}};
  std::vector<UINT> ids(count);
  for (UINT i = 0; i < count; ++i)
    ids[i] = indexed ? (i * 7 + 3) % 397 + draw_start : i + draw_start;
  auto buffer = [&](D3D12_HEAP_TYPE heap, UINT size,
                    D3D12_RESOURCE_STATES state, ID3D12Resource **out) {
    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = heap;
    properties.CreationNodeMask = properties.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (heap == D3D12_HEAP_TYPE_DEFAULT)
      d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return CheckHR("GS buffer", o.device->CreateCommittedResource(
                                    &properties, D3D12_HEAP_FLAG_NONE, &d,
                                    state, nullptr, IID_PPV_ARGS(out)));
  };
  if (!buffer(D3D12_HEAP_TYPE_UPLOAD,
              vertex_offset + vertices.size() * sizeof(Vertex),
              D3D12_RESOURCE_STATE_GENERIC_READ, &o.vb) ||
      !buffer(D3D12_HEAP_TYPE_UPLOAD, index_offset + (count + 2) * 4,
              D3D12_RESOURCE_STATE_GENERIC_READ, &o.ib) ||
      !buffer(D3D12_HEAP_TYPE_READBACK, width * height * 4,
              D3D12_RESOURCE_STATE_COPY_DEST, &o.readback))
    return false;
  void *mapped = nullptr;
  if (invocation_count) {
    if (!buffer(D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_STATE_COPY_DEST,
                &o.counter) ||
        !buffer(D3D12_HEAP_TYPE_UPLOAD, 256, D3D12_RESOURCE_STATE_GENERIC_READ,
                &o.counter_zero) ||
        !buffer(D3D12_HEAP_TYPE_READBACK, 256, D3D12_RESOURCE_STATE_COPY_DEST,
                &o.counter_readback) ||
        !CheckHR("GS counter upload map",
                 o.counter_zero->Map(0, nullptr, &mapped)))
      return false;
    std::memset(mapped, 0, 256);
    o.counter_zero->Unmap(0, nullptr);
  }
  if (!CheckHR("GS VB map", o.vb->Map(0, nullptr, &mapped)))
    return false;
  memset(mapped, 0xcd, vertex_offset);
  memcpy(static_cast<BYTE *>(mapped) + vertex_offset, vertices.data(),
         vertices.size() * sizeof(Vertex));
  o.vb->Unmap(0, nullptr);
  if (!CheckHR("GS IB map", o.ib->Map(0, nullptr, &mapped)))
    return false;
  memset(mapped, 0xcd, index_offset + (count + 2) * 4);
  for (UINT i = 0; i < count; ++i) {
    if (index32)
      reinterpret_cast<UINT *>(static_cast<BYTE *>(mapped) +
                               index_offset)[i + draw_start] =
          ids[i] - draw_start;
    else
      reinterpret_cast<uint16_t *>(static_cast<BYTE *>(mapped) +
                                   index_offset)[i + draw_start] =
          static_cast<uint16_t>(ids[i] - draw_start);
  }
  o.ib->Unmap(0, nullptr);
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC texture = {};
  texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture.Width = width;
  texture.Height = height;
  texture.DepthOrArraySize = texture.MipLevels = texture.SampleDesc.Count = 1;
  texture.Format = DXGI_FORMAT_R8G8B8A8_UINT;
  texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_desc.NumDescriptors = 1;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  if (!CheckHR("GS target", o.device->CreateCommittedResource(
                                &heap, D3D12_HEAP_FLAG_NONE, &texture,
                                D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                IID_PPV_ARGS(&o.target))) ||
      !CheckHR("GS RTV heap", o.device->CreateDescriptorHeap(
                                  &rtv_desc, IID_PPV_ARGS(&o.rtvs))) ||
      !CheckHR("GS queue", o.device->CreateCommandQueue(
                               &queue_desc, IID_PPV_ARGS(&o.queue))) ||
      !CheckHR("GS allocator",
               o.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&o.allocator))) ||
      !CheckHR("GS list", o.device->CreateCommandList(
                              0, D3D12_COMMAND_LIST_TYPE_DIRECT, o.allocator,
                              o.pso, IID_PPV_ARGS(&o.list))) ||
      !CheckHR("GS fence", o.device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                 IID_PPV_ARGS(&o.fence))) ||
      !CheckHR("GS gate", o.device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                IID_PPV_ARGS(&o.gate))))
    return false;
  o.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!o.event)
    return false;
  const auto rtv = o.rtvs->GetCPUDescriptorHandleForHeapStart();
  o.device->CreateRenderTargetView(o.target, nullptr, rtv);
  const D3D12_VERTEX_BUFFER_VIEW vbv = {
      o.vb->GetGPUVirtualAddress() + vertex_offset,
      UINT(vertices.size() * sizeof(Vertex)), sizeof(Vertex)};
  const D3D12_INDEX_BUFFER_VIEW ibv = {
      o.ib->GetGPUVirtualAddress() + index_offset,
      (count + 2) * (index32 ? 4u : 2u),
      index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT};
  // Draw offsets affect fetching, not the system VertexID. Triangle strips
  // keep the leading vertex first and exchange the remaining two on odd
  // primitives.
  const UINT primitives = (count - arity) / step + 1;
  for (UINT pass = 0; pass < 3; ++pass) {
    const UINT phase = pass == 2 ? 31 : 7;
    if (pass != 1) {
      if (pass &&
          (!CheckHR("GS allocator reset", o.allocator->Reset()) ||
           !CheckHR("GS list reset", o.list->Reset(o.allocator, o.pso))))
        return false;
      if (invocation_count) {
        o.list->CopyBufferRegion(o.counter, 0, o.counter_zero, 0, 4);
        D3D12_RESOURCE_BARRIER counter_barrier = {};
        counter_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        counter_barrier.Transition = {o.counter,
                                      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                      D3D12_RESOURCE_STATE_COPY_DEST,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
        o.list->ResourceBarrier(1, &counter_barrier);
      }
      o.list->SetGraphicsRootSignature(o.root);
      o.list->SetGraphicsRoot32BitConstant(0, phase, 0);
      if (invocation_count)
        o.list->SetGraphicsRootUnorderedAccessView(
            1, o.counter->GetGPUVirtualAddress());
      o.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
      const float clear[4] = {};
      o.list->ClearRenderTargetView(rtv, clear, 0, nullptr);
      const D3D12_VIEWPORT viewport = {0, 0, float(width), float(height), 0, 1};
      const D3D12_RECT scissor = {0, 0, LONG(width), LONG(height)};
      o.list->RSSetViewports(1, &viewport);
      o.list->RSSetScissorRects(1, &scissor);
      o.list->IASetPrimitiveTopology(topology);
      o.list->IASetVertexBuffers(0, 1, &vbv);
      if (indexed)
        o.list->IASetIndexBuffer(&ibv);
      const UINT repetitions = performance ? 128 : 1;
      const auto record_start = std::chrono::steady_clock::now();
      for (UINT i = 0; i < repetitions; ++i) {
        if (indexed)
          o.list->DrawIndexedInstanced(count, draw_instances, draw_start,
                                       draw_start, 0);
        else
          o.list->DrawInstanced(count, draw_instances, draw_start, 0);
      }
      if (performance)
        std::cout << "GS performance record_us_per_draw="
                  << std::chrono::duration<double, std::micro>(
                         std::chrono::steady_clock::now() - record_start)
                             .count() /
                         repetitions
                  << "\n";
      o.list->SetGraphicsRoot32BitConstant(0, 222, 0);
      if (invocation_count) {
        D3D12_RESOURCE_BARRIER counter_barrier = {};
        counter_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        counter_barrier.Transition = {o.counter,
                                      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_COPY_SOURCE};
        o.list->ResourceBarrier(1, &counter_barrier);
        o.list->CopyBufferRegion(o.counter_readback, 0, o.counter, 0, 4);
        std::swap(counter_barrier.Transition.StateBefore,
                  counter_barrier.Transition.StateAfter);
        o.list->ResourceBarrier(1, &counter_barrier);
      }
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition = {o.target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                            D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_COPY_SOURCE};
      o.list->ResourceBarrier(1, &barrier);
      D3D12_TEXTURE_COPY_LOCATION source = {}, destination = {};
      source.pResource = o.target;
      source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      destination.pResource = o.readback;
      destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UINT, width,
                                               height, 1, width * 4};
      o.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
      std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      o.list->ResourceBarrier(1, &barrier);
      if (!CheckHR("GS Close", o.list->Close()))
        return false;
    }
    if (pass == 2 && !CheckHR("GS queue gate", o.queue->Wait(o.gate, 1)))
      return false;
    ID3D12CommandList *lists[] = {o.list};
    const auto submit_start = std::chrono::steady_clock::now();
    o.queue->ExecuteCommandLists(1, lists);
    if (pass == 2) {
      if (SUCCEEDED(o.allocator->Reset())) {
        std::cerr << "GS pending Reset incorrectly succeeded\n";
        return false;
      }
      o.release_recording();
      if (!CheckHR("GS signal gate", o.gate->Signal(1)))
        return false;
    }
    if (!CheckHR("GS signal", o.queue->Signal(o.fence, pass + 1)) ||
        !CheckHR("GS completion",
                 o.fence->SetEventOnCompletion(pass + 1, o.event)) ||
        WaitForSingleObject(o.event, 10000) != WAIT_OBJECT_0 ||
        !CheckHR("GS device status", o.device->GetDeviceRemovedReason()))
      return false;
    if (performance)
      std::cout << "GS performance submit_fence_us_per_draw="
                << std::chrono::duration<double, std::micro>(
                       std::chrono::steady_clock::now() - submit_start)
                           .count() /
                       128
                << "\n";
    if (invocation_count) {
      void *counter_data = nullptr;
      if (!CheckHR("GS counter readback",
                   o.counter_readback->Map(0, nullptr, &counter_data)))
        return false;
      const UINT observed = *static_cast<const UINT *>(counter_data);
      o.counter_readback->Unmap(0, nullptr);
      const UINT expected_count =
          primitives * draw_instances * gs_instances + (wrong_count ? 1 : 0);
      std::cout << "GS invocation count observed=" << observed
                << " expected=" << expected_count << "\n";
      if (observed != expected_count)
        return false;
    }
    std::vector<BYTE> expected(width * height * 4, 0);
    if (point_output) {
      for (UINT instance = 0; instance < draw_instances; ++instance)
        for (UINT primitive = 0; primitive < primitives; ++primitive)
          for (UINT gs_instance = 0; gs_instance < gs_instances; ++gs_instance)
            for (UINT emit = 0; emit < primitive % 3; ++emit) {
              const UINT id = ids[primitive];
              const UINT tag =
                  UINT(vertices[id].color[0]) + 3 * (id - draw_start);
              auto pixel =
                  expected.data() + 4 * (gs_instance * width + instance * 256 +
                                         primitive * 2 + emit);
              pixel[0] = BYTE(primitive + 1);
              pixel[1] = BYTE(tag + phase + 17 * emit);
              pixel[2] = BYTE(instance + 1);
              pixel[3] = BYTE(gs_instance + 1);
            }
    } else {
      for (UINT instance = 0; instance < draw_instances; ++instance)
        for (UINT primitive = 0; primitive < primitives; ++primitive) {
          if (primitive == 1)
            continue;
          UINT hash = 0;
          for (UINT j = 0; j < arity; ++j) {
            const UINT local = strip && (primitive & 1) && j > 0 ? 3 - j : j;
            const UINT id = ids[primitive * step + local];
            hash +=
                (j + 1) * (UINT(vertices[id].color[0]) + 3 * (id - draw_start));
          }
          for (UINT gs_instance = 0; gs_instance < gs_instances; ++gs_instance)
            for (UINT local_y : {0u, 2u}) {
              const UINT y = local_y + gs_instance * 3;
              BYTE *pixel =
                  expected.data() + 4 * (y * width + primitive + instance * 128);
              pixel[0] = BYTE(primitive + 1);
              pixel[1] = BYTE(hash + phase);
              pixel[2] = BYTE(instance + 1);
              pixel[3] = BYTE(y + arity);
            }
        }
    }
    if (wrong)
      expected[0] ^= 1;
    if (!CheckHR("GS readback", o.readback->Map(0, nullptr, &mapped)))
      return false;
    bool correct = true;
    for (UINT i = 0; i < expected.size(); ++i)
      if (static_cast<BYTE *>(mapped)[i] != expected[i]) {
        std::cerr << "FAIL GS kind=" << kind << " index=" << index_mode
                  << " pass=" << pass << " byte=" << i
                  << " got=" << UINT(static_cast<BYTE *>(mapped)[i])
                  << " expected=" << UINT(expected[i]) << "\n";
        correct = false;
        break;
      }
    o.readback->Unmap(0, nullptr);
    if (!correct)
      return false;
    std::cout << "GS differential kind=" << kind << " index=" << index_mode
              << " pass=" << pass << " primitives=" << primitives
              << " draw_instances=" << draw_instances << " exact_bytes=" << expected.size()
              << " passed\n";
  }
  std::cout << "GS replay/fenced reset/pending reset rejection/released refs "
               "passed\n";
  return true;
}

bool RunVertexStride(const char *vs_path, const char *ps_path,
                     const char *mode) {
  using dxmt::Com;
  const bool instanced =
      !strcmp(mode, "instance") || !strcmp(mode, "instance-zero");
  const bool instance_zero = !strcmp(mode, "instance-zero");
  const bool indexed = !strcmp(mode, "indexed");
  const bool indirect = !strcmp(mode, "indirect");
  const bool performance =
      !strcmp(mode, "perf-packed") || !strcmp(mode, "perf-vertex");
  const bool packed = !strcmp(mode, "packed") || !strcmp(mode, "perf-packed");
  const bool wrong = !strcmp(mode, "wrong");
  const bool lifetime = !strcmp(mode, "lifetime");
  if (!instanced && !packed && !wrong && !indexed && !indirect &&
      !performance && !lifetime && strcmp(mode, "vertex"))
    return false;
  auto read = [](const char *path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f || f.tellg() <= 0 || f.tellg() > 16 * 1024 * 1024)
      return std::vector<char>{};
    std::vector<char> data(static_cast<size_t>(f.tellg()));
    f.seekg(0);
    if (!f.read(data.data(), data.size()))
      data.clear();
    return data;
  };
  auto vs = read(vs_path), ps = read(ps_path);
  if (vs.empty() || ps.empty())
    return false;
  Com<ID3D12Device> device;
  Com<ID3D12RootSignature> root;
  Com<ID3D12PipelineState> pso, second_pso;
  Com<ID3D12CommandQueue> queue;
  Com<ID3D12CommandAllocator> allocator;
  Com<ID3D12GraphicsCommandList> list;
  Com<ID3D12DescriptorHeap> rtvs;
  Com<ID3D12Resource> vb, ib, arguments, target, readback;
  Com<ID3D12CommandSignature> signature;
  Com<ID3D12Fence> fence, gate;
  Com<ID3DBlob> blob;
  struct Event {
    HANDLE value = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    ~Event() {
      if (value)
        CloseHandle(value);
    }
  } event;
  if (!event.value ||
      !CheckHR("Stride device",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device))))
    return false;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants = {0, 0, 1};
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {
      1, &parameter, 0, nullptr,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  if (!CheckHR("Stride serialize",
               D3D12SerializeRootSignature(
                   &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr)) ||
      !CheckHR("Stride root", device->CreateRootSignature(
                                  0, blob->GetBufferPointer(),
                                  blob->GetBufferSize(), IID_PPV_ARGS(&root))))
    return false;
  const D3D12_INPUT_ELEMENT_DESC layout = {
      "POSITION",
      0,
      DXGI_FORMAT_R32G32B32_FLOAT,
      3,
      0,
      instanced ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
      instanced && !instance_zero ? 2u : 0u};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root.ptr();
  desc.VS = {vs.data(), vs.size()};
  desc.PS = {ps.data(), ps.size()};
  desc.InputLayout = {&layout, 1};
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UINT;
  desc.SampleDesc.Count = 1;
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  auto create_pipelines = [&] {
    return CheckHR("Stride PSO", device->CreateGraphicsPipelineState(
                                     &desc, IID_PPV_ARGS(&pso))) &&
           (!lifetime || CheckHR("Stride second PSO",
                                 device->CreateGraphicsPipelineState(
                                     &desc, IID_PPV_ARGS(&second_pso))));
  };
  if (!create_pipelines())
    return false;
  if (static_cast<dxmt::MTLD3D12PipelineState *>(pso.ptr())->shader_backend !=
      dxmt::D3D12ShaderBackend::MetalShaderConverter)
    return false;
  const UINT width = 256, height = 4, count = 32, instances = 4;
  UINT strides[] = {packed ? 12u : 16u, packed ? 12u : 32u, packed ? 12u : 16u,
                    packed ? 12u : 0u};
  auto make_buffer = [&](D3D12_HEAP_TYPE type, UINT size,
                         D3D12_RESOURCE_STATES state, ID3D12Resource **out) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return CheckHR("Stride buffer", device->CreateCommittedResource(
                                        &heap, D3D12_HEAP_FLAG_NONE, &d, state,
                                        nullptr, IID_PPV_ARGS(out)));
  };
  if (!make_buffer(D3D12_HEAP_TYPE_UPLOAD, 8192,
                   D3D12_RESOURCE_STATE_GENERIC_READ, &vb) ||
      !make_buffer(D3D12_HEAP_TYPE_READBACK, width * height * 4,
                   D3D12_RESOURCE_STATE_COPY_DEST, &readback))
    return false;
  void *mapped = nullptr;
  if (!CheckHR("Stride map", vb->Map(0, nullptr, &mapped)))
    return false;
  memset(mapped, 0xcd, 8192);
  for (UINT row = 0; row < height; ++row)
    for (UINT i = 0; i < count; ++i) {
      const float value[] = {float(i + row * 40 + 1), float(i * 3 + row + 2),
                             float(i * 5 + row + 3)};
      memcpy(static_cast<BYTE *>(mapped) + 32 + row * 2048 +
                 i * (strides[row] ? strides[row] : 12),
             value, 12);
    }
  vb->Unmap(0, nullptr);
  if (indexed) {
    if (!make_buffer(D3D12_HEAP_TYPE_UPLOAD, 128,
                     D3D12_RESOURCE_STATE_GENERIC_READ, &ib) ||
        !CheckHR("Stride IB map", ib->Map(0, nullptr, &mapped)))
      return false;
    auto indices = static_cast<UINT16 *>(mapped);
    for (UINT i = 0; i < count; ++i)
      indices[i] = UINT16(count - 1 - i);
    ib->Unmap(0, nullptr);
  }
  if (indirect) {
    if (!make_buffer(D3D12_HEAP_TYPE_UPLOAD, sizeof(D3D12_DRAW_ARGUMENTS),
                     D3D12_RESOURCE_STATE_GENERIC_READ, &arguments) ||
        !CheckHR("Stride args map", arguments->Map(0, nullptr, &mapped)))
      return false;
    const D3D12_DRAW_ARGUMENTS args = {count, instances, 0, 0};
    memcpy(mapped, &args, sizeof(args));
    arguments->Unmap(0, nullptr);
    D3D12_INDIRECT_ARGUMENT_DESC argument = {};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC sig = {sizeof(args), 1, &argument, 0};
    if (!CheckHR("Stride signature",
                 device->CreateCommandSignature(&sig, nullptr,
                                                IID_PPV_ARGS(&signature))))
      return false;
  }
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC tex = {};
  tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  tex.Width = width;
  tex.Height = height;
  tex.DepthOrArraySize = tex.MipLevels = tex.SampleDesc.Count = 1;
  tex.Format = DXGI_FORMAT_R8G8B8A8_UINT;
  tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_desc.NumDescriptors = 1;
  D3D12_COMMAND_QUEUE_DESC q = {};
  if (!CheckHR("Stride target", device->CreateCommittedResource(
                                    &heap, D3D12_HEAP_FLAG_NONE, &tex,
                                    D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                    IID_PPV_ARGS(&target))) ||
      !CheckHR("Stride rtv",
               device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtvs))) ||
      !CheckHR("Stride queue",
               device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))) ||
      !CheckHR("Stride allocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator))) ||
      !CheckHR("Stride list",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator.ptr(), pso.ptr(),
                                         IID_PPV_ARGS(&list))) ||
      !CheckHR("Stride fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                   IID_PPV_ARGS(&fence))) ||
      !CheckHR("Stride gate", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                  IID_PPV_ARGS(&gate))))
    return false;
  struct Unblock {
    ID3D12Fence *gate;
    ~Unblock() { gate->Signal(1); }
  } unblock{gate.ptr()};
  auto rtv = rtvs->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(target.ptr(), nullptr, rtv);
  for (UINT pass = 0; pass < 3; ++pass) {
    if (pass != 1) {
      if (pass && lifetime && !create_pipelines())
        return false;
      if (pass && (!CheckHR("Stride allocator reset", allocator->Reset()) ||
                   !CheckHR("Stride list reset",
                            list->Reset(allocator.ptr(), pso.ptr()))))
        return false;
      list->SetGraphicsRootSignature(root.ptr());
      list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
      const float clear[4] = {};
      list->ClearRenderTargetView(rtv, clear, 0, nullptr);
      const D3D12_VIEWPORT viewport = {0, 0, float(width), float(height), 0, 1};
      const D3D12_RECT scissor = {0, 0, width, height};
      list->RSSetViewports(1, &viewport);
      list->RSSetScissorRects(1, &scissor);
      list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
      using Clock = std::chrono::steady_clock;
      const auto record_start = Clock::now();
      auto warm_start = record_start;
      const UINT draws = performance ? 4096u : height;
      for (UINT draw = 0; draw < draws; ++draw) {
        if (draw == height)
          warm_start = Clock::now();
        const UINT row = draw % height;
        if (lifetime)
          list->SetPipelineState(row < 2 ? pso.ptr() : second_pso.ptr());
        D3D12_VERTEX_BUFFER_VIEW view = {
            vb->GetGPUVirtualAddress() + 32 + row * 2048, 1024, strides[row]};
        list->IASetVertexBuffers(3, 1, &view);
        list->SetGraphicsRoot32BitConstant(0, row, 0);
        if (indexed) {
          const D3D12_INDEX_BUFFER_VIEW index_view = {
              ib->GetGPUVirtualAddress(), 128, DXGI_FORMAT_R16_UINT};
          list->IASetIndexBuffer(&index_view);
          list->DrawIndexedInstanced(count, instances, 0, 0, 0);
        } else if (indirect) {
          list->ExecuteIndirect(signature.ptr(), 1, arguments.ptr(), 0, nullptr,
                                0);
        } else {
          list->DrawInstanced(count, instances, 0, 0);
        }
      }
      const auto record_end = Clock::now();
      if (performance)
        std::cout << "Stride perf pass=" << pass << " first4_us="
                  << std::chrono::duration<double, std::micro>(warm_start -
                                                               record_start)
                         .count()
                  << " warm_us_per_draw="
                  << std::chrono::duration<double, std::micro>(record_end -
                                                               warm_start)
                             .count() /
                         (draws - height)
                  << "\n";
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition = {
          target.ptr(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
          D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
      list->ResourceBarrier(1, &barrier);
      D3D12_TEXTURE_COPY_LOCATION source = {}, destination = {};
      source.pResource = target.ptr();
      source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      destination.pResource = readback.ptr();
      destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UINT, width,
                                               height, 1, width * 4};
      list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
      std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      list->ResourceBarrier(1, &barrier);
      if (!CheckHR("Stride Close", list->Close()))
        return false;
      // Only the recording owns the first PSO after the switch to the second.
      // Release both public refs before execution and replay of the closed
      // list.
      if (lifetime) {
        pso = nullptr;
        second_pso = nullptr;
      }
    }
    if (pass == 2 && !CheckHR("Stride gate wait", queue->Wait(gate.ptr(), 1)))
      return false;
    using Clock = std::chrono::steady_clock;
    const auto submit_start = Clock::now();
    ID3D12CommandList *lists[] = {list.ptr()};
    queue->ExecuteCommandLists(1, lists);
    const auto submit_end = Clock::now();
    if (pass == 2) {
      if (SUCCEEDED(allocator->Reset())) {
        std::cerr << "Stride pending reset admitted\n";
        return false;
      }
      list = nullptr;
      allocator = nullptr;
      pso = nullptr;
      second_pso = nullptr;
      root = nullptr;
      rtvs = nullptr;
      if (!CheckHR("Stride gate signal", gate->Signal(1)))
        return false;
    }
    if (!CheckHR("Stride signal", queue->Signal(fence.ptr(), pass + 1)) ||
        !CheckHR("Stride wait",
                 fence->SetEventOnCompletion(pass + 1, event.value)) ||
        WaitForSingleObject(event.value, 10000) != WAIT_OBJECT_0 ||
        !CheckHR("Stride device status", device->GetDeviceRemovedReason()))
      return false;
    if (performance)
      std::cout << "Stride submit pass=" << pass << " cpu_us="
                << std::chrono::duration<double, std::micro>(submit_end -
                                                             submit_start)
                       .count()
                << " wait_ms="
                << std::chrono::duration<double, std::milli>(Clock::now() -
                                                             submit_end)
                       .count()
                << "\n";
    if (!CheckHR("Stride readback", readback->Map(0, nullptr, &mapped)))
      return false;
    bool correct = true;
    for (UINT row = 0; row < height; ++row)
      for (UINT x = 0; x < width; ++x) {
        const UINT instance = x / 64, vertex = x % 64;
        BYTE expected[4] = {};
        if (instance < instances && vertex < count) {
          const UINT i =
              strides[row]
                  ? (instanced ? (instance_zero ? 0 : instance / 2) : vertex)
                  : 0;
          expected[0] = BYTE(i + row * 40 + 1);
          expected[1] = BYTE(i * 3 + row + 2);
          expected[2] = BYTE(i * 5 + row + 3);
          expected[3] = 255;
        }
        if (wrong && !row && !x)
          expected[0] ^= 1;
        const auto got = static_cast<BYTE *>(mapped) + 4 * (row * width + x);
        if (memcmp(got, expected, 4) && correct) {
          std::cerr << "FAIL stride mode=" << mode << " pass=" << pass
                    << " row=" << row << " x=" << x
                    << " actual=" << unsigned(got[0]) << "," << unsigned(got[1])
                    << "," << unsigned(got[2])
                    << " expected=" << unsigned(expected[0]) << ","
                    << unsigned(expected[1]) << "," << unsigned(expected[2])
                    << "\n";
          correct = false;
        }
      }
    readback->Unmap(0, nullptr);
    if (!correct)
      return false;
    std::cout << "Vertex stride mode=" << mode << " pass=" << pass
              << " exact_bytes=4096 passed\n";
  }
  std::cout << "Vertex stride replay/reset/released pipeline refs passed\n";
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 5 && !strcmp(argv[1], "--vertex-stride"))
    return RunVertexStride(argv[2], argv[3], argv[4]) ? 0 : 1;
  if ((argc == 7 || argc == 8) && !strcmp(argv[1], "--gs-differential"))
    return RunGeometryDifferential(argv[2], argv[3], argv[4], argv[5], argv[6],
                                   argc == 8 ? argv[7] : nullptr)
               ? 0
               : 1;
  if (argc < 3 || argc > 5)
    return 2;
  const bool geometry_adjacency_indexed =
      argc == 5 && strcmp(argv[3], "--geometry-adj-indexed") == 0;
  const bool geometry_adjacency =
      argc == 5 &&
      (strcmp(argv[3], "--geometry-adj") == 0 || geometry_adjacency_indexed);
  const bool geometry_root_cbv =
      argc == 5 && strcmp(argv[3], "--geometry-root-cbv") == 0;
  const bool geometry_instanced =
      argc == 5 && strcmp(argv[3], "--geometry-instanced") == 0;
  const bool geometry_indexed =
      argc == 5 && (strcmp(argv[3], "--geometry-indexed") == 0 ||
                    geometry_adjacency_indexed);
  const bool geometry = argc == 5 && (strcmp(argv[3], "--geometry") == 0 ||
                                      geometry_indexed || geometry_adjacency ||
                                      geometry_root_cbv || geometry_instanced);
  const bool textured = argc == 4 && strcmp(argv[3], "--texture") == 0;
  const bool root_cbv = argc == 4 && strcmp(argv[3], "--root-cbv") == 0;
  const bool root_constants =
      argc == 4 && strcmp(argv[3], "--root-constants") == 0;
  const bool root_srv = argc == 4 && strcmp(argv[3], "--root-srv") == 0;
  const bool root_uav = argc == 4 && strcmp(argv[3], "--root-uav") == 0;
  const bool textured_root_cbv = argc == 4 && strcmp(argv[3], "--texture-root-cbv") == 0;
  const bool logic_op = argc == 4 && strcmp(argv[3], "--logic-op") == 0;
  const bool stencil = argc == 4 && strcmp(argv[3], "--stencil") == 0;
  const bool barycentrics = argc == 4 && strcmp(argv[3], "--barycentrics") == 0;
  const bool wave_quad_ops = argc == 4 && strcmp(argv[3], "--wave-quad-ops") == 0;
  const bool int64_ops = argc == 4 && strcmp(argv[3], "--int64-ops") == 0;
  const bool native16_ops = argc == 4 && strcmp(argv[3], "--native16-ops") == 0;
  const bool helper_lane = argc == 4 && strcmp(argv[3], "--helper-lane") == 0;
  const bool helper_lane_derivative = argc == 4 && strcmp(argv[3], "--helper-lane-derivative") == 0;
  const bool helper_lane_discard = argc == 4 && strcmp(argv[3], "--helper-lane-discard") == 0;
  const bool view_id_unsupported = argc == 4 && strcmp(argv[3], "--view-id-unsupported") == 0;
  const bool get_attribute_unsupported = argc == 4 && strcmp(argv[3], "--get-attribute-unsupported") == 0;
  const bool vrs_unsupported = argc == 4 && strcmp(argv[3], "--vrs-unsupported") == 0;
  const bool stencil_ref_unsupported = argc == 4 && strcmp(argv[3], "--stencil-ref-unsupported") == 0;
  if ((argc == 4 && !textured && !root_cbv && !root_constants && !root_srv &&
       !root_uav && !textured_root_cbv && !logic_op && !stencil && !barycentrics &&
       !wave_quad_ops && !int64_ops && !native16_ops && !helper_lane && !helper_lane_derivative &&
       !helper_lane_discard && !view_id_unsupported &&
       !get_attribute_unsupported && !vrs_unsupported && !stencil_ref_unsupported) ||
      (argc == 5 && !geometry))
    return 2;

  std::ifstream vertex_file(argv[1], std::ios::binary | std::ios::ate);
  std::ifstream pixel_file(argv[2], std::ios::binary | std::ios::ate);
  std::ifstream geometry_file;
  if (geometry)
    geometry_file.open(argv[4], std::ios::binary | std::ios::ate);
  if (!vertex_file || !pixel_file || (geometry && !geometry_file))
    return 3;
  auto vertex_size = vertex_file.tellg();
  auto pixel_size = pixel_file.tellg();
  std::streamoff geometry_size = 0;
  if (geometry)
    geometry_size = static_cast<std::streamoff>(geometry_file.tellg());
  vertex_file.seekg(0);
  pixel_file.seekg(0);
  if (geometry)
    geometry_file.seekg(0);
  std::vector<char> vertex_shader(static_cast<size_t>(vertex_size));
  std::vector<char> pixel_shader(static_cast<size_t>(pixel_size));
  std::vector<char> geometry_shader(static_cast<size_t>(geometry_size));
  vertex_file.read(vertex_shader.data(), vertex_shader.size());
  pixel_file.read(pixel_shader.data(), pixel_shader.size());
  if (geometry)
    geometry_file.read(geometry_shader.data(), geometry_shader.size());

  struct Vertex {
    float position[2];
    float color[4];
  };
  static const Vertex vertices[] = {
      {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{3.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{-1.0f, 3.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
  };
  static const Vertex adjacency_vertices[] = {
      {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{3.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{-1.0f, 3.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
  };
  static const uint16_t indices[] = {0, 1, 2};
  static const uint16_t adjacency_indices[] = {0, 1, 2, 3, 4, 5};
  const Vertex *vertex_data =
      geometry_adjacency ? adjacency_vertices : vertices;
  const size_t vertex_data_size =
      geometry_adjacency ? sizeof(adjacency_vertices) : sizeof(vertices);
  const uint16_t *index_data = geometry_adjacency ? adjacency_indices : indices;
  const size_t index_data_size =
      geometry_adjacency ? sizeof(adjacency_indices) : sizeof(indices);
  const UINT draw_count = geometry_adjacency ? 6 : 3;
  static const float root_color[] = {0.0f, 1.0f, 0.0f, 1.0f};
  static const UINT root_color_bits[] = {0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u};

  ID3D12Device *device = nullptr;
  ID3D12CommandQueue *queue = nullptr;
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12RootSignature *root_signature = nullptr;
  ID3DBlob *root_blob = nullptr;
  ID3DBlob *root_error = nullptr;
  ID3D12PipelineState *pso = nullptr;
  ID3D12PipelineState *stencil_pso = nullptr;
  ID3D12GraphicsCommandList *list = nullptr;
  ID3D12DescriptorHeap *rtv_heap = nullptr;
  ID3D12DescriptorHeap *dsv_heap = nullptr;
  ID3D12DescriptorHeap *resource_heap = nullptr;
  ID3D12DescriptorHeap *sampler_heap = nullptr;
  ID3D12QueryHeap *query_heap = nullptr;
  ID3D12QueryHeap *timestamp_heap = nullptr;
  ID3D12Resource *render_target = nullptr;
  ID3D12Resource *depth_stencil = nullptr;
  ID3D12Resource *vertex_buffer = nullptr;
  ID3D12Resource *index_buffer = nullptr;
  ID3D12Resource *root_data_buffer = nullptr;
  ID3D12Resource *root_uav_buffer = nullptr;
  ID3D12Resource *texture = nullptr;
  ID3D12Resource *texture_upload = nullptr;
  ID3D12Resource *readback = nullptr;
  ID3D12Resource *query_readback = nullptr;
  ID3D12Fence *fence = nullptr;
  HANDLE event = nullptr;
  ID3D12CommandList *lists[1] = {};
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  D3D12_DESCRIPTOR_RANGE descriptor_ranges[2] = {};
  D3D12_ROOT_PARAMETER root_parameters[3] = {};
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  D3D12_HEAP_PROPERTIES default_heap = {};
  D3D12_HEAP_PROPERTIES upload_heap = {};
  D3D12_HEAP_PROPERTIES texture_upload_heap = {};
  D3D12_HEAP_PROPERTIES readback_heap = {};
  D3D12_RESOURCE_DESC render_target_desc = {};
  D3D12_RESOURCE_DESC depth_stencil_desc = {};
  D3D12_RESOURCE_DESC buffer_desc = {};
  D3D12_RESOURCE_DESC texture_desc = {};
  D3D12_RESOURCE_DESC readback_desc = {};
  D3D12_RESOURCE_DESC query_readback_desc = {};
  D3D12_QUERY_HEAP_DESC query_heap_desc = {};
  D3D12_CLEAR_VALUE clear_value = {};
  D3D12_CLEAR_VALUE depth_stencil_clear = {};
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC resource_heap_desc = {};
  D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
  D3D12_INPUT_ELEMENT_DESC input_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  D3D12_VERTEX_BUFFER_VIEW vertex_view = {};
  D3D12_INDEX_BUFFER_VIEW index_view = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = {};
  D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  D3D12_TEXTURE_COPY_LOCATION copy_dst = {};
  D3D12_TEXTURE_COPY_LOCATION copy_src = {};
  D3D12_TEXTURE_COPY_LOCATION texture_upload_dst = {};
  D3D12_TEXTURE_COPY_LOCATION texture_upload_src = {};
  D3D12_RESOURCE_BARRIER barrier = {};
  D3D12_RESOURCE_BARRIER root_uav_barrier = {};
  D3D12_RESOURCE_BARRIER texture_barrier = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  D3D12_SAMPLER_DESC sampler_desc = {};
  D3D12_CPU_DESCRIPTOR_HANDLE resource_cpu = {};
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu = {};
  D3D12_VIEWPORT viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  D3D12_RECT scissor = {0, 0, 1, 1};
  ID3D12DescriptorHeap *descriptor_heaps[2] = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  void *mapped_upload = nullptr;
  void *mapped_index_upload = nullptr;
  void *mapped_root_data = nullptr;
  void *mapped_texture_upload = nullptr;
  BYTE *mapped_readback = nullptr;
  UINT64 *mapped_query_readback = nullptr;
  UINT pixel = 0;
  UINT expected_pixel = 0;
  UINT64 query_result = 0;
  UINT64 timestamp_frequency = 0;
  UINT64 timestamp_begin = 0;
  UINT64 timestamp_end = 0;
  UINT64 calibration_gpu = 0;
  UINT64 calibration_cpu = 0;
  HRESULT instanced_geometry_hr = E_FAIL;
  int result = 1;

  if (!CheckHR("D3D12CreateDevice",
               D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device))))
    goto cleanup;
  if (barycentrics) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
    if (!CheckHR("CheckFeatureSupport(OPTIONS3)",
                 device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,
                                             &options3, sizeof(options3))) ||
        !options3.BarycentricsSupported) {
      std::cerr << "barycentrics feature query was not advertised\n";
      goto cleanup;
    }
  }

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!CheckHR("CreateCommandQueue",
               device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    goto cleanup;
  if (!CheckHR("CreateCommandAllocator",
               device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator))))
    goto cleanup;
  query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  query_heap_desc.Count = 1;
  if (!CheckHR("CreateQueryHeap", device->CreateQueryHeap(&query_heap_desc, IID_PPV_ARGS(&query_heap))))
    goto cleanup;
  query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  query_heap_desc.Count = 2;
  if (!CheckHR("CreateTimestampQueryHeap", device->CreateQueryHeap(&query_heap_desc, IID_PPV_ARGS(&timestamp_heap))))
    goto cleanup;
  if (!CheckHR("GetTimestampFrequency", queue->GetTimestampFrequency(&timestamp_frequency)))
    goto cleanup;
  if (!timestamp_frequency) {
    std::cerr << "timestamp frequency returned zero\n";
    goto cleanup;
  }
  if (!geometry) {
    if (!CheckHR(
            "GetClockCalibration",
            queue->GetClockCalibration(&calibration_gpu, &calibration_cpu)))
      goto cleanup;
    if (!calibration_gpu || !calibration_cpu) {
      std::cerr << "clock calibration returned zero\n";
      goto cleanup;
    }
  }

  if (root_cbv || geometry_root_cbv) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_desc.NumParameters = 1;
    root_desc.pParameters = root_parameters;
  } else if (root_constants) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].Constants.Num32BitValues = 4;
    root_parameters[0].Constants.ShaderRegister = 0;
    root_parameters[0].Constants.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_desc.NumParameters = 1;
    root_desc.pParameters = root_parameters;
  } else if (root_srv) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_desc.NumParameters = 1;
    root_desc.pParameters = root_parameters;
  } else if (root_uav) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_desc.NumParameters = 1;
    root_desc.pParameters = root_parameters;
  } else if (textured_root_cbv) {
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    descriptor_ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    descriptor_ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0};
    for (unsigned i = 0; i < 2; i++) {
      root_parameters[i + 1].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[i + 1].DescriptorTable.NumDescriptorRanges = 1;
      root_parameters[i + 1].DescriptorTable.pDescriptorRanges =
          &descriptor_ranges[i];
    }
    root_desc.NumParameters = 3;
    root_desc.pParameters = root_parameters;
  } else if (textured) {
    descriptor_ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    descriptor_ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0};
    for (unsigned i = 0; i < 2; i++) {
      root_parameters[i].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      root_parameters[i].DescriptorTable.NumDescriptorRanges = 1;
      root_parameters[i].DescriptorTable.pDescriptorRanges =
          &descriptor_ranges[i];
    }
    root_desc.NumParameters = 2;
    root_desc.pParameters = root_parameters;
  }

  if (!CheckHR("D3D12SerializeRootSignature",
               D3D12SerializeRootSignature(&root_desc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           &root_blob, &root_error)))
    goto cleanup;
  if (!CheckHR("CreateRootSignature",
               device->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                           root_blob->GetBufferSize(),
                                           IID_PPV_ARGS(&root_signature))))
    goto cleanup;

  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;
  render_target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  render_target_desc.Width = 1;
  render_target_desc.Height = 1;
  render_target_desc.DepthOrArraySize = 1;
  render_target_desc.MipLevels = 1;
  render_target_desc.Format = logic_op ? DXGI_FORMAT_R8G8B8A8_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
  render_target_desc.SampleDesc.Count = 1;
  render_target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  clear_value.Format = render_target_desc.Format;
  clear_value.Color[0] = 0.0f;
  clear_value.Color[1] = 0.0f;
  clear_value.Color[2] = 0.0f;
  clear_value.Color[3] = 1.0f;
  if (logic_op) {
    clear_value.Color[0] = 15.0f;
    clear_value.Color[1] = 240.0f;
    clear_value.Color[2] = 85.0f;
    clear_value.Color[3] = 170.0f;
  }
  if (!CheckHR("CreateRenderTarget",
               device->CreateCommittedResource(
                   &default_heap, D3D12_HEAP_FLAG_NONE, &render_target_desc,
                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
                   IID_PPV_ARGS(&render_target))))
    goto cleanup;

  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  if (!CheckHR("CreateRTVHeap", device->CreateDescriptorHeap(
                                    &rtv_heap_desc, IID_PPV_ARGS(&rtv_heap))))
    goto cleanup;
  rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(render_target, nullptr, rtv_handle);

  if (stencil) {
    depth_stencil_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depth_stencil_desc.Width = 1;
    depth_stencil_desc.Height = 1;
    depth_stencil_desc.DepthOrArraySize = 1;
    depth_stencil_desc.MipLevels = 1;
    depth_stencil_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_stencil_desc.SampleDesc.Count = 1;
    depth_stencil_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depth_stencil_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    depth_stencil_clear.Format = depth_stencil_desc.Format;
    depth_stencil_clear.DepthStencil.Depth = 1.0f;
    depth_stencil_clear.DepthStencil.Stencil = 0x2a;
    if (!CheckHR("CreateDepthStencil",
                 device->CreateCommittedResource(
                     &default_heap, D3D12_HEAP_FLAG_NONE, &depth_stencil_desc,
                     D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_stencil_clear,
                     IID_PPV_ARGS(&depth_stencil))))
      goto cleanup;

    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.NumDescriptors = 1;
    if (!CheckHR("CreateDSVHeap", device->CreateDescriptorHeap(
                                      &dsv_heap_desc, IID_PPV_ARGS(&dsv_heap))))
      goto cleanup;
    dsv_handle = dsv_heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateDepthStencilView(depth_stencil, nullptr, dsv_handle);
  }

  if (textured || textured_root_cbv) {
    resource_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    resource_heap_desc.NumDescriptors = 1;
    resource_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_heap_desc.NumDescriptors = 1;
    sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!CheckHR("CreateResourceHeap",
                 device->CreateDescriptorHeap(&resource_heap_desc,
                                              IID_PPV_ARGS(&resource_heap))))
      goto cleanup;
    if (!CheckHR("CreateSamplerHeap",
                 device->CreateDescriptorHeap(&sampler_heap_desc,
                                              IID_PPV_ARGS(&sampler_heap))))
      goto cleanup;
    texture_upload_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    texture_upload_heap.CreationNodeMask = 1;
    texture_upload_heap.VisibleNodeMask = 1;
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = 1;
    texture_desc.Height = 1;
    texture_desc.DepthOrArraySize = 1;
    texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    if (!CheckHR("CreateTexture",
                 device->CreateCommittedResource(
                     &texture_upload_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                     IID_PPV_ARGS(&texture))))
      goto cleanup;

    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    upload_heap.CreationNodeMask = 1;
    upload_heap.VisibleNodeMask = 1;
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = 256;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (!CheckHR("CreateTextureUpload",
                 device->CreateCommittedResource(
                     &upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                     IID_PPV_ARGS(&texture_upload))))
      goto cleanup;
    device->GetCopyableFootprints(&texture_desc, 0, 1, 0,
                                  &texture_upload_dst.PlacedFootprint,
                                  &row_count, &row_size, &total_size);
    if (!CheckHR("MapTextureUpload",
                 texture_upload->Map(0, nullptr, &mapped_texture_upload)))
      goto cleanup;
    static const UINT texture_pixel = 0xff0000ff;
    memcpy(mapped_texture_upload, &texture_pixel, sizeof(texture_pixel));
    texture_upload->Unmap(0, nullptr);

    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    resource_cpu = resource_heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateShaderResourceView(texture, &srv_desc, resource_cpu);

    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    sampler_cpu = sampler_heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateSampler(&sampler_desc, sampler_cpu);
  }

  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_heap.CreationNodeMask = 1;
  upload_heap.VisibleNodeMask = 1;
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = vertex_data_size;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (!CheckHR("CreateVertexBuffer",
               device->CreateCommittedResource(
                   &upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                   IID_PPV_ARGS(&vertex_buffer))))
    goto cleanup;
  if (!CheckHR("MapVertexBuffer",
               vertex_buffer->Map(0, nullptr, &mapped_upload)))
    goto cleanup;
  memcpy(mapped_upload, vertex_data, vertex_data_size);
  vertex_buffer->Unmap(0, nullptr);
  vertex_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = vertex_data_size;
  vertex_view.StrideInBytes = sizeof(Vertex);

  if (geometry_indexed) {
    buffer_desc.Width = index_data_size;
    if (!CheckHR("CreateIndexBuffer",
                 device->CreateCommittedResource(
                     &upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                     IID_PPV_ARGS(&index_buffer))))
      goto cleanup;
    if (!CheckHR("MapIndexBuffer",
                 index_buffer->Map(0, nullptr, &mapped_index_upload)))
      goto cleanup;
    memcpy(mapped_index_upload, index_data, index_data_size);
    index_buffer->Unmap(0, nullptr);
    index_view.BufferLocation = index_buffer->GetGPUVirtualAddress();
    index_view.SizeInBytes = index_data_size;
    index_view.Format = DXGI_FORMAT_R16_UINT;
  }

  if (root_cbv || root_srv || textured_root_cbv || geometry_root_cbv) {
    buffer_desc.Width = 256;
    if (!CheckHR("CreateRootData",
                 device->CreateCommittedResource(
                     &upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                     IID_PPV_ARGS(&root_data_buffer))))
      goto cleanup;
    if (!CheckHR("MapRootData",
                 root_data_buffer->Map(0, nullptr, &mapped_root_data)))
      goto cleanup;
    memcpy(mapped_root_data, root_color, sizeof(root_color));
    root_data_buffer->Unmap(0, nullptr);
  }
  if (root_uav) {
    buffer_desc.Width = 256;
    buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!CheckHR("CreateRootUAV",
                 device->CreateCommittedResource(
                     &default_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                     IID_PPV_ARGS(&root_uav_buffer))))
      goto cleanup;
  }

  pso_desc.pRootSignature = root_signature;
  pso_desc.VS.pShaderBytecode = vertex_shader.data();
  pso_desc.VS.BytecodeLength = vertex_shader.size();
  pso_desc.PS.pShaderBytecode = pixel_shader.data();
  pso_desc.PS.BytecodeLength = pixel_shader.size();
  if (geometry) {
    pso_desc.GS.pShaderBytecode = geometry_shader.data();
    pso_desc.GS.BytecodeLength = geometry_shader.size();
  }
  pso_desc.InputLayout.pInputElementDescs = input_layout;
  pso_desc.InputLayout.NumElements = 2;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = logic_op ? DXGI_FORMAT_R8G8B8A8_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleMask = UINT_MAX;
  if (stencil) {
    pso_desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso_desc.DepthStencilState.DepthEnable = TRUE;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.StencilEnable = TRUE;
    pso_desc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    pso_desc.DepthStencilState.StencilWriteMask = 0;
    pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    pso_desc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    pso_desc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    pso_desc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    pso_desc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
  }
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  if (logic_op) {
    pso_desc.BlendState.RenderTarget[0].LogicOpEnable = TRUE;
    pso_desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_OR;
  }
  if (geometry_instanced) {
    instanced_geometry_hr =
        device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    if (instanced_geometry_hr != E_NOTIMPL || pso) {
      std::cerr << "CreateGraphicsPipelineState with instanced geometry returned 0x"
                << std::hex << static_cast<unsigned long>(instanced_geometry_hr)
                << std::dec << "\n";
      std::cerr << "instanced geometry shader was not rejected\n";
      goto cleanup;
    }
    std::cout << "DXIL instanced geometry rejection passed\n";
    result = 0;
    goto cleanup;
  }
  if (view_id_unsupported || get_attribute_unsupported || vrs_unsupported || stencil_ref_unsupported) {
    const HRESULT unsupported_hr =
        device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    if (unsupported_hr != E_NOTIMPL || pso) {
      std::cerr << "unsupported shader semantic was not rejected: "
                << (view_id_unsupported ? "SV_ViewID"
                    : get_attribute_unsupported ? "GetAttributeAtVertex"
                    : vrs_unsupported ? "SV_ShadingRate"
                                       : "SV_StencilRef")
                << " returned 0x" << std::hex
                << static_cast<unsigned long>(unsupported_hr) << std::dec << "\n";
      goto cleanup;
    }
    std::cout << "DXIL unsupported "
              << (view_id_unsupported ? "SV_ViewID"
                  : get_attribute_unsupported ? "GetAttributeAtVertex"
                  : vrs_unsupported ? "SV_ShadingRate"
                                     : "SV_StencilRef")
              << " rejected: 0x" << std::hex
              << static_cast<unsigned long>(unsupported_hr) << std::dec << "\n";
    result = 0;
    goto cleanup;
  }
  if (!CheckHR(
          "CreateGraphicsPipelineState",
          device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
    goto cleanup;

  if (stencil) {
    auto second_pso_desc = pso_desc;
    second_pso_desc.RasterizerState.DepthClipEnable = FALSE;
    // With the application reference still at 0x2a, this PSO must reject
    // the second draw.  If a PSO switch rewrites the reference to zero, the
    // NOT_EQUAL test would pass and the zero blend state would erase the
    // first draw, making the regression observable in the readback.
    second_pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL;
    second_pso_desc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL;
    second_pso_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    second_pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
    second_pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    second_pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    second_pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    second_pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    second_pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (!CheckHR("CreateSecondGraphicsPipelineState",
                 device->CreateGraphicsPipelineState(&second_pso_desc,
                                                     IID_PPV_ARGS(&stencil_pso))))
      goto cleanup;
  }

  if (!CheckHR("CreateCommandList",
               device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator, pso, IID_PPV_ARGS(&list))))
    goto cleanup;
  if (textured || textured_root_cbv) {
    texture_upload_dst.pResource = texture;
    texture_upload_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    texture_upload_src.pResource = texture_upload;
    texture_upload_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    texture_upload_src.PlacedFootprint = texture_upload_dst.PlacedFootprint;
    list->CopyTextureRegion(&texture_upload_dst, 0, 0, 0, &texture_upload_src,
                            nullptr);
    texture_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    texture_barrier.Transition.pResource = texture;
    texture_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    texture_barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    texture_barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &texture_barrier);
  }
  list->SetPipelineState(pso);
  list->SetGraphicsRootSignature(root_signature);
  if (textured || textured_root_cbv) {
    descriptor_heaps[0] = resource_heap;
    descriptor_heaps[1] = sampler_heap;
    list->SetDescriptorHeaps(2, descriptor_heaps);
    const UINT resource_root_index = textured_root_cbv ? 1 : 0;
    const UINT sampler_root_index = textured_root_cbv ? 2 : 1;
    list->SetGraphicsRootDescriptorTable(
        resource_root_index, resource_heap->GetGPUDescriptorHandleForHeapStart());
    list->SetGraphicsRootDescriptorTable(
        sampler_root_index, sampler_heap->GetGPUDescriptorHandleForHeapStart());
  }
  if (root_cbv || textured_root_cbv || geometry_root_cbv)
    list->SetGraphicsRootConstantBufferView(0, root_data_buffer->GetGPUVirtualAddress());
  if (root_constants)
    list->SetGraphicsRoot32BitConstants(0, 4, root_color_bits, 0);
  if (root_srv)
    list->SetGraphicsRootShaderResourceView(0, root_data_buffer->GetGPUVirtualAddress());
  if (root_uav)
    list->SetGraphicsRootUnorderedAccessView(0, root_uav_buffer->GetGPUVirtualAddress());
  list->IASetPrimitiveTopology(geometry_adjacency
                                   ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ
                                   : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->IASetVertexBuffers(0, 1, &vertex_view);
  if (geometry_indexed)
    list->IASetIndexBuffer(&index_view);
  list->OMSetRenderTargets(1, &rtv_handle, FALSE, stencil ? &dsv_handle : nullptr);
  if (logic_op || stencil)
    list->ClearRenderTargetView(rtv_handle, clear_value.Color, 0, nullptr);
  if (stencil)
    list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0x2a, 0, nullptr);
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  list->EndQuery(timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
  list->BeginQuery(query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0);
  if (stencil)
    list->OMSetStencilRef(0x2a);
  if (geometry_indexed)
    list->DrawIndexedInstanced(draw_count, 1, 0, 0, 0);
  else
    list->DrawInstanced(draw_count, 1, 0, 0);
  if (stencil) {
    // Exercise both a changed value and a restoration before the PSO switch;
    // the second draw must still observe the restored application value.
    list->OMSetStencilRef(0);
    list->OMSetStencilRef(0x2a);
    list->OMSetStencilRef(0x2a);
    list->SetPipelineState(stencil_pso);
    if (geometry_indexed)
      list->DrawIndexedInstanced(draw_count, 1, 0, 0, 0);
    else
      list->DrawInstanced(draw_count, 1, 0, 0);
  }
  list->EndQuery(query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0);
  list->EndQuery(timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);

  if (root_uav) {
    root_uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    root_uav_barrier.Transition.pResource = root_uav_buffer;
    root_uav_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    root_uav_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    root_uav_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &root_uav_barrier);
    readback_desc.Width = 256;
  } else {
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = render_target;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);

    device->GetCopyableFootprints(&render_target_desc, 0, 1, 0, &footprint,
                                  &row_count, &row_size, &total_size);
    readback_desc.Width = total_size;
  }
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;
  readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readback_desc.Height = 1;
  readback_desc.DepthOrArraySize = 1;
  readback_desc.MipLevels = 1;
  readback_desc.SampleDesc.Count = 1;
  readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  query_readback_desc = readback_desc;
  query_readback_desc.Width = sizeof(UINT64) * 3;
  if (!CheckHR("CreateQueryReadback",
               device->CreateCommittedResource(
                   &readback_heap, D3D12_HEAP_FLAG_NONE, &query_readback_desc,
                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&query_readback))))
    goto cleanup;
  if (!CheckHR("CreateReadbackBuffer",
               device->CreateCommittedResource(
                   &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                   IID_PPV_ARGS(&readback))))
    goto cleanup;
  if (root_uav) {
    list->CopyBufferRegion(readback, 0, root_uav_buffer, 0, sizeof(UINT));
  } else {
    copy_dst.pResource = readback;
    copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    copy_dst.PlacedFootprint = footprint;
    copy_src.pResource = render_target;
    copy_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, nullptr);
  }
  list->ResolveQueryData(query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, query_readback, sizeof(UINT64) * 2);
  list->ResolveQueryData(timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, query_readback, 0);
  if (!CheckHR("Close", list->Close()))
    goto cleanup;

  lists[0] = list;
  queue->ExecuteCommandLists(1, lists);
  if (!CheckHR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                  IID_PPV_ARGS(&fence))))
    goto cleanup;
  if (!CheckHR("Signal", queue->Signal(fence, 1)))
    goto cleanup;
  event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!event ||
      !CheckHR("SetEventOnCompletion", fence->SetEventOnCompletion(1, event)))
    goto cleanup;
  WaitForSingleObject(event, INFINITE);

  if (!CheckHR("MapReadback",
               readback->Map(0, nullptr,
                             reinterpret_cast<void **>(&mapped_readback))))
    goto cleanup;
  pixel = *reinterpret_cast<UINT *>(mapped_readback);
  readback->Unmap(0, nullptr);
  if (!CheckHR("MapQueryReadback",
               query_readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped_query_readback))))
    goto cleanup;
  timestamp_begin = mapped_query_readback[0];
  timestamp_end = mapped_query_readback[1];
  query_result = mapped_query_readback[2];
  query_readback->Unmap(0, nullptr);
  if (!query_result) {
    std::cerr << "occlusion query returned zero\n";
    goto cleanup;
  }
  if (timestamp_end <= timestamp_begin) {
    std::cerr << "timestamp query did not advance: " << timestamp_begin << " -> " << timestamp_end << "\n";
    goto cleanup;
  }
  expected_pixel = (wave_quad_ops || int64_ops || native16_ops || helper_lane || helper_lane_derivative || helper_lane_discard)
                       ? 0xff00ff00u
                   : logic_op
                       ? 0xffffffffu
                       : barycentrics
                           ? 0xff404080u
                       : (geometry || root_cbv || root_constants || root_srv || root_uav || textured_root_cbv)
                           ? 0xff00ff00u
                           : 0xff0000ffu;
  if ((pixel & 0x00ffffffu) != (expected_pixel & 0x00ffffffu)) {
    std::cerr << "graphics readback mismatch: 0x" << std::hex << pixel
              << std::dec << "\n";
    goto cleanup;
  }
  std::cout << "DXIL "
            << (geometry_adjacency_indexed
                    ? "indexed adjacency geometry graphics"
                : geometry_adjacency ? "adjacency geometry graphics"
                : geometry_root_cbv  ? "root CBV geometry graphics"
                : geometry_indexed   ? "indexed geometry graphics"
                : geometry           ? "geometry graphics"
                : root_cbv           ? "root CBV graphics"
                : root_constants     ? "root constants graphics"
                : root_srv           ? "root SRV graphics"
                : root_uav           ? "root UAV graphics"
                : logic_op           ? "logic op graphics"
                : stencil            ? "stencil graphics"
                : barycentrics       ? "barycentrics graphics"
                : wave_quad_ops      ? "wave quad graphics"
                : int64_ops          ? "int64 graphics"
                : native16_ops       ? "native16 graphics"
                : helper_lane_discard ? "helper lane discard graphics"
                : helper_lane_derivative ? "helper lane derivative graphics"
                : helper_lane ? "helper lane graphics"
                : textured_root_cbv  ? "root CBV textured graphics"
                : textured           ? "textured graphics"
                                     : "graphics")
            << " readback passed: 0x" << std::hex << pixel << std::dec << "\n";
  result = 0;

cleanup:
  if (event)
    CloseHandle(event);
  if (fence)
    fence->Release();
  if (list)
    list->Release();
  if (pso)
    pso->Release();
  if (stencil_pso)
    stencil_pso->Release();
  if (readback)
    readback->Release();
  if (query_readback)
    query_readback->Release();
  if (texture_upload)
    texture_upload->Release();
  if (texture)
    texture->Release();
  if (root_uav_buffer)
    root_uav_buffer->Release();
  if (root_data_buffer)
    root_data_buffer->Release();
  if (index_buffer)
    index_buffer->Release();
  if (vertex_buffer)
    vertex_buffer->Release();
  if (render_target)
    render_target->Release();
  if (depth_stencil)
    depth_stencil->Release();
  if (rtv_heap)
    rtv_heap->Release();
  if (dsv_heap)
    dsv_heap->Release();
  if (sampler_heap)
    sampler_heap->Release();
  if (resource_heap)
    resource_heap->Release();
  if (query_heap)
    query_heap->Release();
  if (timestamp_heap)
    timestamp_heap->Release();
  if (root_signature)
    root_signature->Release();
  if (root_blob)
    root_blob->Release();
  if (root_error)
    root_error->Release();
  if (allocator)
    allocator->Release();
  if (queue)
    queue->Release();
  if (device)
    device->Release();
  return result;
}

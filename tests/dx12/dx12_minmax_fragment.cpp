#include "d3d12_device.hpp"
#include "d3d12_command_allocator.hpp"
#include "d3d12_minmax.hpp"
#include "d3d12_minmax_dispatch.hpp"
#include "d3d12_sampler.hpp"
#include "d3d12_shader_converter.hpp"
#include "log/log.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>

dxmt::Logger dxmt::Logger::s_instance("dx12_minmax_fragment");
template <typename T> struct ReleaseCOM { void operator()(T *p) const { if (p) p->Release(); } };
template <typename T> using OwnedCOM = std::unique_ptr<T, ReleaseCOM<T>>;
static bool root_updates_fixture = false;
static bool root_buffers_fixture = false;
static bool implicit_fixture = false;
static bool implicit_bias_fixture = false;
static bool vertex_sampling_fixture = false;
static bool vertex_only_fixture = false;
static bool vertex_shared_fixture = false;
static bool geometry_fixture = false, tessellation_fixture = false;
static std::vector<uint8_t> geometry_shader, hull_shader, domain_shader;

static bool Load(const wchar_t *path, std::vector<uint8_t> &bytes) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {}; DWORD read = 0;
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 32 * 1024 * 1024;
  if (ok) { bytes.resize(size.QuadPart); ok = ReadFile(file, bytes.data(), bytes.size(), &read, nullptr) && read == bytes.size(); }
  CloseHandle(file); return ok;
}

static bool CheckDraw(ID3D12Device *device, ID3D12PipelineState *pso, ID3D12RootSignature *root,
    ID3D12DescriptorHeap *heap, ID3D12DescriptorHeap *samplers, ID3D12Resource *texture,
    ID3D12Resource *replacement, bool static_sampler, bool live, unsigned indirect_kind) {
  const bool updates = indirect_kind == 3 || indirect_kind == 4;
  const bool indexed = indirect_kind == 2 || indirect_kind == 4 || indirect_kind == 5;
  const bool indirect = indirect_kind && indirect_kind != 5;
  const auto check = [](HRESULT hr, const char *what) {
    if (FAILED(hr)) std::printf("MinMax draw %s failed %08lx\n", what, (unsigned long)hr);
    return SUCCEEDED(hr);
  };
  ID3D12CommandQueue *raw_queue = nullptr; D3D12_COMMAND_QUEUE_DESC qd = {};
  if (!check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&raw_queue)), "queue")) return false;
  OwnedCOM<ID3D12CommandQueue> queue(raw_queue);
  ID3D12Fence *raw_fence = nullptr;
  if (!check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&raw_fence)), "fence")) return false;
  OwnedCOM<ID3D12Fence> fence(raw_fence);
  const auto execute = [&](ID3D12GraphicsCommandList *list, UINT64 serial) {
    ID3D12CommandList *lists[] = {list}; queue->ExecuteCommandLists(1, lists);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return false;
    const bool ok = check(queue->Signal(fence.get(), serial), "signal") &&
        check(fence->SetEventOnCompletion(serial, event), "completion") && WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
    CloseHandle(event); return ok;
  };
  D3D12_RESOURCE_DESC buffer_desc = {}; buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = implicit_fixture ? 2048 : 1024;
  buffer_desc.Height = buffer_desc.DepthOrArraySize = buffer_desc.MipLevels = buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES props = {}; props.Type = D3D12_HEAP_TYPE_UPLOAD;
  ID3D12Resource *raw_upload = nullptr;
  if (!check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &buffer_desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&raw_upload)), "upload")) return false;
  OwnedCOM<ID3D12Resource> upload(raw_upload);
  void *mapped = nullptr;
  if (!check(upload->Map(0, nullptr, &mapped), "upload map")) return false;
  std::memset(mapped, 0, buffer_desc.Width);
  const UINT pixels[] = {0xff000010, 0xff000040, 0xff0000c0, 0xff0000f0,
      0xff000020, 0xff000050, 0xff0000b0, 0xff0000e0};
  for (unsigned row = 0; row < 4; ++row) std::memcpy(static_cast<uint8_t *>(mapped) + 256 * row, pixels + 2 * row, 8);
  if (implicit_fixture) {
    const UINT mips[] = {0xff000060, 0xff000090};
    for (unsigned i = 0; i < 2; ++i) std::memcpy(static_cast<uint8_t *>(mapped) + 1024 + i * 512, mips + i, 4);
  }
  upload->Unmap(0, nullptr);
  ID3D12CommandAllocator *raw_allocator = nullptr;
  ID3D12GraphicsCommandList *raw_list = nullptr;
  if (!check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "upload allocator")) return false;
  OwnedCOM<ID3D12CommandAllocator> upload_allocator(raw_allocator);
  if (!check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, upload_allocator.get(), nullptr,
      IID_PPV_ARGS(&raw_list)), "upload list")) return false;
  OwnedCOM<ID3D12GraphicsCommandList> upload_list(raw_list);
  for (unsigned i = 0; i < 2; ++i) {
    auto *input = i ? replacement : texture;
    D3D12_RESOURCE_BARRIER barrier = {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {input, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        D3D12_RESOURCE_STATE_COPY_DEST};
    upload_list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = input; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = upload.get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = {i * 512u, {DXGI_FORMAT_R8G8B8A8_UNORM, 2, 2, 1, 256}};
    upload_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    if (implicit_fixture) {
      dst.SubresourceIndex = 1;
      src.PlacedFootprint = {1024 + i * 512u, {DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 256}};
      upload_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    upload_list->ResourceBarrier(1, &barrier);
  }
  if (!check(upload_list->Close(), "upload close") || !execute(upload_list.get(), 1)) return false;
  OwnedCOM<ID3D12Resource> root_uav;
  const UINT constants_parameter = static_sampler ? 3 : 4;
  if (root_buffers_fixture) {
    if (!check(upload->Map(0, nullptr, &mapped), "root data map")) return false;
    const UINT cbvs[] = {13, 17}, srvs[] = {19, 23}, uavs[] = {5, 9};
    for (unsigned i = 0; i < 2; ++i) {
      std::memcpy(static_cast<uint8_t *>(mapped) + 512 + 256 * i, cbvs + i, 4);
      std::memcpy(static_cast<uint8_t *>(mapped) + 576 + 256 * i, srvs + i, 4);
    }
    std::memcpy(static_cast<uint8_t *>(mapped) + 384, uavs, sizeof(uavs)); upload->Unmap(0, nullptr);
    auto uav_desc = buffer_desc; uav_desc.Width = 256; uav_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto uav_props = props; uav_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    ID3D12Resource *raw = nullptr;
    if (!check(device->CreateCommittedResource(&uav_props, D3D12_HEAP_FLAG_NONE, &uav_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw)), "root UAV")) return false;
    root_uav.reset(raw);
    ID3D12CommandAllocator *raw_root_allocator = nullptr;
    ID3D12GraphicsCommandList *raw_root_list = nullptr;
    if (!check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_root_allocator)), "root upload allocator")) return false;
    OwnedCOM<ID3D12CommandAllocator> root_allocator(raw_root_allocator);
    if (!check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, root_allocator.get(), nullptr,
        IID_PPV_ARGS(&raw_root_list)), "root upload list")) return false;
    OwnedCOM<ID3D12GraphicsCommandList> root_list(raw_root_list);
    root_list->CopyBufferRegion(root_uav.get(), 0, upload.get(), 384, sizeof(uavs));
    D3D12_RESOURCE_BARRIER transition = {}; transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition = {root_uav.get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    root_list->ResourceBarrier(1, &transition);
    if (!check(root_list->Close(), "root upload close") || !execute(root_list.get(), 2)) return false;
  }
  const auto set_root_buffers = [&](ID3D12GraphicsCommandList *command) {
    if (!root_buffers_fixture) return;
    command->SetGraphicsRootConstantBufferView(constants_parameter + 1, upload->GetGPUVirtualAddress() + 512);
    command->SetGraphicsRootShaderResourceView(constants_parameter + 2, upload->GetGPUVirtualAddress() + 576);
    command->SetGraphicsRootUnorderedAccessView(constants_parameter + 3, root_uav->GetGPUVirtualAddress());
  };
  OwnedCOM<ID3D12CommandSignature> signature;
  if (indirect) {
    D3D12_INDIRECT_ARGUMENT_DESC arguments[5] = {};
    arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    arguments[0].Constant = {static_sampler ? 3u : 4u, 0, 1};
    const unsigned draw_argument = updates && root_buffers_fixture ? 4 : 1;
    if (draw_argument == 4) {
      arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
      arguments[1].ConstantBufferView.RootParameterIndex = constants_parameter + 1;
      arguments[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
      arguments[2].ShaderResourceView.RootParameterIndex = constants_parameter + 2;
      arguments[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
      arguments[3].UnorderedAccessView.RootParameterIndex = constants_parameter + 3;
    }
    arguments[draw_argument].Type = indexed ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED : D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC desc = {(indexed ? 20u : 16u) + (updates ? 4u : 0u) + (draw_argument == 4 ? 24u : 0u),
        updates ? draw_argument + 1 : 1u, arguments + (updates ? 0 : draw_argument), 0};
    ID3D12CommandSignature *raw = nullptr;
    if (!check(device->CreateCommandSignature(&desc, updates ? root : nullptr, IID_PPV_ARGS(&raw)), "indirect signature") ||
        !check(upload->Map(0, nullptr, &mapped), "argument map")) return false;
    signature.reset(raw);
    const UINT draw[] = {3, 1, 0, 0, 0};
    for (unsigned i = 0; i < 2; ++i) {
      auto *command = static_cast<uint8_t *>(mapped) + 16 + i * desc.ByteStride;
      if (updates) {
        const UINT value = i ? 11 : 7; std::memcpy(command, &value, 4); command += 4;
        if (root_buffers_fixture) {
          const UINT64 addresses[] = {upload->GetGPUVirtualAddress() + 512 + 256 * i,
              upload->GetGPUVirtualAddress() + 576 + 256 * i, root_uav->GetGPUVirtualAddress() + 4 * i};
          std::memcpy(command, addresses, sizeof(addresses)); command += sizeof(addresses);
        }
      }
      std::memcpy(command, draw, indexed ? 20 : 16);
    }
    const UINT indices[] = {0, 1, 2};
    std::memcpy(static_cast<uint8_t *>(mapped) + 128, indices, sizeof(indices));
    upload->Unmap(0, nullptr);
  }
  if (indirect_kind == 5) {
    if (!check(upload->Map(0, nullptr, &mapped), "direct index map")) return false;
    const UINT indices[] = {0, 1, 2};
    std::memcpy(static_cast<uint8_t *>(mapped) + 128, indices, sizeof(indices));
    upload->Unmap(0, nullptr);
  }
  auto rt_desc = texture->GetDesc(); rt_desc.Width = rt_desc.Height = 4; rt_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  rt_desc.MipLevels = 1;
  props.Type = D3D12_HEAP_TYPE_DEFAULT;
  ID3D12Resource *raw_rt = nullptr;
  if (!check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &rt_desc,
      D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&raw_rt)), "render target")) return false;
  OwnedCOM<ID3D12Resource> rt(raw_rt);
  props.Type = D3D12_HEAP_TYPE_READBACK;
  ID3D12Resource *raw_readback = nullptr;
  if (!check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &buffer_desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&raw_readback)), "readback")) return false;
  OwnedCOM<ID3D12Resource> readback(raw_readback);
  D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
  ID3D12DescriptorHeap *raw_rtv = nullptr;
  if (!check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&raw_rtv)), "RTV heap")) return false;
  OwnedCOM<ID3D12DescriptorHeap> rtv(raw_rtv);
  const auto target = rtv->GetCPUDescriptorHandleForHeapStart(); device->CreateRenderTargetView(rt.get(), nullptr, target);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {}; srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = implicit_fixture ? 2 : 1;
  auto cpu = heap->GetCPUDescriptorHandleForHeapStart(); cpu.ptr += 2 * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  OwnedCOM<ID3D12DescriptorHeap> ordinary_heap, ordinary_samplers;
  if (!static_sampler) {
    hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ID3D12DescriptorHeap *raw = nullptr;
    if (!check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&raw)), "ordinary heap")) return false;
    ordinary_heap.reset(raw);
    auto handle = ordinary_heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateShaderResourceView(texture, &srv, handle);
    handle.ptr += 2 * device->GetDescriptorHandleIncrementSize(hd.Type); device->CreateShaderResourceView(texture, &srv, handle);
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    if (!check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&raw)), "ordinary sampler heap")) return false;
    ordinary_samplers.reset(raw);
    handle = ordinary_samplers->GetCPUDescriptorHandleForHeapStart();
    const auto stride = device->GetDescriptorHandleIncrementSize(hd.Type);
    D3D12_SAMPLER_DESC sd = {}; sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sd.MaxLOD = D3D12_FLOAT32_MAX;
    sd.MipLODBias = implicit_fixture ? 1 : 0;
    for (unsigned i = 0; i < 2; ++i) { handle.ptr += stride; device->CreateSampler(&sd, handle); }
  }
  const auto write = [&](bool changed) {
    device->CreateShaderResourceView(changed ? replacement : texture, &srv, cpu);
    if (!static_sampler) {
      auto sampler_cpu = samplers->GetCPUDescriptorHandleForHeapStart();
      const auto stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
      D3D12_SAMPLER_DESC sd = {}; sd.Filter = changed ? D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR;
      sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sd.MaxLOD = D3D12_FLOAT32_MAX;
      sd.MipLODBias = implicit_fixture ? 1 : 0;
      for (unsigned i = 0; i < 2; ++i) { sampler_cpu.ptr += stride; device->CreateSampler(&sd, sampler_cpu); }
    }
  };
  write(false);
  if (!check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "draw allocator")) return false;
  OwnedCOM<ID3D12CommandAllocator> allocator(raw_allocator);
  if (!check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), pso,
      IID_PPV_ARGS(&raw_list)), "draw list")) return false;
  OwnedCOM<ID3D12GraphicsCommandList> list(raw_list);
  ID3D12DescriptorHeap *heaps[] = {heap, samplers}; list->SetDescriptorHeaps(static_sampler ? 1 : 2, heaps);
  list->SetGraphicsRootSignature(root);
  const UINT values[] = {99, 31};
  list->SetGraphicsRoot32BitConstants(static_sampler ? 3 : 4, 2, values, 0);
  set_root_buffers(list.get());
  list->SetGraphicsRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
  if (!static_sampler) list->SetGraphicsRootDescriptorTable(1, samplers->GetGPUDescriptorHandleForHeapStart());
  list->SetGraphicsRootDescriptorTable(static_sampler ? 1 : 2, heap->GetGPUDescriptorHandleForHeapStart());
  list->SetGraphicsRootDescriptorTable(static_sampler ? 2 : 3, heap->GetGPUDescriptorHandleForHeapStart());
  D3D12_RESOURCE_BARRIER barrier = {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {rt.get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
  list->ResourceBarrier(1, &barrier);
  D3D12_VIEWPORT viewport = {0, 0, 4, 4, 0, 1}; D3D12_RECT scissor = {0, 0, static_sampler ? 4 : 2, 4};
  list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &scissor);
  list->OMSetRenderTargets(1, &target, FALSE, nullptr);
  const float clear[] = {0.25f, 0.25f, 0.25f, 0.25f}; list->ClearRenderTargetView(target, clear, 0, nullptr);
  list->IASetPrimitiveTopology(tessellation_fixture ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST :
      D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  if (indexed) {
    const D3D12_INDEX_BUFFER_VIEW indices = {upload->GetGPUVirtualAddress() + 128, 12, DXGI_FORMAT_R32_UINT};
    list->IASetIndexBuffer(&indices);
  }
  if (indirect) {
    list->ExecuteIndirect(signature.get(), 2, upload.get(), 16, upload.get(), 112);
  } else if (indexed) {
    list->DrawIndexedInstanced(3, 1, 0, 0, 0);
  } else {
    list->DrawInstanced(3, 1, 0, 0);
  }
  if (!static_sampler) {
    wchar_t selected[32768];
    const auto length = GetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", selected, 32768);
    if (!length || length >= 32768 || !SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", nullptr)) return false;
    ID3D12DescriptorHeap *ordinary[] = {ordinary_heap.get(), ordinary_samplers.get()};
    list->SetDescriptorHeaps(2, ordinary);
    list->SetGraphicsRootDescriptorTable(0, ordinary_heap->GetGPUDescriptorHandleForHeapStart());
    list->SetGraphicsRootDescriptorTable(1, ordinary_samplers->GetGPUDescriptorHandleForHeapStart());
    list->SetGraphicsRootDescriptorTable(2, ordinary_heap->GetGPUDescriptorHandleForHeapStart());
    list->SetGraphicsRootDescriptorTable(3, ordinary_heap->GetGPUDescriptorHandleForHeapStart());
    set_root_buffers(list.get());
    scissor = {2, 0, 4, 4}; list->RSSetScissorRects(1, &scissor);
    if (indexed) list->DrawIndexedInstanced(3, 1, 0, 0, 0);
    else list->DrawInstanced(3, 1, 0, 0);
    if (!SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", selected)) return false;
  }
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &barrier);
  D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
  dst.pResource = readback.get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = {0, {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, 256}};
  src.pResource = rt.get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  if (!check(list->Close(), "draw close")) return false;
  auto *native_list = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list.get());
  const dxmt::D3D12MinMaxDispatch *private_draw = nullptr;
  std::vector<std::pair<wmtcmd_base *, void *>> links;
  for (auto *pass = native_list->entry; pass; pass = pass->next) {
    if (pass->type != dxmt::EncoderType::Render) continue;
    auto *render = static_cast<dxmt::RenderEncoderData *>(pass);
    if (!render->minmax_draws.empty()) {
      if (private_draw || render->minmax_draws.size() != 1) return false;
      private_draw = render->minmax_draws[0].get();
    }
    for (auto *node = reinterpret_cast<wmtcmd_base *>(&render->cmd_head); node;
         node = static_cast<wmtcmd_base *>(node->next.get())) links.emplace_back(node, node->next.get());
  }
  if (!private_draw || !private_draw->graphics_variant || !private_draw->render_marker) return false;
  const auto immutable_template = private_draw->argument_template;
  dxmt::IndirectRenderCommandData immutable_payload = {};
  if (updates) {
    if (!private_draw->indirect_render_data || !private_draw->indirect_render_binding) return false;
    immutable_payload = *private_draw->indirect_render_data;
  } else if (private_draw->indirect_render_data) return false;
  for (unsigned submission = 0; submission < (indirect ? 3u : 2u); ++submission) {
    if (submission) write(true);
    const bool empty = indirect && !submission;
    if (indirect) {
      if (!check(upload->Map(0, nullptr, &mapped), "count map")) return false;
      const UINT count = submission == 0 ? 0 : submission == 1 ? 1 : 7;
      std::memcpy(static_cast<uint8_t *>(mapped) + 112, &count, sizeof(count));
      upload->Unmap(0, nullptr);
    }
    if (!execute(list.get(), 3 + submission) || !check(readback->Map(0, nullptr, &mapped), "readback map")) return false;
    const bool changed = submission && live;
    const unsigned r = static_sampler ? (changed ? 32 : 16) : changed ? 224 : 16;
    const unsigned g = static_sampler ? (changed ? 224 : 240) : r;
    bool ok = true;
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
      const auto *pixel = static_cast<const uint8_t *>(mapped) + y * 256 + x * 4;
      unsigned expected_r = !static_sampler && x >= 2 ? 128u : r;
      unsigned expected_g = !static_sampler && x >= 2 ? 128u : g;
      if (implicit_fixture) {
        const bool ordinary = !static_sampler && x >= 2;
        if (implicit_bias_fixture) expected_r = expected_g = ordinary ? 96 : changed ? 144 : 96;
        else {
          const unsigned original[] = {16, 64, 192, 240}, replacement[] = {32, 80, 176, 224};
          const auto *input = ordinary || !changed ? original : replacement;
          const float u = (float(x) + 0.5f) * 0.5f - 0.5f;
          const float v = (float(y) + 0.5f) * 0.5f - 0.5f;
          const int left = int(std::floor(u)), top = int(std::floor(v));
          const auto tap = [&](int dx, int dy) {
            const unsigned cx = unsigned(std::max(0, std::min(1, left + dx)));
            const unsigned cy = unsigned(std::max(0, std::min(1, top + dy)));
            return input[cx + 2 * cy];
          };
          if (ordinary) {
            const float fx = u - left, fy = v - top;
            expected_r = expected_g = unsigned(std::lround((1 - fy) * ((1 - fx) * tap(0, 0) + fx * tap(1, 0)) +
                fy * ((1 - fx) * tap(0, 1) + fx * tap(1, 1))));
          } else {
            const unsigned minimum = std::min({tap(0, 0), tap(1, 0), tap(0, 1), tap(1, 1)});
            const unsigned maximum = std::max({tap(0, 0), tap(1, 0), tap(0, 1), tap(1, 1)});
            expected_r = static_sampler || !changed ? minimum : maximum;
            expected_g = static_sampler ? maximum : expected_r;
          }
        }
      }
      if (vertex_only_fixture) {
        expected_r = !static_sampler && x >= 2 ? 128 : static_sampler || !changed ? 16 : 240;
        expected_g = !static_sampler && x >= 2 ? 128 : static_sampler || changed ? 240 : 16;
      }
      const bool untouched = empty && (static_sampler || x < 2);
      unsigned blue = !root_updates_fixture ? 0 : !static_sampler && x >= 2 ? (updates ? 31 : 130) :
          updates ? (submission == 1 ? 38 : 42) : 130;
      if (vertex_sampling_fixture) blue = !static_sampler && x >= 2 ? 128 : static_sampler ? 184 : changed ? 240 : 16;
      if (geometry_fixture) blue = !static_sampler && x >= 2 ? 128 : static_sampler ? 184 : changed ? 240 : 16;
      if (tessellation_fixture) blue = !static_sampler && x >= 2 ? 76 : static_sampler ? 104 : changed ? 132 : 20;
      if (root_buffers_fixture) blue += updates && (static_sampler || x < 2) && submission == 2 ? 49 : 37;
      if (pixel[0] != (untouched ? 64u : expected_r) || pixel[1] != (untouched ? 64u : expected_g) ||
          pixel[2] != (untouched ? 64u : blue) || pixel[3] != (untouched ? 64u : 255u)) {
        std::printf("pixel mismatch kind=%u static=%u live=%u submit=%u at %u,%u got %u/%u/%u/%u\n",
            indirect_kind, static_sampler, live, submission, x, y, pixel[0], pixel[1], pixel[2], pixel[3]); ok = false;
      }
    }
    readback->Unmap(0, nullptr); if (!ok) return false;
    if (private_draw->argument_template != immutable_template) return false;
    if (updates && std::memcmp(private_draw->indirect_render_data, &immutable_payload, sizeof(immutable_payload))) return false;
    for (const auto &link : links) if (link.first->next.get() != link.second) return false;
  }
  // Cached PSO artifacts are pinned to the selected compiler directory.
  const dxmt::D3D12MinMaxGraphicsVariant *mismatched = private_draw->graphics_variant;
  if (static_cast<dxmt::MTLD3D12GraphicsPipelineState *>(pso)->GetMinMaxVariant(L"Z:\\mismatched-dxc", &mismatched) != E_INVALIDARG || mismatched)
    return false;
  // Updating signatures cannot inherit the submission-private TLAB.
  D3D12_INDIRECT_ARGUMENT_DESC arguments[2] = {};
  arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
  arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
  D3D12_COMMAND_SIGNATURE_DESC signature_desc = {32, 2, arguments, 0};
  ID3D12CommandSignature *raw_signature = nullptr;
  if (!check(device->CreateCommandSignature(&signature_desc, nullptr, IID_PPV_ARGS(&raw_signature)), "indirect signature")) return false;
  OwnedCOM<ID3D12CommandSignature> updating_signature(raw_signature);
  if (!check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&raw_allocator)), "negative allocator")) return false;
  OwnedCOM<ID3D12CommandAllocator> negative_allocator(raw_allocator);
  if (!check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, negative_allocator.get(), pso,
      IID_PPV_ARGS(&raw_list)), "negative list")) return false;
  OwnedCOM<ID3D12GraphicsCommandList> negative(raw_list);
  negative->SetGraphicsRootSignature(root); negative->SetDescriptorHeaps(static_sampler ? 1 : 2, heaps);
  negative->IASetPrimitiveTopology(tessellation_fixture ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST :
      D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  negative->ExecuteIndirect(updating_signature.get(), 1, upload.get(), 0, nullptr, 0);
  if (negative->Close() != E_FAIL) return false;
  std::printf("MINMAX_FRAGMENT real D3D12 graphics readback PASS kind=%u (%s)\n", indirect_kind,
      indirect ? "indirect counts 0/1/7" : "direct 2 submits");
  return true;
}

static bool CheckBinding(dxmt::MTLD3D12Device *device, D3D12_ROOT_SIGNATURE_DESC1 application,
    const dxmt::D3D12MinMaxBindingVariant &variant, D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps,
    bool static_sampler, bool live, D3D12_SHADER_BYTECODE sampling_vs) {
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {}; desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  desc.Desc_1_1 = application;
  ID3DBlob *raw_blob = nullptr;
  if (FAILED(D3D12SerializeVersionedRootSignature(&desc, &raw_blob, nullptr))) return false;
  OwnedCOM<ID3DBlob> blob(raw_blob);
  ID3D12RootSignature *raw_root = nullptr;
  if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&raw_root)))) return false;
  OwnedCOM<ID3D12RootSignature> root(raw_root);
  auto *native_root = static_cast<dxmt::MTLD3D12RootSignature *>(root.get());
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root.get(); pso_desc.VS = vs; pso_desc.PS = ps;
  if (geometry_fixture) pso_desc.GS = {geometry_shader.data(), geometry_shader.size()};
  if (tessellation_fixture) {
    pso_desc.HS = {hull_shader.data(), hull_shader.size()};
    pso_desc.DS = {domain_shader.data(), domain_shader.size()};
  }
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pso_desc.SampleMask = UINT_MAX; pso_desc.SampleDesc.Count = 1;
  pso_desc.PrimitiveTopologyType = tessellation_fixture ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH :
      D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1; pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  ID3D12PipelineState *raw_pso = nullptr;
  if (static_sampler) {
    wchar_t selected[32768];
    const auto length = GetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", selected, 32768);
    if (!length || length >= 32768 || !SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", nullptr)) return false;
    const auto hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&raw_pso));
    OwnedCOM<ID3D12PipelineState> rejected(raw_pso);
    if (!SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", selected) || hr != E_NOTIMPL || rejected) return false;
  }
  if (sampling_vs.BytecodeLength) {
    auto rejected_desc = pso_desc; rejected_desc.VS = sampling_vs;
    if (FAILED(device->CreateGraphicsPipelineState(&rejected_desc, IID_PPV_ARGS(&raw_pso)))) return false;
    OwnedCOM<ID3D12PipelineState> sampled(raw_pso);
    const dxmt::D3D12MinMaxGraphicsVariant *selected_variant = nullptr;
    wchar_t selected[32768];
    if (!GetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", selected, 32768) ||
        FAILED(static_cast<dxmt::MTLD3D12GraphicsPipelineState *>(sampled.get())->GetMinMaxVariant(selected, &selected_variant)) ||
        !selected_variant || selected_variant->bindings.size() != 3 || selected_variant->binding_stages.size() != 3 ||
        selected_variant->binding_stages[0] != dxmt::D3D12MinMaxShaderStage::Vertex)
      return false;
  }
  if (FAILED(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&raw_pso)))) return false;
  OwnedCOM<ID3D12PipelineState> pso(raw_pso);
  if (vertex_sampling_fixture) {
    wchar_t selected[32768];
    const dxmt::D3D12MinMaxGraphicsVariant *selected_variant = nullptr;
    if (!GetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", selected, 32768) ||
        FAILED(static_cast<dxmt::MTLD3D12GraphicsPipelineState *>(pso.get())->GetMinMaxVariant(selected, &selected_variant)) ||
        !selected_variant || selected_variant->bindings.size() != (vertex_only_fixture ? 2u : 4u) ||
        selected_variant->binding_stages.size() != selected_variant->bindings.size() ||
        selected_variant->binding_stages[0] != dxmt::D3D12MinMaxShaderStage::Vertex ||
        (!vertex_only_fixture && (selected_variant->binding_stages[2] != dxmt::D3D12MinMaxShaderStage::Pixel ||
        selected_variant->locations[0].texture.parameter_index == selected_variant->locations[2].texture.parameter_index))) return false;
    auto depth_only = pso_desc;
    depth_only.PS = {}; depth_only.NumRenderTargets = 0; depth_only.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    depth_only.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    depth_only.DepthStencilState.DepthEnable = TRUE;
    depth_only.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth_only.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    ID3D12PipelineState *raw_depth = nullptr;
    if (FAILED(device->CreateGraphicsPipelineState(&depth_only, IID_PPV_ARGS(&raw_depth)))) return false;
    OwnedCOM<ID3D12PipelineState> depth_pso(raw_depth);
    if (FAILED(static_cast<dxmt::MTLD3D12GraphicsPipelineState *>(depth_pso.get())->GetMinMaxVariant(selected, &selected_variant)) ||
        !selected_variant || selected_variant->bindings.size() != 2) return false;
  }
  D3D12_HEAP_PROPERTIES props = {}; props.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC texture_desc = {};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = texture_desc.Height = 2;
  texture_desc.DepthOrArraySize = texture_desc.MipLevels = texture_desc.SampleDesc.Count = 1;
  texture_desc.MipLevels = implicit_fixture ? 2 : 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  ID3D12Resource *raw_texture = nullptr;
  if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &texture_desc,
      static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
      nullptr, IID_PPV_ARGS(&raw_texture)))) return false;
  OwnedCOM<ID3D12Resource> texture(raw_texture);
  D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  ID3D12DescriptorHeap *raw_heap = nullptr, *raw_samplers = nullptr;
  if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&raw_heap)))) return false;
  OwnedCOM<ID3D12DescriptorHeap> heap(raw_heap);
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  if (!static_sampler && FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&raw_samplers)))) return false;
  OwnedCOM<ID3D12DescriptorHeap> samplers(raw_samplers);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = texture_desc.Format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = implicit_fixture ? 2 : 1;
  auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateShaderResourceView(texture.get(), &srv, cpu);
  cpu.ptr += 2 * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  device->CreateShaderResourceView(texture.get(), &srv, cpu);
  const auto set_samplers = [&](D3D12_FILTER filter) {
    if (static_sampler) return;
    auto scpu = samplers->GetCPUDescriptorHandleForHeapStart();
    const auto stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_SAMPLER_DESC sampler = {}; sampler.Filter = filter;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    for (unsigned i = 1; i <= 2; ++i) { scpu.ptr += stride; device->CreateSampler(&sampler, scpu); }
  };
  set_samplers(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR);
  if (geometry_fixture || tessellation_fixture) {
    wchar_t selected[32768];
    const dxmt::D3D12MinMaxGraphicsVariant *actual = nullptr;
    using Stage = dxmt::D3D12MinMaxShaderStage;
    if (!GetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", selected, 32768) ||
        FAILED(static_cast<dxmt::MTLD3D12GraphicsPipelineState *>(pso.get())->GetMinMaxVariant(selected, &actual)) ||
        !actual || actual->bindings.size() != (tessellation_fixture ? 6u : 4u) ||
        actual->binding_stages.size() != actual->bindings.size() ||
        actual->geometry != geometry_fixture || actual->tessellation != tessellation_fixture ||
        actual->binding_stages[0] != Stage::Pixel ||
        actual->binding_stages[2] != (tessellation_fixture ? Stage::Hull : Stage::Geometry) ||
        (tessellation_fixture && actual->binding_stages[4] != Stage::Domain)) return false;
    ID3D12Resource *raw_replacement = nullptr;
    if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &texture_desc,
        static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        nullptr, IID_PPV_ARGS(&raw_replacement)))) return false;
    OwnedCOM<ID3D12Resource> replacement(raw_replacement);
    for (const unsigned kind : {0u, 5u})
      if (!CheckDraw(device, pso.get(), root.get(), heap.get(), samplers.get(), texture.get(), replacement.get(),
              static_sampler, live, kind)) return false;
    return true;
  }
  std::vector<uint64_t> staging(native_root->UploadQwords), argument_template((variant.root.layout.argument_buffer_size + 7) / 8);
  staging[native_root->SlotQwordOffsets[0]] = heap->GetGPUDescriptorHandleForHeapStart().ptr;
  if (!static_sampler) staging[native_root->SlotQwordOffsets[1]] = samplers->GetGPUDescriptorHandleForHeapStart().ptr;
  const unsigned vertex_index = static_sampler ? 1 : 2;
  staging[native_root->SlotQwordOffsets[vertex_index]] = heap->GetGPUDescriptorHandleForHeapStart().ptr;
  // Hull-only table deliberately remains unbound and must not be captured.
  std::shared_ptr<dxmt::D3D12MinMaxDispatch> recorded;
  if (FAILED(dxmt::RecordD3D12MinMaxBinding(static_cast<dxmt::MTLD3D12PipelineState *>(pso.get()), &variant, native_root, staging.data(),
      static_cast<dxmt::MTLD3D12DescriptorHeap *>(heap.get()),
      static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(samplers.get()), argument_template.data(), recorded))) return false;
  if (recorded->variant || recorded->binding_variant != &variant || recorded->tables.size() != (static_sampler ? 2u : 3u) ||
      recorded->tables.back().parameter != vertex_index || recorded->tables.back().slots[0].live) return false;
  const auto saved_recorded = recorded;
  auto wrong_stage = variant; wrong_stage.stage = dxmt::D3D12MinMaxShaderStage::Compute;
  if (dxmt::RecordD3D12MinMaxBinding(static_cast<dxmt::MTLD3D12PipelineState *>(pso.get()), &wrong_stage,
      native_root, staging.data(), static_cast<dxmt::MTLD3D12DescriptorHeap *>(heap.get()),
      static_cast<dxmt::MTLD3D12SamplerDescriptorHeap *>(samplers.get()), argument_template.data(), recorded) != E_INVALIDARG ||
      recorded != saved_recorded) return false;
  const auto saved = recorded->argument_template;
  std::shared_ptr<dxmt::D3D12MinMaxSubmissionBinding> first, second;
  if (FAILED(dxmt::MaterializeD3D12MinMaxDispatch(device, *recorded, first))) return false;
  set_samplers(D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR);
  ID3D12Resource *raw_replacement = nullptr;
  if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &texture_desc,
      static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
      nullptr, IID_PPV_ARGS(&raw_replacement)))) return false;
  OwnedCOM<ID3D12Resource> replacement(raw_replacement);
  device->CreateShaderResourceView(replacement.get(), &srv, cpu);
  if (FAILED(dxmt::MaterializeD3D12MinMaxDispatch(device, *recorded, second)) || first->pairs.size() != 2 || second->pairs.size() != 2 ||
      first->buffer.handle == second->buffer.handle || recorded->argument_template != saved) return false;
  if (!static_sampler && (first->pairs[0].state.flags != (7u | dxmt::GetAIRSamplerReductionFlags(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR)) ||
      second->pairs[0].state.flags != (7u | dxmt::GetAIRSamplerReductionFlags(live ?
          D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR)))) return false;
  if (static_sampler && (first->pairs[0].state.flags != (7u | dxmt::GetAIRSamplerReductionFlags(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR)) ||
      second->pairs[0].state.flags != first->pairs[0].state.flags)) return false;
  if ((first->pairs[0].texture_descriptor.texture_view_id != second->pairs[0].texture_descriptor.texture_view_id) !=
          (live && !vertex_only_fixture) ||
      first->snapshots.size() != 2 || second->snapshots.size() != 2 ||
      first->snapshots.back().msc_descriptor.texture_view_id != second->snapshots.back().msc_descriptor.texture_view_id)
    return false;
  std::puts("MINMAX_FRAGMENT binding capture/materialization PASS (no draw)");
  for (unsigned kind = 0; kind < (root_updates_fixture ? 5u : 3u); ++kind)
    if (!CheckDraw(device, pso.get(), root.get(), heap.get(), samplers.get(), texture.get(), replacement.get(), static_sampler, live, kind))
      return false;
  return true;
}

int wmain(int argc, wchar_t **argv) {
  if (argc != 4 && argc != 5 && argc != 6 && argc != 7) return 1;
  if (argc >= 6) {
    const auto *option = argv[argc - 1];
    if (!std::wcscmp(option, L"--implicit-sample") ||
        !std::wcscmp(option, L"--grad-bias")) implicit_fixture = true;
    else if (!std::wcscmp(option, L"--implicit-bias") || !std::wcscmp(option, L"--level-bias"))
      implicit_fixture = implicit_bias_fixture = true;
    else if (!std::wcscmp(option, L"--root-buffers")) root_buffers_fixture = true;
    else if (!std::wcscmp(option, L"--vertex-sampling")) vertex_sampling_fixture = true;
    else if (!std::wcscmp(option, L"--vertex-only")) vertex_sampling_fixture = vertex_only_fixture = true;
    else if (!std::wcscmp(option, L"--vertex-shared")) vertex_sampling_fixture = vertex_shared_fixture = true;
    else if (!std::wcscmp(option, L"--geometry") && argc == 6) geometry_fixture = true;
    else if (!std::wcscmp(option, L"--tessellation") && argc == 7) tessellation_fixture = true;
    else if (std::wcscmp(option, L"--root-updates")) return 1;
    if (argc == 7 && !tessellation_fixture) return 1;
    root_updates_fixture = !implicit_fixture && !vertex_sampling_fixture && !geometry_fixture && !tessellation_fixture;
  }
  if (!SetEnvironmentVariableW(L"DXMT_MINMAX_DXC_DIRECTORY", argv[3])) return 1;
  std::vector<uint8_t> ps, vs, sampling_vs;
  if (!Load(argv[1], ps) || !Load(argv[2], vs)) return 1;
  if (geometry_fixture && !Load(argv[4], geometry_shader)) return 1;
  if (tessellation_fixture && (!Load(argv[4], hull_shader) || !Load(argv[5], domain_shader))) return 1;
  if (argc >= 5 && !geometry_fixture && !tessellation_fixture && (!Load(argv[4], sampling_vs) ||
      !dxmt::ClassifyD3D12Shader({sampling_vs.data(), sampling_vs.size()}).uses_texture_sampling)) return 1;
  const D3D12_SHADER_BYTECODE pixel = {ps.data(), ps.size()}, vertex = {vs.data(), vs.size()};
  if (dxmt::ClassifyD3D12Shader(pixel).uses_texture_sampling == vertex_only_fixture ||
      dxmt::ClassifyD3D12Shader(vertex).uses_texture_sampling != vertex_sampling_fixture) return 1;
  dxmt::D3D12MinMaxShader prepared;
  std::string error;
  using Stage = dxmt::D3D12MinMaxShaderStage;
  const auto sampled_stage = vertex_only_fixture ? Stage::Vertex : Stage::Pixel;
  const auto sampled_input = vertex_only_fixture ? vertex : pixel;
  auto hr = dxmt::PrepareD3D12MinMaxShader(sampled_input, argv[3], prepared, error, sampled_stage);
  if (FAILED(hr) || prepared.stage != sampled_stage || prepared.bindings.size() != 2) {
    std::printf("pixel preparation failed %08lx %s\n", (unsigned long)hr, error.c_str()); return 1;
  }
  const auto saved = prepared;
  const auto unchanged = [&] {
    return prepared.stage == saved.stage && prepared.bytecode == saved.bytecode && prepared.bindings.size() == 2 &&
        prepared.pair_offset == saved.pair_offset && prepared.pair_count == saved.pair_count &&
        !std::memcmp(prepared.bindings.data(), saved.bindings.data(), 2 * sizeof(saved.bindings[0]));
  };
  if (dxmt::PrepareD3D12MinMaxShader(sampled_input, argv[3], prepared, error) != E_NOTIMPL || !unchanged() ||
      dxmt::PrepareD3D12MinMaxShader(vertex_only_fixture ? pixel : vertex, argv[3], prepared, error, sampled_stage) != E_NOTIMPL || !unchanged() ||
      dxmt::PrepareD3D12MinMaxShader(pixel, argv[3], prepared, error, static_cast<Stage>(99)) != E_INVALIDARG || !unchanged())
    return 1;
  if (dxmt::PrepareD3D12MinMaxShader(sampled_input, argv[3], prepared, error, sampled_stage, 64, 64) != E_INVALIDARG || !unchanged() ||
      dxmt::PrepareD3D12MinMaxShader(sampled_input, argv[3], prepared, error, sampled_stage, 1, 2) != E_NOTIMPL || !unchanged()) return 1;
  if (vertex_sampling_fixture) {
    dxmt::D3D12MinMaxShader prepared_vertex;
    if (FAILED(dxmt::PrepareD3D12MinMaxShader(vertex, argv[3], prepared_vertex, error, Stage::Vertex, 0, 4)) ||
        prepared_vertex.stage != Stage::Vertex || prepared_vertex.bindings.size() != 2 ||
        prepared_vertex.pair_offset != 0 || prepared_vertex.pair_count != 4) return 1;
  }
  ID3D12Device *raw_device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&raw_device)))) return 1;
  OwnedCOM<ID3D12Device> device(raw_device);
  auto *native = static_cast<dxmt::MTLD3D12Device *>(device.get());
  auto metal = native->GetMTLDevice();
  for (unsigned mode = 0; mode < 4; ++mode) {
    // ALL and stage-specific duplicate registers are invalid root signatures.
    if (vertex_shared_fixture && !(mode & 1)) continue;
    const bool static_sampler = mode & 2;
    const auto visibility = mode & 1 ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_ALL;
    const auto flags = mode & 1 ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE : D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    D3D12_DESCRIPTOR_RANGE1 ranges[] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
            static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(flags | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE), 2},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 0, flags, 1},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 6, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 7, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0}};
    D3D12_ROOT_PARAMETER1 parameters[8] = {};
    if (vertex_shared_fixture) ranges[2].RegisterSpace = 0;
    for (unsigned i = 0; i < 2; ++i) {
      parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[i].DescriptorTable = {1, ranges + i}; parameters[i].ShaderVisibility = visibility;
    }
    if (vertex_sampling_fixture || geometry_fixture || tessellation_fixture)
      parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    const unsigned vertex_index = static_sampler ? 1 : 2;
    parameters[vertex_index].ParameterType = parameters[vertex_index + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[vertex_index].DescriptorTable = {1, ranges + 2};
    parameters[vertex_index].ShaderVisibility = geometry_fixture ? D3D12_SHADER_VISIBILITY_GEOMETRY :
        tessellation_fixture ? D3D12_SHADER_VISIBILITY_HULL : D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[vertex_index + 1].DescriptorTable = {1, ranges + 3};
    parameters[vertex_index + 1].ShaderVisibility = tessellation_fixture ? D3D12_SHADER_VISIBILITY_DOMAIN :
        D3D12_SHADER_VISIBILITY_HULL;
    parameters[vertex_index + 2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[vertex_index + 2].Constants = {0, 8, 2};
    parameters[vertex_index + 2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    if (root_buffers_fixture) {
      for (unsigned i = 0; i < 3; ++i) {
        auto &parameter = parameters[vertex_index + 3 + i];
        parameter.ParameterType = static_cast<D3D12_ROOT_PARAMETER_TYPE>(D3D12_ROOT_PARAMETER_TYPE_CBV + i);
        parameter.Descriptor = {1, 8, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
      }
    }
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    for (unsigned i = 0; i < 2; ++i) {
      samplers[i].Filter = i ? D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR;
      samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      samplers[i].MaxLOD = D3D12_FLOAT32_MAX; samplers[i].ShaderRegister = i; samplers[i].ShaderVisibility = visibility;
      if (vertex_sampling_fixture || geometry_fixture || tessellation_fixture)
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      samplers[i].MipLODBias = implicit_fixture ? 1 : 0;
    }
    D3D12_ROOT_SIGNATURE_DESC1 application = {(static_sampler ? 4u : 5u) + (root_buffers_fixture ? 3u : 0u), parameters,
        static_sampler ? 2u : 0u, samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    dxmt::D3D12MinMaxRoot root;
    if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, 2, root, error))) return 1;
    if (geometry_fixture || tessellation_fixture) {
      const auto count = tessellation_fixture ? 6u : 4u;
      dxmt::D3D12MinMaxRoot shared;
      if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, count, shared, error))) return 1;
      for (unsigned i = 0; i < (tessellation_fixture ? 2u : 1u); ++i) {
        const auto kind = geometry_fixture ? Stage::Geometry : i ? Stage::Domain : Stage::Hull;
        const auto &bytes = geometry_fixture ? geometry_shader : i ? domain_shader : hull_shader;
        dxmt::D3D12MinMaxShader stage_shader;
        const D3D12_SHADER_BYTECODE input = {bytes.data(), bytes.size()};
        if (FAILED(dxmt::PrepareD3D12MinMaxShader(input, argv[3], stage_shader, error, kind, 2 + 2 * i, count)) ||
            stage_shader.stage != kind || stage_shader.pair_offset != 2 + 2 * i ||
            stage_shader.pair_count != count || stage_shader.bindings.size() != 2) return 1;
        std::vector<dxmt::D3D12MinMaxPairLocation> stage_locations;
        if (FAILED(dxmt::ResolveD3D12MinMaxBindings(shared, stage_shader.bindings, stage_locations, error, kind)) ||
            stage_locations.size() != 2 || stage_locations[0].texture.parameter_index != vertex_index + i) return 1;
        const auto saved_stage_locations = stage_locations;
        auto denied_application = application;
        denied_application.Flags = geometry_fixture ? D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS :
            i ? D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS : D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;
        dxmt::D3D12MinMaxRoot denied_stage;
        if (FAILED(dxmt::PrepareD3D12MinMaxRoot(denied_application, count, denied_stage, error)) ||
            dxmt::ResolveD3D12MinMaxBindings(denied_stage, stage_shader.bindings, stage_locations, error, kind) != E_NOTIMPL ||
            stage_locations.size() != saved_stage_locations.size() ||
            std::memcmp(stage_locations.data(), saved_stage_locations.data(), stage_locations.size() * sizeof(stage_locations[0]))) return 1;
        const auto saved_visibility = parameters[vertex_index + i].ShaderVisibility;
        parameters[vertex_index + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        dxmt::D3D12MinMaxRoot wrong_stage;
        if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, count, wrong_stage, error)) ||
            dxmt::ResolveD3D12MinMaxBindings(wrong_stage, stage_shader.bindings, stage_locations, error, kind) != E_NOTIMPL ||
            stage_locations.size() != saved_stage_locations.size() ||
            std::memcmp(stage_locations.data(), saved_stage_locations.data(), stage_locations.size() * sizeof(stage_locations[0]))) return 1;
        parameters[vertex_index + i].ShaderVisibility = saved_visibility;
      }
    }
    std::vector<dxmt::D3D12MinMaxPairLocation> locations;
    if (FAILED(dxmt::ResolveD3D12MinMaxBindings(root, prepared.bindings, locations, error, sampled_stage)) ||
        locations.size() != 2 || locations[0].texture.parameter_index != (vertex_only_fixture ? vertex_index : 0) ||
        locations[0].texture.table_offset != (vertex_only_fixture ? 0u : 2u) ||
        (static_sampler ? locations[1].sampler.static_sampler_index != 1 : locations[1].sampler.table_offset != 2)) return 1;
    const auto saved_locations = locations;
    if ((mode & 1) && (dxmt::ResolveD3D12MinMaxBindings(root, prepared.bindings, locations, error) != E_NOTIMPL ||
        locations.size() != saved_locations.size() || locations[0].texture.table_offset != (vertex_only_fixture ? 0u : 2u))) return 1;
    dxmt::D3D12ConvertedShader converted_ps, converted_vs;
    if (dxmt::ConvertD3D12MinMaxComputeShader(prepared, root, converted_ps, &native->GetMSCCapabilities()) != E_INVALIDARG)
      return 1;
    hr = dxmt::ConvertD3D12MinMaxShader(prepared, root, converted_ps, &native->GetMSCCapabilities());
    if (FAILED(hr) || converted_ps.metallib.empty() || converted_ps.entry_point.empty()) {
      std::printf("pixel conversion failed %08lx\n", (unsigned long)hr); return 1;
    }
    auto mislabeled = prepared; mislabeled.stage = Stage::Compute;
    const auto saved_metallib = converted_ps.metallib;
    const auto saved_entry = converted_ps.entry_point;
    if (SUCCEEDED(dxmt::ConvertD3D12MinMaxShader(mislabeled, root, converted_ps, &native->GetMSCCapabilities())) ||
        converted_ps.metallib != saved_metallib || converted_ps.entry_point != saved_entry) return 1;
    if (vertex_only_fixture) {
      converted_vs = std::move(converted_ps);
      hr = dxmt::ConvertD3D12Shader(pixel, DXMT_MSC_STAGE_FRAGMENT, converted_ps,
          root.layout.bytecode.data(), root.layout.bytecode.size(), nullptr, 0, &native->GetMSCCapabilities());
    } else hr = dxmt::ConvertD3D12Shader(vertex, DXMT_MSC_STAGE_VERTEX, converted_vs,
        root.layout.bytecode.data(), root.layout.bytecode.size(), nullptr, 0, &native->GetMSCCapabilities());
    if (FAILED(hr)) return 1;
    WMT::Error metal_error;
    auto ps_lib = metal.newLibrary(converted_ps.metallib.data(), converted_ps.metallib.size(), metal_error);
    auto vs_lib = metal.newLibrary(converted_vs.metallib.data(), converted_vs.metallib.size(), metal_error);
    if (!ps_lib || !vs_lib) return 1;
    auto ps_function = ps_lib.newFunction(converted_ps.entry_point.c_str());
    auto vs_function = vs_lib.newFunction(converted_vs.entry_point.c_str());
    if (!ps_function || !vs_function) return 1;
    WMTRenderPipelineInfo info; WMT::InitializeRenderPipelineInfo(info);
    info.vertex_function = vs_function.handle; info.fragment_function = ps_function.handle;
    info.colors[0].pixel_format = WMTPixelFormatRGBA8Unorm;
    auto pso = metal.newRenderPipelineState(info, metal_error);
    if (!pso) {
      const auto message = metal_error ? metal_error.description().getUTF8String() : "unknown";
      std::printf("native render PSO failed: %s\n", message.c_str()); return 1;
    }
    dxmt::D3D12MinMaxBindingVariant binding_variant;
    binding_variant.stage = Stage::Pixel; binding_variant.root = root;
    binding_variant.bindings = prepared.bindings; binding_variant.locations = saved_locations;
    if (vertex_only_fixture) binding_variant.binding_stages.assign(2, Stage::Vertex);
    if (!CheckBinding(native, application, binding_variant, vertex, pixel, static_sampler, mode & 1,
        mode == 0 && !vertex_sampling_fixture && !geometry_fixture && !tessellation_fixture ?
            D3D12_SHADER_BYTECODE{sampling_vs.data(), sampling_vs.size()} : D3D12_SHADER_BYTECODE{})) return 1;
    const auto unchanged_locations = [&] {
      return locations.size() == saved_locations.size() &&
          !std::memcmp(locations.data(), saved_locations.data(), locations.size() * sizeof(locations[0]));
    };
    if (dxmt::ResolveD3D12MinMaxBindings(root, prepared.bindings, locations, error,
        static_cast<Stage>(99)) != E_INVALIDARG || !unchanged_locations()) return 1;
    application.Flags = vertex_only_fixture ? D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS :
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
    dxmt::D3D12MinMaxRoot denied;
    if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, 2, denied, error)) ||
        dxmt::ResolveD3D12MinMaxBindings(denied, prepared.bindings, locations, error, sampled_stage) != E_NOTIMPL ||
        !unchanged_locations()) return 1;
    application.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    const auto wrong_stage_visibility = vertex_only_fixture ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_VERTEX;
    if (static_sampler) samplers[0].ShaderVisibility = wrong_stage_visibility;
    else parameters[1].ShaderVisibility = wrong_stage_visibility;
    dxmt::D3D12MinMaxRoot wrong_visibility;
    if (FAILED(dxmt::PrepareD3D12MinMaxRoot(application, 2, wrong_visibility, error)) ||
        dxmt::ResolveD3D12MinMaxBindings(wrong_visibility, prepared.bindings, locations, error, sampled_stage) != E_NOTIMPL ||
        !unchanged_locations()) return 1;
    std::printf("MINMAX_FRAGMENT mode=%u compiler/binding GPU draw PASS (pre-raster=%u)\n", mode,
        geometry_fixture ? 1 : tessellation_fixture ? 2 : 0);
  }
  std::puts("MinMax graphics integration PASS; full graphics qualification remains open");
  return 0;
}

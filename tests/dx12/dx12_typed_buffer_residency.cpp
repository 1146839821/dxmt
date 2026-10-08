#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "d3d12_device.hpp"
#include <iostream>

template <typename T> struct Owned {
  T *p = nullptr;
  Owned() = default;
  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
  ~Owned() { if (p) p->Release(); }
};

int main() {
  Owned<ID3D12Device> device;
  Owned<ID3D12Resource> buffer;
  Owned<ID3D12DescriptorHeap> heap;
  Owned<ID3D12CommandAllocator> allocator;
  Owned<ID3D12GraphicsCommandList> list;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device.p)))) return 1;
  D3D12_HEAP_PROPERTIES properties = {}; properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  properties.CreationNodeMask = properties.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC resource = {};
  resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; resource.Width = 2048;
  resource.Height = resource.DepthOrArraySize = resource.MipLevels = resource.SampleDesc.Count = 1;
  resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (FAILED(device.p->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &resource,
      D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer.p)))) return 1;
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; desc.NumDescriptors = 1;
  desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device.p->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap.p))) ||
      FAILED(device.p->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.p))) ||
      FAILED(device.p->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.p, nullptr, IID_PPV_ARGS(&list.p))) ||
      FAILED(list.p->Close())) return 1;
  auto *native_list = static_cast<dxmt::MTLD3D12GraphicsCommandList *>(list.p);
  auto *native_heap = static_cast<dxmt::MTLD3D12DescriptorHeap *>(heap.p);
  const auto cpu = heap.p->GetCPUDescriptorHandleForHeapStart();
  auto write = [&](bool uav, unsigned first, ID3D12Resource *target = nullptr) {
    if (!target) target = buffer.p;
    if (uav) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
      view.Format = DXGI_FORMAT_R32_UINT; view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      view.Buffer.FirstElement = first; view.Buffer.NumElements = 8;
      device.p->CreateUnorderedAccessView(target, nullptr, &view, cpu);
    } else {
      D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
      view.Format = DXGI_FORMAT_R32_UINT; view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      view.Buffer.FirstElement = first; view.Buffer.NumElements = 8;
      device.p->CreateShaderResourceView(target, &view, cpu);
    }
  };
  // Exercise the actual resolver called by queue translation. No GPU work is
  // submitted here; shader execution is independently covered by the format fixture.
  for (bool uav : {false, true}) {
    for (bool msc : {false, true}) {
      dxmt::EncoderData encoder = {dxmt::EncoderType::Compute, nullptr, 1};
      encoder.pending_descriptor_uses.push_back({native_heap, 0,
          uav ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
          false, true, static_cast<WMTRenderStages>(0), msc, true});
      for (unsigned final_first : {16u, 1u, 16u}) {
        write(uav, final_first);
        unsigned uses = 0;
        std::vector<WMT::Reference<WMT::Resource>> submission_resources;
        const bool accepted = native_list->ResolvePendingDescriptorUses(&encoder, submission_resources,
            [&](obj_handle_t, WMTResourceUsage, WMTRenderStages) { ++uses; });
        if (accepted != (!msc || final_first == 16) || (accepted && uses < 2)) return 1;
        if (!encoder.resource_refs.empty()) {
          std::cerr << "live resolution appended execution references to recording encoder\n";
          return 1;
        }
      }
      // Static uses have no pending descriptors and never reread a later slot.
      encoder.pending_descriptor_uses.clear(); write(uav, 1);
      std::vector<WMT::Reference<WMT::Resource>> static_submission_resources;
      if (!native_list->ResolvePendingDescriptorUses(&encoder, static_submission_resources,
          [](obj_handle_t, WMTResourceUsage, WMTRenderStages) { ExitProcess(1); })) return 1;
    }
    for (bool msc_first : {false, true}) {
      dxmt::EncoderData encoder = {dxmt::EncoderType::Compute, nullptr, 2};
      for (bool msc : {msc_first, !msc_first})
        encoder.pending_descriptor_uses.push_back({native_heap, 0,
            uav ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            false, true, static_cast<WMTRenderStages>(0), msc, true});
      write(uav, 1);
      std::vector<WMT::Reference<WMT::Resource>> rejected_submission_resources;
      if (native_list->ResolvePendingDescriptorUses(&encoder, rejected_submission_resources,
          [](obj_handle_t, WMTResourceUsage, WMTRenderStages) {})) return 1;
    }
  }
  // Separate execution owners must not overwrite an earlier observation, and
  // repeated execution must leave recording references and pending uses intact.
  for (bool msc : {false, true}) {
    Owned<ID3D12Resource> replacement;
    if (FAILED(device.p->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &resource,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&replacement.p)))) return 1;
    dxmt::EncoderData encoder = {dxmt::EncoderType::Render, nullptr, 3};
    const auto object = static_cast<WMTRenderStages>(WMTRenderStageObject);
    const auto mesh = static_cast<WMTRenderStages>(WMTRenderStageMesh);
    encoder.pending_descriptor_uses.push_back({native_heap, 0,
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, false, false, object, msc, true});
    encoder.pending_descriptor_uses.push_back({native_heap, 0,
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, false, false, mesh, msc, true});
    auto resolve = [&](std::vector<WMT::Reference<WMT::Resource>> &references) {
      unsigned calls = 0;
      WMTRenderStages stages = static_cast<WMTRenderStages>(0);
      const bool accepted = native_list->ResolvePendingDescriptorUses(&encoder, references,
          [&](obj_handle_t, WMTResourceUsage, WMTRenderStages observed) {
            ++calls;
            stages = static_cast<WMTRenderStages>(stages | observed);
          });
      return accepted && references.size() == 2 && calls == 4 && stages == (object | mesh);
    };
    write(false, 16);
    std::vector<WMT::Reference<WMT::Resource>> first_execution;
    if (!resolve(first_execution) || !encoder.resource_refs.empty()) return 1;
    const auto first_view = first_execution.back().handle;
    encoder.resource_refs.push_back(first_execution.front()); // Recording-time sentinel.
    const auto recorded = encoder.resource_refs.front().handle;
    for (unsigned iteration = 0; iteration < 32; ++iteration) {
      write(false, 16, replacement.p);
      std::vector<WMT::Reference<WMT::Resource>> next_execution;
      if (!resolve(next_execution) || next_execution.back().handle == first_view ||
          first_execution.size() != 2 || first_execution.back().handle != first_view ||
          encoder.resource_refs.size() != 1 || encoder.resource_refs.front().handle != recorded ||
          encoder.pending_descriptor_uses.size() != 2) return 1;
    }
    std::vector<WMT::Reference<WMT::Resource>> last_execution;
    if (!resolve(last_execution)) return 1;
    const auto last_view = last_execution.back().handle;
    // Destroying one execution's owner must not clear another execution or the
    // reusable recording. This is resolver ownership coverage, not GPU timing.
    first_execution.clear();
    if (last_execution.size() != 2 || last_execution.back().handle != last_view ||
        encoder.resource_refs.size() != 1 || encoder.pending_descriptor_uses.size() != 2) return 1;
  }
  std::cout << "typed buffer submission residency contracts passed\n";
  return 0;
}

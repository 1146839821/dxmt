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
  auto write = [&](bool uav, unsigned first) {
    if (uav) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
      view.Format = DXGI_FORMAT_R32_UINT; view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      view.Buffer.FirstElement = first; view.Buffer.NumElements = 8;
      device.p->CreateUnorderedAccessView(buffer.p, nullptr, &view, cpu);
    } else {
      D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
      view.Format = DXGI_FORMAT_R32_UINT; view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      view.Buffer.FirstElement = first; view.Buffer.NumElements = 8;
      device.p->CreateShaderResourceView(buffer.p, &view, cpu);
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
        const bool accepted = native_list->ResolvePendingDescriptorUses(&encoder,
            [&](obj_handle_t, WMTResourceUsage, WMTRenderStages) { ++uses; });
        if (accepted != (!msc || final_first == 16) || (accepted && uses < 2)) return 1;
      }
      // Static uses have no pending descriptors and never reread a later slot.
      encoder.pending_descriptor_uses.clear(); write(uav, 1);
      if (!native_list->ResolvePendingDescriptorUses(&encoder,
          [](obj_handle_t, WMTResourceUsage, WMTRenderStages) { ExitProcess(1); })) return 1;
    }
    for (bool msc_first : {false, true}) {
      dxmt::EncoderData encoder = {dxmt::EncoderType::Compute, nullptr, 2};
      for (bool msc : {msc_first, !msc_first})
        encoder.pending_descriptor_uses.push_back({native_heap, 0,
            uav ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            false, true, static_cast<WMTRenderStages>(0), msc, true});
      write(uav, 1);
      if (native_list->ResolvePendingDescriptorUses(&encoder,
          [](obj_handle_t, WMTResourceUsage, WMTRenderStages) {})) return 1;
    }
  }
  std::cout << "typed buffer submission residency contracts passed\n";
  return 0;
}

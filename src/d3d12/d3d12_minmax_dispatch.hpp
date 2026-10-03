#pragma once

#include "d3d12_device.hpp"
#include "d3d12_minmax_binding.hpp"
#include "d3d12_minmax_pipeline.hpp"

namespace dxmt {
struct IndirectComputeCommandData;

struct D3D12MinMaxDispatch {
  struct Slot {
    UINT index = 0;
    D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    bool live = false;
    bool populated = false;
    ShaderVisibleDescriptorSnapshot resource;
    SamplerDescriptorSnapshot sampler;
  };
  struct Table {
    uint32_t parameter = 0;
    bool sampler = false;
    std::vector<Slot> slots;
  };
  const wmtcmd_compute_nop *marker = nullptr;
  Com<MTLD3D12ComputePipelineState> application_pso;
  Com<MTLD3D12RootSignature> application_root;
  Com<MTLD3D12DescriptorHeap> heap;
  Com<MTLD3D12SamplerDescriptorHeap> sampler_heap;
  const D3D12MinMaxComputeVariant *variant = nullptr;
  std::vector<uint8_t> argument_template;
  std::vector<Table> tables;
  std::vector<D3D12_SAMPLER_DESC> static_samplers;
  std::vector<dxmt_msc_descriptor_entry> static_entries;
  // Borrowed immutable allocator payload/node; replay clones into submission storage.
  const IndirectComputeCommandData *indirect_data = nullptr;
  const wmtcmd_compute_setbuffer *indirect_data_binding = nullptr;
};

struct D3D12MinMaxSubmissionBinding {
  struct ResourceUse {
    WMT::Reference<WMT::Resource> resource;
    WMTResourceUsage usage;
  };
  WMT::Reference<WMT::Buffer> buffer;
  uint64_t indirect_data_offset = 0;
  Com<MTLD3D12RootSignature> application_root;
  std::vector<ResourceUse> resources;
  std::vector<ShaderVisibleDescriptorSnapshot> snapshots;
  std::vector<SamplerDescriptorSnapshot> samplers;
  std::vector<D3D12MinMaxPairBinding> pairs;
};

HRESULT RecordD3D12MinMaxDispatch(MTLD3D12ComputePipelineState *pso, const D3D12MinMaxComputeVariant *variant,
    MTLD3D12RootSignature *root, const uint64_t *staging, MTLD3D12DescriptorHeap *heap,
    MTLD3D12SamplerDescriptorHeap *samplers, const void *argument_template,
    std::shared_ptr<D3D12MinMaxDispatch> &dispatch);
HRESULT MaterializeD3D12MinMaxDispatch(MTLD3D12Device *device, const D3D12MinMaxDispatch &dispatch,
    std::shared_ptr<D3D12MinMaxSubmissionBinding> &binding);

} // namespace dxmt

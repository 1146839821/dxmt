#pragma once

#include "d3d12_device.hpp"
#include "d3d12_descriptor_heap.hpp"
#include "d3d12_typed_origin_pipeline.hpp"

namespace dxmt {
struct IndirectComputeCommandData;

struct D3D12TypedOriginDispatch {
  struct Slot {
    UINT index = 0;
    D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    bool live = false;
    bool populated = false;
    ShaderVisibleDescriptorSnapshot snapshot;
  };
  struct Table {
    uint32_t parameter = 0;
    std::vector<Slot> slots;
  };
  const wmtcmd_compute_nop *marker = nullptr;
  const wmtcmd_render_nop *render_marker = nullptr;
  Com<MTLD3D12PipelineState> application_pso;
  Com<MTLD3D12RootSignature> application_root;
  Com<MTLD3D12SamplerDescriptorHeap> sampler_heap;
  Com<MTLD3D12DescriptorHeap> heap;
  const D3D12TypedOriginBindingVariant *variant = nullptr;
  const D3D12TypedOriginComputeVariant *compute_variant = nullptr;
  const D3D12TypedOriginGraphicsVariant *graphics_variant = nullptr;
  std::vector<uint8_t> argument_template;
  std::vector<Table> tables;
  // Borrowed allocator-owned immutable resolver payload/node. Submission replay
  // clones the payload into its own binding buffer; never patches these objects.
  const IndirectComputeCommandData *indirect_data = nullptr;
  const wmtcmd_compute_setbuffer *indirect_data_binding = nullptr;
};

struct D3D12TypedOriginSubmissionBinding {
  struct ResourceUse {
    WMT::Reference<WMT::Resource> resource;
    WMTResourceUsage usage;
  };
  WMT::Reference<WMT::Buffer> buffer;
  std::vector<ResourceUse> resources;
  std::vector<ShaderVisibleDescriptorSnapshot> snapshots;
  uint64_t indirect_data_offset = 0;
  uint64_t fragment_argument_offset = 0;
};

HRESULT RecordD3D12TypedOriginDispatch(
    MTLD3D12ComputePipelineState *pso, const D3D12TypedOriginComputeVariant *variant,
    MTLD3D12RootSignature *application_root, const uint64_t *staging, MTLD3D12DescriptorHeap *heap,
    const void *argument_template, std::shared_ptr<D3D12TypedOriginDispatch> &dispatch);

HRESULT RecordD3D12TypedOriginDraw(
    MTLD3D12GraphicsPipelineState *pso, const D3D12TypedOriginGraphicsVariant *variant,
    MTLD3D12RootSignature *application_root, const uint64_t *staging, MTLD3D12DescriptorHeap *heap,
    const void *argument_template, std::shared_ptr<D3D12TypedOriginDispatch> &dispatch);

HRESULT MaterializeD3D12TypedOriginDispatch(
    MTLD3D12Device *device, const D3D12TypedOriginDispatch &dispatch,
    std::shared_ptr<D3D12TypedOriginSubmissionBinding> &binding);

} // namespace dxmt

#pragma once

#include "d3d12_device.hpp"

namespace dxmt {

struct D3D12RaytracingDispatchState {
  WMT::Reference<WMT::ComputePipelineState> compute_pipeline;
  WMT::Reference<WMT::VisibleFunctionTable> visible_function_table;
  WMT::Reference<WMT::IntersectionFunctionTable> intersection_function_table;
  MTLD3D12RootSignature *global_root_signature = nullptr;
};

class D3D12RaytracingStateObjectExt {
public:
  virtual ~D3D12RaytracingStateObjectExt() = default;
  virtual HRESULT GetDispatchState(D3D12RaytracingDispatchState &state) = 0;
};

HRESULT
CreateD3D12RaytracingStateObject(
    MTLD3D12Device *device, const D3D12_STATE_OBJECT_DESC *desc, REFIID riid, void **state_object
);

HRESULT
AddD3D12RaytracingStateObject(
    MTLD3D12Device *device, const D3D12_STATE_OBJECT_DESC *addition, ID3D12StateObject *state_object_to_grow_from,
    REFIID riid, void **state_object
);

HRESULT
GetD3D12RaytracingDispatchState(ID3D12StateObject *state_object, D3D12RaytracingDispatchState &state);

} // namespace dxmt

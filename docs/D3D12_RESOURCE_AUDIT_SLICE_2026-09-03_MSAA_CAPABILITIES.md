# D3D12 Resource Audit Slice: Per-Format MSAA Capabilities

Status: Complete
Date: 2026-09-03
Scope: Multisample texture creation and quality queries.

## Contract

- A texture with `SampleDesc.Count > 1` requires both device-level sample-count
  support and `FormatCapability::MSAA` for its mapped Metal format.
- `D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS` reports a quality level only when
  both conditions are met.
- The same format capability boundary applies to committed, placed, reserved,
  and allocation-info texture paths through the shared texture-info helper.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
  - Extends texture capability validation to multisample formats.
- `src/d3d12/d3d12_device.cpp`
  - Requires the mapped format's MSAA capability in the quality-level query.
- `tests/dx12/dx12_resource_tests.cpp`
  - Checks feature-query consistency and rejects a render target whose mapped
    format lacks MSAA capability when the active device exposes that case.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Records this completed validation slice.

## Validation

- Private and no-private x64 resource runners.
- Private and no-private x64 feature-support runners.
- `git diff --check`.

## Follow-Up

Continue with the remaining resource-helper range and alignment contracts. Do
not raise the advertised tiled-resource tier or format capability to make a
test pass.

# D3D12 Resource Audit Slice: Texture Format Capabilities

Status: Complete
Date: 2026-09-03
Scope: Format capabilities required by texture resource flags.

## Contract

- `D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET` requires the mapped Metal format to
  have `FormatCapability::Color`.
- `D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL` requires
  `FormatCapability::DepthStencil`.
- `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS` requires
  `FormatCapability::Write`.
- The capability check runs after D3D12 depth-compatible formats are normalized
  to their Metal depth format.
- The shared check covers committed, placed, reserved, and allocation-info
  texture paths without changing the advertised typed-UAV capability.

## Changed Files

- `src/d3d12/d3d12_device.hpp`
  - Exposes the shared capability validator and passes the D3D12 device through
    the texture-info helper.
- `src/d3d12/d3d12_resource_helper.cpp`
  - Validates resource flags against mapped Metal format capabilities.
- `src/d3d12/d3d12_texture.cpp`
  - Applies capability validation after depth-format normalization.
- `src/d3d12/d3d12_device.cpp`
  - Uses the device-aware texture-info path for allocation-info queries.
- `tests/dx12/dx12_resource_tests.cpp`
  - Covers incompatible RT, DS, and UAV format/flag combinations in resource
    creation, reserved texture creation, and allocation-info queries.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Records this completed validation slice.

## Validation

- Private and no-private x64 resource runners.
- `git diff --check`.

## Follow-Up

Remaining resource-helper range and alignment contracts remain separate. Do not
raise the advertised tiled-resource tier or typed-UAV capability to make a test
pass.

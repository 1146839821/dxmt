# D3D12 Resource Audit Slice: Initial-State Resource Flags

Status: Complete
Date: 2026-09-03
Scope: Initial resource states that require render-target or depth-stencil usage.

## Contract

`D3D12_RESOURCE_STATE_RENDER_TARGET` requires
`D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET`. `D3D12_RESOURCE_STATE_DEPTH_READ`
and `D3D12_RESOURCE_STATE_DEPTH_WRITE` require
`D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL`. These checks apply consistently to
committed, placed, and reserved resources.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
  - Extends shared initial-state validation with the resource description.
  - Rejects state/flag mismatches before resource allocation.
- `src/d3d12/d3d12_device.hpp`
  - Updates the shared validation declaration.
- `src/d3d12/d3d12_device.cpp`
  - Passes resource descriptions from committed and placed creation paths.
- `src/d3d12/d3d12_buffer.cpp`
  - Passes the resource description from reserved-buffer creation.
- `src/d3d12/d3d12_texture.cpp`
  - Passes the resource description from reserved-texture creation.
- `tests/dx12/dx12_resource_tests.cpp`
  - Covers invalid render-target and depth-write initial states for committed,
    placed, and reserved resources.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Records this completed contract slice.

## Validation

- Private and no-private x64 resource runners.
- `git diff --check`.

## Follow-Up

Audit format capability requirements for render-target, depth-stencil, and UAV
resource flags separately.

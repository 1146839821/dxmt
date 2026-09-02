# D3D12 Resource Audit Slice: Buffer Flags

Status: Complete
Date: 2026-09-03
Scope: Buffer resource descriptor flags at creation and allocation-info boundaries.

## Contract

Buffer descriptors retain support for buffer-relevant flags such as unordered
access and cross-adapter compatibility checks. They reject flags whose resource
semantics are texture-only or require depth-stencil resources:

- `D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET`
- `D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL`
- `D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE`

The shared `IsValidBufferResourceDesc` validator now enforces this for both
resource creation and `GetResourceAllocationInfo1`.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
- `tests/dx12/dx12_resource_tests.cpp`
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Validation

- `git diff --check`
- Private and no-private x64 builds.
- Private and no-private `dx12_resource_tests.exe` runs.

Both resource runners reported `D3D12 allocation and placed resource tests
passed` and returned status 0.

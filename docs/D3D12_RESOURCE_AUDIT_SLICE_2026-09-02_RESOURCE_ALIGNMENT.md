# Resource Audit Slice: Resource Alignment

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

Texture alignment validation accepted 64 KiB alignment for large MSAA
resources without applying the D3D12 4 MiB small-resource restriction. The
shared texture flag validation also allowed MSAA textures without the required
render-target or depth-stencil capability flag.

## Implemented Contract

- Buffer alignment remains limited to zero or
  `D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT`.
- Texture alignment accepts only zero, 4 KiB, 64 KiB, or 4 MiB in the valid
  dimension/sample combinations.
- A 4 KiB texture must use UNKNOWN layout, be non-MSAA, lack render-target and
  depth-stencil flags, and have its most-detailed mip allocation no larger than
  64 KiB.
- An MSAA texture may use 64 KiB alignment only when its most-detailed mip
  allocation is no larger than 4 MiB.
- MSAA textures must carry either the render-target or depth-stencil flag.
- Invalid alignment descriptions return `E_INVALIDARG`; allocation queries
  return the existing invalid allocation sentinel.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
  - Required RT/DS capability flags for MSAA textures.
- `src/d3d12/d3d12_texture.cpp`
  - Applied the 4 KiB and MSAA 64 KiB small-resource size restrictions.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added invalid alignment, oversized small-resource, valid 4 KiB, and
    oversized MSAA coverage.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Recorded this completed alignment slice.

## Validation

- x64 private-build and no-private-build resource runners pass.
- `git diff --check` passes before staging.
- 32-bit runtime validation remains outside the approved scope.

## Follow-Up

Continue with row-pitch and slice-pitch contracts for texture transfer APIs.

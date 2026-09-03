# D3D12 Resource Audit Slice: Allocation-Info Validation Parity

Status: Complete
Date: 2026-09-03
Scope: Texture descriptors passed to `GetResourceAllocationInfo1` and
`GetResourceAllocationInfo2`.

## Contract

Allocation-info queries must reject the same invalid texture descriptors as
resource creation. This includes dimensions and mip counts outside the
supported range, non-unknown texture layouts, and unknown or incompatible
resource flags.

## Changed Files

- `src/d3d12/d3d12_device.cpp`
  - Runs the shared texture descriptor, layout, and flag validators before
    querying Metal texture size and alignment.
- `tests/dx12/dx12_resource_tests.cpp`
  - Rejects an oversized texture width, row-major layout, and unknown texture
    resource flags through `GetResourceAllocationInfo1`.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Records this completed allocation-info parity slice.

## Validation

- Private and no-private x64 resource runners.
- `git diff --check`.

## Follow-Up

Continue auditing the remaining resource-helper and `CopyTiles` paths.

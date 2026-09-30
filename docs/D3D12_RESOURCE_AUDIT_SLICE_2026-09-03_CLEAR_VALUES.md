# D3D12 Resource Audit Slice: Optimized Clear Values

Status: Complete
Date: 2026-09-03
Scope: `pOptimizedClearValue` handling at reserved-resource creation.

## Contract

`pOptimizedClearValue` is optional creation metadata for textures and is not
the value supplied to later explicit clear commands. Reserved textures accept
the value, matching committed and placed texture creation. Buffer resources
continue to reject a non-null optimized clear value.

DXMT does not need to retain the value because its clear commands receive the
clear value explicitly.

## Changed Files

- `src/d3d12/d3d12_texture.cpp`
  - Stops rejecting optimized clear values for reserved textures.
- `tests/dx12/dx12_resource_tests.cpp`
  - Creates a reserved render-target texture with an optimized clear value.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Records this completed contract slice.

## Validation

- Private and no-private x64 resource runners.
- `git diff --check`.

## Follow-Up

Continue auditing the remaining resource-helper and `CopyTiles` paths.

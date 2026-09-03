# D3D12 Resource Audit Slice: Buffer Tiling Boxes

Status: Complete
Date: 2026-09-03
Scope: `UseBox` tile regions for reserved buffer resources.

## Contract

`D3D12_TILE_REGION_SIZE::UseBox` is valid for buffers. A buffer box uses its
`Width` as the tile count and must have `Height == 1`, `Depth == 1`, and
`NumTiles == Width`. The coordinate must remain within the buffer's single
subresource and one-dimensional tile range.

## Changed Files

- `src/d3d12/d3d12_buffer.cpp`
  - Accepts valid buffer boxes in `GetTileIndices`, `UpdateTileMappings`, and
    `CopyTileMappingsFrom`.
  - Reuses `GetTileIndices` from update-mapping region traversal.
- `tests/dx12/dx12_resource_tests.cpp`
  - Exercises a boxed reserved-buffer mapping and `CopyTiles` upload.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Records this completed contract slice.

## Validation

- Private and no-private x64 resource runners.
- `git diff --check`.

## Follow-Up

Continue auditing the remaining resource-helper and `CopyTiles` paths.

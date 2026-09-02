# Resource Audit Slice: Tiled Copy Contracts

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

`CopyTileMappings` treated a missing `pRegionSize` as a request to copy the
whole resource even though the D3D12 API requires that parameter. `CopyTiles`
also accepted a missing tile-region start coordinate and substituted zeroes.

## Implemented Contract

- `CopyTileMappings` rejects a null `pRegionSize` for both reserved buffer and
  reserved texture resources.
- `CopyTiles` rejects a null `pTileRegionStartCoordinate` while retaining the
  existing deferred tile-resolution behavior for valid recorded commands.
- Invalid tiled-copy calls do not mutate tile mappings or produce a copy.

## Changed Files

- `src/d3d12/d3d12_buffer.cpp`
  - Rejects missing `CopyTileMappings` region sizes.
- `src/d3d12/d3d12_texture.cpp`
  - Rejects missing `CopyTileMappings` region sizes.
- `src/d3d12/d3d12_command_list.cpp`
  - Rejects missing `CopyTiles` start coordinates.
- `tests/dx12/dx12_resource_tests.cpp`
  - Covers null mapping regions and null `CopyTiles` coordinates.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Recorded this completed tiled-copy contract slice.

## Validation

- x64 private-build and no-private-build resource runners pass.
- `git diff --check` passes before staging.
- 32-bit runtime validation remains outside the approved scope.

## Follow-Up

Continue with the remaining resource helper and `CopyTiles` range/output
checks, then complete the broader private/no-private x64 validation matrix.

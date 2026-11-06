# D3D12 Resource Audit Slice: CopyTileMappings

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Scope

This slice covers the command-queue `CopyTileMappings` contract and the
resource-region validation used by reserved buffers and textures.

## Invariants

- Destination and source resources must be same-device reserved resources of a
  compatible resource kind.
- The destination coordinate, source coordinate, and region size are required
  for `CopyTileMappings`.
- Each region must fit entirely in its resource, and the source and destination
  regions must enumerate the same number of tiles.
- Only `D3D12_TILE_MAPPING_FLAG_NO_HAZARD` is supported.
- Invalid calls are no-ops because the public API has no HRESULT return value.

## Changes

- Added x64 runner coverage for null region arguments, out-of-bounds source and
  destination coordinates, an over-large region, and an unsupported flag.
- Each invalid call is checked to leave the destination mapping unchanged.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Continue auditing the remaining resource helper and `CopyTiles` contract paths.

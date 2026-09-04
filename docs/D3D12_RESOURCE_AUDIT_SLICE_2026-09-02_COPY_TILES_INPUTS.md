# D3D12 Resource Audit Slice: CopyTiles Inputs

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Scope

This slice covers the `CopyTiles` command-list input contract at recording
time.

## Invariants

- The tiled resource, tile-region coordinate, tile-region size, and linear
  buffer are required.
- The tiled resource must be a reserved resource and the buffer must be a
  valid non-reserved buffer resource.
- Only the supported copy flags are accepted, with at most one direction.
- The linear buffer offset must be aligned to one 64 KiB tile.
- Invalid calls fail command-list recording and do not allocate a copy encoder.

## Changes

- Added x64 runner coverage for null resources and region parameters, an
  unsupported flag, conflicting copy directions, an unaligned buffer offset,
  and a non-reserved tiled resource.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Continue auditing the remaining resource helper and `CopyTiles` output paths.

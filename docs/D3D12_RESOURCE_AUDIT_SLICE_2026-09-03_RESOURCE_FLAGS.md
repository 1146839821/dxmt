# D3D12 Resource Audit Slice: Resource Flags

Status: Complete
Date: 2026-09-03
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Scope

This slice covers validation of the `D3D12_RESOURCE_FLAGS` bitmask before
resource creation, allocation-info calculation, and Metal texture setup.

## Invariants

- Only resource flags defined by the repository's D3D12 contract are accepted.
- Existing buffer and texture flag-combination rules remain unchanged.
- Invalid flags produce the existing invalid allocation-info sentinel.

## Changes

- Added a shared known-resource-flags mask to buffer and texture validation.
- Added x64 regressions for unknown flags on buffer and texture descriptors.
- Added allocation-info coverage for both descriptor kinds.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Continue auditing the remaining resource helper and `CopyTiles` output paths.

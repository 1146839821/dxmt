# D3D12 Resource Audit Slice: Texture Sample Descriptors

Status: Complete
Date: 2026-09-03
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Scope

This slice covers multisample count and quality validation for texture
creation and allocation-info queries.

## Invariants

- Multisample textures are limited to quality level zero by the current device
  contract, which reports one quality level for supported sample counts.
- Unsupported Metal sample counts are rejected before texture setup.
- Invalid sample descriptors return `E_INVALIDARG` or the existing invalid
  allocation-info sentinel.

## Changes

- Rejected non-zero quality on multisample texture descriptors.
- Rejected sample counts unsupported by the Metal device.
- Added x64 creation and allocation-info regressions for both cases.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Continue auditing the remaining resource helper and `CopyTiles` output paths.

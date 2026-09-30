# D3D12 Resource Audit Slice: Resource States

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Scope

This slice covers the resource-state bitmask validation used by committed,
placed, and reserved resource creation.

## Invariants

- Only resource-state bits defined by the repository's D3D12 contract are
  accepted.
- Exclusive-write states remain mutually exclusive with all other states.
- Upload and readback heaps retain their required initial-state restrictions.
- Invalid state masks are rejected before a resource is created.

## Changes

- Added a shared known-state mask to `ValidateResourceStates`.
- Added an x64 regression case for an unknown initial-state bit on a default
  heap resource.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Continue auditing the remaining resource helper and `CopyTiles` output paths.

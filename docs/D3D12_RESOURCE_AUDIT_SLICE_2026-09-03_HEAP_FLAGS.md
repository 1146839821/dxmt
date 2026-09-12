# D3D12 Resource Audit Slice: Heap Flags

Status: Complete
Date: 2026-09-03
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Scope

This slice covers the `D3D12_HEAP_FLAGS` bitmask at heap creation
boundaries.

## Invariants

- Only heap flags defined by the repository's D3D12 contract are accepted.
- `CreateHeap` and `CreateHeap1` return the same result for invalid flags.
- Existing shared, cross-adapter, display, and resource-type flag rules remain
  unchanged.

## Changes

- Added a shared known-heap-flags mask to `ValidateHeapProperties`.
- Added x64 parity coverage for an unknown heap flag.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Continue auditing the remaining resource helper and `CopyTiles` output paths.

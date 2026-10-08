# D3D12 Resource Audit Slice: Heap Property Enumerations

Status: Complete
Date: 2026-09-03
Scope: `D3D12_HEAP_PROPERTIES` enum validation at heap creation.

## Contract

`CPUPageProperty` accepts only `UNKNOWN`, `NOT_AVAILABLE`, `WRITE_COMBINE`, or
`WRITE_BACK`. `MemoryPoolPreference` accepts only `UNKNOWN`, `L0`, or `L1`.
Custom heaps still require both fields to be non-`UNKNOWN`; non-custom heaps
still require both fields to be `UNKNOWN`.

Invalid enum values must be rejected before a Metal heap is created.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
  - Rejects unknown CPU page-property and memory-pool enum values in the shared
    heap-property validator.
- `tests/dx12/dx12_resource_tests.cpp`
  - Covers invalid custom CPU page-property and memory-pool values through
    `CreateHeap`/`CreateHeap1` parity checks.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Records this completed contract slice.

## Validation

- Private and no-private x64 resource runners.
- `git diff --check`.

## Follow-Up

Continue auditing the remaining resource-helper and `CopyTiles` paths.

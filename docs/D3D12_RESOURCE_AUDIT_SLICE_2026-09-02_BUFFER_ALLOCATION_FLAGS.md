# Resource Audit Slice: Buffer Allocation Flags

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

Buffer creation rejected `D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS`, but
the `GetResourceAllocationInfo1` buffer path only checked descriptor shape and
alignment. The query could therefore return an allocation for a descriptor
that resource creation would reject.

## Implemented Contract

- Buffer descriptor shape, alignment, and simultaneous-access flag validity are
  centralized in `IsValidBufferResourceDesc`.
- Resource creation and allocation-info queries reject the same invalid buffer
  descriptor with the existing invalid allocation sentinel.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
  - Included the buffer flag restriction in the shared validator.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added invalid buffer-flag allocation-info coverage.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Recorded this completed consistency slice.

## Validation

- x64 private-build and no-private-build resource runners pass.
- `git diff --check` passes before staging.
- 32-bit runtime validation remains outside the approved scope.

## Follow-Up

Continue with the remaining resource helper and `CopyTiles` contract checks.

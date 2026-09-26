# Resource Audit Slice: Upload and Readback Initial States

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

`ValidateResourceStates` did not enforce the heap-specific initial state
contract for upload resources and allowed `COMMON` for readback resources.
Direct3D 12 requires upload resources to start in `GENERIC_READ` and readback
resources to start in `COPY_DEST`.

## Implemented Contract

- `D3D12_HEAP_TYPE_UPLOAD` accepts only `D3D12_RESOURCE_STATE_GENERIC_READ`.
- `D3D12_HEAP_TYPE_READBACK` accepts only `D3D12_RESOURCE_STATE_COPY_DEST`.
- Default and custom heaps retain the existing exclusive-write validation.
- Both committed and placed resource creation use the same helper path.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
  - Enforced the upload and readback initial-state requirements.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added committed upload/readback invalid-state cases.
  - Added placed upload/readback invalid-state cases.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Recorded this completed validation slice and narrowed the next audit item.

## Validation

- x64 private-build resource runner passed.
- x64 no-private-build resource runner passed.
- `git diff --check` passed before staging.
- 32-bit runtime validation remains outside the approved scope.

## Do Not Reimplement

- Do not relax upload `GENERIC_READ` or readback `COPY_DEST` to accommodate
  callers; that would violate the D3D12 creation contract.
- Do not create separate state checks in committed and placed resource paths.
- Do not change capability reporting or advertise tiled-resource Tier 2.

## Follow-Up

Continue with one focused resource-helper contract at a time, prioritizing
descriptor ranges, alignment, and node-mask validation.

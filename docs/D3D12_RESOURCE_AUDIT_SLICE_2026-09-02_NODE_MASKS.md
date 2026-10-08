# Resource Audit Slice: Single-Node Heap Masks

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

`ValidateHeapProperties` left node-mask validation as a TODO. The current
backend exposes one adapter node, so heap properties containing unsupported
node bits could pass validation and reach heap creation.

## Implemented Contract

- Creation and visible node masks may contain only the single exposed node bit.
- Zero masks remain valid because D3D12 defines zero as equivalent to node 1 on
  single-node devices.
- Invalid node masks return `E_INVALIDARG` before heap allocation.
- A null heap-properties pointer is rejected by the shared heap validator.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
  - Added null and single-node mask validation.
  - Removed the completed node-mask TODO.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added zero-mask acceptance coverage.
  - Added unsupported creation and visible mask rejection coverage.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Recorded this completed validation slice and narrowed the next audit item.

## Validation

- x64 private-build resource runner passed.
- x64 no-private-build resource runner passed.
- `git diff --check` passed before staging.
- 32-bit runtime validation remains outside the approved scope.

## Do Not Reimplement

- Do not reject zero node masks; they are valid single-node D3D12 input.
- Do not claim multi-node visibility that the backend cannot provide.
- Do not change capability reporting or advertise tiled-resource Tier 2.

## Follow-Up

Continue with focused resource-helper checks for descriptor null/range and
resource allocation alignment contracts.

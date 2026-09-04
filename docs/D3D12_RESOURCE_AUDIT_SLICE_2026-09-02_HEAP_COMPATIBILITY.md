# Resource Audit Slice: Heap Validation and Compatibility

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

`CreateHeap1` bypassed the validation used by `CreateHeap`. Resources carrying
`D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER` were also accepted without checking
that the selected heap had `D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER`.

## Implemented Contract

- `CreateHeap1` delegates to `CreateHeap` when no protected session is passed,
  so descriptor, node-mask, property, and unsupported-flag results stay in
  parity.
- A resource with `D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER` requires
  `D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER` wherever a heap flag is supplied.
- Committed and placed resource creation applies the compatibility check for
  both buffer and texture descriptors.
- Reserved resources reject the cross-adapter flag because they have no shared
  cross-adapter heap.
- `D3D12_HEAP_FLAG_SHARED` and
  `D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER` remain unsupported and return
  `E_NOTIMPL` from heap creation.
- `GetResourceAllocationInfo*` has no heap-flags parameter. Its descriptor-only
  allocation result does not imply that cross-adapter resource creation is
  supported; compatibility is enforced at the creation boundary.

## Changed Files

- `src/d3d12/d3d12_device.hpp`
  - Declared `ValidateResourceHeapCompatibility`.
- `src/d3d12/d3d12_resource_helper.cpp`
  - Added the cross-adapter resource/heap compatibility check.
- `src/d3d12/d3d12_device.cpp`
  - Reused `CreateHeap` from `CreateHeap1`.
  - Applied compatibility validation to committed, placed, and reserved paths.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added `CreateHeap`/`CreateHeap1` parity cases and cross-adapter committed,
    placed, and reserved resource cases.

## Validation

- x64 private-build and no-private-build resource runners pass.
- `git diff --check` passes before staging.
- 32-bit runtime validation remains outside the approved scope.

## Follow-Up

Cross-adapter heap creation and cross-adapter resource execution remain
unsupported; do not advertise those capabilities without a separate
implementation and validation slice. When shared-cross-adapter heap support is
introduced, make resource/heap compatibility bidirectional and implement the
remaining shared-heap restrictions, including the required shared flag,
resource-type flags, heap-property restrictions, and protected-session rules.

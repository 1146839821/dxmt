# Resource Audit Slice: Committed Heap Flags

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

`CreateCommittedResource` validated that heap flags were individually valid,
but did not validate the selected resource type against the heap's resource
type restriction. A committed buffer or texture could therefore reach the
allocator with an incompatible `D3D12_HEAP_FLAG_ALLOW_ONLY_*` combination.

## Implemented Contract

`ValidateResourceHeapFlags` in the existing resource-helper seam now applies
the same resource-type contract to both committed and placed resources:

- `ALLOW_ONLY_BUFFERS` accepts buffers only.
- `ALLOW_ONLY_NON_RT_DS_TEXTURES` accepts ordinary textures only.
- `ALLOW_ONLY_RT_DS_TEXTURES` accepts RT/DS textures only.
- No resource-type restriction accepts any resource type; later descriptor
  validation remains responsible for other invalid combinations.

`CreateCommittedResource` calls the helper after heap-flag validation and
before allocation. `CreatePlacedResource` uses the same helper instead of
duplicating the type decision. The existing `CreatePlacedTexture` guard and
all capability reporting remain unchanged.

## Changed Files

- `src/d3d12/d3d12_device.hpp`
  - Declared the shared resource/heap flag validator.
- `src/d3d12/d3d12_resource_helper.cpp`
  - Implemented the resource dimension and RT/DS flag matrix.
- `src/d3d12/d3d12_device.cpp`
  - Applied the helper to committed and placed resource creation.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added committed buffer on RT/DS-only heap rejection.
  - Added ordinary texture on buffer-only heap rejection.

## Validation

- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
  passed.
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
  passed.
- x64 resource runner passed with the current private build.
- no-private resource runner passed with the current no-private build.
- `git diff --check` passed before staging.
- 32-bit runtime validation remains outside the approved scope.

## Do Not Reimplement

- Do not add separate committed and placed resource-type decision trees.
- Do not infer heap compatibility from the Metal allocation result.
- Do not weaken `ValidateHeapProperties`; it still owns heap-flag validity,
  while this helper owns resource-to-heap compatibility.
- Do not change capability reporting or advertise tiled-resource Tier 2.

## Follow-Up

The next candidate is a focused audit of remaining resource-helper validation
contracts: null inputs, ranges, initial states, alignment, and node masks.
Each contract should receive its own test and slice record rather than a broad
refactor.

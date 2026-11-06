# Resource Audit Slice: Buffer Descriptors

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

Regular buffer creation validated only a small subset of the buffer descriptor
shape. The allocation-info query duplicated another partial check, while
reserved buffers had a third local copy of the same contract.

## Implemented Contract

The shared `IsValidBufferResourceDesc` contract now requires:

- non-zero `Width`;
- `Height`, `DepthOrArraySize`, and `MipLevels` equal to 1;
- `DXGI_FORMAT_UNKNOWN`;
- sample count 1 and quality 0;
- `D3D12_TEXTURE_LAYOUT_ROW_MAJOR`;
- alignment 0 or `D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT`.

The same check is used by committed/placed descriptor validation, reserved
buffer initialization, and `GetResourceAllocationInfo1`.

## Changed Files

- `src/d3d12/d3d12_device.hpp`
  - Declared the shared buffer descriptor validator.
- `src/d3d12/d3d12_resource_helper.cpp`
  - Implemented the buffer shape and alignment contract.
  - Applied it from `ValidateResourceDescs`.
- `src/d3d12/d3d12_buffer.cpp`
  - Reused the shared contract for reserved buffers.
- `src/d3d12/d3d12_device.cpp`
  - Reused the shared contract for allocation-info queries.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added creation rejection for invalid layout, sample quality, and alignment.
  - Added allocation-info rejection for invalid layout.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Recorded this completed validation slice.

## Validation

- x64 private-build resource runner passed.
- x64 no-private-build resource runner passed.
- `git diff --check` passed before staging.
- 32-bit runtime validation remains outside the approved scope.

## Do Not Reimplement

- Do not maintain separate buffer descriptor shape checks in reserved,
  committed, placed, and allocation-info paths.
- Do not accept texture layouts, sample quality, or small-resource alignment
  for buffers.
- Do not change capability reporting or advertise tiled-resource Tier 2.

## Follow-Up

Continue with texture descriptor range and alignment checks, keeping small
resource eligibility separate from the fixed buffer contract.

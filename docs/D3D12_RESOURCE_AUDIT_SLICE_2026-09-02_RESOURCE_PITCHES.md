# Resource Audit Slice: Texture Transfer Pitches

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

`WriteToSubresource` and `ReadFromSubresource` forwarded caller-provided row
and slice pitches directly to Metal without checking that they could contain
the requested box. The BC path also used the format's bytes-per-block value as
the texel block extent when validating box alignment.

## Implemented Contract

- Row pitch must contain the requested row's source or destination data.
- 3D slice pitch must contain all rows in one requested depth slice.
- Non-3D slice pitch remains ignored, matching the single-slice API use.
- BC formats use 4x4 texel blocks and their format-specific bytes-per-block
  value for pitch calculation.
- BC box starts and interior ends must be block aligned; a right or bottom edge
  may end at the resource boundary when the dimension is not block aligned.
- Invalid pitch or box alignment returns `E_INVALIDARG` before calling Metal.

## Changed Files

- `src/d3d12/d3d12_texture.cpp`
  - Added shared transfer-layout and pitch validation.
  - Corrected BC block alignment and row-size calculation.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added 2D, BC, and 3D undersized-pitch regression cases.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Recorded this completed pitch slice.

## Validation

- x64 private-build and no-private-build resource runners pass.
- `git diff --check` passes before staging.
- 32-bit runtime validation remains outside the approved scope.

## Follow-Up

Continue with the remaining resource helper and `CopyTiles` contract checks.

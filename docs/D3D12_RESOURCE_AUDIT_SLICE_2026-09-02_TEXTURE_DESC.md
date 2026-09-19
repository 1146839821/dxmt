# Resource Audit Slice: Texture Descriptors

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

Texture creation did not consistently reject zero dimensions, invalid 1D
height, excessive mip counts, or sample configurations unsupported by the
texture dimension. Invalid zero-sized input could also reach
`PopulateWMTTextureInfo` before its mip-count calculation.

## Implemented Contract

The shared `ValidateTextureResourceDesc` contract now requires:

- non-zero width, height, depth/array size, and sample count;
- texture width representable by the Metal wrapper's 32-bit texture width;
- 1D textures to have height 1 and single-sample quality 0;
- 2D single-sample textures to have quality 0;
- multisample 2D textures to use exactly one mip level;
- 3D textures to be single-sample with quality 0;
- explicit mip counts not to exceed the dimensions' maximum mip count.

The validator is used by resource descriptor validation and
`PopulateWMTTextureInfo`, so committed, placed, reserved, and allocation-info
paths share the same structural checks.

## Changed Files

- `src/d3d12/d3d12_device.hpp`
  - Declared the shared texture descriptor validator.
- `src/d3d12/d3d12_resource_helper.cpp`
  - Implemented dimensions, mip, sample, and width-range validation.
  - Applied it from `ValidateResourceDescs`.
- `src/d3d12/d3d12_texture.cpp`
  - Rejected invalid texture descriptions before format and mip processing.
- `src/d3d12/d3d12_device.cpp`
  - Reused the validator through `PopulateWMTTextureInfo` for allocation
    queries.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added invalid committed texture cases and invalid mip allocation-query
    coverage.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Recorded this completed validation slice.

## Validation

- x64 private-build resource runner passed.
- x64 no-private-build resource runner passed.
- `git diff --check` passed before staging.
- 32-bit runtime validation remains outside the approved scope.

## Do Not Reimplement

- Do not duplicate texture shape checks in allocation, creation, and reserved
  resource paths.
- Do not infer format capability or MSAA quality support from these structural
  checks; those remain separate contracts.
- Do not change capability reporting or advertise tiled-resource Tier 2.

## Follow-Up

Continue with texture resource-flag, layout, format-support, and alignment
contracts, recording unsupported Metal behavior separately from invalid D3D12
input.

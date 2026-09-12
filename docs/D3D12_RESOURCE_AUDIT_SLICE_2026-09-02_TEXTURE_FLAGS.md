# Resource Audit Slice: Texture Flag Combinations

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

Texture descriptors could combine mutually exclusive D3D12 resource flags and
could request flag/layout or flag/sample combinations that the advertised
device contract does not support. These descriptors could reach Metal texture
creation without a deterministic validation result.

## Implemented Contract

- Render-target and depth-stencil flags are mutually exclusive.
- Depth-stencil resources cannot also use UAV or simultaneous-access flags.
- `DENY_SHADER_RESOURCE` requires the depth-stencil flag.
- UAV and simultaneous-access flags are rejected for multisample textures.
- Render-target, depth-stencil, and UAV flags are rejected with row-major
  textures because the device reports no cross-adapter row-major support.
- The same flag checks run before Metal texture creation and allocation-info
  queries through the shared texture-info path.

## Changed Files

- `src/d3d12/d3d12_device.hpp`
  - Declared the shared texture flag validator.
- `src/d3d12/d3d12_resource_helper.cpp`
  - Implemented the texture flag/layout/sample matrix.
  - Applied it from `ValidateResourceDescs`.
- `src/d3d12/d3d12_texture.cpp`
  - Applied the flag checks before format and Metal texture setup.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added invalid RT/DS, DS/UAV, shader-deny, MSAA, and row-major cases.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Recorded this completed validation slice.

## Validation

- x64 private-build resource runner passed.
- x64 no-private-build resource runner passed.
- `git diff --check` passed before staging.
- 32-bit runtime validation remains outside the approved scope.

## Do Not Reimplement

- Do not duplicate texture flag checks in committed, placed, reserved, and
  allocation-info paths.
- Do not claim row-major RT/UAV support while the feature query reports it as
  unavailable.
- Keep format capability checks in the shared texture-info path rather than
  duplicating them in individual resource creation paths.

## Follow-Up

Continue with unsupported layout, range, and alignment contracts, separating
invalid D3D12 combinations from bounded Metal feature gaps.

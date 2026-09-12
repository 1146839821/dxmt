# Resource Audit Slice: Small-Resource Estimator

Status: Complete
Date: 2026-09-03
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

Small-resource alignment eligibility was based on Metal's heap size query for a
single slice and mip. That made the D3D12 alignment contract depend on the
backend architecture and could reject resources that fit the D3D12 estimate.

## Implemented Contract

- 4 KiB alignment uses the D3D12 4 KiB tile-shape tables for non-MSAA,
  non-RT/DS, unknown-layout textures.
- MSAA textures may use 64 KiB alignment only when their most-detailed mip
  fits the D3D12 64 KiB tile estimate of 4 MiB or less and the resource is
  RT/DS.
- The estimate uses only mip 0 dimensions. Array size and mip count do not
  multiply the eligibility calculation.
- BC formats are measured in 4x4 blocks; compressed 1D and compressed MSAA
  resources remain outside the supported small-alignment subset.
- Unsupported format unit sizes and overflowing tile-count calculations are
  rejected conservatively.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
  - Added architecture-independent small-resource tile-shape and eligibility
    calculation.
- `src/d3d12/d3d12_device.hpp`
  - Declared the shared eligibility helper.
- `src/d3d12/d3d12_texture.cpp`
  - Replaced the Metal-dependent small-resource size check.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added 2D, array/mip, BC, 3D, and MSAA boundary coverage.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Records the alignment estimator as complete.

## Validation

- x64 private-build and no-private-build resource runners are required for this
  slice.
- `git diff --check` is required before staging.
- 32-bit runtime validation remains outside the approved scope.

## References

- `https://github.com/microsoft/DirectX-Specs/blob/master/d3d/ResourceHeaps.md#smaller-alignments`
- `https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator/master/src/D3D12MemAlloc.cpp`

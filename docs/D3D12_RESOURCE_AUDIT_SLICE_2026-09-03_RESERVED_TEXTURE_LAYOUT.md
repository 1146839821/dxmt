# D3D12 Resource Audit Slice: Reserved Texture Layout

Status: Complete
Date: 2026-09-03
Scope: Layout validation at the reserved-texture D3D12 boundary.

## Contract

- Reserved textures accept only `D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE`.
- Ordinary committed, placed, and allocation-info texture paths continue to
  accept only `D3D12_TEXTURE_LAYOUT_UNKNOWN` in the current implementation.
- The Metal texture-construction path normalizes the reserved layout to
  `UNKNOWN`; the resource-facing descriptor retains the undefined-swizzle
  layout for tiled-resource bookkeeping.
- `D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE` remains unsupported.

This is a boundary-correctness prerequisite for a future advertised tiled
resource tier. It does not advertise tiled-resource support; feature reporting
continues to return `D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED`.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
  - Adds specialized reserved-texture descriptor validation without widening
    generic texture-layout validation.
- `src/d3d12/d3d12_device.hpp`
  - Declares the specialized validation helper.
- `src/d3d12/d3d12_texture.cpp`
  - Requires undefined 64 KiB swizzle for reserved textures and normalizes only
    the internal Metal texture descriptor.
- `tests/dx12/dx12_resource_tests.cpp`
  - Migrates reserved texture fixtures to undefined swizzle and rejects unknown
    and standard-swizzle layouts at the reserved-resource boundary.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Separates ordinary layout validation from the reserved-texture boundary.

## Validation

- Private and no-private x64 resource runners.
- `git diff --check`.

## Follow-Up

- The packed-mip array regression remains a bounded logical model and is marked
  Partial in its audit record because the tested array-plus-packed-tail case is
  Tier 4 semantics, not an advertised Tier 2/3 capability.

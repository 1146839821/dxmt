# Resource Audit Slice: Reserved Texture Packed Mips

Status: Partial
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

Reserved textures rejected any mip whose extent was smaller than the standard
tile shape. `GetResourceTiling` consequently reported every mip as standard and
did not model the packed tail used by the D3D12 tile address contract.

## Implemented Contract

- The first mip with an extent smaller than the standard tile shape and all
  coarser mips are represented as a packed tail per array slice.
- Packed-tail storage uses the bounded logical model's 64 KiB tiles, with the
  number of tiles rounded up from the packed mip byte footprint.
- `GetResourceTiling` reports `NumStandardMips`, `NumPackedMips`,
  `NumTilesForPackedMips`, and the per-array packed-tile start offset.
- Packed subresource tilings have zero dimensions and
  `StartTileIndexInOverallResource == 0xffffffff`.
- Packed regions use flat `X` coordinates and linear traversal only. The
  logical mapping and tile-copy-mapping paths validate complete packed-tail
  ranges, but native shader-visible mapping is conditional: it is emitted as
  one Metal sparse-tail operation only when the D3D12 and Metal tail boundary
  and byte size match exactly.
- `CopyTiles` rejects any region containing packed mip tiles, matching the
  D3D12 contract that packed mip data must use non-tile-specific copy APIs.
- Capability reporting and the Metal 3 shadow-backing model remain unchanged.

## D3D12-Facing Boundary

- This slice covers logical packed-mip bookkeeping only; it is not a complete
  D3D12 tiled-resource capability claim.
- Reserved texture callers must use
  `D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE`. The Metal allocation path may
  normalize that layout to `D3D12_TEXTURE_LAYOUT_UNKNOWN` internally while the
  resource-facing descriptor retains the tiled layout.
- Packed tails are accepted only for a single array slice. Arrayed packed tails
  are rejected at the reserved-resource boundary and must not be treated as
  Tier 2 or Tier 3 support.
- `CreateShaderResourceView`, `CreateUnorderedAccessView`, and packed mapping
  translation remain rejected when the Metal sparse texture reports a
  different `firstMipmapInTail` or `tailSizeInBytes` from the D3D12 logical
  layout. This prevents treating a D3D12 logical tile index as a Metal tail
  index.
- DXMT continues to report `TiledResourcesTier` as
  `D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED`; no tiled-resource tier is claimed
  by this logical model.

## Changed Files

- `src/d3d12/d3d12_device.hpp`
  - Added the resource-level packed-tile query used by defensive `CopyTiles`
    validation.
- `src/d3d12/d3d12_texture.cpp`
  - Added packed-tail bookkeeping, tiling output, logical region traversal,
    and exact Metal sparse-tail compatibility checks for views and mapping.
- `src/winemetal/winemetal.h`, `src/winemetal/winemetal_thunks.c`,
  `src/winemetal/winemetal_thunks.h`, `src/winemetal/unix/winemetal_unix.c`,
  and `src/winemetal/Metal.hpp`
  - Added the `tailSizeInBytes` query used by the compatibility check.
- `src/d3d12/d3d12_command_list.cpp`
  - Rejects packed mip tiles while recording `CopyTiles`.
- `src/d3d12/d3d12_command_queue.cpp`
  - Retains the same rejection as a translation-time defensive check.
- `tests/dx12/dx12_resource_tests.cpp`
  - Covers single-array packed-tail `GetResourceTiling`, mapping, and `CopyTiles`
    rejection, plus arrayed packed-tail rejection.
- `tests/dx12/dx12_reserved_texture_packed.cpp` and
  `tests/dx12/reserved_texture_packed.hlsl`
  - Provide a buildable shader/remap/lifetime fixture for a complete packed
    tail; the current host is explicitly blocked by the Metal/D3D tail-boundary
    mismatch.
- `tests/dx12/dx12_tiled_tier2_oracle.cpp` and
  `tests/dx12/dx12_tiled_tier2_gate`
  - Provide the native-Windows oracle target and formal non-PASS gate.

## Validation

- Both private and no-private x64 resource runners passed for the existing
  resource/feature fixtures before the packed shader fixture was classified as
  blocked on this host.
- The packed fixture and all three CSOs build successfully; its direct runtime
  probe is not a semantic pass because the current host reports
  `d3d_first=0`, `metal_first=1`, and rejects the packed shader views/mapping.
- `dx12_tiled_tier2_gate` returns nonzero and prints
  `TIER2_GATE=NOT_SATISFIED` because packed-tail architecture, raw/structured
  status, filtering/LOD, and native-Windows oracle cases are non-PASS.
- `git diff --check` passed.

## Follow-Up

- The packed pixel layout remains opaque by design. The conditional sparse-tail
  translation is a verified seam, not a claim that the current Metal 3
  runtime can represent every D3D12 packed layout. Reserved texture rendering
  and generic texture copy paths remain separate work.
- Do not raise `TiledResourcesTier` or present this arrayed packed-mip model as
  a D3D12-facing Tier 2, Tier 3, or Tier 4 implementation.

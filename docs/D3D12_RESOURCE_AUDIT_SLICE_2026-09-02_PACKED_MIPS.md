# Resource Audit Slice: Reserved Texture Packed Mips

Status: Complete
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
- Packed regions use flat `X` coordinates and linear traversal only. Mapping
  and tile-copy-mapping paths can address the packed tile range.
- `CopyTiles` rejects any region containing packed mip tiles, matching the
  D3D12 contract that packed mip data must use non-tile-specific copy APIs.
- Capability reporting and the Metal 3 shadow-backing model remain unchanged.

## Changed Files

- `src/d3d12/d3d12_device.hpp`
  - Added the resource-level packed-tile query used by defensive `CopyTiles`
    validation.
- `src/d3d12/d3d12_texture.cpp`
  - Added packed-tail bookkeeping, tiling output, and logical region traversal.
- `src/d3d12/d3d12_command_list.cpp`
  - Rejects packed mip tiles while recording `CopyTiles`.
- `src/d3d12/d3d12_command_queue.cpp`
  - Retains the same rejection as a translation-time defensive check.
- `tests/dx12/dx12_resource_tests.cpp`
  - Covers packed-tail `GetResourceTiling`, mapping, and `CopyTiles` rejection.

## Validation

- Both private and no-private x64 resource runners passed.
- Both private and no-private feature-support runners passed.
- `git diff --check` passed.

## Follow-Up

- The packed pixel layout remains opaque by design; this slice only exposes the
  logical tile/mapping contract. Reserved texture rendering and generic texture
  copy paths remain separate work.
- Do not raise `TiledResourcesTier` as part of this slice.

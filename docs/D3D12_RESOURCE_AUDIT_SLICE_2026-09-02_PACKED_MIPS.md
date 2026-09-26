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
- Tier 2 explicitly rejects a tiled texture with sub-standard mips when its
  array size is greater than one. The arrayed mixed-mip rejection is therefore
  a normative Tier 2 boundary, not a missing packed-tail implementation; this
  slice does not claim arrayed packed-tail support.
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
    tail; it holds heap B through the remap and GPU use and releases the final
    application reference only after the completion fence. The current host is
    explicitly blocked by the Metal/D3D tail-boundary mismatch.
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
  `TIER2_GATE=NOT_SATISFIED`. The current non-passing cases are the packed-tail
  architecture mismatch, raw/structured status lowering, SRV LOD validation on
  the current MSC host, and the native Windows runtime/debug-layer case;
  filtering now has a passing local fixture.
- The supplied native-Windows oracle was executed on an NVIDIA GeForce GTX
  1650 (`vendor=0x10de`, `device=0x1f0a`) and reported
  `TiledResourcesTier=3`. It returned the supported matrices
  `64x64 R32 mips4 array1: total=1, standard=0, packed=4, packedTiles=1,
  start=0, shape=128x128x1` and `192x128 R32 mips2 array1: total=3,
  standard=1, packed=1, packedTiles=1, start=2, shape=128x128x1`, with the
  latter's standard tiling `2x1x1 start=0` and packed tiling
  `0x0x0 start=0xffffffff`. The `array2` descriptor returned
  `0x80070057 (E_INVALIDARG)`, consistent with the Tier 2 array restriction.
  The native all-packed nonzero shape is retained as an adapter-specific
  observation only: Microsoft's `GetResourceTiling` documentation says the
  shape should be zero when every mip is packed, and DXMT's production behavior
  follows that normative rule. The corrected native packed fixture then reported
  `packed tail read on heap A passed`, `packed tail read on heap B passed`,
  `packed tail remap to heap A preserves data passed`, and
  `Packed mip sparse-tail mapping, remap, and lifetime tests passed` on the
  same GTX 1650. Native debug-layer output was not supplied.
- An earlier native packed-fixture revision released the final heap-B reference
  before the queued remap was consumed and reached
  `MapReadback: 0x887a0005 (DXGI_ERROR_DEVICE_REMOVED)`. The fixture now keeps
  heap B alive until its GPU fence completes, matching the D3D12 heap-lifetime
  contract.
- `git diff --check` passed.

## Round 2 Re-audit Notes — 2026-09-12

- `GetResourceTiling` now returns `D3D12_TILE_SHAPE{0, 0, 0}` for an entirely
  packed resource. The GTX 1650 `128x128x1` result remains
  `NATIVE_OBSERVED` adapter behavior and is not used as DXMT's normative
  expectation.
- `192x128 R32_UINT mips2 array2` remains rejected with `E_INVALIDARG` because
  Tier 2 disallows an array when a mip is smaller than the standard tile shape.
  The new `dx12_reserved_texture_view_range` fixture proves this boundary and
  separately proves that a standard-only view of the corresponding single-array
  mixed resource can read its mapped mip. A view intersecting the incompatible
  packed tail remains rejected or null rather than receiving an invented offset.
- Mapping tables use `Com<MTLD3D12Heap, false>`. The private reference keeps the
  DXMT/WMT heap object and backing allocation alive without adding a public COM
  reference for each mapped tile. The buffer lifetime regression reports
  `reserved buffer mapping kept heap public ref count at 1` after mapping.
- The independent `ResourceMinLODClamp` fixture covers ordinary and reserved
  standard-mip textures, fractional clamps, and `MostDetailedMip`. Its source
  fix is retained, but MSC 4.0.1 on the current M1 host ignores the SRV minLOD
  metadata, so SRV clamp semantics remain `UNVERIFIED / MISSING_VALIDATION`.
- The status fixture now covers a true linear fully-mapped, fully-NULL, and
  mixed footprint plus a tiled per-sample `SampleGrad` clamp. Its GPU result is
  `2.0/true`, `0.0/false`, `0.5/false`, and `1.0` versus `3.0` for the LOD
  samples. This closes the representative filtering validation but does not
  claim gather/compare/typed-UAV runtime coverage.

## Follow-Up

- The packed pixel layout remains opaque by design. The conditional sparse-tail
  translation is a verified seam, not a claim that the current Metal 3
  runtime can represent every D3D12 packed layout. Reserved texture rendering
  and generic texture copy paths remain separate work.
- If available, repeat the corrected packed fixture with the D3D12 debug layer
  enabled and capture its output; the supported native runtime semantics are
  otherwise closed by the supplied pass, while the Metal 3 implementation
  remains architecture-blocked.
- Do not raise `TiledResourcesTier` or present this arrayed packed-mip model as
  a D3D12-facing Tier 2, Tier 3, or Tier 4 implementation.

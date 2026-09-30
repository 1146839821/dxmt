# D3D12 Resource Audit Checklist

Status: In progress
Branch: `feat/d3d12`
Scope: Phase B resource, heap, tiled-resource, and `CopyTiles` contract audit.

This ledger is the source of truth for this audit. Before starting a new slice,
check the completed list and its slice record. Every completed slice must add a
record under `docs/` and link it below. Do not reimplement a completed item
without first documenting a changed contract or a regression.

## Status Rules

- `[x]` implemented and covered by the recorded validation.
- `[~]` partially implemented or known to have a bounded limitation.
- `[ ]` not started or not yet validated.
- A slice record must state changed files, invariants, tests, and follow-ups.

## Completed Slices

- [x] Capability boundary and feature-level contract. `D3D12CreateDevice`
  keeps the supported maximum at FL11_1 on Apple 7 hardware and does not claim
  tiled-resource Tier 2 support. Established before this audit.
- [x] Reserved buffer bookkeeping, tiling, mapping, and buffer `CopyTiles`.
  Existing commits: `fad8f7d`, `9503ae0`.
- [x] Reserved texture bookkeeping, standard-mip tiling, partial tiling, and
  texture mapping. Existing commits: `9469816`, `17d86aa`.
- [~] Reserved texture packed-mip metadata, logical tile mapping, and explicit
   `CopyTiles` rejection for packed tiles are implemented as a bounded logical
   model. Arrayed packed tails are rejected at the reserved-resource boundary
   and are not a claimed Tier 2/3 capability. See
   `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_PACKED_MIPS.md`.
- [x] Command-queue ordering and complete-submission serialization for tiled
  resource operations. Existing commits: `d756051`, `e487a63`, `8af503c`.
- [x] Deferred tile resolution for recorded `CopyTiles` operations. Existing
  commit: `617e94f`.
- [x] Explicit default `CopyTiles` direction handling. Existing commit:
  `b38f069`.
- [x] Resource and heap type validation for placed resources, including
  buffer-only, non-RT/DS-only, and RT/DS-only heaps. Existing validation is in
  `src/d3d12/d3d12_device.cpp` and is covered by the resource runner.
- [x] Reserved RT/DS texture mapping compatibility. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_HEAP_MAPPING.md`.
- [x] Committed-resource heap flag compatibility. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_COMMITTED_HEAP_FLAGS.md`.
- [x] Upload/readback initial-state compatibility. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_INITIAL_STATES.md`.
- [x] Single-node heap node-mask validation. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_NODE_MASKS.md`.
- [x] Buffer resource descriptor shape and alignment validation. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_BUFFER_DESC.md`.
- [x] Texture dimension, mip, sample, and zero-size validation. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_TEXTURE_DESC.md`.
- [x] Texture resource-flag combination validation. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_TEXTURE_FLAGS.md`.
- [x] Ordinary committed/placed/allocation-info texture layout validation. See
   `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_TEXTURE_LAYOUT.md`.
- [x] Reserved texture creation accepts only `64KB_UNDEFINED_SWIZZLE` at the
   D3D12 boundary while retaining the internal `UNKNOWN` representation. See
   `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_RESERVED_TEXTURE_LAYOUT.md`.
- [x] Texture format validity validation. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_TEXTURE_FORMAT.md`.
- [x] Resource null-output and failed allocation-info output handling. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_NULL_OUTPUTS.md`.
- [x] Texture subresource and data-pointer range validation. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_RESOURCE_RANGES.md`.
- [x] Texture resource alignment form and eligibility validation, including 4 KiB
  small resources and 64 KiB-aligned small MSAA resources. Small-resource size
  eligibility uses an architecture-independent D3D12 tile estimate. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_SMALL_RESOURCE_ESTIMATOR.md` and the
  earlier form-validation record in
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_RESOURCE_ALIGNMENT.md`.
- [x] Texture transfer row-pitch, depth-slice-pitch, and BC box-alignment
  validation. See `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_RESOURCE_PITCHES.md`.
- [x] Buffer flag validation is consistent between resource creation and
  allocation-info queries. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_BUFFER_ALLOCATION_FLAGS.md`.
- [x] Tiled copy parameter validation covers all required `CopyTileMappings`
  coordinates and region sizes plus `CopyTiles` start coordinates. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_TILED_COPY_CONTRACTS.md`.
- [x] `CopyTiles` validates the logical tile region and linear-buffer footprint
  while recording, before allocating a copy encoder. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_COPY_TILES_RANGES.md`.
- [x] Heap validation is shared by `CreateHeap` and `CreateHeap1`, and
  cross-adapter resource flags are checked against heap flags at resource
  creation boundaries. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_HEAP_COMPATIBILITY.md`.
- [x] `GetCopyableFootprints` validates resource descriptor shapes before
  calculating layouts, preserves empty-query semantics, and uses independent
  256-byte row-pitch alignment for each depth/stencil plane. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_COPYABLE_FOOTPRINTS.md`.
- [x] `CopyTileMappings` rejects missing or invalid region arguments and
  unsupported flags without mutating destination mappings. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_COPY_TILE_MAPPINGS.md`.
- [x] `CopyTiles` rejects missing resources, unsupported or conflicting
  direction flags, and unaligned buffer offsets while recording. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_COPY_TILES_INPUTS.md`.
- [x] Resource initial-state validation rejects unknown state bits before
  resource creation while preserving the existing exclusive-write and heap
  compatibility rules. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_RESOURCE_STATES.md`.
- [x] Resource descriptor validation rejects unknown resource flag bits across
  creation and allocation-info paths. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_RESOURCE_FLAGS.md`.
- [x] Texture sample descriptors reject non-zero MSAA quality and sample counts
  unsupported by the Metal device in creation and allocation-info paths. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_SAMPLE_DESC.md`.
- [x] Heap creation rejects unknown heap flag bits consistently through
  `CreateHeap` and `CreateHeap1`. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_HEAP_FLAGS.md`.
- [x] Buffer resource descriptors reject incompatible render-target and
  depth-stencil flags while accepting the observed deny-shader-resource
  compatibility flag consistently in creation and allocation-info paths. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_BUFFER_FLAGS.md`.
- [x] Reserved textures accept optional optimized clear values while buffers
  continue to reject them. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_CLEAR_VALUES.md`.
- [x] Heap properties reject unknown CPU page-property and memory-pool enum
   values while preserving custom/non-custom field rules. See
   `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_HEAP_PROPERTIES.md`.
- [x] Display heap flags enforce committed default-heap, scan-out format, and
   displayable Texture2D description compatibility. See
   `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_DISPLAY_HEAPS.md`.
- [x] Allocation-info texture queries share resource descriptor, layout, and
   flag validation with creation. See
   `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_ALLOCATION_INFO.md`.
- [x] Reserved buffers accept valid `UseBox` tile regions consistently across
   tile mapping and `CopyTiles` paths. See
   `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_BUFFER_TILING_BOXES.md`.
- [x] Resource initial states require matching render-target and depth-stencil
   resource flags across committed, placed, and reserved resource creation. See
   `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_INITIAL_STATE_FLAGS.md`.
- [x] Texture resource flags require matching mapped Metal format capabilities
   for render-target, depth-stencil, and UAV usage. See
   `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_FORMAT_CAPABILITIES.md`.
- [x] Multisample texture creation and quality queries require the mapped format
   `MSAA` capability in addition to device-level sample-count support. See
   `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_MSAA_CAPABILITIES.md`.

## Next Slices

- [ ] Audit remaining resource helper and `CopyTiles` contract paths.
  Record each contract separately.
- [x] Complete the x64 validation matrix for private and no-private builds,
  including API/shader validation where the fixture supports it. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_VALIDATION_MATRIX.md`.
- [~] Real Metal sparse residency remains unavailable through the current
  Metal 3 wrapper. Shadow backing is the supported bounded emulation; do not
  raise the advertised D3D12 tiled-resource tier as part of this audit.

## Permanent Constraints

- Target Metal 3 only for this work.
- Do not add a new dedicated `src/d3d12` source file unless a later slice
  explicitly justifies a new seam.
- Do not change the advertised capability tier to make a test pass.
- 32-bit validation is outside the current user-approved scope.
- The repository's reference-document files may be untracked local material;
  do not stage them with implementation commits unless explicitly requested.

## Validation Notes

- Build targets used for the current slice are `build`, `build-no-private`,
  and `build-X86`; current runtime conclusions are x64 and no-private only.
- The test executable directories contain stale local DLL copies. Run the
  executable from an isolated directory without those copies, or otherwise
  verify the loaded DLL path before trusting a failure.
- `git diff --check` is required for each slice. Whole-file clang-format checks
  currently report pre-existing violations in these large test/source files;
  do not reformat unrelated lines while closing a slice.

## Post-GO Tier2 audit — 2026-09-12 (Round 2 re-audit)

This is an audit and semantic-closure record, not a capability declaration.
`D3D12_FEATURE_D3D12_OPTIONS.TiledResourcesTier` remains
`D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED`.

### Feature ledger

| Area | Current evidence | Gate status |
| --- | --- | --- |
| Reserved buffers | Shadow backing, descriptor-table and independent root SRV/UAV access, remap, NULL handling, stable GPU VA, `CopyTiles`, multiple heaps, cross-queue cases, and the mapping heap public-reference check are covered by the x64 resource fixtures. | Partial closure; keep the existing conservative tier. |
| Reserved 2D textures | Creation, standard tile mapping, `CopyTiles`, two-heap remap/lifetime, `Texture.Load`, point sampling, and a mixed standard+packed view-range case are covered. | Partial closure. |
| Mapping ranges and NULL mappings | Existing buffer/texture mapping tests cover range validation, remap, NULL, `CopyTileMappings`, and boxed regions; mapping-table heap handles use DXMT private references while the application heap public count remains unchanged. | Local contract coverage; supported native shader/runtime comparison remains required. |
| Shader status feedback | DXBC parsing accepts feedback forms of `LD`, `LD_MS`, typed UAV load, sample bias/LOD/gradient/compare, gather, and gather compare. The current GPU fixture proves `Load`, point/linear `SampleLevel`, tiled `SampleGrad`, and `CheckAccessFullyMapped` for mapped, NULL, and mixed accesses. | `PARTIAL`: representative texture paths pass; gather/compare/typed-UAV runtime proof and a native debug-layer comparison remain open. |
| Raw/structured buffer feedback | The D3D/HLSL status-bearing load contract is defined (`SPEC_CLOSED`), but the direct raw/structured AIR load path has no residency/status sideband or shader-visible mapping metadata. | `BLOCKED_BY_ARCHITECTURE / DESIGN_REQUIRED`; do not fake fully-mapped status. |
| LOD clamp | The common metadata path now preserves fractional `ResourceMinLODClamp`, and the independent ordinary/reserved fixture covers `0.0`, `0.5`, `1.0`, `1.5`, and `MostDetailedMip`. | `UNVERIFIED / MISSING_VALIDATION` for SRV clamp on this host: MSC 4.0.1/M1 ignores the descriptor minLOD in the final linked shader. Tiled per-sample clamp is covered separately by the passing status fixture. |
| Packed mip tail | D3D12 packed metadata and logical tile ranges are modeled. Native sparse mapping is attempted only when `firstMipmapInTail` equals the D3D standard-mip boundary and Metal `tailSizeInBytes` equals the D3D packed-tail byte count; otherwise packed views/mappings stay rejected. A mixed resource now allows a representable standard-only SRV while rejecting an incompatible packed-intersecting view. The current host reports `d3d_first=0`, `metal_first=1`, and 65536-byte tails on both sides. The supplied native Windows oracle on an NVIDIA GeForce GTX 1650 (`0x10de:0x1f0a`, `TiledResourcesTier=3`) observed `64x64 R32 mips4 array1: total=1, standard=0, packed=4, packedTiles=1, start=0, shape=128x128x1` and `192x128 R32 mips2 array1: total=3, standard=1, packed=1, packedTiles=1, start=2, shape=128x128x1`; the array2 create returned `E_INVALIDARG`. Microsoft documents a zero shape for an entirely packed resource, so the native nonzero shape remains an adapter-specific observation; DXMT returns zero. | Current Metal 3 implementation: `BLOCKED_BY_ARCHITECTURE` for the incompatible packed-tail layout; Tier2 array rejection is a normative boundary, not a missing feature. |
| Filtering footprint | The GPU fixture now uses a true linear sampler: fully mapped produces `2.0/true`, fully NULL produces `0.0/false`, and a mapped/NULL 50/50 footprint produces `0.5/false`, with exact `CheckAccessFullyMapped` predicates. | `LOCAL_PASS` for this representative footprint; broader format/dimension coverage and native comparison remain open. |

### Requirements and assumptions

The audit follows Microsoft's tiled-resource exposure and shader-status
requirements, including the distinction between Tier 2 and higher-tier 3D
requirements.  The implementation assumes that the Metal sparse texture
operation's residency byte has bit 0 set for a nonresident access; the helper
canonicalizes that representation to DXBC's opaque status form (all bits set
for fully mapped, zero otherwise) before `CheckAccessFullyMapped` consumes it.
This assumption is not a replacement for a native Windows oracle.

Sources consulted: [Microsoft tiled resources exposure](https://learn.microsoft.com/en-us/windows/win32/direct3d11/hlsl-tiled-resources-exposure),
[Microsoft tiled-resource texture sampling features](https://learn.microsoft.com/en-us/windows/win32/direct3d11/tiled-resources-texture-sampling-features),
[DXBC `ld` feedback](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/t2d-load-float-int-uint-),
[DXBC `samplelevel` feedback](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/samplelevel-s-float-float-int-uint-),
[Metal sparse textures](https://developer.apple.com/documentation/metal/reading-and-writing-to-sparse-textures),
and [Metal feature sets](https://developer.apple.com/metal/feature-sets/).

### Semantic closure in this slice

The packed-tail fixture is `tests/dx12/dx12_reserved_texture_packed.cpp` with
`reserved_texture_packed.hlsl`. It checks the single-array `64x64 R32_UINT`
four-mip matrix, creates SRV/UAV descriptors before mapping, maps the logical
tail to heap A, remaps to heap B, and holds heap B through its GPU use. On the
current M1/Metal 3 host the D3D and Metal tail boundaries are incompatible
(`d3d_first=0`, `metal_first=1`, both tails 65536 bytes), so the production
view/mapping path returns `E_NOTIMPL` instead of assuming an offset. The
fixture exits 1 with the expected packed shader mismatch (`got 0x0/0x0`) and
is classified `BLOCKED_BY_ARCHITECTURE`, not as a semantic pass.

`tests/dx12/dx12_tiled_tier2_oracle.cpp` remains a buildable native-Windows
oracle target. The supplied run used an NVIDIA GeForce GTX 1650
(`vendor=0x10de`, `device=0x1f0a`) and reported `TiledResourcesTier=3`. It
observed `64x64 R32 mips4 array1: total=1, standard=0, packed=4,
packedTiles=1, start=0, shape=128x128x1`, `192x128 R32 mips2 array1:
total=3, standard=1, packed=1, packedTiles=1, start=2, shape=128x128x1`, and
`E_INVALIDARG` for the `array2` descriptor. The all-packed nonzero shape is
retained as `NATIVE_OBSERVED` only: Microsoft's documented contract requires a
zero `StandardTileShape` when every mip is packed, and DXMT now returns zero.
No native debug-layer output was supplied. The native packed remap fixture's
reported heap-A/heap-B/remap passes remain evidence for that adapter only.

The standard-only mixed-resource fixture is
`tests/dx12/dx12_reserved_texture_view_range.cpp`. It rejects the Tier2-
forbidden `192x128 R32_UINT mips2 array2` descriptor with `E_INVALIDARG`,
reports the valid single-array mixed tiling (`total=3`, one standard mip and
one packed mip), reads the mapped standard mip successfully, and leaves the
packed-intersecting descriptor either representable or rejected according to
the Metal tail boundary. On this host its exact output is:

```text
Tier2 mixed-mip array rejection: E_INVALIDARG
mixed texture tiling: total=3 standard_mips=1 packed_mips=1 packed_tiles=1 shape=128x128x1
standard-only view readback: 0x12345678 packed-intersecting descriptor mip0=0x12345678 mip1=0x0
standard-only mixed-resource view passed; packed-intersecting view was representable
```

The LOD fixture is `tests/dx12/dx12_texture_lod_clamp.cpp` with
`texture_lod_clamp.hlsl`. It covers ordinary and reserved standard-mip
textures, pure `ResourceMinLODClamp` values `0.0`, `0.5`, `1.0`, `1.5`, and a
separate `MostDetailedMip=1` case. The source fix preserves the fractional
value, but the current MSC 4.0.1/M1 runtime produces mip-0 for all four SRV
clamp cases. The exact ordinary output is
`0x3f800000 0x3f800000 0x3f800000 0x3f800000 0x40400000` against the expected
`0x3f800000 0x40000000 0x40400000 0x40800000 0x40400000`; the reserved output
has the same first four values and `0x40400000` for the `MostDetailedMip`
case. This is `UNVERIFIED / MISSING_VALIDATION` for SRV clamp semantics on
this host, with the compiler/runtime limitation recorded rather than
misattributed to the D3D12 metadata fix.

The focused test `tests/dx12/dx12_tiled_status_sm5.cpp` now uses separate
reserved textures for point access, a true linear sampler, and an all-standard
two-mip chain for per-sample `SampleGrad` LOD clamp. It verifies numeric
values with an epsilon and tests status only through `CheckAccessFullyMapped`.
The current run's exact relevant output is:

```text
status texture tiling: total=2 standard=128x128x1 subresource=2x1 start=0
feedback outputs: 0x3f800000 0xffffffff 0x3f800000 0xffffffff 0x0 0x0 0x0 0x0 0x40000000 0xffffffff 0x0 0x0 0x3f000000 0x0 0x3f800000 0xffffffff 0x40400000 0xffffffff
DXBC cs_5_0 tiled Load/Sample/linear-footprint/per-sample-LOD feedback and CheckAccessFullyMapped passed
```

These outputs prove mapped and NULL `Load`, point `SampleLevel`, fully mapped
linear filtering (`2.0/true`), fully NULL filtering (`0.0/false`), mixed
50/50 filtering (`0.5/false`), and tiled per-sample `SampleGrad` clamp
(`1.0` without clamp and `3.0` with clamp). They do not close parser/runtime
coverage for gather, comparison sampling, or typed UAV load. The Wine builtin
compiler result remains an environment limitation, not a semantic pass.

The heap mapping tables now hold `Com<MTLD3D12Heap, false>` private references.
This keeps the DXMT/WMT heap object and its backing allocation alive without
adding a public COM `AddRef` per mapped tile. The focused buffer fixture prints
`reserved buffer mapping kept heap public ref count at 1` and exits 0; the
packed texture fixture performs the same check before reaching its expected
Metal tail rejection. This closes the public-ref regression for the narrow
mapping-table ownership change, while submission lifetime remains governed by
the WMT sparse-operation/reference path and the application's fence ordering.

### Remaining formal-gate gaps

The final local gate remains non-PASS. The mandatory blockers are the
Metal-3/D3D12 packed-tail layout mismatch, raw/structured buffer feedback
lowering with no residency sideband, SRV `ResourceMinLODClamp` semantics not
validated by the current MSC host, and the missing native Windows debug-layer
comparison. Texture status feedback is `PARTIAL`, not blanket PASS: the
representative Load/sample/filter/per-sample paths pass, while gather/compare/
typed-UAV runtime cases remain unexecuted. Tier2's array-size restriction is
kept and is not a blocker. The updated gate script reports filtering as a
runtime case, raw/structured as `BLOCKED_BY_ARCHITECTURE`, and SRV LOD as
`UNVERIFIED / MISSING_VALIDATION`.

The first native packed-fixture attempt released heap B before the GPU consumed
the queued remap and reached `MapReadback: 0x887a0005`
(`DXGI_ERROR_DEVICE_REMOVED`). That was invalid application lifetime ordering,
not evidence that a final heap reference may be released while the GPU still
uses the mapping; the fixture now releases heap B only after its fence.

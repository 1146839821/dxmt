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

## Post-GO Tier2 audit — 2026-09-11

This is an audit and semantic-closure record, not a capability declaration.
`D3D12_FEATURE_D3D12_OPTIONS.TiledResourcesTier` remains
`D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED`.

### Feature ledger

| Area | Current evidence | Gate status |
| --- | --- | --- |
| Reserved buffers | Shadow backing, descriptor-table and independent root SRV/UAV access, remap, NULL handling, stable GPU VA, `CopyTiles`, multiple heaps, and cross-queue cases are covered by the existing x64 resource fixtures. | Partial closure; keep the existing conservative tier. |
| Reserved 2D textures | Creation, standard tile mapping, `CopyTiles`, two-heap remap/lifetime, and `Texture.Load` paths are present; the status fixture also exercises point `Texture.SampleLevel`. | Partial closure. |
| Mapping ranges and NULL mappings | Existing buffer/texture mapping tests cover range validation, remap, NULL, `CopyTileMappings`, and boxed regions; the texture fixtures hold a mapped heap through the GPU fence and release it only after the last use. | Covered by local fixtures; supported native shader/runtime comparison remains required. |
| Shader status feedback | DXBC parsing now accepts feedback forms of `LD`, `LD_MS`, typed UAV load, sample, sample bias/LOD/gradient/compare, gather, and gather compare. Airconv stores the residency result and lowers `CheckAccessFullyMapped`. | Implemented for the texture/sampled paths exercised here. |
| Raw/structured buffer feedback | The direct buffer read path has no texture residency result to map to the DXBC status operand. | Not implemented; Tier2 blocker. |
| LOD clamp | Existing metadata and sampler plumbing carries resource/sampler minimum LOD information into Airconv/Metal. | Plumbing exists, but no independent sparse LOD-clamp semantic fixture was added in this slice. |
| Packed mip tail | D3D12 packed metadata and logical tile ranges are modeled. Native sparse mapping is attempted only when `firstMipmapInTail` equals the D3D standard-mip boundary and Metal `tailSizeInBytes` equals the D3D packed-tail byte count; otherwise shader views and packed mappings stay rejected. The focused fixture is built but the current host reports `d3d_first=0`, `metal_first=1`, and 65536-byte tails on both sides. The supplied native Windows oracle on an NVIDIA GeForce GTX 1650 (`0x10de:0x1f0a`, `TiledResourcesTier=3`) reports the supported packed matrices: `64x64 R32 mips4 array1` is `total=1, standard=0, packed=4, packedTiles=1, start=0, shape=128x128x1`; `192x128 R32 mips2 array1` is `total=3, standard=1, packed=1, packedTiles=1, start=2, shape=128x128x1`, with tilings `2x1x1 start=0` and packed `0x0x0 start=0xffffffff`; the array2 create returns `E_INVALIDARG`. The corrected native packed shader/remap/lifetime fixture also passes on that adapter. The current Microsoft `GetResourceTiling` documentation says an entirely packed resource should report a zero standard tile shape, so the nonzero all-packed shape is recorded as an adapter-specific observation pending a second native/debug-layer comparison. | Supplied native supported runtime: PASS as observed; current Metal 3 implementation: `BLOCKED_BY_ARCHITECTURE`; cross-runtime metadata generalization remains open and Tier2 remains blocked. |
| Filtering footprint | The fixture uses point sampling only and does not independently prove fully mapped, fully NULL, or mixed mapped/NULL filter footprints. | Unverified; Tier2 blocker. |

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

The new packed-tail fixture is `tests/dx12/dx12_reserved_texture_packed.cpp`
with `reserved_texture_packed.hlsl`. It independently checks the D3D12
packed-mip matrix for a single-array `64x64 R32_UINT` resource with four mips,
creates SRV/UAV descriptors before mapping, maps the complete logical tail to
heap A, performs shader write/readback, remaps to heap B, keeps heap B alive
through the remap and its GPU use, releases the final application heap-B
reference after the completion fence, and verifies the remap back to heap A.
Its runtime proof is intentionally not counted on this host:
the exact D3D/Metal tail boundary does not match, so the production view and
mapping path remains `E_NOTIMPL`/rejected rather than using an unsafe offset
assumption. The formal `dx12_tiled_tier2_gate` records this fixture as
`BLOCKED_BY_ARCHITECTURE` after checking that the executable and all three CSO
fixtures exist.

`tests/dx12/dx12_tiled_tier2_oracle.cpp` is a buildable native-Windows oracle
target. It prints the adapter, `D3D12CreateDevice`, tiled-resource feature
level, and exact packed-mip tiling results for single-slice and arrayed
descriptors. The supplied Windows run used an NVIDIA GeForce GTX 1650
(`vendor=0x10de`, `device=0x1f0a`); factory/device/feature queries succeeded
and reported `TiledResourcesTier=3`. It confirmed the `64x64 R32 mips4
array1` and `192x128 R32 mips2 array1` matrices recorded above, including the
`128x128x1` standard tile shape even when every mip is packed, and rejected
the `192x128 R32 mips2 array2` descriptor with `0x80070057 (E_INVALIDARG)`.
This closes the supplied adapter observation for these descriptors. The
current Microsoft `GetResourceTiling` documentation says the shape should be
zero when every mip is packed, so a second supported adapter or debug-layer
comparison is still needed before generalizing the nonzero all-packed result.
The corrected
`dx12_reserved_texture_packed.exe` run on the same adapter also reported
`packed tail read on heap A passed`, `packed tail read on heap B passed`,
`packed tail remap to heap A preserves data passed`, and
`Packed mip sparse-tail mapping, remap, and lifetime tests passed`. No native
debug-layer output was supplied. The local gate remains non-PASS for this case
because it is a Wine-hosted aggregator and cannot execute an out-of-band
native Windows result as part of this local gate.

The focused test is `tests/dx12/dx12_tiled_status_sm5.cpp`.  It compiles a
`cs_5_0` shader with Microsoft's `d3dcompiler_47.dll`, maps one 64 KiB tile of
a reserved `R32_FLOAT` texture, performs mapped and NULL `Load` and point
`SampleLevel` operations, and reads back both values and status predicates.
The shader writes each predicate through an explicit `0xffffffff/0` branch,
so the assertion is independent of the compiler's internal boolean register
encoding.
With the current DXMT DLLs under the matching Wine toolchain and the native
Microsoft compiler fixture, the exact success line is:

```text
DXBC cs_5_0 tiled Load/Sample feedback and CheckAccessFullyMapped passed
```

The same test with the Wine builtin compiler is a valid environment result,
not a semantic pass: that compiler reports the feedback opcode and
`CheckAccessFullyMapped` as unsupported. The supplied supported Windows run
above covers both the metadata oracle and the packed shader/remap/lifetime
fixture; a native debug-layer comparison is still required.

### Remaining formal-gate gaps

Tier 2 is not ready to advertise until the following have independent
evidence: raw/structured feedback semantics (or an explicit contract decision
that excludes them), sparse LOD-clamp behavior, filtering footprints crossing
mapped and NULL texels, and a native Windows debug-layer comparison from an
adapter that exposes tiled resources. Packed metadata and the corrected native
packed shader/remap/lifetime run are captured above, while the current Metal 3
implementation still cannot represent this adapter's packed-tail boundary.
Multiple heaps and application-reference release now have local fixture
evidence. The formal gate output is
`TIER2_GATE=NOT_SATISFIED`; `NO CAPABILITY BUMP` is the decision for this
slice. The first native packed-fixture attempt released heap B before the GPU
consumed the queued remap and reached `MapReadback: 0x887a0005`
(`DXGI_ERROR_DEVICE_REMOVED`). That is an invalid heap-lifetime test ordering,
not evidence that a final heap reference may be released while the GPU still
uses the mapping; the fixture now releases heap B only after its fence.

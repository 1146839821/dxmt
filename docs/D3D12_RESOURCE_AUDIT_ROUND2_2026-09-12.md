# D3D12 Resource Audit — Round 2 Re-audit

Date: 2026-09-12
Branch: `feat/d3d12`
Scope: post-GO Tiled/Reserved Tier 2 semantic closure only.
Capability policy: no feature exposure changes in this round.

## Repository State

- Starting local HEAD: `1ccb5440db30e0debded724245b3557b02b1ed5f`
  (`fix(d3d12): zero tile shape for fully packed resources`).
- Starting remote HEAD: `0c35caab9feda2f8bfeb212dbbe19569a8b8fea7`
  (`fix(d3d12): honor packed tile shape and heap lifetime`).
- Final local HEAD: the documentation commit containing this report; the exact
  SHA is recorded by the final `git rev-parse HEAD` delivery check. The last
  implementation/documentation parent before this report is
  `e1ef9aae57e8a52d12ada71f4bb20830abc78462`.
- Final remote HEAD: unchanged at `0c35caab9feda2f8bfeb212dbbe19569a8b8fea7`;
  no push was performed.
- Committed but unpushed: every local commit after the remote baseline,
  including `1ccb544`, `289a25a` (private mapping ownership and view-range
  guards), `e5f26d0` (MSC texture-min-LOD compatibility request), `a3fae81`
  (new boundary and feedback fixtures), `e1ef9aa` (audit ledger updates), and
  the report/documentation commits through final HEAD.
- Staged at delivery: none.
- Unstaged at delivery: none.
- Untracked at delivery: `.porting/`, pre-existing local milestone material;
  it was not staged.
- Destructive history/worktree operations: `NONE`.

The audited source tree was built from the local checkout, not from the remote
baseline. The executable tests were run from an isolated temporary directory
without the stale DLL copies present in `build/tests/dx12`; the existing Wine
prefix was synchronized in place and was not deleted or recreated.

## External Findings Disposition

- **All-packed `StandardTileShape`: FIXED / SPEC_CLOSED.** DXMT now returns
  `D3D12_TILE_SHAPE{0, 0, 0}` when `NumStandardMips == 0`. The GTX 1650
  nonzero shape is retained as `NATIVE_OBSERVED` adapter behavior only.
- **Heap lifetime test ordering: KEPT.** The packed fixture keeps heap B alive
  through the GPU fence that consumes the remap. Releasing the final public
  reference earlier was invalid application lifetime ordering.
- **Per-mapping public heap references: NARROW FIX / LOCAL_PASS.** Reserved
  buffer and texture mapping tables now use `Com<MTLD3D12Heap, false>`, which
  retains the DXMT heap privately through `AddRefPrivate`/`ReleasePrivate`
  without adding a public COM reference per mapping. The focused buffer check
  remains at public reference count 1 after mapping.
- **Tier 2 array restriction: SPEC_CLOSED.** A tiled texture with a
  sub-standard mip cannot have an array size greater than one at Tier 2. The
  `192x128 R32_UINT mips2 array2` negative test remains `E_INVALIDARG`; no
  array-packed-tail implementation was started.
- **Fractional `ResourceMinLODClamp`: FIX_REQUIRED source defect corrected;
  SRV runtime UNVERIFIED.** The integer truncation was removed, and an
  independent ordinary/reserved fixture covers `0.0`, `0.5`, `1.0`, `1.5`, and
  `MostDetailedMip`. MSC 4.0.1 on the current M1 host ignores the SRV minLOD
  metadata, so the result is not promoted to a semantic pass.
- **Standard-only mixed-resource views: LOCAL_PASS.** View representability is
  checked after the SRV/UAV mip range is known. The mapped standard mip of the
  mixed resource reads correctly; incompatible packed-tail views remain
  rejected/null, while the current host's matching-boundary mixed view is
  representable.
- **Filtering footprint: LOCAL_PASS for the representative fixture.** A true
  linear sampler now covers fully mapped, fully NULL, and 50/50 mapped/NULL
  footprints, checking both the sampled value and
  `CheckAccessFullyMapped`.
- **Raw/structured feedback: SPEC_CLOSED at the D3D/HLSL boundary;
  BLOCKED_BY_ARCHITECTURE / DESIGN_REQUIRED in DXMT.** The D3D/HLSL status ABI
  is defined, but the current direct buffer AIR lowering has no residency
  sideband. No always-fully-mapped fallback was added.

## Specification vs Oracle

The primary contract is the Microsoft API/HLSL specification. The native
adapter run is evidence about one implementation, and the DXMT/M1 result is a
separate implementation/runtime observation.

- Microsoft documents that an entirely packed resource has no defined
  standard tile shape and that D3D12 returns zero members. See
  [ID3D12Device::GetResourceTiling](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getresourcetiling).
- Microsoft Tier 2 disallows an array when any mip is smaller than the
  standard tile shape. See [Tier 2 tiled resources](https://learn.microsoft.com/en-us/windows/win32/direct3d11/tier-2).
- The supplied native Windows oracle ran on an NVIDIA GeForce GTX 1650
  (`vendor=0x10de`, `device=0x1f0a`) with `TiledResourcesTier=3` and observed:

  ```text
  64x64 R32 mips4 array1: total=1 standard=0 packed=4 packedTiles=1 start=0 shape=128x128x1
  192x128 R32 mips2 array1: total=3 standard=1 packed=1 packedTiles=1 start=2 shape=128x128x1
  192x128 R32 mips2 array2: E_INVALIDARG
  ```

- DXMT therefore returns zero for the all-packed shape, preserves the Tier 2
  array rejection, and treats the GTX 1650 nonzero all-packed shape as
  `NATIVE_OBSERVED`, not as a production requirement.
- The HLSL status contract is defined by the documented
  [tiled-resources shader exposure](https://learn.microsoft.com/fi-fi/windows/win32/direct3d11/hlsl-tiled-resources-exposure)
  and [CheckAccessFullyMapped](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/checkaccessfullymapped)
  boundary. The unresolved question is DXMT's Metal/AIR lowering, not the
  D3D/HLSL contract.
- Microsoft's heap contract distinguishes the application's final public
  heap reference from the reserved resource's internal mapping bookkeeping;
  [CreateHeap](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createheap)
  remains the lifetime reference.

## Packed Tail Compatibility Matrix

| Descriptor | D3D standard/packed | D3D packed tiles/start | Metal first mip in tail | Metal tail bytes | Standard-only view | Packed-intersecting view | Mapping result |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- |
| `64x64 R32_UINT mips4 array1` | `0 / 4` | `1 / 0` | `1` | `65536` | N/A | `E_NOTIMPL` in the incompatible all-packed path | `E_NOTIMPL`; fixture exits 1 with `got 0x0/0x0` rather than inventing an offset |
| `192x128 R32_UINT mips2 array1` | `1 / 1` | `1 / 2` | `1` | `65536` | Reads `0x12345678` | Representable on this host; mip0 reads `0x12345678`, mip1 is `0x0` | Standard tile mapping succeeds; packed support remains conditional |
| `192x128 R32_UINT mips2 array2` | creation rejected | N/A | N/A | N/A | N/A | `E_INVALIDARG` | Normative Tier 2 boundary |

The current M1 run reports matching 65536-byte tails for the mixed case but
that equality alone does not prove packed pixel/tile equivalence. The all-packed
case reports the incompatible boundary `d3d_first=0`, `metal_first=1`, so the
packed sparse operation is not submitted. No packed-tail offset mapping is
assumed.

The focused mixed-view output was:

```text
Tier2 mixed-mip array rejection: E_INVALIDARG
mixed texture tiling: total=3 standard_mips=1 packed_mips=1 packed_tiles=1 shape=128x128x1
standard-only view readback: 0x12345678 packed-intersecting descriptor mip0=0x12345678 mip1=0x0
standard-only mixed-resource view passed; packed-intersecting view was representable
```

## LOD Clamp

`tests/dx12/dx12_texture_lod_clamp.cpp` independently exercises ordinary and
reserved standard-mip textures. The first four SRVs use
`ResourceMinLODClamp = 0.0, 0.5, 1.0, 1.5`; the fifth uses
`MostDetailedMip = 1` with a zero resource clamp.

Expected and current ordinary output:

```text
ordinary ResourceMinLODClamp outputs: 0x3f800000 0x3f800000 0x3f800000 0x3f800000 0x40400000
expected:                         0x3f800000 0x40000000 0x40400000 0x40800000 0x40400000
```

Expected and current reserved output:

```text
reserved standard-mip ResourceMinLODClamp outputs: 0x3f800000 0x3f800000 0x3f800000 0x3f800000 0x40400000
expected:                                          0x3f800000 0x40000000 0x40400000 0x40400000 0x40400000
```

The `MostDetailedMip` case itself returns `3.0` (`0x40400000`) in both
textures. The four min-LOD clamp cases are not semantically validated because
the current MSC 4.0.1/M1 runtime ignores the descriptor minLOD during final
shader generation. The optional `IRCompilerSetCompatibilityFlags` call is
present, but did not change this host result. The local MSC manual at
`/opt/metal-shaderconverter/docs/UserManual.md` documents that
`minLODClamp` with texture reads may be unsupported.

The separate tiled per-sample clamp path is covered by the passing status
fixture: `SampleGrad` without the clamp returns `1.0` (`0x3f800000`) and with
the per-sample clamp returns `3.0` (`0x40400000`), both with a fully mapped
status. This is not evidence that SRV `ResourceMinLODClamp` is supported.

## Status Feedback

The authoritative GPU output from `dx12_tiled_status_sm5` was:

```text
status texture tiling: total=2 standard=128x128x1 subresource=2x1 start=0
feedback outputs: 0x3f800000 0xffffffff 0x3f800000 0xffffffff 0x0 0x0 0x0 0x0 0x40000000 0xffffffff 0x0 0x0 0x3f000000 0x0 0x3f800000 0xffffffff 0x40400000 0xffffffff
DXBC cs_5_0 tiled Load/Sample/linear-footprint/per-sample-LOD feedback and CheckAccessFullyMapped passed
```

Interpretation by operation class:

- `Load`: mapped value `1.0`/true and NULL value `0.0`/false.
- Point `SampleLevel`: mapped value `1.0`/true and NULL value `0.0`/false.
- Linear fully mapped footprint: `2.0`/true.
- Linear fully NULL footprint: `0.0`/false.
- Linear 50/50 mapped/NULL footprint: `0.5`/false.
- Tiled per-sample `SampleGrad`: unclamped `1.0` and clamped `3.0`, both
  fully mapped. The test asserts the exact opaque predicate, not native raw
  status bits.
- `Gather`, comparison sampling, and typed UAV load have parser/lowering
  evidence in the source but no independent GPU runtime proof in this round:
  `PARTIAL`, not blanket PASS.
- Ordinary non-tiled sampling is exercised by the LOD fixture, but it is not a
  dedicated status-sideband test. Additional status coverage remains useful.
- Raw/structured loads: the D3D/HLSL status overloads are
  `SPEC_CLOSED`; DXMT's direct pointer load path remains
  `BLOCKED_BY_ARCHITECTURE / DESIGN_REQUIRED`.

## Heap Lifetime / Ownership

- **Application public refs:** The application-owned public heap reference is
  kept while a queued mapping or GPU copy can use the heap. The packed test
  keeps heap B through the remap fence, then releases it.
- **TileMapping ownership:** `TileMapping` and `TileUpdate` retain
  `Com<MTLD3D12Heap, false>` objects. This is a DXMT private implementation
  reference, not a public `ID3D12Heap::AddRef`.
- **Submission ownership:** Mapping commands are submitted through the WMT
  sparse mapping queue and are ordered with the command-queue/fence path. The
  private heap object owns the WMT heap and lazily-created tile backing buffer.
- **Private/internal lifetime:** `AddRefPrivate`/`ReleasePrivate` keep the heap
  object and its WMT backing resources alive for mapping-table bookkeeping after
  the application's public reference is released at a legal time.
- **Release-after-fence test:** `dx12_reserved_buffer` prints
  `reserved buffer mapping kept heap public ref count at 1` and exits 0;
  `dx12_reserved_texture_packed` performs the same check before its expected
  packed-tail failure.
- **Remaining mismatch:** The public-reference-per-mapping issue is addressed
  narrowly. Packed-tail physical layout equivalence is still unresolved and is
  independent of heap object lifetime.

## Targeted Tests

All runtime commands below used the custom Wine runner
`/Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wine`, the existing prefix
`/Users/zhangbo/Documents/Vibe-Codeding/wineprefix`,
`DXMT_SHADER_CACHE=0`, and `WINEDEBUG=-all`; runtime staging was isolated from
the build-directory DLL copies.

### Formal local gate

Command:

```text
env DXMT_WINE=/Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wine WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix DXMT_SHADER_CACHE=0 WINEDEBUG=-all python3 tests/dx12/dx12_tiled_tier2_gate --build-dir build/tests/dx12 --timeout 60
```

Exit code: `1`. Classification: `TIER2_GATE=NOT_SATISFIED`.

```text
[PASS] resource-tiling-and-boundaries
[PASS] reserved-buffer-descriptor-table
[PASS] reserved-buffer-copy-mapping
[PASS] reserved-buffer-root-srv
[PASS] reserved-buffer-root-uav
[PASS] reserved-texture-standard-multi-heap
[PASS] reserved-texture-array
[BLOCKED_BY_ARCHITECTURE] reserved-texture-packed-tail
[PASS] shader-status-feedback-texture
[BLOCKED_BY_ARCHITECTURE] raw-structured-buffer-feedback
[PASS] sparse-filtering-footprint
[UNVERIFIED / MISSING_VALIDATION] sparse-lod-clamp
[NEEDS_WINDOWS_ORACLE] native-windows-runtime-oracle

TIER2_GATE=NOT_SATISFIED
TiledResourcesTier remains D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED
non-passing mandatory cases:
  BLOCKED_BY_ARCHITECTURE: reserved-texture-packed-tail
  BLOCKED_BY_ARCHITECTURE: raw-structured-buffer-feedback
  UNVERIFIED / MISSING_VALIDATION: sparse-lod-clamp
  NEEDS_WINDOWS_ORACLE: native-windows-runtime-oracle
```

### Focused closure cases

- `dx12_reserved_texture_view_range.exe reserved_texture.view_range.cs.cso`:
  exit `0`; mixed-array rejection, standard-only readback, and packed-view
  boundary output shown above; `LOCAL_PASS` for the current representable
  mixed case.
- `dx12_tiled_status_sm5.exe`: exit `0`; exact feedback output is recorded in
  **Status Feedback**; `LOCAL_PASS` for representative Load, point/linear
  sampling, filtering footprint, per-sample LOD, and opaque predicates.
- `dx12_texture_lod_clamp.exe texture_lod_clamp.cs.cso`: exit `1`; exact
  ordinary/reserved output is recorded in **LOD Clamp**;
  `UNVERIFIED / MISSING_VALIDATION` due the MSC host limitation.
- `dx12_reserved_texture_packed.exe reserved_texture.packed.read_phase.cs.cso
  reserved_texture.packed.write_phase_a.cs.cso
  reserved_texture.packed.write_phase_b.cs.cso`: exit `1`; public heap check
  passes, then packed shader access remains `0x0/0x0` versus
  `0x11112222`; `BLOCKED_BY_ARCHITECTURE`.
- `dx12_reserved_buffer.exe reserved_buffer.probe_read.cs.cso
  reserved_buffer.probe_write.cs.cso`: exit `0`; public heap count remains 1,
  all remap/NULL/root/CopyTiles phases pass.

## Regression

- `ninja -C build all`: exit `0`.
- `ninja -C build-no-private all`: exit `0`.
- The private x64 and no-private x64 selected targets, including DX12 DLLs,
  reserved-buffer/texture fixtures, status, LOD, view-range, and resource
  tests, built without compiler warnings or errors.
- Isolated x64 Wine regressions exit `0`: reserved buffer,
  `CopyTileMappings`, standard reserved texture, mip/array texture,
  allocation/resource tests, descriptor ownership, descriptor stress,
  texture CPU access, predication, enhanced barriers, compute SM6,
  `dx12_texture_sampler`, SM5 graphics `--all`, pipeline statistics, tiled
  status, view-range, and present oracle. The exact success lines include:

  ```text
  D3D12 descriptor heap registry stress passed: 1024 live heaps + 16K shader-visible descriptors
  D3D12 predication draw, dispatch and ExecuteIndirect semantics passed
  DXBC SM5 geometry-root-srv-uav readback passed: 0xff00ff00
  create.present_target=0x00000000
  ```

- D3D11 build coverage is included in the two full builds and exits 0. The
  practical runtime smoke was attempted from the isolated directory with
  `timeout 15s ... wine ./dx11_tri.exe`; it returned `124` with no test output.
  It is recorded as `UNVERIFIED`, never as PASS. No prefix recreation or
  destructive workaround was attempted.
- Native Windows: no new oracle execution was available in this local round;
  the supplied GTX 1650 observations are retained as `NATIVE_OBSERVED` and
  separated from the Microsoft normative rules.

## Hosted CI

- Baseline exact SHA: `0c35caab9feda2f8bfeb212dbbe19569a8b8fea7`.
- Run: [CI Build #118](https://github.com/1146839821/dxmt/actions/runs/34584989293).
- Overall status: `FAILURE`.
- Failing job: `build-clang-release-arm64-macos`.
- Failing step: `Install Metal Toolchain`; the build step was skipped.
- All listed x86/x64/ARM64EC cross-build jobs and `package` completed
  successfully. The observed failure occurred before source compilation and
  is classified as `INFRASTRUCTURE_FAILURE`, not a demonstrated code compile
  failure.
- The forward local commits after the baseline were not pushed, so the
  final local SHA has `HOSTED_CI = NOT_AVAILABLE_FOR_LOCAL_HEAD`; run #118 is
  not claimed as validation of the new code.

## Capability Impact

- `TiledResourcesTier`: remains
  `D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED`.
- Feature Level: unchanged.
- `HighestShaderModel`: unchanged.
- WaveOps, Int64, Atomic64, Native16, Typed UAV capability exposure, ROV,
  Conservative Raster, DXR, Mesh, VRS, Sampler Feedback, and Agility:
  unchanged.

## Gate Decision

`TIER2_CLOSURE_STILL_BLOCKED`

The local gate is intentionally not a capability-enabling gate. The remaining
mandatory evidence is listed below, and no Tier 2 capability bit is changed in
response to the local passing cases.

## Remaining Blockers

### P1 — raw/structured buffer status requires a shader-visible residency design

Evidence: `src/airconv/dxbc_instructions.hpp` has no feedback field on
`InstLoadRaw`/`InstLoadStructured`; `src/airconv/nt/dxbc_converter_base.cpp`
lowers these operations as direct pointer loads without `StoreFeedback`; and
`BufferResourceHandle` currently carries pointer/metadata/structure-stride,
not a residency sideband. The D3D/HLSL contract itself is closed by the
documented [ByteAddressBuffer load](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-object-byteaddressbuffer-load-2),
[StructuredBuffer load](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/structuredbuffer-load-float-uint-),
and [CheckAccessFullyMapped](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/checkaccessfullymapped)
requirements.

The required design answers are:

1. **Metal 3/AIR sparse-buffer residency result:** no proven exposed result
   exists on the current Metal 3/AIR target. The existing residency sideband is
   on texture operations, not direct buffer loads.
2. **Texture-buffer-like representation:** no proven equivalent is available
   for arbitrary raw/structured byte addressing. A texel-buffer conversion
   would change the binding/format/byte-address contract and cannot be assumed.
3. **Software page table:** with the current target, a software page table or
   equivalent shadow-materialization path is required to preserve partial
   residency semantics.
4. **Existing shadow/mapping metadata:** CPU `tile_mappings_` is not
   shader-visible today. A GPU page-record buffer would need to be generated or
   updated when mappings change, alongside the existing shadow backing data.
5. **Descriptor-table SRV/UAV:** descriptor records would need a page-table
   base, logical byte extent, tile size, and shadow-data/base metadata; the
   lowered load would consult that record and aggregate validity.
6. **Root SRV/UAV with only GPU VA:** a root GPU VA alone is insufficient to
   locate residency metadata. The ABI would need a VA-to-resource metadata
   table or an expanded root descriptor carrying the page-table handle; this is
   a major root-descriptor change.
7. **Non-reserved buffers:** ordinary fully resident buffers can use a constant
   fully-mapped status with no page-table lookup.
8. **Vector loads crossing tile boundaries:** split the access at 64 KiB page
   boundaries, load each covered page, OR the validity predicate, and supply
   zero/default bytes for NULL pages.
9. **Structured/raw accesses spanning mapped and NULL tiles:** apply the same
   per-byte or per-tile validity rule to the complete accessed range; return
   the defined default data for unmapped bytes and a non-fully-mapped opaque
   status if any accessed byte is not resident.
10. **Cost:** an extra indirection and validity branch for reserved-buffer
    loads, page-table update traffic, possible access splitting, larger
    descriptor/root payloads, and page-table cache pressure. The cost must be
    measured before capability exposure.

Concrete staged proposal for a future milestone:

1. Define a private reserved-buffer resource ABI containing shadow data and a
   GPU page table; add scalar, vector-crossing, structured, descriptor-table,
   and root-descriptor tests without changing capability bits.
2. Teach DXBC/AIR lowering to select this ABI only for reserved buffers,
   aggregate validity through a canonical internal predicate, and preserve
   `CheckAccessFullyMapped` semantics. Keep ordinary buffers on the constant
   fully-mapped path.
3. Add Windows-oracle comparisons and performance measurements, then optimize
   page-table layout/coalescing or adopt a future native primitive if one is
   proven. Until these stages exist, the blocker remains
   `BLOCKED_BY_ARCHITECTURE / DESIGN_REQUIRED`.

### P1 — packed-tail layout equivalence on Metal 3

The all-packed M1 observation has `d3d_first=0` and `metal_first=1` even
though both tails report 65536 bytes. The current implementation correctly
rejects the packed shader-visible path rather than inventing an offset. Next
action: obtain a Metal-compatible physical layout/translation or retain the
conservative rejection; do not implement packed `CopyTiles` or raise the
advertised tier based on the GTX 1650 result.

### P1 — SRV `ResourceMinLODClamp` oracle/compiler support

The source-level float truncation defect is fixed and the matrix is present,
but MSC 4.0.1 on M1 ignores the descriptor minLOD. Next action: validate the
same ordinary/reserved matrix with a compiler/runtime that honors the
descriptor, ideally a native Windows oracle and debug layer. The independent
tiled per-sample clamp is already a local pass and must remain a separate
claim.

### P2 — representative status breadth and native comparison

Gather, comparison sampling, typed UAV load, and a dedicated ordinary-resource
status case need GPU runtime proof. A native Windows debug-layer comparison is
also absent. Add focused cases by AIR equivalence class rather than a
Cartesian product, and continue asserting `CheckAccessFullyMapped` rather
than raw status bits.

### P2 — D3D11 runtime smoke environment

The shared-AIR build is green, but `dx11_tri.exe` timed out after 15 seconds
with no output on this Wine/GUI setup. Re-run in a meaningful non-interactive
runtime environment before using the overall milestone as capability-enabling
evidence.

No capability bits changed.
No next-phase work started.
Ready for external audit.

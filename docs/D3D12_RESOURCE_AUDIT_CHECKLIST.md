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

### N09 — structured UAV counter residency, 2026-10-23

`UAVBufferCPUStorage` now carries the counter identity owned by the existing
descriptor heap's `counter_resources_`. Both recording-time resource encoding
and submit-time volatile descriptor resolution include its native buffer with
read/write usage. Descriptor copies already copy the payload and owning refs
under the same locks; no additional lifetime owner or heap scan was added.
Replacing the descriptor with an SRV or a counterless UAV clears the identity.
The x64 CPU descriptor remains 48 bytes; GPU descriptor layouts are unchanged.

The existing `dx12_airconv_compute_residency` runner now records counter A,
copies a CPU-only descriptor containing counter B after Close, checks both the
recorded references and submit-latest resource declarations before submitting,
and reads back four exact shader results plus both counters. After a fence it
reuses the slot with a direct B-to-A update. The original DLL fails the resource
declaration assertions before GPU dispatch; the fixed DLL passes with Metal API
and shader validation enabled. Counter values are A=0/B=4, then A=4/B=4, with
sorted output 100/101/102/103 in both passes. The source CPU heap is released
after the copy. Application resource references remain alive through the fence.

Ownership, registry stress, and AIRCONV/MSC mixed shader-runtime runners also
exit 0 on the matched private WineMetal runtime. This validates the SM5 counter
path, not DXIL counter lowering or a capability promotion. FL/SM/Tiers are unchanged.
External evidence: `../dxmt-test/n09-20261023/N09-counter-residency.md`.
The warm collector control probe retains one heap lock and 2/21/257 reachable
slots at heap capacities 258/4096/65536. The 257-slot conservative case increases
from about 11.2 to 11.7 microseconds; this measured cost is retained rather than
claiming a performance improvement. The new counter case adds one live native
resource declaration, one heap lock, and zero descriptor VA lookups per call.

### N09 — bounded nonuniform descriptor mutation oracle, 2026-10-23

The existing `dx12_compute_sm6 --direct-indexed-nonuniform` runner now dispatches
one group for its `numthreads(2,1,1)` fixture. The old two-group dispatch produced
four threads against a three-slot heap, including a UAV interpreted as SRV and
an out-of-heap descriptor index. That invalid workload was established from
source and DXIL metadata; it was not deliberately executed for a GPU failure.
The runner also sets directly indexed descriptor heaps before their root
signature, as required by the SM6.6 Dynamic Resources contract.

The same fixture now verifies two legal timings with different native input
buffers: after Close, CPU-only heap descriptor copies select B and read back
1888/1777; after a completed fence, direct descriptor updates select A and replay
the same closed list to read back 777/888. Readback uses explicit UAV-to-copy and
copy-to-UAV transitions so replay starts in the expected resource state. The
source CPU heap is released after copying; both input resources remain alive
through GPU completion. Fences have bounded waits and device-removal checks.

Metal API/shader validation passes both timings. A constant-index shader and an
external variant omitting the completed-slot update independently fail the two
oracles with exit 1. Direct-indexed and descriptor-table adjacent cases pass.
Production code, descriptor ownership, backend routing, and capabilities are
unchanged; these internal probes do not establish complete SM6.6 support.

Direct indexing still visits all slots because no exact reachable footprint is
available. At capacities 3/4096/65536 the warm collector takes approximately
1.7/40/604 microseconds, one batch lock, and no descriptor VA lookup per call.
This is an unresolved capacity cost, not a performance improvement or a closed
N09 exit gate. The next candidate is a heap/encoder submission intent that
avoids per-slot pending records while preserving updates to previously empty
slots. External evidence: `../dxmt-test/n09-nonuniform-20261023/`.

### N09 — compact direct-indexed residency intents, 2026-10-23

Direct-indexed recording now retains one whole-heap intent per existing
encoder/heap/binding-context key in `PendingDescriptorUseKey`, using `UINT_MAX`
instead of allocating and hashing one pending entry per slot. Recording still
encodes every current resource. At submission, the existing retained heap and
batch lock resolve its latest descriptors, including slots that were null at
recording time. Exact table slots and whole-heap intents share one slot read;
resource references and encoder usage/stage masks still use the existing
submission owner. Reuse counters count actual fanout minus unique slot reads,
not compact intent count minus slot reads.

The existing SM6 nonuniform runner records slot 1 as null, then copies A[1]/B[0]
after Close. A pre-submit check requires B's native resource in the collector's
read uses, and GPU readback requires 888/1777. After a completed fence, updating
both slots to A and replaying the same closed list requires 777/888. The mixed
mode also binds an overlapping two-slot volatile descriptor table: each slot is
read once, while both binding forms receive their uses. These cases pass on
both baseline and changed DLLs with Metal API/shader validation. The existing
SM5 direct-indexed case now crosses an encoder boundary: two intents resolve
four slots once and fan out eight uses; its compact-count assertion fails on
the baseline before GPU submission and its pixel oracle passes on the change.

On the same M1 Max and isolated matched Wine 11 runtime, three alternating
baseline/change runs give collector median-of-medians (microseconds):

| Heap slots | Baseline | Compact intents | Pending entries before/after |
| --- | ---: | ---: | ---: |
| 3 | 1.59 | 1.57 | 3 / 1 |
| 4096 | 38.80 | 23.65 | 4096 / 1 |
| 65536 | 582.10 | 341.90 | 65536 / 1 |

Each call still reads N slots with one batch lock and zero descriptor VA
lookups. All 18 timing runs verify both GPU readbacks. These collector timings
exclude GPU completion and do not establish overall frame performance. Three
whole Execute calls per condition are too few for a latency percentile claim.
Table controls retain 2/21/257 slot reads independent of heap capacity; their
medians remain approximately 0.23/1.4/12 microseconds.

Negative variants independently reject an unchanged null slot before GPU
submission, a constant-index shader, and an omitted fenced replay update.
Related graphics table/encoder/range cases, UAV counter/array compute probes,
and descriptor ownership/registry stress pass. Scoped builds and diff checks
pass. No public capability, shader backend, resource owner, or bridge ABI
changes. Full direct-heap scans remain O(N), so this is a partial N09 performance
slice, not the N09 exit gate or complete SM6.6 support. External commands,
artifacts, hashes, and raw timings: `../dxmt-test/n09-intents-20261023/`.

### N09 — heap-owned resource-slot index, 2026-10-23

The existing descriptor heap maintains a hierarchical bitmap under its descriptor
mutex. Level zero marks resource-bearing slots; upper levels mark nonempty words.
View writes, null replacement, and copies update it alongside the existing payload
and owners, before publishing the mutation generation. It contains no resource
references and introduces no per-stage cache. The locked batch snapshots density
after acquiring the mutex. An empty recording still retains one whole-heap intent,
so Close-to-Execute population and fenced replay see the latest descriptors.

For capacity N and population P, sparse walks use ordered bitmap candidates in
O(P log64 N); at P >= ceil(N/4), sequential walking bounds candidates by N <= 4P.
The submit loop merges ordered table slots and direct candidates, reading their
union once under one batch lock per heap. Updates have at most six bitmap levels
for UINT slot indices, with no update-time allocation. Heap creation allocates and
zeros the bitmap: 15,875 uint64 words (127,000 bytes) at one million slots, plus
constant metadata. Existing descriptor and native resource owners remain unchanged.

Three alternating old/new pairs per scene, each with 20 warmups and 100 collector
samples, produced these median-of-medians on M1 Max with matched isolated Wine.
The same executable and shader ran against both DLLs; all 60 processes passed
888/1777 pre-submit and 777/888 fenced-replay GPU readbacks.

| Capacity / population | Baseline us | Bitmap us | Candidate slots before / after |
| --- | ---: | ---: | ---: |
| 3/sparse/default | 7.87 | 7.71 | 3 / 3 |
| 4096/sparse/default | 30.05 | 8.05 | 4096 / 3 |
| 65536/sparse/default | 346.10 | 8.30 | 65536 / 3 |
| 1000000/sparse/default | 5303.50 | 8.60 | 1000000 / 3 |
| 65536/ordered/default | 1457.00 | 1415.20 | 65536 / 65536 |
| 65536/shuffle/default | 1397.50 | 1330.90 | 65536 / 65536 |
| 65536/shuffle/16383 | 738.10 | 356.70 | 65536 / 16383 |
| 65536/shuffle/16384 | 762.70 | 760.10 | 65536 / 65536 |
| 1000000/shuffle/65536 | 6850.70 | 3241.30 | 1000000 / 65536 |
| 1000000/shuffle/249999 | 11365.50 | 5733.00 | 1000000 / 249999 |

Every collector call retains one pending direct intent, takes one heap batch lock,
and performs zero descriptor VA lookups. Sparse scenes record an entirely empty
heap, so compare these paired measurements rather than earlier fixtures. Cold
heap creation, single Dispatch/Execute samples, mutation timings and p95 ranges
are preserved in external performance-summary.json; these are CPU collector
measurements, with no frame-rate or end-to-end GPU throughput claim.

Metal validation passes empty-at-recording, mixed table/direct with a null table
slot, and the 128-slot graphics encoder case. The ownership runner extension
checks seven view classes, 5000 mixed mutations, sparse/dense transitions, <=4P
candidate bounds, same-heap CPU copies and reader/writer exclusion. A 999,983-slot
CPU heap checks bit/word/directory boundaries at 63/64, 4095/4096 and
262143/262144, last-slot padding, and full removal back to an empty root.
Counter, ownership, stress, adjacent GPU cases, three expected-failure controls
and ordinary-table capacity controls pass. Scoped builds pass. Public COM and
WineMetal ABIs, shader routing and capability declarations are unchanged; private
C++ read-batch consumers were rebuilt together. AS execution and full N09/FL12_2
gates remain unproven. Evidence:
`../dxmt-test/n09-resource-index-20261023/N09-resource-slot-index.md`.

### N09 — persistent resource-index contract, 2026-10-23

The existing `dx12_descriptor_ownership` runner now carries the independent
expected-slot oracle previously kept externally. It checks seven view classes,
null replacement/copies, 256 mixed updates, sparse/dense transitions, sorted and
bounded iteration, CPU same-heap copies and four-level directory boundaries in
a 262,147-slot CPU heap. The guarded concurrent writer now clears its slot while
the read batch protects both payload and index traversal. Expected sets derive
from API operations; no bitmap internals are read by the test.

Scoped build and the ownership runner pass. Empty-at-recording and mixed
nonuniform GPU cases pass 888/1777 then 777/888 fenced replay; sparse graphics
passes its pixel/counter assertions, all with Metal validation enabled. The
production DLL hash is identical to the previous slice, with no new runtime
cost or capability change. This preserves regression evidence rather than
claiming full N09 completion. Commands, hashes and logs:
`../dxmt-test/n09-index-contract-20261023/N09-index-contract.md`.

### Earlier validation notes

- Build targets used for the current slice are `build`, `build-no-private`,
  and `build-X86`; current runtime conclusions are x64 and no-private only.
- The test executable directories contain stale local DLL copies. Run the
  executable from an isolated directory without those copies, or otherwise
  verify the loaded DLL path before trusting a failure.
- `git diff --check` is required for each slice. Whole-file clang-format checks
  currently report pre-existing violations in these large test/source files;
  do not reformat unrelated lines while closing a slice.

### Runtime identity correction — 2026-10-22

N15 investigated a reproducible CPU write fault during
`CreateReservedResource -> MTLD3D12Texture::InitializeReserved ->
Texture::allocatePlacementSparse`. The cause was deployment ABI mismatch,
not a demonstrated sparse-resource implementation defect. The installed Wine
builtin export writes through the fourth x64 argument (`r9`); the current
`MTLDevice_newPlacementSparseTexture` declaration and caller pass three arguments.
The fault at export offset `+0x0d` is that write. No resource workaround or
production ABI change was made.

An external identity probe inspected the loaded export without creating a GPU
resource. It rejected the installed bridge and accepted the private build:

| Loaded PE image | Image size | Sparse texture export RVA | Identity result |
| --- | --- | --- | --- |
| Installed WineMetal | `0x25000` | `0x2010` | mismatch, exit 1 |
| Current build | `0x1a000` | `0x2090` | match, exit 0 |

The original configuration used `winemetal=b` and `WINEDLLPATH`, but Wine 11.0
searched its installation first. Its PE loading log named the requested
app-local path even though the loaded image matched the installed builtin.
`DYLD_PRINT_LIBRARIES=1` also identified the installed Unix companion.
An isolated Wine installation with the matching PE/Unix pair, and PE builtin
postprocessing, loaded both intended images. The original resource executable
and D3D12 DLL then changed from exit 5 to exit 0. This corrects the earlier
2026-10-22 N02 classification of the crash as a possible resource defect.
See [private-build deployment](DEVELOPMENT.md#testing-a-private-winemetal-build).

Environment: macOS 27.0.1, Xcode 27.1 (the actual installed version), M1 Max /
Apple7, Wine 11.0, repository LLVM/Wine build dependencies, MSC API 4.0.1.
After a scoped rebuild, the following existing runners all exited 0 with the
paired runtime:

- `dx12_resource_tests`: allocation/validation and queue/fence assertions.
- `dx12_reserved_texture`: SM6 shader reads/writes, remap, NULL access,
  cross-queue waits, and exact `CopyTiles` readback.
- `dx12_reserved_buffer`: SM6 shader reads/writes, remap, NULL access,
  backing preservation, cross-queue waits, and exact `CopyTiles` readback.
- `dx12_tiled_status_sm5`: mapped status, linear filtering footprint,
  per-sample LOD, and `CheckAccessFullyMapped` assertions.
- `dx12_feature_support`: public capability consistency assertions.

Commands, DLL/shader hashes, the build-specific identity probe, and raw logs
are retained outside the repository under `dxmt-test/n15-20261022`. No gate
runner was added to the repository. The user's Wine installation and shared
prefix were not modified. Future runtime validation must use the verified
pair; previous runs do not prove behavior of the intended source-built bridge
unless its identity was established.

No production hot path changed, so no performance improvement is claimed; the
crashing configuration is not a meaningful performance baseline. Packed-tail
layout, broader shader status coverage, native Windows comparison, and the
other outstanding contracts above remain open. Feature Level, Shader Model,
and TiledResourcesTier are unchanged; these passes do not establish Tier 2/3.

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
| Packed mip tail | D3D12 packed metadata and logical tile ranges are modeled. Native sparse mapping is attempted only when `firstMipmapInTail` equals the D3D standard-mip boundary and Metal `tailSizeInBytes` equals the D3D packed-tail byte count; otherwise packed views/mappings stay rejected. A mixed resource now allows a representable standard-only SRV while rejecting an incompatible packed-intersecting view. The UAV fixture reports `d3d_first=0`, `metal_first=1`, and 65536-byte tails on both sides. The supplied native Windows oracle on an NVIDIA GeForce GTX 1650 (`0x10de:0x1f0a`, `TiledResourcesTier=3`) observed `64x64 R32 mips4 array1: total=1, standard=0, packed=4, packedTiles=1, start=0, shape=128x128x1` and `192x128 R32 mips2 array1: total=3, standard=1, packed=1, packedTiles=1, start=2, shape=128x128x1`; the array2 create returned `E_INVALIDARG`. Microsoft documents a zero shape for an entirely packed resource, so the native nonzero shape remains an adapter-specific observation; DXMT returns zero. | `PARTIAL`: the read-only copy-upload case below passes locally; the UAV fixture remains `BLOCKED_BY_ARCHITECTURE` for its incompatible layout. Tier2 array rejection remains a normative boundary. |
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
M1 host in default UAV mode the D3D and Metal tail boundaries are incompatible
(`d3d_first=0`, `metal_first=1`, both tails 65536 bytes), so the production
view/mapping path returns `E_NOTIMPL` instead of assuming an offset. The
fixture exits 1 with the expected packed shader mismatch (`got 0x0/0x0`) and
is classified `BLOCKED_BY_ARCHITECTURE`, not as a semantic pass.

### Packed-tail usage and failure verdicts — 2026-10-23

N15 extended the existing packed fixture with `--copy-upload`. This creates the
same `64x64 R32_UINT`, four-mip resource without UAV usage and populates mip 1
through `CopyTextureRegion`. The original UAV mode remains available. A native
metadata-only probe on M1 Max/macOS 27.0.1 observed Metal tail start 0 without
shader-write usage and 1 with it; both tails require 65536 bytes. The D3D12
logical start is 0 in both cases. Matching one mode does not validate the other.

Run the existing executable with its three existing shader fixtures and append
`--copy-upload` for the copy case. With the paired runtime documented above:

- Copy mode exited 0: mip-1 GPU reads at `(0,0)` and `(31,31)` returned heap A's
  `0x11112222`, heap B's `0x33334444`, zero after a complete NULL mapping, and
  A's original value after remapping. The heap public-reference check passed;
  heap B stayed alive through its GPU use and unmapping.
- The UAV mode still produced zero instead of the written value and exited 1.
  Native metadata and resource-side `E_NOTIMPL` establish its layout blocker;
  the shader mismatch alone is not evidence of an architectural limitation.
- An external corrupted read shader produced `0xdeadbeef` and exited 1. The
  fixture now treats every GPU value mismatch as failure, rather than returning
  77 and automatically calling it an architecture blocker. Explicit unsupported
  resource-creation HRESULTs retain the existing skip behavior.
- View-range and ordinary reserved-texture GPU regressions exited 0.

Changed files: this checklist and `dx12_reserved_texture_packed.cpp`. Build,
changed-line formatting, and `git diff --check` passed. Raw commands, layout
observations, negative fixtures and hashes remain outside the repository in
`dxmt-test/n15-packed-20261022` (the investigation began October 22).
No production hot path changed; no performance improvement is claimed.
Only the stated mip-1 points and operations were verified. Other mips/formats,
UAV layout equivalence, broader feedback, native Windows comparison and complete
Tier 2/3 coverage remain open. Public capability declarations are unchanged.

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

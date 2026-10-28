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

## Current Device task acceptance — N02, 2026-10-24

Reconciled against `DXMT D3D12：修复 Device相关.md` on base `1b74fa94`.
This is an index of recorded evidence, not a new runtime test or complete
Device acceptance. Historical slice paragraphs describe their own baseline;
their pending items are superseded only by the specific later evidence below.
In particular, the old InfoQueue stub and component MS_LOAD underreport notes
are no longer current defects. Preserve their original red results and costs.

| Requested mechanism | Current bounded result / commits | Remaining acceptance gap |
|---|---|---|
| Device QI, including grouped Device5–10 | `82a9660e`; canonical identity, supported interfaces and rejected legacy queries covered by recorded feature tests | D3D10/11 upstream interface exposure is not a requirement to expose those interfaces from this D3D12 Device; broader D3D11 rendering regression remains NOT_RUN |
| UNKNOWN MSAA counts/flags | `8be5c596`; same-runner red/green, counts0/1/2/4/8, NONE/TILED/invalid flags | Native Windows differential NOT_RUN; do not infer unsupported hardware sample counts |
| Multiple-fence NULL/event ALL/ANY | N14 `5a2f4d9a`; shared notification/cancellation, duplicate/rewound waits, OOM and abandoned ownership covered | Extended T4 concurrency and native Windows differential NOT_RUN |
| InfoQueue COM identity/lifetime | `78f4e9eb`; embedded controlling identity and concurrent query/state test | Full debug-layer compatibility is not established |
| InfoQueue messages/filters/limits/mute | `59ebde1b`, `b627f28d`; owned messages, filter stacks and 12 allocation rollback positions covered | Native counter/filter/default equivalence and persistent/global OOM NOT_RUN |
| InfoQueue break/output | `6458896f`; stored controls, intercepted output/break and reentrant callbacks covered | Real OS debugger delivery NOT_RUN; not a complete Debug Layer |
| Enhanced split execution order | N12 `a262662e`; submission-order pairing, recording/reset isolation, admission failure and lifetime coverage; the N12 resource-order slice below corrects same-resource disjoint barrier acceptance | Direct->Direct fence handoff has corrected enhanced-only N12 readback evidence; cross-queue half-pair rejection reproduced but normative status unresolved; other queue types, extended stress and performance acceptance OPEN; EnhancedBarriersSupported remains FALSE |
| Format report/create/view/footprint consistency | N02/N08/N09 slices below; logical storage/view roles, BC sampling, RGB96 copies, typed UAV eligibility/readback and N08 shared texture extent guards have bounded evidence | Exhaustive format/planar/dimension/allocation coverage and native differential remain OPEN; additional typed-UAV Options remains FALSE |
| Depth/stencil component MS_LOAD | `960ec7b0`, `4fe09340`, `1b74fa94`; sample-mask prerequisite, SM5/SM6 compute and pixel 2x/4x readbacks, four report bits corrected | 8x unsupported on this host; other devices/counts and native differential NOT_RUN; no full format/FL promotion |
| Performance and deployment | Recorded matched-runtime AB/BA evidence, including N13 attribution controls | Historical recording/Submit warnings remain OPEN/INCONCLUSIVE; latest query improvement does not discharge them. Matched no-private x64 Device/resource/SM5/SM6 runtime is covered by the N02 slice below; full no-private T3/T4 and D3D11 rendering NOT_RUN |

Evidence locations are the named N02/N07/N08/N09/N12/N14 slice records below.
The reconciliation manifest verifies ten referenced fix/test commits are HEAD
ancestors and hashes their existing reports:
`../dxmt-test/n02-device-reconciliation-20261024/manifest.json`.
This documentation-only round changes no runtime owner, source/header, ABI,
capability, layout or scheduling. Build/GPU/performance reruns are NOT_RUN;
commit ancestry, evidence-file existence and `git diff --check` are the checks.
The user's `.gitignore` remains outside the commit.

The selected N12 split subresource-range contract slice is recorded below;
same-resource disjoint interleaving is invalid, while complete sequential pairs
and a matched multi-subresource range remain valid. Cross-queue handoff is a
separate remaining contract gate before any enhanced-barrier exposure.
Missing Windows/multi-device evidence remains explicitly missing,
and does not justify another unchanged local MS_LOAD or Submit timing matrix.

## Completed Slices

- [x] N09 typed-buffer format admission rejects unsupported native SRV/UAV
  formats before publication. See the 2026-10-27 slice below; BGRA SRV usage,
  RGB96 execution and BUFFER query reporting remain separate OPEN items.

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

### N13 — allocation failure and allocator rollback, 2026-10-23

`ExecuteCommandLists` previously caught only its first allocation. Injected
metadata failures escaped the COM entry, and translation cache failures after
allocator marking left in-flight counts set. The live descriptor collector's
returned E_OUTOFMEMORY was also silently dropped with a healthy device reason.

The existing translation lifetime guard now rolls back uncommitted allocator
marks on returns and exceptions; successful commit transfers the obligation to
queue completion. One function-level bad_alloc boundary and the collector's
failure path preserve the existing OOM removal policy. The two manual abort
calls/helper are removed. No new lock, allocation, scan or encoder is added.

The existing command-list runner adds opt-in TLS-scoped, one-shot malloc
failure and explicit translation-abort/replay controls, with matching private
include paths in Meson. Same-executable old/new checks reproduce and fix the
metadata/post-mark failures. External sweeps cover 6 empty, 9 clear and 16
multi-list descriptor allocation positions; fixed outcomes contain exceptions,
clear allocator counts and report E_OUTOFMEMORY. Non-OOM rejection can still
restore and replay its recording. Full command-list, enhanced-barrier and
ownership runners pass; empty/mixed nonuniform GPU readbacks pass 888/1777 then
777/888, with Metal validation enabled.

Three paired CPU submission runs per scene, with GPU correctness, show median
changes of -2.27% to +1.23% across no-op, ordinary/exact/conservative descriptor
and mixed occlusion scenes. Small differences are INCONCLUSIVE; no sustained
regression or speedup is claimed. Persistent/global OOM, native Metal allocation
failures and full N13/FL12_2 remain unproven. Capabilities and ABI are unchanged.
Commands, raw results, timing/P95 and binary hashes:
`../dxmt-test/n13-submit-failure-20261023/N13-submit-failure.md`.

### N13 — nullable native command-buffer creation, 2026-10-23

The Metal SDK declares both native queue factories nullable. Previously a zero
handle reached commit; completion did not report an error, Signal/Wait returned
S_OK and pending fence events stayed unsignaled. The same existing command-list
runner reproduces all four entries (Execute/Signal/Wait/calibration) in both
ordinary and error-options modes: eight expected failures on `0158ad1d`.

The private checked `NewCommandBuffer` now marks generic device removal and all
six callers return before encoding, fence-wait registration, sparse timeline or
presenter work. The factory provides no concrete failure cause, so this does not
assert OOM. Execute retains its existing translation guard, before allocator
marking. No new scheduler, state machine, lock, scan, allocation or encoder.
The existing test-only IAT patch helper is reused for malloc/native factories;
scoped zero-return injection records nil commits without forwarding them.

Fixed: eight failure cases pass (no nil commits, no allocator count, removed
device, pending fence wakes), plus nine allocation/abort/success and adjacent
runner processes. GPU copy/transition marker, query replay, barrier and ownership
assertions pass. Twelve paired performance processes retain GPU assertions;
aggregate median changes -11.82% to -3.14%, but third-pair occlusion regresses
and run variance is high: performance is INCONCLUSIVE, with no speedup claim.
Present/sparse failure injection, sustained exhaustion and Windows differential
are NOT_RUN. Capabilities and ABI are unchanged; no new src/d3d12 files.
Evidence, exact commands, hashes, environment, raw p50/p95 and rollback notes:
`../dxmt-test/n13-command-buffer-20261023/N13-command-buffer.md`.

### N13 — nullable native encoder creation, 2026-10-23

All fourteen queue-owned native encoder factories now use the same checked
submission-object policy before encoding. Nil marks generic device removal and
returns through existing translation rollback; the AS-specific late check is
removed. No new state, lock, allocation, encoder or src/d3d12 file.

The same existing runner on 391c720a/fixed DLLs reproduces six injected modes in
both diagnostics settings: 12 expected failures old, 12 passes fixed, including
late failure after a valid blit. Checks include zero commits, allocator/recording
retirement and fence notification. Nineteen adjacent processes retain GPU copy,
query, barrier, ownership and SM6 descriptor oracles. AS/sparse/temporal failure
injection and sustained exhaustion are NOT_RUN; public capabilities unchanged.

Unannotated checks regress the original mixed conservative submit scene +9.67%
and +9.06% on confirmation; an isolated control does not erase those results.
A likely-success hint gives 188.45→186.65 us on the original mixed scene.
Other process variance remains high (including query); overall performance is
INCONCLUSIVE. No general speedup claim. Exact evidence, all three paired tables,
commands, limits and environment: external
`../dxmt-test/n13-encoder-failure-20261023/N13-encoder-failure.md`.

### N10 — depth/stencil copy staging capacity, 2026-10-23

Correctness PASS; performance gate OPEN. The same existing runner on the old
DLL demonstrates a >32MiB copy silently omitted despite successful Close and
cumulative ten 4MiB copies failing recording. The final DLL passes twelve
scoped cases: tiny/large/repeated/growing/mip-array copies, separate plane
preservation, multi-list/multi-allocator batches, in-flight Reset, allocation
failure, invalid ranges and submission rollback. Five adjacent processes,
including the resource GPU runner, also pass with Metal validation enabled.

AllocateCopyStaging keeps a bounded first-64KiB arena fast path and reuses
GPU-private tracked scratch scoped to the exact active blit encoder. The
allocator's memo owns no buffer; existing encoder resource_refs retain all
growth versions, and existing submission/Reset lifetime remains authoritative.
The ordinary BlitEncoderData layout and argument allocator fast path are
unchanged. Full matching native packed 2D/array single-sample CopyResource
uses one direct copy per mip/slice (old four commands); plane copies still
stage with 256-byte row/start alignment. No new src/d3d12 file or capability.

Native scratch: one 33,570,816-byte buffer for the large case, one 4MiB for
repeats, one 64KiB for small overflow, three growing versions totaling 5.25MiB.
Tiny and ordinary small blits add zero native buffers. With the final cleanup-
safe fixture, three paired processes give small CPU recording -1.86% and
submit medians -0.59% to +2.67%; ordinary-submit p95 rises +16.1%. The prior
fixture setup order measured recording +10.07% (same-DLL control -0.38%).
That conditional regression is preserved, not dismissed. Overall performance
is INCONCLUSIVE / gate OPEN, with no blanket no-regression claim.
ZZZ/Windows differential and other GPUs are NOT_RUN. A single-slice array DSV
setup has the same stencil anomaly on both DLLs; the direct 2D-view fixture
passes. That independent combination remains unverified. Exact evidence,
commands, all performance variants, scope and rollback:
`../dxmt-test/n10-copy-staging-20261023/N10-copy-staging.md`.

N10 recording-performance follow-up: the existing command-list runner accepts
`--small-blit-cost early|late|early-long|late-long`. The event's owner always
outlives device teardown; creation order varies independently of batch length.
Short batches keep 10,000 copies/9 rounds; long batches use 50,000 copies/25
rounds, below the existing 4MiB CPU arena capacity. Both preserve the GPU
readback/fence oracle and reject native buffer allocation while recording.
The original no-argument cost option remains available.

The same PE on captured old/final DLLs passes all 24 alternating processes.
Median-of-process-median recording deltas are early +1.04%, late +6.02%,
early-long +0.40%, late-long +1.87%. Short early and long late p95 deltas are
+12.00% and +13.28%. These do not close the gate or erase earlier failures.
Normalized instructions match in CopyBufferRegion and its inspected allocator,
device-identity and buffer-description callees; this excludes added instructions
in those bodies, not address/layout/translation effects or scheduling costs.
No production change is justified by this evidence yet. Performance remains
INCONCLUSIVE / OPEN. Commands, hashes, all samples and hypothesis limits:
`../dxmt-test/n10-recording-perf-20261023/N10-recording-perf.md`.

N10 thread-CPU follow-up separates recording wall time from the named native
thread's CPU time using an external-only PE/native handshake probe. Sleep/busy
controls pass; both versions pass all 24 four-mode paired processes and GPU
oracles. CPU-bracket median deltas range -0.56% to +2.28%, but early-long CPU
p95 is +7.97% and wall p95 +11.74%. Both DLLs have wall-only tail components;
the largest observed gaps are 1.34ms old and 1.93ms current. Thus descheduling
alone does not explain all recorded differences. The wider CPU bracket adds
handshake/stdio work and changes pacing; these figures cannot replace the
original uninstrumented results. HUD is disabled for both DLLs in this probe.
No production changes or blanket performance acceptance. Gate remains OPEN;
next evidence must identify on-CPU tail cost rather than repeat median-only
comparisons. Method, controls, all samples and limitations:
`../dxmt-test/n10-thread-cpu-20261023/N10-thread-cpu.md`.

Follow-up identity check: the captured current DLL and rebuilt DLL have identical
`.text` bytes and section RVAs. The full-file hash differs because the version
string/build metadata changed; exact measured-image hashes remain authoritative.
Native macOS sampling completes but cannot attribute translated PE frames.
An external Windows-context sampler instead captures recording-window RIPs,
maps them using the verified loaded DLL base, and passes 100 GPU-checked rounds
per DLL with zero sampling errors. Device QueryInterface accounts for 306/483
old and 310/469 current samples. Source inspection locates the common IUnknown
identity route after the other device-interface checks. This is a shared hot
cost, not proof of the staging change's conditional regression. Sample shares
are diagnostic, not latency acceptance; suspension perturbs execution. No
production optimization or gate closure is included in this evidence round.

### N02 — device identity query ordering, 2026-10-23

The device-owner slice prioritizes the existing IUnknown branch in
MTLD3D12DeviceImpl::QueryInterface. Canonical pointer, ref operation, null-output
guard and unsupported-interface handling are preserved. IsSameDevice and all
recording/submission lifetime checks remain authoritative. No new cache/state,
lock, allocation, source/header, ABI or capability is introduced. N10 is the
caller benchmark; allocator/list/queue implementations are unchanged here.

The existing feature runner verifies canonical identity through thirteen device
interfaces and error outputs on both pre-optimization and final DLLs. Eleven
scoped/adjacent processes pass, including GPU copy capacity/plane preservation,
foreign allocator rejection, descriptor ownership and submission-failure rollback.
Three rotating-process comparisons per setup/batch variant use identical PEs:
recording medians improve 35.38–36.23% versus pre-optimization and 34.04–35.53%
versus the pre-capacity-fix DLL. This removes twelve preceding IID predicates
per IUnknown query without deleting the four query/ref cycles per buffer copy.
Forty-five performance processes pass their GPU/fence oracles. Submission
medians improve, but ordinary-submit p95 is +6.99% versus pre-optimization and
+10.17% versus pre-capacity baseline. Overall performance gate stays OPEN;
this does not establish the cause of earlier conditional staging regressions.
Exact hashes, all timing variants, costs and limits:
`../dxmt-test/n10-identity-query-20261023/N02-device-identity.md`.

### N13 — ordinary submission tail intervals, 2026-10-23

Diagnostic-only follow-up to the open ordinary-submit performance alert; no
production source, ABI, owner or capability changes. The older probe's
`collector_us` measures an independent post-wait collector invocation, not
collection inside timed ExecuteCommandLists. External scoped IAT timing now
measures native imports and the prefix before autorelease-pool creation, which
includes validation, commit-lock/admission and the actual resource collector.

Four processes pass existing GPU/descriptor/fence/lifetime assertions (700
samples). After ten warmup rounds, each ordinary scene has sixty samples and
identical native call counts on both DLLs (three compute and two blit factories).
Current mixed outer p50/p95 is 189.75/267.00 us; pre-capacity baseline mixed is
182.75/226.50 us. Current ordinary-only is 183.50/229.70 us, yet one native
blit-factory interval reaches 203.9 us. HUD-off mixed remains 183.40/266.20 us.
The ordinary prefix p95 is 5.7–9.1 us across controls. Multiple native factory,
encoding and cleanup intervals carry tails on both versions; these observations
do not justify removing queue synchronization or resource/lifetime checks.

These instrumented wall intervals include dispatch/scheduling and probe overhead;
residual time includes bookkeeping and unwrapped operations. Prefix and native
intervals overlap, and separate percentiles cannot be summed. Results are not
acceptance timings. Mixed pacing and HUD do not individually eliminate tails;
the root of the earlier uninstrumented +10.17% p95 alert remains unproven and
the overall performance gate remains OPEN. Exact intervals, identities, commands,
limits and raw evidence: `../dxmt-test/n13-submit-tail-20261023/N13-submit-tail.md`.

### N02 — grouped device interface queries, 2026-10-23

Per user request, follow the grouped QueryInterface style in upstream commit
`a148782d431249405acdb1531dbb0c7e737bbece`, extended to the locally supported
Device5–10 interfaces. IUnknown, ID3D12Object and Device through Device10 now
share the first success branch and `ref(this)`; redundant per-version success
branches are deleted. Explicit typed rejection groups cover DXGI Device–4,
D3D10 Device/Device1 and D3D11 Device–5. These groups return E_NOINTERFACE;
they do not expose D3D10/11 device functionality. Streamline and InfoQueue
behavior is preserved. No new owner, cache/state, ABI or capability change.

Build and diff checks pass. The existing feature runner covers canonical
identity for thirteen supported interfaces plus thirteen legacy IID rejections,
cleared outputs and E_POINTER. Same final PE passes against both DLLs; final
command-list and descriptor-ownership runners pass (four correctness processes).
Twelve performance processes pass GPU/fence checks. Median-of-process recording
p50/p95 changes +3.36%/+10.54%; ordinary submit changes -2.76%/+4.75%.
Recording p95 reaches the warning threshold, so this is not performance PASS.
Disassembly confirms the first IUnknown comparison and identical AddRef/output
success-epilogue instructions, with different branch distances/method layout;
this does not explain the tail or rule out layout/runtime effects. Preserve all
raw results and keep the overall performance gate OPEN; no additional retry
matrix or unrelated performance patch is included. Exact identities, commands,
limits and evidence: `../dxmt-test/n02-query-interface-20261023/N02-query-interface.md`.

### N02 — grouped-query recording CPU follow-up, 2026-10-23

One bounded diagnostic uses the previously validated named-thread CPU probe,
the exact before/after DLLs from the grouped-query slice and identical probe/
companion DLLs. HUD remains enabled. Sleep/busy controls distinguish wall delay
from recording-thread user+system CPU; eight processes pass original GPU/fence
oracles, with 144 measured recording observations and sixteen control samples.
The existing small-blit runner source is unchanged from fcaf7a05 to 82a9660e.

Three alternating pairs give median-of-process CPU p50 3085.5 -> 3085.5 us
(0.00%) and CPU p95 3301 -> 3255 us (-1.39%). Diagnostic wall p50/p95 changes
-0.31%/-4.65%. The third pair still has a higher after CPU p95; no samples or
pairs are dropped. CPU snapshots bracket more than the copy-only QPC loop,
include pipe/wakeup/publication cost and change pacing. They establish neither
an exact stall duration nor a scheduler/driver cause. These results do not
cancel the original uninstrumented +10.54% recording-p95 alert or the separate
ordinary-submit alert; overall performance gate stays OPEN.

No production/test source change or speculative QI optimization is justified.
Keep the user-requested grouped interfaces and lifetime checks. Stop repeating
this handshake matrix; further attribution needs a different causal measurement.
Evidence is appended to `../dxmt-test/n02-query-interface-20261023/N02-query-interface.md`;
exact identities, command, per-process results and raw logs are in
`../dxmt-test/n02-query-cpu-20261023/`. This closes only the diagnostic slice.

### N13 — completed cross-queue replay and withdrawn batching trial, 2026-10-23

The existing command-list runner now reuses a completed closed copy recording
on a second queue after changing upload marker1234 -> 4321 and clearing readback.
Exact4321 readback plus a healthy device verifies new execution with no Reset/
re-recording and another queue's fence. Existing same-queue growth/replay tests
remain. No new runner or production source/header/ABI/capability is introduced.

A stack-only fence-prefix candidate was evaluated, then WITHDRAWN. It kept
recorded links immutable and all fence/residency/completion operations, reducing
compute/blit encode bridge calls9/6 -> 6/4 in all420 trace samples while preserving
3/2 factories,5 endEncoding calls and1 commit. Fourteen scoped correctness
processes passed including descriptor/SM6, copy capacity and failure rollback.
However twelve uninstrumented performance processes showed exact-descriptor
p50/p95 +6.14%/+20.17% and no-op +5.03%/+15.38%, despite ordinary-submit
-2.86%/-5.64% and occlusion -2.77%/-2.42%. No-op bypasses modified branches;
this contradicts a simple local call-count explanation, not proof of harmless
noise. All raw results remain; no retry matrix or speculative workaround follows.

Production edits were removed and rebuilt. Restored DLL .text bytes/RVA equal
the original; only embedded-version .rdata and .buildid differ. The restored
full command-list runner is a fifteenth correctness PASS. Final delivery is
regression coverage only, with no production latency claim. Overall performance
gate remains OPEN; stop this optimization trial until causal evidence improves.
Exact identities, candidate patch, final restoration, per-process results and
commands: `../dxmt-test/n13-encode-prefix-20261023/N13-encode-prefix.md`.

### N02 — UNKNOWN single-sample quality query, 2026-10-23

The new Device priority list starts with MSAA, then multiple-fence waits and
InfoQueue, followed by Enhanced Barrier ordering and format capabilities.
This round executes only the N02 MSAA slice. UNKNOWN/count1/NONE returned0
because its predicate tested count0 after count0 had already returned E_FAIL.
The existing feature runner reproduces that exact failure on the baseline DLL
and passes after the predicate fix. Counts0/1/2/4/8, NONE/TILED, combined invalid
flags and unchanged inputs are covered. Tiled tier0 and ordinary per-format
Metal MSAA checks remain; no ABI, scheduling or feature-level promotion.

Feature, resource and full command-list runners pass, including canonical
Device/legacy QI, GPU copy replay and allocation/copy validation. Final formatted
sources are rebuilt and rerun separately. NO_PRIVATE_API device translation-unit
syntax check passes; full no-private linked/runtime validation is NOT_RUN.
Pinned upstream returns1 for UNKNOWN/count1; the fork additionally keeps tiled
queries at0. Microsoft structure/flag pages do not explicitly specify the
UNKNOWN special case; native Windows differential validation remains NOT_RUN.

Twelve alternating performance processes all exit0. Median of three process
p50/p95 values: recording3141.30/3484.20 ->3168.10/3604.10us (+0.85/+3.44%),
ordinary submission143.45/194.70 ->145.30/213.80us (+1.29/+9.81%). Each stage has
72 recording and180 ordinary-submit measured samples. No causal latency or
speedup claim; overall performance gate stays OPEN. No QI regrouping revert.

Pending: null-event synchronous multi-fence wait is statically rejected, async
waits create polling threads, and InfoQueue QI creates distinct objects with no
Device roundtrip. Their runtime/lifetime fixes are not part of this commit.
Reproduction commands, manifests, status table, sources, raw logs and limitations:
`../dxmt-test/n02-device-msaa-20261023/N02-device-msaa.md`.

### N14 — multiple-fence notification and cancellation, 2026-10-23

Null-event ALL/ANY now block on the same latched registrations as async waits.
The old per-call detached polling thread and COM-owning WaitSet are removed.
Groups own one event and weak cancellation links. ANY, registration failure,
and fence destruction cancel pending peers; removal completes latched waits.
Reached nodes splice out of the existing pending list under one fence lock;
notifications/cancellation run outside it. No extra submission or worker thread.

Same-runner baseline reproduces E_INVALIDARG for completed null waits. Final
target covers pending null ALL/ANY, duplicate/rewound thresholds,129 entries,
six OOM sites and96 abandoned groups. Threads remain1 ->1;96 duplicated event
handles close, last Fence Release returns0, cancellation never signals completion.
Full command-list/resource/feature runners also pass, including removal,
CPU/GPU rewind, cross-queue replay and Device identity. Device/fence no-private
translation-unit checks pass; full no-private link/runtime, native Windows
differential and extended T4 concurrency are NOT_RUN.

Twelve alternating performance processes pass correctness. Recording p50/P95
-0.73%/+21.73%, ordinary submission +3.79%/+4.55%; recording tail triggers the
team alert. Overall performance gate remains OPEN; no causal speedup/regression
claim or speculative QI rollback. Public COM/WMT ABI, capability declarations,
GPU scheduling and resource layout unchanged; private RegisterWait signature
and pending-list storage change. Next Device priority is InfoQueue.
Commands, ownership/timing proof, limitations and raw evidence:
`../dxmt-test/n14-multiple-fences-20261023/N14-multiple-fences.md`.

### N02 — InfoQueue controlling identity and lifetime, 2026-10-23

Device now embeds its InfoQueue view instead of allocating a standalone COM
object on every query. The view delegates QI/AddRef/Release to the controlling
Device, sharing canonical IUnknown and supporting the reverse Device query.
Raw owner pointer plus embedded storage avoids a COM reference cycle; public
references to either view own the same Device. Mutable stub state is protected
by one InfoQueue-only mutex; Device5–10 grouping remains intact.

Baseline feature runner reproduces E_NOINTERFACE on the Device roundtrip and
canonical/shared-state mismatch. Final feature test passes null-output,
canonical identity, shared mute, and an isolated Device lifetime held only by
InfoQueue:4 threads x128 queries/state operations, balanced final Release0.
Full command-list and resource runners also pass. No-private Device syntax
check passes; full no-private runtime and native Windows differential NOT_RUN.

Twelve alternating performance processes: recording p50/P95 -0.85%/-3.43%,
ordinary submission -0.81%/-4.87%. No causal speedup claim; historical tail alerts
and overall performance gate stay OPEN. Public COM/WMT ABI, capabilities,
resource layout and GPU scheduling unchanged. Message storage, filtering and
break-control stubs remain confirmed pending; this is not a full debug layer.
Commands, hashes, ownership proof, raw logs and scope:
`../dxmt-test/n02-infoqueue-identity-20261023/N02-infoqueue-identity.md`.

### N02 — owned InfoQueue messages and shared filters, 2026-10-24

AddMessage/ApplicationMessage now own text/metadata, support bounded oldest
eviction, zero capacity, counters and clear. GetMessage supports size query,
short-buffer reporting and copied descriptions without removing messages.
Storage/retrieval filters share owned descriptors, atomic copy-then-commit
updates, packed export and stacks with an unpoppable base. Retrieval hides
messages without deleting stored entries; allow union precedes deny override.
All state uses the existing mutex; controlling Device identity is preserved.

Baseline existing feature runner fails the lost-message oracle. Final feature,
command-list and resource runners pass, including input ownership, metadata,
capacity, filter copy/hide/pop, deny precedence, invalid arrays and4x64 concurrent
inserts. Final null-buffer length-read correction is separately rebuilt and
feature-tested. No-private TU syntax passes; full no-private runtime, native
Windows differential and allocation-fault sweep are NOT_RUN. Do not treat tests
alone as proof of native allow/counter/default/stack equivalence.

Twelve alternating processes show recording p50/P95 -0.67%/+19.47%, ordinary
submit -6.46%/-7.68%. Recording tail alert and overall performance gate stay OPEN;
no causal performance claim. No new source/header, public ABI, capability,
layout or scheduler. Break controls/debug output remain stubs, not a full debug
layer. Exact scope, Microsoft snapshots, assumptions, commands/hashes/raw logs:
`../dxmt-test/n02-infoqueue-messages-20261023/N02-infoqueue-messages.md`.

### N02 — InfoQueue allocation-failure rollback evidence, 2026-10-24

The existing command-list runner adds opt-in --infoqueue-allocation-failure,
reusing its scoped TLS malloc IAT injector. Fresh fixtures sweep2 message-write,
4 storage-update,2 copy-push and4 retrieval-update allocation positions. All12
failures return E_OUTOFMEMORY with old stored/allowed/discarded counts, both
stack depths and copied filter contents intact. Existing implementation passes;
no speculative production fix is introduced. Persistent/global OOM is not covered.

Scoped build, targeted probe, full command-list and feature runners pass.
Deployed production DLL is byte-identical to the previous validated final DLL.
Performance NOT_RUN for this test-only slice; existing tail alert and overall
gate remain OPEN. No ABI/capability/scheduling/layout changes. No-private full
runtime/native Windows differential remain pending, as do break/output stubs.
Commands, manifests,12-site results and limits:
`../dxmt-test/n02-infoqueue-oom-20261024/N02-infoqueue-oom.md`.

### N02 — InfoQueue break controls and debug output, 2026-10-24

Confirmed setters returned success without retaining break state; getters were
always FALSE and AddMessage never dispatched output/breaks. Three mutex-protected
sets now persist category/severity/ID controls. Storage-accepted messages snapshot
dispatch decisions under the mutex and call OutputDebugStringA/DebugBreak after
unlock, including zero-capacity messages. Mute suppresses output only; retrieval
filters do not suppress dispatch. Device0–10 QI grouping remains intact.

Same feature PE fails on baseline and passes on candidate. Opt-in command-list
--infoqueue-debug-controls intercepts only the driver imports: baseline lacks
imports (exit1), candidate captures7 outputs/6 breaks (exit0), validates filters,
mute, zero limit, invalid parameters and reentrant query/clear callbacks. Three
fresh-device break-set allocation failures return E_OUTOFMEMORY without changing
state. Existing12-site OOM probe and full command-list/feature/resource runners
pass. Device NO_PRIVATE_API TU syntax passes; full no-private runtime, real OS
debugger delivery and native Windows differential are NOT_RUN.

Twelve alternating processes: recording p50/P95 +2.25%/+11.72%, ordinary submit
+5.05%/+2.82%. Recording tail and submit median trigger team alerts; overall gate
remains OPEN, without causal QI/performance claims. No public ABI, capability,
layout or scheduler change, and no complete debug-layer claim. Next priority is
Enhanced Barrier split state, then format consistency; capability remains FALSE.
Commands, source snapshot, manifests, raw logs and scope:
`../dxmt-test/n02-infoqueue-break-20261024/N02-infoqueue-break.md`.

### N12 — enhanced split execution-order diagnosis and owner design, 2026-10-24

Baseline6458896f; production is unchanged in this diagnostic slice. The Device
attachment explicitly asks for failing tests, root cause and a minimal design
before a larger split-state ownership change. Existing enhanced-barrier smoke
passes but accepts Global Split and never submits a reverse-recorded texture
pair, so its old PASS is not a specification conformance result.

New opt-in `dx12_enhanced_barriers --split-order-probe <mode>` uses an isolated
Device per process. Eight modes plus the existing runner were built and run.
EnhancedBarriersSupported remains FALSE; these probe the internal compatibility
path and cannot justify feature promotion.

| Status | Input / result | Baseline source and severity |
|---|---|---|
| Confirmed | `reversed`: END recorded first, planned execution BEGIN/END; END Close=0x80004005 instead of S_OK | command_list.cpp:2106; device.cpp:594; medium latent compatibility bug |
| Confirmed | `threaded-reversed`: deterministic END-first on two CPU threads gives the same failure | same owner; not overlapping-thread stress |
| Confirmed | `reset-isolation`: A-BEGIN/B-END, then C-BEGIN; Reset(A) cancels C and D-END Close fails | command_list.cpp:976,2084; device.cpp:615; medium lifetime/state isolation bug |
| Confirmed | `clear-state`: BEGIN/ClearState/END makes Close fail | command_list.cpp:1461; medium pipeline-state/recorded-command confusion |
| Confirmed | `global`: paired classic Global Split Close=S_OK, expected rejection | command_list.cpp:6640; medium validation bug |
| Suspected contract issue | `disjoint`: BEGIN mip0/mip1, END mip0/mip1 rejected as same-resource overlap | device.cpp:579; current spec also says no intervening same-resource barriers; finer-grained legality needs native/spec clarification |
| Not reproduced | `ordered`, `same-list` | both texture copy -> split layout transition -> readback pass, Fence complete, Device S_OK |

Microsoft spec snapshot confirms cross-Execute texture splits and forbids
classic Global Split. Cross-Execute buffer splits are effectively unused with
warnings, not equivalent to persistent texture layout transitions. ClearState
documentation describes unbinding/resetting pipeline state. Native Windows
comparison remains NOT_RUN. Source snapshots and exact commands are external.

The root is timing/ownership, not the equality predicate alone. Device BEGIN
immediately mutates a global container and takes a private resource reference;
END consumes it while recording, before a queue or execution order is known.
The originating list retains its stale local signature after another list's
END consumes it. Reset then cancels whichever later identical entry it finds.
Submission failure also occurs after this irreversible record-time mutation.

```text
GOAL: Interpret split/layout events in command execution order (INV-03/04/07/08/15).
CURRENT PATH: Barrier -> Device global pending -> Close; queue validates only completed transitions.
PLAN A: Keep Device record-time pairing; rejected because CPU order and Reset ownership remain wrong.
PLAN B: Record immutable ordered events; pair in existing submission validation/admission.
SELECTION: Extend the existing transition stream/context; no second resource-state model or scheduler.
FILES: Existing device.hpp, device.cpp, command_list.cpp, command_queue.cpp plus existing runner.
OWNER: Recording owns events/references; queue execution timeline owns committed pending texture splits.
COMMIT: Stage pending changes; publish allocation-free inside existing successful commit critical section.
CLEANUP: Reset/failed Close discard only local recording; failure/removal/queue teardown release staged/pending refs.
PERF: No global recording lock; O(recorded events + touched ranges), no whole-heap/resource sweep.
PROOF: Reverse order, replay, cross-Execute texture, failure rollback, queues/Fences, ranges, GPU readback.
CAPABILITY: Keep EnhancedBarriersSupported FALSE; no public ABI/WMT/layout change or new production file.
```

Implementation constraints for the next N12 slice:

1. Preserve a single ordered transition/event stream, including ordinary
   barriers between split halves. Close validates local structure and inputs;
   an END that may be supplied by an earlier-executing list cannot fail merely
   because BEGIN has not been recorded on another CPU thread.
2. Reuse queue admission and its commit mutex. Validate candidate state in
   ppCommandLists order, seeded from that timeline's committed pending texture
   splits. Keep buffer/simultaneous-access boundary behavior distinct. Matching
   must include resource, access/layout and normalized range; do not silently
   broaden disjoint-range legality before resolving the spec ambiguity above.
3. Move pending publication into CommitSubmissionLocked's existing state mutex
   critical section alongside native commit/queue insertion; post-return
   publication would race removal and could resurrect cleared state. Preallocate
   all candidate storage. Failed translation/admission leaves old pending state
   intact; removal makes it terminal and releases references without new work.
4. Pending records use owned private resource references and the existing weak
   removal-listener arrangement, without a queue/device reference cycle. List
   Reset never edits executed pending state. ClearState must leave recorded
   barrier events intact. Remove Device's global recording container, its four
   private split methods, duplicated signature matcher and local cancellation
   list/submitted flag together, rather than retain competing owners.
5. Prove queue identity and Fence-order rules before allowing cross-queue
   consumption; Device-global CPU arrival is not evidence of synchronization.
   Add recording/submission-failure, replay and actual overlapping-recording
   cases before declaring the implementation fixed. Correct the existing
   Global Split positive fixture when production rejection lands.

New probe build and existing enhanced/feature/command-list runners pass;
6 opt-in modes intentionally exit1 exposing the issues above. Only ordered and
same-list reach GPU readback. Cross-queue, fault rollback, native Windows,
full no-private runtime and true overlapping-thread stress are NOT_RUN in this
slice. Performance NOT_RUN: production DLL is byte-identical to the previous
validated candidate. Overall correctness/performance gate stays OPEN. No ABI,
capability, scheduler or layout changes. This is a diagnosis/design commit,
not a claim that Enhanced Barrier correctness has been repaired.
Evidence: `../dxmt-test/n12-split-order-20261024/N12-split-order.md`.

### N12 — split validation in submission order, 2026-10-24

Implemented the preceding owner design on base 9fdd6d08. Device-global
recording-time split state, cancellation signatures and MarkSubmitted are
removed. Immutable normalized BEGIN/END payloads use the existing ordered
resource transition stream. Close checks local structure; queue validation
pairs events in ppCommandLists order against its committed pending texture
splits. Publication swaps preallocated candidates inside the existing native
commit critical section. Reset/ClearState cannot cancel other recordings or
executed pending state. Global Split rejects. Only surviving pending entries
own independent private resource references; recording validation borrows its
existing references. Removal/failed admission/queue teardown release owners.
No second layout manager, new scheduler or production file. Pending lookup is
O(E * Pmax), snapshot O(P); no whole-resource scan or recording global lock.

Four same-PE red baseline probes (reversed, reset-isolation, clear-state,
global) exit1; all 30 candidate correctness processes exit0 with API/shader
validation enabled. Coverage includes GPU replay and cross-Execute Reset,
actual concurrent recording, fence-ordered queue isolation, normalized/mismatched
ranges, orphan END rejection, full existing runners, recording malloc failure,
eight submission malloc positions, native creation failure and queue abandonment.
Private-data lifetime observer verifies actual resource destruction. Queue
isolation does not prove split handoff across queues. Disjoint-range CPU success
is not native conformance; precise same-resource legality remains OPEN.

Final Meson production/test builds pass. Device/list/queue no-private syntax
checks pass; full no-private link/runtime and native Windows remain NOT_RUN.
Private C++ virtual ABI changed, public COM/WMT ABI and resource layout unchanged.
Old private submit probe failed due to shifted vtable and is retained as invalid
performance evidence; updated same PE keeps descriptor/GPU/fence/lifetime
assertions, excludes only the extra collector micro-measurement. Validation-enabled
preflight passes both DLLs; current submit cadence differs from earlier rounds.

Performance: M1 Max/Apple7, macOS27.0.1, Xcode27.1 (requested26 differs), MSC4.0.1,
matched Wine11 x64/Rosetta, validation disabled. AB/BA/AB, 18 processes exit0;
median of three process p50/p95 values; record/split 72 retained samples per side,
ordinary submit 180. See table (microseconds) below.

| Scenario / metric | Before p50 / p95 us | After p50 / p95 us | Change p50 / p95 |
|---|---:|---:|---:|
| N13_FULL / submit_us | 109.20 / 144.90 | 115.50 / 146.40 | +5.77% / +1.04% |
| small_blit / record_us | 3032.60 / 3176.30 | 3315.75 / 3406.10 | +9.34% / +7.23% |
| split_cost / close_us | 30.05 / 32.30 | 179.35 / 189.60 | +496.84% / +487.00% |
| split_cost / record_us | 363.95 / 406.80 | 229.90 / 234.90 | -36.83% / -42.26% |
| split_cost / submit_us | 84.75 / 123.20 | 115.20 / 155.40 | +35.93% / +26.14% |
| split_cost / total_us | 478.45 / 576.50 | 526.25 / 566.80 | +9.99% / -1.68% |

Split fixture has 2,000 pairs, two copies, exactly two native blit encoders and
GPU readback each round. Added Close/submit costs and ordinary recording p95
increase prevent performance acceptance. Overall gate stays OPEN. Full enhanced
semantics, cross-queue handoff, precise disjoint-range contract, extended stress
and performance remediation remain. EnhancedBarriersSupported stays FALSE;
Device interface grouping, capability claims and shared Wine untouched.
Evidence: `../dxmt-test/n12-split-execution-20261024/N12-split-execution.md`;
exact commands/results/environment/hashes in adjacent JSON and logs.

### N02 — separate storage and typed format support, 2026-10-24

Base a262662e. FORMAT_SUPPORT previously inherited typed shader/RT/UAV/atomic
operations from the backing Metal format even for typeless storage, unconditionally
reported filtered sampling, and advertised BC1D and depth/stencil3D. Assign storage
dimensions first, return storage-only masks for fully typeless formats, distinguish
the two internal TYPELESS component-view mappings, gate filtered sampling on the
existing Filter capability, and separate DSV target from shader/MSAA load/resolve
operations. Existing depth1D restriction is reflected honestly, not newly implemented.
No second capability table/cache or production file; only device query branch changes.

Same final PE: baseline --format-support exit1 (52/54 selected mask assertions
fail); candidate all54 pass. Covers22 typeless,19 integer,4 DSV,4 component-view,
3 BC,RGBA8,UNKNOWN plus null/size/planar-invalid cases and BC1D committed creation /
allocation-info contradiction. Eleven correctness processes: expected baseline1,
other10 exit0 (8 candidate,2 baseline GPU). Full feature/resource/command-list/
enhanced runners and updated descriptor/GPU submit probe pass API/shader validation.
Both DLLs pass existing nine-dimension typed/point and typeless-storage/typed-view/
linear-sampler GPU readback11..99, including MSAA. Constant samples verify the view
path, not all filtering math. Final Meson build and no-private device TU syntax
check pass; full no-private link/runtime, native Windows and T4 NOT_RUN.

M1 Max/Apple7, macOS27.0.1, Xcode27.1 (requested26 differs), MSC4.0.1, Wine11 x64/
Rosetta, clang23.1.1. Perf validation disabled; AB/BA/AB,18 processes exit0. Median
of three process p50/p95 values, microseconds:

| Scenario / metric | Before p50 / p95 (us) | After p50 / p95 (us) | Change p50 / p95 |
|---|---:|---:|---:|
| N13_FULL / submit_us | 140.15 / 188.40 | 140.55 / 184.20 | +0.29% / -2.23% |
| format_cost / query_us | 2882.50 / 3001.90 | 3017.90 / 3136.20 | +4.70% / +4.47% |
| small_blit / record_us | 3429.80 / 3671.70 | 3426.10 / 3638.80 | -0.11% / -0.90% |

Each side: record72 retained samples, ordinary submit180, format72 batches of
120000 queries (12-format mix). Format p95 increase is about1.12ns/query averaged
within the batch, not individual-call p95. Fixed O(1) branches; no new allocation,
lock, Metal work or native resource. No causal ordinary-path speedup claim.
Overall performance gate remains OPEN, including prior N12 costs. Same updated
full-submit probe as preceding N12 (collector micro-measurement NOT_RUN).

Pinned upstream a148782 returns all format bits with a TODO; not adopted.
D3D12 flag definitions and Microsoft's shared DXGI FL11 table are saved externally;
that table is not full D3D12 conformance proof. Other dimensions/96-bit textures,
BC3D, all typed-UAV/format/view combinations and precise depth-plane filtering
remain OPEN. No FL/SM/Tier, COM/WMT ABI, scheduling or layout changes; format
claims only reduced, EnhancedBarriersSupported remains FALSE. User .gitignore
excluded. Evidence and exact commands/results/manifests:
`../dxmt-test/n02-format-support-20261024/N02-format-support.md`.

### N08 — share texture format/dimension validation, 2026-10-24

Base08f34684. One node; continues Device attachment's format consistency priority.
Footprints accepted BC1D/depth3D independently of native setup; depth3D could
reach native size/factory calls. Pure helpers in existing resource_helper share
dimension legality across query, footprint and native setup. Shared flag validation
rejects3D ALLOW_DEPTH_STENCIL, including R32/R16 typeless. Committed null-output
validation previously returned S_FALSE before texture format/dimension checks;
it now uses PopulateWMTTextureInfo without native allocation. No extra Populate
call on successful normal creation. Native depth1D remains unsupported while its
legal logical footprint is preserved. Reserved1D/3D retains E_NOTIMPL.

Same final PE baseline focused metadata exit1 (all20 invalid cases accepted),
candidate metadata/full checks exit0. Covers4 BC1D,14 depth/plane3D and2 typeless3D
with DS flag; allocation-info/details, committed/placed and null-output results;
3 positive footprints.13 final correctness processes: expected baseline1, other12
exit0. Full feature/resource/command-list/enhanced and submit probe pass API/shader
validation; baseline/candidate typed and typeless view nine-dimension GPU readbacks
11..99 pass. Final Meson build and no-private device/helper/texture syntax checks
exit0. Full no-private link/runtime, native Windows and T4 NOT_RUN.
Initial test/build/stale-PE/wrong-compiler attempts retained externally and excluded;
see report for the incorrect BaseOffset test oracle and its correction.

M1Max/Apple7, macOS27.0.1, Xcode27.1 (requested26 differs), MSC4.0.1, matched Wine11
x64/Rosetta, clang23.1.1. Perf validation disabled; AB/BA/AB18 processes exit0.
Median of3 process p50/p95 values, microseconds:

| Scenario / metric | Before p50 / p95 us | After p50 / p95 us | Change p50 / p95 |
|---|---:|---:|---:|
| N13_FULL / submit_us | 142.85 / 180.20 | 143.55 / 186.10 | +0.49% / +3.27% |
| format_cost / query_us | 2991.20 / 3118.30 | 2778.35 / 2892.30 | -7.12% / -7.25% |
| small_blit / record_us | 3401.55 / 3841.80 | 3522.95 / 3722.50 | +3.57% / -3.11% |

Per side: record72 retained samples, ordinary submit180, format72 batches of120000
queries. Same updated submit probe; collector micro NOT_RUN. No causal speedup
claim. Global gate remains OPEN including prior N12 costs. Fixed pure checks;
no new lock/allocation/cache/manager. No public/private COM/C++ or WMT ABI, resource
layout, GPU scheduling or feature-level/tier changes. Format query masks equivalent
to preceding N02; EnhancedBarriersSupported remains FALSE.

Upstream pinned a148782 broad format TODO not copied. Shared DXGI FL11 table
requires RGB96 textures; do not reject legal textures to mask the confirmed static
R32 backing/12-byte logical-texel mismatch (dxmt_format.cpp:470-492). GPU data-loss
reproducer and packing/lowering remain OPEN, along with BC3D/full view/typed-UAV/
planar support and enhanced barrier follow-up. Native1D depth is an implementation
limitation, not DXGI format illegality. User .gitignore and shared Wine untouched.
Evidence, exact commands/statuses/hashes and issue table:
`../dxmt-test/n08-format-dimensions-20261024/N08-format-dimensions.md`.

### N08 — pin RGB96 GPU data loss and representation design, 2026-10-24

Base5b4fd397; diagnostic/design slice only. RGB96 is CONFIRMED and NOT FIXED.
Existing resource runner adds selectable --rgb96-copy, --rgb96-copy-minimal and
--rgb96-copy-control; no production changes or new runner/source file. The minimal
1x1 R32G32B32_UINT texture receives words3f000001/3f000002/3f000003 but readback
returns3f000001/cdcdcdcd/cdcdcdcd;8/12 payload bytes differ, Fence completes, device
removal succeeds, outer guards intact. Same-size RGBA32_UINT control preserves16
bytes. Shader/descriptor/Present code is absent from the minimal reproducer.

Two repeated final matrix runs each fail all12 RGB96 cases (TYPELESS/FLOAT/UINT/
SINT x1D/2D/3D), while all3 RGBA128 controls pass. Covers odd width5,two mips,two
array slices or depth3, with buffer->texture->texture->buffer copies. Per RGB
format, payload168/408/564 bytes and mismatches112/272/376: exactly2/3 lost.
Padding is excluded from data equality; outer guard ranges remain unchanged.
Known diagnostic commands exit1, not correctness PASS. Separate ordinary full
resource runner, preceding format/dimension rejection runner and selected54 format
query assertions exit0. Exact commands/results in external final-results.json.

Root: dxmt_format.cpp:470-492 maps logical12-byte formats to native R32*, and
texture.cpp:102 consumes it as a complete texel. Copy footprints/bounds retain12
bytes, but command_list.cpp:4316/4498 native blit width consumes4 bytes per texel.
Static SRV single-component mapping and CPU Map element-size12 rejection remain;
shader/CPU RGB96 behavior NOT_RUN. Microsoft shared FL11 format table freshly
saved externally requires these textures; rejecting them is not a complete fix.

Root design: keep shared D3D11/buffer DXGI mapping stable; existing D3D12 resource
helper owns pure logical-to-physical texture format policy. Candidate RGBA32 backing
preserves logical coordinates and12-byte public footprints. Allocation/views must
consume physical policy; pack/unpack byte boundaries through existing internal
compute/allocator staging, with ordered submission and retention; CPU transfers
need matching conversion. Validate missing-alpha/custom component mapping and
logical cast compatibility; physical RGBA equivalence must not legalize unrelated
DXGI formats. No second manager/cache/scheduler or app-shader workaround. R32x3
width alternative needs shader coordinate lowering across Airconv/opaque MSC and
is not a bounded repair. Full design and acceptance matrix are external.

Theoretical physical texel payload+33.3%, O(texels) conversion and additional
compute/staging traffic need actual allocation/GPU/Recording/Submit measurements
before accepting implementation; these are NOT measured performance numbers.
Production DLL text section matches preceding round; generated version string /
build ID changes relinked binary. Final tests use current rebuilt DLL, with loaded
paths/hashes retained; earlier DLL runs kept separately. No new perf run, full
no-private link/runtime, native Windows comparison or T3/T4: NOT_RUN.

M1Max/Apple7, macOS27.0.1(26A434), Xcode27.1(27A9275; requested26 differs), MSC4.0.1,
matched Wine11.0 x64/Rosetta, bundled clang23.1.1. API/shader validation enabled.
Final Meson resource-runner build exit0; existing unrelated enum comparison warnings
remain. COM/private/WMT ABI, resource layout, scheduling and capability claims
unchanged. Native layout bug remains OPEN; no FL/Tier promotion or push. User
.gitignore unchanged. Evidence/root design:
`../dxmt-test/n08-rgb96-20261024/N08-rgb96-design.md`.

### N08 — RGB96 physical texels and packed copy boundaries, 2026-10-24

Base5cfac03f; one representation mechanism. Confirmed8/12-byte minimal GPU loss
is fixed: same final PE exits1 on baseline,0 on final. D3D12 maps RGB96 to RGBA32
native texels while keeping public footprints12 bytes. Committed/placed allocation,
MSAA, format/view queries and CPU read/write consume one physical policy. Shared
D3D11/buffer/vertex format mapping unchanged. Internal compute packs/unpacks three
words only at buffer/texture boundaries; native texture copies retain coordinates.
SRV missing-alpha1 composes with custom swizzle. Reject RGB96/RGBA128 logical
casts/copies and RGB96 typed UAV; no inherited RGBA typed-UAV/index-buffer claim.

Use existing allocator scratch, not a new pool: tiny copies keep unique bounded
64KiB untracked ranges; large RGB96 copies reuse tracked scratch across compute/blit
passes within one recording. Depth/stencil default scope stays per encoder.
Encoder refs own native resources; ComputeEncoderData retains internal PSO. No
new scheduler/thread/cache/manager. Allocation/PSO failure fails recording.

Final Meson build and DXC shader builds exit0. API/shader validation enabled:
12 RGB formats/dimensions plus3 RGBA controls pass exact GPU readback, guards,
Fence completion; includes mips,arrays,volume,odd widths,committed->placed,CPU
custom pitches/short-pitch rejection and incompatible-copy negatives. Repeated
matrix runs pass. Partial-copy repeats detect corruption, not an independent
partial no-op.512x512 scratch reuse9 repeats passes. Float/typeless SM6 nine-view
oracles pass including default alpha/custom mapping; MSAA checksR only. Ordinary
typeless,resource,format-dimension,selected format,command-list and enhanced-barrier
runners pass. D3D11 SHDR/SHEX vs DXIL boundary test passes (shader creation only).
Five no-private translation-unit syntax checks pass; full no-private link/runtime,
Windows differential,RGB96 SM5/integer shader loads,full rendering/blending/fault
sweep,reserved RGB96 andT3/T4 NOT_RUN/unproven. No full conformance claim.

Final AB/BA/AB18 processes all pass; median of3 process p50/p95 values,us:

| Probe | Before | Final | Change |
|---|---:|---:|---:|
| Recording |3281.05/3440.90|3072.10/3237.00|-6.37%/-5.93%|
| Ordinary submit |106.45/137.90|111.40/148.10|+4.65%/+7.40%|
|120000 format queries |2686.85/2727.90|3158.70/3217.40|+17.56%/+17.94%|

Per side72 recording/180 submit/72 format batches. Initial format overhead37.39%
p95 reduced by direct ordinary query path; residual~4.08ns/query recorded, no
cache or causal speedup claim. Historical/global performance gate remains OPEN.
Three processes x8 retained512x512 cases per format: RGB96 record135.10/186.90,
submit167.45/223.20,submit+Fence1081.35/1273.70us; RGBA128 control153.65/173.80,
129.95/160.60,864.95/944.90us. These are CPU+completion latencies, not GPU timestamps.
RGB96 payload3MiB,actual allocation4MiB (same as correct RGBA128),two textures plus
up to4MiB scratch; native texture4x broken R32 allocation. Conversion requires
O(texels) extra traffic/dispatches; physical payload+33.3% over logical. No claim
of zero-cost emulation or allocation-count measurement.

Public COM/private virtual/WMT ABI unchanged; private ComputeEncoderData layout
grows by a strong PSO ref and nonvirtual staging method gains scope argument.
Native RGB96 layout and its internal compute/blit command sequence change. FL/SM/
Options/Tiers unchanged; EnhancedBarriersSupported FALSE. No src/d3d12 new files,
push or user .gitignore changes. M1Max/Apple7,macOS27.0.1,Xcode27.1(requested26
mismatch),MSC4.0.1,matched Wine11 x64/Rosetta,bundled clang23.1.1.
Exact commands/exits,issue table,all changed files,ABI/cost/coverage limits and
loaded-path/hash manifest: `../dxmt-test/n08-rgb96-fix-20261024/N08-rgb96-fix.md`.
Next node prioritizes remaining Device attachment gaps against current HEAD;
already committed MSAA/fence/InfoQueue/split slices are not reimplemented here.

### N08 — logical depth/stencil format eligibility, 2026-10-24

Base7cda7564; continues Device attachment's format consistency priority. Confirmed
12 invalid typed-color/component formats reported no DEPTH_STENCIL but still
returned valid allocation/footprint and committed/placed empty-output S_FALSE.
Native R16/R32 promotion and depth-backed component mappings hid missing logical
eligibility. Existing shared ValidateTextureResourceFlags now permits only4 typed
depth formats or4 fully typeless depth groups with ALLOW_DEPTH_STENCIL. Native
promotion keys only R16_TYPELESS/R32_TYPELESS. No creation-only duplicate guard,
new format registry/cache or production file. Microsoft resource-flags source
freshly saved; pinned upstream a148782 broad promotion not copied.

Same final existing resource PE: baseline28 cases has16 pass/12 fail(exit1);
candidate28/28 twice(exit0).8 valid depth/typeless and8 flagless color controls
preserve actual committed/placed creation;12 negatives reject footprint,allocation,
null-output and actual output-pointer creation. Invalid native factories are not
called on red baseline. Full resource/feature/command-list/enhanced runners pass,
as do format-dimension and RGB96 GPU regressions. --copy-staging multi passes
D32S8 depth/stencil GPU readback,3 mips/2 slices,2 rounds,preservation and in-flight
checks. R16/R32 typeless rendered shader paths are not established by metadata.
Final Meson build and no-private device/helper/texture syntax checks exit0;
full no-private link/runtime,native Windows,D3D11 rendered frame andT4 NOT_RUN.
Initial missing staged winemetal loader failure retained separately,not counted
as bug reproduction. Exact current DLL paths and hashes recorded externally.

AB/BA/AB18 processes all exit0; median of3 process p50/p95 values,us:

| Probe | Before | Final | Change |
|---|---:|---:|---:|
| Recording |3091.45/3378.40|3080.65/3206.00|-0.35%/-5.10%|
| Ordinary submit |111.60/147.00|110.00/141.80|-1.43%/-3.54%|
|120000 format queries |3140.85/3200.20|3466.85/3554.20|+10.38%/+11.06%|

Per side72 record/180 submit/72 format batches. Format query code/masks unchanged;
observed+11.06% p95 (~2.95ns/query averaged in batch) has INCONCLUSIVE cause,not
silently called noise. No causal speedup or QI rollback. New switch only on depth
flag descriptors; no lock/allocation/per-command work. Historical/global performance
gate remains OPEN. Public/private/WMT ABI,legal native layouts,GPU scheduling,
capability masks and FL/SM/Options/Tiers unchanged; EnhancedBarriersSupported FALSE.
Device5–10 grouping/legacy rejections preserved. User .gitignore untouched; no push.
M1Max/Apple7,macOS27.0.1,Xcode27.1(requested26 differs),MSC4.0.1,matched Wine11
x64/Rosetta,bundled clang23.1.1. Full file list,root,issue table,official/upstream
snapshots,exact commands/exits,hashes,performance and limitations:
`../dxmt-test/n08-depth-format-flags-20261024/N08-depth-format-flags.md`.

### N09 — resource flags gate public view creation, 2026-10-24

Base2d57b2f1; Device attachment format/view consistency priority. Confirmed10
negative descriptor writes (five kinds x empty/seeded) all published or replaced
views despite resource permissions:denied SRV,texture/buffer missing UAV flag,
missing RTV or DSV flag. Logical flags are correctly stored; native format support
was incorrectly enough to reach descriptor publication. Device's existing public
view entries now check immutable GetDesc flags after ownership and null handling,
before native/heap dispatch. Exactly one guard per kind; no duplicate buffer/
texture policy,state/cache/manager or production file. Void rejection warns and
preserves target slot; not a Windows invalid-call output-conformance claim.

Same final descriptor PE baseline exits1(all10 fail); candidate twice exits0.
Five allowed view seeds remain correct; existing null clearing preserved. Full
descriptor ownership,resource,feature,command-list and enhanced runners pass,
as do prior28 depth-format and RGB96 GPU matrices. --copy-staging multi passes
D32S8 plane/preservation/in-flight GPU readback,three mips/two slices/two rounds.
Focused invalid views are inspected,never submitted to GPU. Final Meson build,
no-private Device TU syntax and diff check pass. Full no-private linked/runtime,
native Windows,D3D11 rendering,all-format casts/counter contracts andT4 NOT_RUN.
Primary Microsoft flag source and pinned upstream a148782's unguarded dispatch
saved externally; not copied as the contract. No newest-upstream claim.

AB/BA/AB24 processes all exit0,median of3 process p50/p95 values,us:

| Probe | Before | Final | Change |
|---|---:|---:|---:|
| Recording |3079.65/3203.30|3077.35/3179.10|-0.07%/-0.76%|
| Ordinary submit |110.00/146.10|104.20/144.70|-5.27%/-0.96%|
|120000 format queries |3554.35/3617.10|3404.40/3505.50|-4.22%/-3.09%|
|10000 mixed view writes |1758.60/1805.20|1753.15/1801.10|-0.31%/-0.23%|

Per side72 record/180 submit/72 format batches/72 view batches. View benchmark
mixes SRV,texture UAV,buffer UAV,RTV,DSV with warmed existing views and verifies
final descriptor/resource identities. No causal speedup or zero-cost claim;
small view delta is INCONCLUSIVE. Cost is one GetDesc/bit test per non-null public
view,no new lock/allocation/whole-heap scan or draw/submit work. Historical/global
performance gate remains OPEN. Public/private/WMT ABI,legal layouts,GPU scheduling,
capability masks and FL/SM/Options/Tiers unchanged; EnhancedBarriersSupported FALSE.
Device5–10 grouping/legacy rejections and user .gitignore untouched; no push.
M1Max/Apple7,macOS27.0.1,Xcode27.1(requested26 differs),MSC4.0.1,matched Wine11
x64/Rosetta,clang23.1.1. Full changes,root,issue table,exact commands/exits,source
snapshots,loaded-path hashes and remaining conformance limits:
`../dxmt-test/n09-view-permissions-20261024/N09-view-permissions.md`.

### N09 — UAV counter descriptor admission, 2026-10-24

Actual base1b9593aa, Device attachment view-contract priority; one node only.
Confirmed invalid descriptor publication: counter offsets4/4092/4100 accepted
by a four-byte test, RAW+counter accepted in shared raw/structured path, nonzero
counterless buffer offsets ignored, null-resource counter calls clearing seeded
slots. Existing buffer owner now requires UNKNOWN/nonzero stride/non-RAW for
counter views and4096-byte placement alignment, or zero offset without counter.
Device rejects counter calls missing resource/desc before null handling, including
counterless null buffer descriptors with nonzero offsets. Existing buffer-kind,
foreign-owner and overflow-safe complete-UINT bounds checks remain. No counter
flag assumption,new production file,manager/cache/state/ABI/capability promotion.

Final same PE baseline twice exits1(16 failures of37); candidate twice exits0.
Thirty invalid slot checks and seven legal controls, with distinct seeded buffer
identity; void rejection preserves slot as local safety policy, not a native
Windows invalid-output guarantee. Offset0/4096/8192 accepted; last fits8196-byte
counter. Existing4096-offset GPU counter descriptor copy/mutation/fenced reuse,
residency/output/counter readback pass before and after with unchanged GPU test.
Full ownership,view permissions,resource,feature,command-list,enhanced barriers,
prior28 depth-format andRGB96 matrices,andD32S8 multi-mip/slice in-flight plane
copy pass. Device+Buffer no-private syntax passes; not full linked/runtime proof.
Microsoft SDK primary bytes refreshed unchanged; pinned upstream a148782 Device/
Buffer comparison retained without copying its missing checks or latest claim.

AB/BA/AB24 processes all0; medians of3 process p50/p95,us:

| Probe | Before p50/p95 us | Final p50/p95 us | Change p50/p95 |
|---|---:|---:|---:|
| Ordinary submit | 104.50/141.70 | 107.50/144.40 | +2.87%/+1.91% |
| 120000 format queries | 3469.50/3508.70 | 3435.40/3526.60 | -0.98%/+0.51% |
| Recording | 3083.75/3178.10 | 3168.05/3499.40 | +2.73%/+10.11% |
| 10000 mixed view writes | 1772.85/1842.80 | 1790.90/1896.90 | +1.02%/+2.94% |
| Recording isolated repeat | 3091.25/3438.30 | 3186.95/3363.00 | +3.10%/-2.19% |

Independent record repeat6 processes all0. Each side first group72 record/180
submit/72 format batches/72 view batches; repeat another72 record. Recording p50
+2.73%/+3.10% remains an unresolved performance observation; p95 does not repeat
its direction. Do not claim no regression or attribute cause from these samples.
View delta is inconclusive; counter-positive standalone microbenchmark NOT_RUN.
Global performance gate remains OPEN. No extra draw/submit work,allocation/lock/
heap scan; predicates only in descriptor creation. Public/private/WMT ABI,legal
lifetime/scheduling/layouts,FL/SM/Options/Tiers and EnhancedBarriersSupported FALSE
unchanged. Device5–10 grouping and legacy interface rejections preserved.
Full no-private linked/runtime,native Windows,D3D11 rendering,all-format casts/
RAW-stride combinations,sparse counter GPU andT4 NOT_RUN. Not all ofN09/Device
attachment closed. M1Max/Apple7,macOS27.0.1,Xcode27.1(requested26 differs),MSC4.0.1,
matched Wine11 x64/Rosetta,clang23.1.1. User.gitignore untouched,no push. Exact
commands,exits,issue table,source/loaded-path hashes,raw timing and limitations:
`../dxmt-test/n09-uav-counter-contract-20261024/N09-uav-counter-contract.md`.

### N09 — preserve logical texture view dimension and sampling, 2026-10-24

Base41b69a18; Device attachment format/resource/view consistency, one node.
Confirmed22 unsafe publications: logical1D/2D mismatch onSRV/UAV/RTV both ways,
DSV2D resource using1D view; RTV/DSV sample1 vsMS andsample2 vsnonMS, each empty/
seeded slot. Native1D->2D lowering erases logical dimension; existing shared
ValidateTextureViewType checked only3D parity. Its four callers now pass original
1D/1DARRAY tag; checker preserves3D and checks logical1D and MSAA parity before
createView/publication. Single owner,constant predicates,no new state/cache/
production file. Internal helper signature changes,noCOM/private/WMT ABI change.

Initial fixture failed creating intentionally unsupported depth1D resource; not
counted as target red. Preserve that native limitation; after fixture correction,
final same PE baseline twice22/32 fail and candidate twice32/32 pass. Ten legal
controls retained. Invalid views checked onCPU only; SRV MSAA invalid combinations
not directly probed because old diagnostic can eagerly materialize native views.
Slot preservation is local rejection policy,notWindows invalid-call conformance.

Existing unmodified nine-dimensional DXIL SRV GPU oracle before/after returns
11,22,33,44,55,66,77,88,99; covers1D/array,2D/array,Cube/array,MS/array,3D. Candidate
typeless variant passes. Existing counter GPU copy/mutation/reuse,residency and
readback pass before/after; fullownership,viewflags,countercontract,resource,
feature,command-list,enhancedbarriers,28depthformats,RGB96copy,D32S8multi-plane
multi-mip/slice/in-flight readback pass. Nineteen processes meetexpected exits:
two baseline negative1,other17zero. FinalMeson andno-private textureTU syntax
pass; notfullno-private linked/runtime. MicrosoftSDK sources refreshed;pinned
upstream a148782 texture1D lowering comparison retained,notcopied asvalidation.

AB/BA/AB24processes all0; medianof3processp50/p95,us:

| Probe | Before p50/p95 us | After p50/p95 us | Change p50/p95 |
|---|---:|---:|---:|
| Ordinary submit | 110.40/158.10 | 111.10/143.90 | +0.63%/-8.98% |
| 120000 format queries | 3374.45/3473.90 | 3308.85/3379.00 | -1.94%/-2.73% |
| Recording | 3126.70/3275.90 | 3142.35/3243.70 | +0.50%/-0.98% |
| 10000 mixed view writes | 1796.60/1855.90 | 1774.25/1806.20 | -1.24%/-2.68% |

Eachside72record/180submit/72format/72viewbatches. No causal speedup or zero-cost
claim; viewdeltaINCONCLUSIVE. Priorroundrelative-earlier-base recordp50~+3% is
notresolved by this adjacentcomparison; globalperformancegateOPEN. No draw/submit
work or newallocation/lock/heapscan. PublicFL/SM/Options/Tiers unchanged,Enhanced
BarriersSupported FALSE; Device5–10group/legacyidentityrejections untouched.
Full no-private runtime,nativeWindows,D3D11rendering,allnonarray/cube/castnegative
combinations,allUAVdimensionsGPU,sparseMSviews,T4 NOT_RUN. N09/Device attachment
notfullyclosed. M1Max/Apple7,macOS27.0.1,Xcode27.1(requested26 differs),MSC4.0.1,
matchedWine11x64/Rosetta,clang23.1.1. User.gitignore untouched,no push. Issue table,
root/files/commands/exits,rawtimings,source andloadedpathhashes,remaininglimits:
`../dxmt-test/n09-texture-view-dimensions-20261024/N09-texture-view-dimensions.md`.

### N09 — storage-only formats cannot become resource-backed typed views, 2026-10-24

Base42b5a8ea; one shared mechanism for the Device attachment's format/view
consistency requirement. Native format mapping succeeded for fully typeless
storage, and six typed view paths treated that as permission to publish a view.
Extract the existing FORMAT_SUPPORT classification into inline
IsFullyTypelessFormat in the existing Device header, preserving two selected
64-bit depth/stencil component exceptions. Reuse it before four texture and two
typed buffer view publications. RAW R32_TYPELESS and structured UNKNOWN paths
remain separate. Format-query masks unchanged; no new format family table,
state/cache/manager or production file. Default texture desc inheritance reaches
the same guard; default buffer descriptions were already rejected.

Final same PE twice on baseline:48 failures of73 (26 explicit and22 default
texture checks). Candidate twice73/73:52 negative checks plus21 legal controls,
including13 typed views on typeless storage,4 RAW/structured and4 selected
D24/D32S8 component SRVs. Initial test compile failed on Buffer*/Texture* ternary
conversion and an old PE incorrectly ran the default runner; those exit0 logs
are explicitly excluded. Build-success staging and a mandatory73-row log assertion
prevent stale-run acceptance. No production edit used those stale results.

Twenty final processes meet expected exits:two baseline target1,other18zero.
Unmodified nine-shape DXIL GPU oracle before/after returns11,22,33,44,55,66,77,88,99;
candidate typeless-storage/typed-view mode passes. Counter GPU copy/mutation/
fenced reuse/residency/readback passes before/after. Full ownership,viewflags,
32 view dimensions,37 counter contract,resource,feature,command-list,enhanced
barriers,28 depthformats,RGB96 andD32S8multi-plane/mip/slice/in-flight GPU copies
pass. Device/Buffer/Texture no-private TU syntax passes,not full linked/runtime.

First AB/BA/AB24 processes and isolated record/view repeat12 processes all0.
Medians of3 process p50/p95,us; both original and repeat retained:

| Group / probe | Before p50/p95 us | After p50/p95 us | Change p50/p95 |
|---|---:|---:|---:|
| First / Ordinary submit | 111.65/147.60 | 115.65/146.30 | +3.58%/-0.88% |
| First / 120000 format queries | 3343.15/3474.60 | 3123.10/3185.10 | -6.58%/-8.33% |
| First / Recording | 3133.20/3388.40 | 3237.55/3387.10 | +3.33%/-0.04% |
| First / 10000 mixed view writes | 1798.50/1850.40 | 1872.35/1901.80 | +4.11%/+2.78% |
| Isolated repeat / Recording | 3145.75/3303.00 | 3247.55/3410.30 | +3.24%/+3.25% |
| Isolated repeat / 10000 mixed view writes | 1797.70/1854.10 | 1831.30/1884.30 | +1.87%/+1.63% |

Each side first72 record/180 submit/72 format/72 view batches; repeat adds72
record and72 view. View p50 overhead persists with differing size; no zero-cost
claim. Recording p50+3.33%/+3.24% remains unresolved,not dismissed as noise merely
because descriptor checks are outside the timed recording loop. No causal query
speedup claim. Global performance gate OPEN. Fixed predicates reuse the existing
format query; no allocation/lock/heap scan/new draw or submit work. Public/private
COM/WMT ABI,virtual layouts,resource layouts,scheduling,lifetime and FL/SM/Options/
Tiers unchanged; EnhancedBarriersSupported FALSE. Device5–10 grouping and legacy
D3D10/11/DXGI rejections preserved. Microsoft SDK view/RAW rules and pinned upstream
a148782 native-only view paths retained externally,not copied as validation.

Full family casts/view-kind format legality,null-resource format validation,
all22 typeless families' actual views,all depth-plane GPU sampling,native Windows,
full no-private runtime,D3D11 rendering,T4 NOT_RUN/unclosed. M1Max/Apple7,
macOS27.0.1,Xcode27.1(requested26 differs),MSC4.0.1,matchedWine11x64/Rosetta,
clang23.1.1. User.gitignore preserved,no push. Full issue/root/file/command/exit/
performance/source/hash/loaded-path evidence and remaining limits:
`../dxmt-test/n09-typed-view-formats-20261024/N09-typed-view-formats.md`.

### N10 — detect stale copy benchmark readback and bound recording attribution, 2026-10-24

Actual base `3dab6e928e384ff96f6f449cf65492e718b09de5`. One N10 node;
production code, ABI, capabilities and scheduling are unchanged.

Confirmed fixture bug: the small-copy benchmark reused one constant in every
round. Deliberately omitting the second ExecuteCommandLists returned exit 0
because the previous readback still matched. The existing runner now writes a
unique per-round payload before timing, after the previous fence completes,
and checks that payload after its current fence. Opt-in
`--small-blit-cost stale` must fail: the old two-round oracle exits 0; the
new one exits 1 at round 1, actual 1369838132, expected 3482413965,
`device_reason=0`. This is an oracle fix, not evidence of a driver submit bug.

Transition coverage is bounded: created/reset recording list -> upload write ->
record -> close -> execute -> signal/wait -> current readback. The injected
closed-but-unsubmitted round reaches a queue fence and is then rejected for
stale data. These fixture transitions do not close the full N10 state machine.
Native buffer-allocation count remains zero; split mode retains its two native
blit encoders. Payload preparation is outside all three timers, but can affect
CPU preconditions; comparing old/new probe timing is not driver speedup proof.

Build and DXMT_NO_PRIVATE_API syntax compilation of the changed test TU exit 0.
With Metal API/shader validation, early/late each pass 9 rounds, early-long/
late-long each pass 25, split passes 25, and the default command-list suite and
multi depth/stencil copy pass. The deliberate stale mode fails as expected.

Original fixed-PE controls: 18 successful processes with A/A and crossed A/B,
25 rounds/process, first discarded; per-process p50/nearest-rank p95 over 24
samples, then median across three processes. Current/current A/A recording
slot B vs A is -0.28%/-2.56%. Current vs old is +2.28%/-7.78% before the path
swap and +2.36%/-9.03% after it. No fixed-slot bias was reproduced in this
small sample; environmental variance is not excluded. CopyBufferRegion has
172 identical symbolized instructions but different raw bytes and an RVA
shift of 64 bytes (0xe170 -> 0xe1b0). Layout/Rosetta remains a hypothesis.

Final fixed-PE controls with unique payloads: 12 successful processes, 300
verified rows. Current/current A/A recording -0.94%/-3.41%; old/current A/B
recording 3132.50/3241.50 vs 3195.40/3327.00 us (+2.01%/+2.64%). Close is
1.30/2.00 vs 1.40/1.90 us. Batch-submit is 22127.55/23291.80 vs
22036.25/22413.70 us (-0.41%/-3.77%); this is the 50000-copy Execute timer,
not the ordinary N13 full-submit probe. Recording performance gate stays OPEN;
no speculative runtime optimization or rollback was made.

Same M1 Max/Apple7, macOS 27.0.1, Xcode 27.1 (26 unavailable), MSC 4.0.1,
matched Wine11 x64/Rosetta host. Native Windows/debug layer, T3/T4, complete
no-private runtime and ordinary N13 submit benchmark NOT_RUN this round.
Device attachment and existing formal/performance gates remain incomplete.
Next bounded candidate: code-placement experiment or CPU profiling for the
DLL-associated recording difference. All scripts/logs/staged DLLs/PEs and
per-process hashes are external; user `.gitignore` remains untouched.

Evidence:
`../dxmt-test/n10-recording-attribution-20261024/N10-recording-attribution.md`,
`validation-results.json`, `control-aggregate.json`,
`final-control-aggregate.json`, `disassembly-summary.json`, `manifest.json`.

### N10 — controlled linked-layout attribution, 2026-10-24

Actual base `0a01f54419ecfaa8cc4aa61b9f6f54facf7f30f8`. One bounded N10
experiment; production source/build settings, ABI and capabilities unchanged.
External snapshots of existing objects/libraries reproduce the previous measured
DLL's exact `.text` bytes/RVAs. Normal relink differs only in version/build-ID
section bytes plus header metadata. A second link adds `/order` for the existing
CopyBufferRegion COMDAT: its RVA moves from 0xe1b0 to 0x1000 while all 172
symbolized instructions remain identical. References/unwind metadata and other
code placements necessarily move; this does not isolate a microarchitectural
cause or establish byte-identical code across layouts.

Both images pass default command-list, 25-round long-copy and multi depth/stencil
GPU cases with Metal API/shader validation. Both reject the deliberately omitted
second submission (exit 1, round 1 mismatch, device_reason=0). Native recording
buffer allocation remains zero. Final links and eight validation processes
complete with expected exits; earlier setup/loader failures are retained and
excluded from correctness outcomes.

Eighteen fixed-PE performance processes pass, 450 GPU-verified rows. Each has
25 rounds of 50000 copies, discards round 0, and computes p50/nearest-rank p95
across 24 samples; aggregate is median of three process statistics. Pair order
alternates AB/BA/AB. Same-.text immutable/normal recording delta is
-0.55%/-1.55%. Normal/front is 3208.35/4152.10 -> 3082.65/3179.00 us
(-3.92%/-23.44%); after path swap 3197.85/3313.80 -> 3069.95/3177.60 us
(-4.00%/-4.11%). Larger first-block baseline tails remain in the raw evidence.
The layout intervention changes measured recording cost on this host; exact
Rosetta/alignment/cache cause and historical +2% attribution remain UNPROVEN.
No production ordering change or performance-gate closure follows from this.

Close and batch-execute statistics are retained separately in the report.
Batch-execute is the 50000-copy ExecuteCommandLists timer, not ordinary N13
full-submit. Native Windows, other GPUs, T3/T4, full no-private runtime and
ordinary N13 benchmark NOT_RUN. Same actual macOS27.0.1/Xcode27.1/M1Max/
Apple7/MSC4.0.1/Wine11/clang23.1.1 host. Existing gates remain OPEN. Next
attribution requires CPU profiling with layout sensitivity controlled; Device
format-family review remains queued. No new source/header or runtime state.

Provenance clarification: the previous external N10 report's “before-42b” label
means the pre-3dab fixture based on `42b5a8ea`, as its N09 manifest records;
it does not mean an implementation predating 42b. Captured hashes are unchanged.

Evidence: `../dxmt-test/n10-code-placement-20261024/N10-code-placement.md`,
`build-commands.json`, `inspection.json`, `section-comparison.json`,
`validation-results.json`, `oriented-aggregate.json`, `manifest.json`.

### N09 — resource/view logical format families and typed casting, 2026-10-24

Actual base `88b1d4aad183e2862d3a27833785ddb8ee4be9a3`. Device attachment
format consistency, one N09 slice. Four texture view owners previously checked
native view-format availability/typelessness without comparing logical resource
and view formats. Equal byte width/native aliases do not establish a DXGI family.
A shared pure predicate in existing d3d12_texture.cpp now checks families and
fully typed FLOAT/non-FLOAT and UNORM/SNORM restrictions. Exact-format views
keep a fast path; typeless resources and selected packed depth/stencil planes
retain legal casts. Two RGB96-vs-other guards are subsumed and removed; unsupported
RGB96 UAV handling remains. No new state/cache/manager or src/d3d12 file.

The existing descriptor runner adds --texture-view-casts, reusing its fixtures.
Final same-PE old DLL runs twice exit 1 with all 50 negative checks failing and
47 legal controls passing. Candidate passes all 97. Tests cover empty/distinct-
resource-seeded slots, four view kinds, cross-family color/depth aliases, both
directions of numeric-class restrictions, integer casts, UNORM/sRGB, BGRA and
BC1/BC4 controls. The distinct seed corrects an initial six-check blind spot;
intermediate results are not treated as the final oracle. Slot preservation is
DXMT's safety check, not a native Windows contract for invalid descriptors.

Twelve final candidate processes pass with Metal API/shader validation: cast97,
typeless73 (including four selected plane SRVs and RAW/structured buffers),
dimensions32, counter37, ownership, feature54, resource21, command/barrier,
RGBA8 and RGB96 typeless linear-filter GPU oracles (nine exact values 11..99),
and multi depth/stencil copy. An initial missing-shader-argument invocation
exits 3 and is retained; corrected invocations pass numeric checks. Scoped
regular build and both changed-TU no-private syntax checks pass. Metadata for
89 family entries matches captured Microsoft parent-format data (STATIC only).
Pinned upstream a148782d has no equivalent SRV family guard; Microsoft casting
rules, not native Metal representation or copying upstream, govern this fix.

Two 18-process alternating performance series are retained. Final recording
p50/p95 is 3189.00/3280.10 -> 3196.95/3296.70 us (+0.25%/+0.51%);
10000 mixed view writes 1848.80/1950.50 -> 1854.05/1929.70 us
(+0.28%/-1.07%); ordinary submit 112.00/143.00 -> 109.05/139.50 us
(-2.63%/-2.45%). Per-process recording/view samples=24 after first-round
warmup; ordinary=60 after ten warmups; aggregate medians of three processes.
Initial deltas remain recording +0.16%/+3.21%, views +0.11%/-1.13%, ordinary
submit +1.62%/+6.83%. Fixed PE within each series; only descriptor PE differs
between series due the fixture correction. All 36 processes exit 0. No new
per-command/submit work or locks; different-format view creation performs at
most two bounded 20-range family searches. Performance gate stays OPEN /
INCONCLUSIVE; final medians do not erase initial tails or historical regressions.

Same actual macOS27.0.1/Xcode27.1/M1Max/Apple7/MSC4.0.1/Wine11/clang23.1.1
host. ABI/export names/ordinals, FL/SM/Options/Tiers, scheduling and resource
layouts unchanged. Windows differential, T3/T4, full no-private runtime,
exhaustive operation/plane/planar matrices and BC/sRGB GPU sampling NOT_RUN.
Next bounded candidate: view-format operation/plane eligibility; Device audit
and earlier gates remain incomplete. User .gitignore preserved; no push.

Evidence: `../dxmt-test/n09-view-format-families-20261024/N09-view-format-families.md`,
`red-results.json`, `validation-results.json`, `perf-aggregate.json`,
`initial-perf-aggregate.json`, `family-metadata-crosscheck.json`, `manifest.json`.

### N09 texture view format roles (2026-10-24)

Baseline 7b577dc7, actual local feat/d3d12. Confirmed missing operation gate:
final identical PE + old DLL twice fails all40 negative checks while19 legal
controls pass. Candidate59/59 passes. The shared format predicate now checks
family/type plus SRV/UAV/RTV/DSV use before native-view registration and heap
publication. Logical DSV formats cannot become SRV/color views; color and
selected-component formats cannot become DSV. RTV/UAV reuse device format
capabilities; UAV matches the existing typed-store query boundary, preserving
RGB96 exclusion and rejecting sRGB UAV despite native Apple texture Write.
Integer Load, selected plane SRVs, sRGB SRV/RTV and RGB96 RTV remain legal.
Four repeated guards and the separate RGB96 UAV guard converge here. Device
capability getter uses one find instead of contains+at; no cache/table added.

Thirteen final candidate processes exit0 with confirmed staged DLL paths and
Metal API/GPU validation banners: roles59, casts97, typeless73, dimensions32,
counter37, feature54, resource21, ownership, command/barrier, RGBA8/RGB96
linear-filter GPU readbacks (11..99 exact), multi depth/stencil copy twice.
Invalid-role checks inspect empty/distinct-seeded CPU descriptors, not invalid
GPU work; preserved slots are DXMT's safety invariant, not a Windows invalid-
input promise. Scoped build and all three changed-TU no-private syntax checks
pass. Captured Microsoft DSV/planar-depth/casting sources govern the contract;
pinned upstream a148782d native-format acceptance does not prove role legality.

Two18-process alternating performance series retained. Initial record p50/p95
-0.76%/+10.33%, views +4.23%/+0.62%, ordinary submit -1.28%/-2.42%.
After single-lookup change, final record3194.05/3282.40 ->3185.00/3297.20us
(-0.28%/+0.45%); 10000 mixed writes1839.75/1886.70 ->1899.95/1986.50us
(+3.27%/+5.29%); ordinary submit111.70/145.40 ->114.00/152.90us
(+2.06%/+5.16%). Median of3 process quantiles; record/view24 samples,
ordinary60 after warmup. Final view cost +60.20us/10000 writes. Role checks
add one immutable capability lookup to3/5 fixture writes, no allocation/lock
or command/submit work. Not an isolated causal proof of the getter benefit.
Performance gate OPEN; regressions are retained, not classified as noise.

Same recorded macOS27.0.1/Xcode27.1/M1Max/Apple7/MSC4.0.1/Wine11/clang23.1.1.
Exports/ABI, FL/SM/Options/Tiers unchanged. Native Windows differential, full
no-private runtime, T3/T4 and exhaustive operation/plane combinations NOT_RUN.
Remaining plane/default/API/capability audit and historical performance gates
stay open. User .gitignore unchanged; no new source/header or push.
Evidence: `../dxmt-test/n09-view-roles-20261024/N09-view-roles.md`,
`red-results.json`, `validation-results.json`, `verification.json`,
`perf-aggregate.json`, `initial-perf-aggregate.json`, `manifest.json`.

### N09 reuse validated attachment capabilities (2026-10-24)

Baseline a07cea0b, actual feat/d3d12; previous round PROGRESS. Existing
view-permission-cost reproduces legacy7b577dc7 ->a07 cost +3.04%/+3.36%
p50/p95 across three alternating process pairs. Confirmed redundant ownership:
committed/placed initialization passes PopulateWMTTextureInfo and validates
Color/DepthStencil on the physical format before allocation; resource flags,
physical format and per-device capability data remain immutable. Reserved
creation also validates, while reserved attachment views remain E_NOTIMPL.

The single format-role predicate now reuses this creation postcondition for
RTV/DSV only when the physical format matches exactly, the corresponding
ALLOW flag is set, and logical family/typed/role checks pass. Cross-format
attachment and every UAV typed-store query remain. All later mip/array/plane/
dimension checks and publication remain unchanged. No new capability cache,
state, table, source/header, lock or allocation. Only existing texture code
changes; helper assembly captures show the native-format/flag return paths
before capability vcall. The existing five-write cost mix goes3 ->1 capability
queries (6000 ->2000 per10000 writes); this is source/control-flow evidence.

Fourteen candidate processes exit0 with Metal API/GPU validation banners and
verified staged DLL loads: permissions11, roles59, casts97, typeless73,
dimensions32, counter37, feature54, resource21, ownership, command/barrier,
RGBA8/RGB96 linear-filter GPU readbacks (11..99 exact), multi depth/stencil
copy twice. Same PE +legacy DLL remains red40/19; baseline and final both
pass59; baseline permissions11 pass. Changed-TU no-private syntax-only and
scoped regular build pass. Exports and public capabilities/ABI/layout/scheduling
unchanged. Full no-private runtime, Windows differential, T3/T4 and exhaustive
plane/default combinations NOT_RUN.

Main18-process alternating comparison, median of3 process quantiles: record
3197.60/3404.10 ->3213.35/3371.70us (+0.49%/-0.95%); 10000 mixed view writes
1888.75/1959.70 ->1829.20/1861.10us (-3.15%/-5.03%); ordinary submit
105.25/132.80 ->105.50/135.00us (+0.24%/+1.66%). Record/view retain24 rounds,
ordinary60 after warmup. Main view median saves59.55us/10000 writes. Direct
legacy/final six-process comparison1844.95/1949.90 ->1824.20/1876.40us
(-1.12%/-3.77%). All30 timing processes across three series exit0; recording
checks unique GPU payload and zero native staging buffers. Local fixture
supports removing the role-admission cost; this does not erase historical
recording/submit regressions or close the overall performance/Device gates.
No claim of isolated instruction attribution or cross-GPU performance.

Actual macOS27.0.1/Xcode27.1/M1Max/Apple7/MSC4.0.1/Wine11/clang23.1.1
probed; existing WMT backend retained despite system Metal4 support. Next
bounded candidate: Device format reporting versus plane/default admission.
User .gitignore unchanged; no push. Evidence:
`../dxmt-test/n09-view-capability-reuse-20261024/N09-view-capability-reuse.md`,
`verification.json`, `baseline-validation.json`, `validation-results.json`,
`perf-aggregate.json`, `repro-perf-aggregate.json`,
`legacy-final-perf-aggregate.json`, `disassembly.json`, `manifest.json`.

### N09 implicit stencil SRV planes (2026-10-24)

Confirmed baseline regression: the shared PlaneSlice validator received zero
for MS/MSArray SRVs, whose public descriptors have no PlaneSlice field, so
four legal stencil-only views were rejected. The existing format flags now
initialize the implicit plane; explicit 2D/2DArray descriptors still overwrite
it and retain all format/plane bounds checks. No new source/header, state,
cache, format table, capability or feature-level promotion.

Same final PE + baseline DLL twice: 4 fail/36 pass. Candidate: 40/40,
including seeded-descriptor preservation on rejected explicit selections.
Sixteen final runtime processes pass with verified DLL loads and Metal
API/GPU validation enabled. Public-API GPU stencil reads cover D24 MS and
D32S8 MSArray, every one of four samples and both array layers (77/88),
plus ordinary typeless/RGB96 controls and two depth/stencil copy rounds.
Scoped build, three changed-TU no-private syntax checks, formatting and
unchanged export names/ordinals101..106 pass. No-private full runtime,
native Windows differential, exhaustive cube/1D/3D/default/video combinations
and T3/T4 remain NOT_RUN.

Three alternating pairs, 18 timing processes, median process p50/p95:
10000 mixed view writes1841.65/1876.80 ->1869.75/1903.40us
(+1.53%/+1.42%); record3185.10/3287.50 ->3172.50/3306.70us
(-0.40%/+0.58%); ordinary submit112.80/143.10 ->101.50/121.80us
(-10.02%/-14.88%). These are local fixture observations, not isolated
instruction attribution; historical performance and overall Device gates stay
OPEN. Initial fixture compile failure and two stale-runner usage failures
are retained separately; corrected final runs are the evidence above.
Evidence: `../dxmt-test/n09-implicit-stencil-plane-20261024/`
`N09-implicit-stencil-plane.md`, `manifest.json`, `validation-results.json`,
`red-results.json`, `no-private-final.json`, `perf-aggregate.json`.

### N02 — selected-plane reporting evidence and role guards, 2026-10-24

Base43546dc1. Current four component-view FORMAT_SUPPORT queries return
Support1=0x1011a0, Support2=0, excluding MULTISAMPLE_LOAD(0x400000).
Current GPU stencil MS/MSArray fixture succeeds for X24_TYPELESS_G8_UINT and
X32_TYPELESS_G8X24_UINT, all four samples and both array layers (77/88).
This confirms a narrow conservative reporting gap, not permission to promote
all component operations. Depth-component MS GPU Load, filtering/comparison,
full sample-count/format combinations and native Windows differential remain
UNVERIFIED/NOT_RUN. Overall Device/FL12_2 gates remain OPEN.

Static root: FORMAT_SUPPORT looks up raw custom-swizzle PixelFormat keys at
`d3d12_device.cpp:1045`; the shared capability accessor at2186 normalizes
ORIGINAL_FORMAT. Both depth and stencil native entries have DepthStencil
capability, so merely normalizing this query would incorrectly classify typed
component SRVs as DSV and remove their SHADER_LOAD. No production fix or
capability promotion is made in this evidence slice. Existing feature fixture
now additionally forbids RTV/blend/MSRT/resolve/UAV and Support2 bits for all
four component formats, preserving format-role separation for a future fix.
Pinned upstream a148782d returns all-one masks (TODO), so is not adopted.

Seven runtime processes exit0 (five original controls, two rebuilt feature
runs). Final format54, feature54, plane40 and both nine-value GPU readbacks
pass; verified staged DLL and API/GPU validation enabled. Scoped Meson build,
changed-test-TU no-private syntax and formatting pass. Full no-private/runtime,
T3/T4 NOT_RUN. This is a test/audit commit, not a runtime bug-fix claim; DLL
hash7010449e... matches43546dc1. Performance NOT_RUN because production code
is unchanged; earlier measured recording/submit regressions are not cleared.
No ABI/layout/scheduling/FL/SM/Options/Tier change or new D3D12 source/header.
Evidence: `../dxmt-test/n02-plane-capability-audit-20261024/`
`N02-plane-capability-audit.md`, `manifest.json`, `results.json`,
`final-results.json`, `no-private.json`, `sources.json`.

### N02 — depth/stencil multisample GPU oracle, 2026-10-24

Base3883152c. Existing texture-dimensions fixture now accepts --depth-ms and
reversed family modes; the same DSV/resource/transition path serves depth and
stencil. Reversal also covers the missing stencil family/dimension pairs.
The existing shader uses one MSValue check and one all-samples/layers loop.
Depth validates R=77/255 or88/255 within1e-6 and default G/B/A=0/0/1;
stencil validates G=77/88 and default R/B/A=0/0/1. No new runner/source/header,
production state, capability table, ABI/layout/scheduling or feature promotion.

Final10 runtime processes: nine expected-success exit0; one deliberate
wrong-depth-expectation shader exits1 with dimension6=0 expected77. This is
oracle sensitivity evidence, not a runtime defect. Seven positive GPU runs
(depth2, stencil2, ordinary, typeless, RGB96) read exact11..99. Both component
families now cover MS and MSArray, every one of4 samples and both array
layers. Samples have uniform clear values: per-sample distinct-value storage,
resolve semantics and D24 quantization are not proven. Plane40 and format54
controls pass. All final runs verify staged DLL and Metal API/GPU validation.
Five CSOs compiled from current HLSL (one external wrong-oracle mutation),
scoped Meson build, changed-TU no-private syntax and formatting pass.

Both depth component formats21/46 and stencil22/47 still query
Support1=0x1011a0 without MS_LOAD despite these narrow GPU successes.
Depth-component MS Load moves from UNVERIFIED to locally GPU_VERIFIED for
this4x MS/MSArray case. Full sample-count/filter/comparison/mip/default/view
matrix, Windows differential, full no-private runtime and T3/T4 remain
NOT_RUN; no capability exposure gate is closed. Existing underreport remains
OPEN, and no blanket format support is inferred. Production DLL hash7010449e
unchanged; performance NOT_RUN for this test-only slice, historical
recording/submit regressions remain OPEN. Evidence:
`../dxmt-test/n02-depth-ms-oracle-20261024/N02-depth-ms-oracle.md`,
`results.json`, `shader-builds.json`, `no-private.json`, `manifest.json`.

### N02 — no-match feature-level query result, 2026-10-24

Base6d8b8e32. Nonempty FEATURE_LEVELS queries with a zero selected result
returned E_FAIL before publishing output, retaining the caller sentinel0xb000.
Same final PE + baseline DLL twice gives8 pass/3 fail (zero-only, unnamed high
0xffff, known unsupported12_2). Remove the post-selection failure return;
retain maximum-of-requested-numeric-levels selection and null/count/size guards.
Now no-match returns S_OK and MaxSupportedFeatureLevel=0; device creation
still rejects unsupported FL. No clamping to an unrequested supported level,
stricter enum validation, new state/cache, source/header or capability promotion.

Final11 matrix checks plus null-data guard pass; full feature65, format54,
plane40, resource21 pass. Nine candidate processes exit0 with verified DLL and
Metal API/GPU validation: depth/stencil reversed-family and ordinary typeless
GPU readbacks11..99 plus two depth/stencil multi-copy rounds. Scoped regular
build and both changed-TU no-private syntax pass; six export names/ordinals
unchanged101..106. Native Windows differential, full no-private runtime and
T3/T4 NOT_RUN. Pinned VKD3D device ef9a02e8 publishes zero and returns S_OK;
its tests preserve numeric0x3000 compatibility. This is reference-source
comparison, not a claimed native Windows run. Upstream DXMT a148782d clamps
the requested maximum to11_1, which can produce an unrequested level; not copied.

Main AB/BA/AB18-process timing (median process p50/p95, us): record
3211.35/3388.00 ->3200.60/3365.60 (-0.33%/-0.66%); view
1841.10/1914.50 ->1853.40/1907.90 (+0.67%/-0.34%); ordinary submit
111.25/135.20 ->111.70/149.40 (+0.40%/+10.50%). Submit p95 triggers the
warning threshold. Independent BA/AB/BA/AB eight-process submit recheck:
125.825/156.65 ->119.125/163.25 (-5.32%/+4.21%). Main retain24 record/view
or60 submit samples/process; recheck60. All26 processes exit0. Results show
run-to-run absolute drift; the first signal is retained. Submit performance
INCONCLUSIVE, not cleared or attributed to the removed query branch; historical
performance/Device gates stay OPEN. Same workload PEs/DXGI/WMT and device,
validation disabled for timings; recording verifies unique GPU payload and
zero native staging buffers. No direct FEATURE_LEVELS latency benchmark.

Actual macOS27.0.1/Xcode27.1(requested26 unavailable)/M1Max/Apple7/MSC4.0.1/
Wine11/clang23.1.1. No ABI/layout/scheduling/FL/SM/Options/Tier change.
Evidence: `../dxmt-test/n02-feature-level-contract-20261024/`
`N02-feature-level-contract.md`, `manifest.json`, `red-results.json`,
`validation-results.json`, `no-private.json`, `perf-aggregate.json`,
`submit-recheck/perf-aggregate.json`, `sources.json`.

### N13 — feature-query branch and PE-layout controls, 2026-10-24

Base3fcf3d42; diagnostic/documentation slice only. B=pre-query-fix DLL;
P=B with exactly two branch bytes replaced by an equal-length NOP, all other
PE bytes/RVAs unchanged; A=current source-fix DLL; A2=byte-identical A in a
second path. P is external diagnostic-only, never ship. Six exports and shared
workload/DXGI/WMT identities verified. PE layout preservation does not guarantee
Rosetta/runtime layout preservation.

Six correctness processes have expected exits: B query8 pass/3 fail; P/A each
11 pass; P full feature65 pass; P/A instrumented probes each210 full GPU cases
plus221 empty fence iterations. Public CheckFeatureSupport vtable counters
both total1/timed0/levels1/timed_levels0, with the known query outside timing
as positive control. This observes virtual dispatch after hook installation,
not creation or internal direct/devirtualized calls. Instrumented timings are
excluded from acceptance. Timed QPC window covers ExecuteCommandLists only;
fence waits/readback/collector are outside; collector_us=NOT_RUN.

Sixteen uninstrumented processes pass, balanced B/P/A/A2, P/A2/B/A,
A/B/A2/P, A2/A/P/B; retain60 per full scenario or200 empty per process.
Median process ordinary p50/p95 us: B123.925/159.40, P123.625/155.20,
A119.35/162.60, A2 128.30/161.60. P/B p95−2.63%, A/P+4.77%; A/P paired
p95 changes are positive in all4 runs (+0.30%..+5.62%). Same-binary A2/A
paired p95−6.25%..+1.95% demonstrates variation, not a universal noise bound.
Exact/fallback/empty results and raw data remain in the external report.

Observed timed virtual route does not support direct execution of the fixed
query branch as the submit-tail cause. Linked-layout/process effects remain
unproven; prior N02+10.50%/+4.21% signals retained. Performance INCONCLUSIVE;
historical performance/Device gates OPEN. No hot-path optimization, state,
cache, source/header, ABI/scheduling or FL/SM/Options/Tier change. Further work
needs a distinct concrete-cost/owner observation; do not repeat this matrix.
Actual macOS27.0.1/Xcode27.1/M1Max/Apple7/MSC4.0.1/Wine11/clang23.1.1.
Regular external probe build passes; full build, no-private runtime, native
Windows and T3/T4 NOT_RUN this round. Validation enabled only for correctness.
Evidence: `../dxmt-test/n13-feature-query-layout-20261024/`
`N13-feature-query-layout.md`, `manifest.json`, `patch-manifest.json`,
`probe-source.json`, `validation-results.json`, `perf-aggregate.json`,
`paired-comparisons.json`, and isolated native load logs.

### N02 — BC1/BC3 mip, cube and sRGB GPU sampling, 2026-10-24

Base72b28001; Device format-consistency evidence slice. Extend only existing
texture-dimensions C++/HLSL fixture: BC1/BC3 typed/typeless storage crossed
with UNORM/SRGB typed SRVs (8 modes). Four2D/cube dimensions, four mips8/4/2/1,
all array layers/cube faces:84 subresources per mode. Distinct RGB565 block
endpoint0 values and BC3 alpha endpoint0 are checked as float4, tolerance0.006.
Per mode105 SampleLevel comparisons (84 mip/layer plus21 cross-block linear
midpoints) and12 2D/array Load checks. SRGB RGB conversion precedes averaging;
alpha stays linear. Integer LOD selection only; no trilinear claim.

Final14 runtime processes have expected outcomes:8 BC modes and ordinary/
typeless/depth-MS GPU controls exit0 with exact nine-word readbacks11..99;
format54 checks pass; two deliberately wrong mip/sRGB shader oracles exit1
at dimension2 (0 versus33). Exact immutable staged DLL de0751e8... and Metal
API/GPU Validation verified. Eight current shader fixtures compile; scoped
Meson build and changed-test-TU no-private syntax exit0. Initial target/PATH
build failures and escaped-log-path verifier correction retained externally.

Selected advertised format operations show no contradiction in this workload;
no production defect claimed fixed or capability promoted. BC transparent/
other palette selectors, BC2/4/5/6H/7,3D,full placed/view matrix, fractional LOD,
cube seams, native Windows, full no-private runtime and T3/T4 NOT_RUN.
No source/header added, production state/cache/table, ABI, layout or scheduling
change. Performance NOT_RUN with unchanged runtime DLL; historical recording/
submit and Device/FL12_2 gates OPEN. Actual macOS27.0.1/Xcode27.1/M1Max/
Apple7/MSC4.0.1/Wine11/clang23.1.1. User .gitignore unchanged.
Evidence: `../dxmt-test/n02-bc-sampling-20261024/N02-bc-sampling.md`,
`manifest.json`, `results.json`, `shader-builds.json`, `no-private.json`,
`machine.json`, `sources.json` and native-load/validation logs.

### N02 — BC volume mip and Z-slice GPU evidence, 2026-10-24

Basec81b2b8b. Existing texture-dimensions helper/fixture now uses volume mip
count (not array-layer count) for3D subresources. BC slot8 is8x8x4 with four
mips, typed/typeless and UNORM/SRGB; distinct RGB/BC3 alpha by Z. Four volume
subresources have depths4/2/1/1:8 slices, each Load+Sample checked, plus one
base-mip Z-midpoint interpolation per mode. Previous2D/cube assertions stay.

Apple archive excludes compressed3D, while pinned modern Dawn Metal backend
74e2c29bb92632dbf7fd6fa0f5bb8ea6ec55b5af enables BC sliced3D behind native
BC support. Neither reference alone proves DXMT behavior. Actual8 BC1/BC3
GPU mode successes reject an unsupported-everywhere premise on this machine;
no speculative support reduction or upstream implementation copying.

Final13 processes:8 BC modes plus ordinary/typeless/depth-MS GPU controls
pass exact11..99; format54 checks pass; wrong expected Z(z+1) shader exits1
at dimension8(0 expected99). Seven final shaders, scoped Meson build and
changed-test-TU no-private syntax exit0. Immutable native DLL de0751e8...,
final staged runner identity and Metal API/GPU validation verified. Selected
BC3D evidence advances from NOT_RUN to locally GPU_VERIFIED; no full format
or Device gate closes. No runtime source/header/state/cache/table/ABI/layout/
scheduling or FL/SM/Options/Tier change. Performance NOT_RUN; prior gates OPEN.
Other BC selectors/formats, sparse/placed permutations, fractional LOD, native
Windows, full no-private runtime, D3D11 rendering and T3/T4 remain NOT_RUN.
Actual macOS27.0.1/Xcode27.1/M1Max/Apple7/MSC4.0.1/Wine11/clang23.1.1.
Evidence: `../dxmt-test/n02-bc-volume-20261024/N02-bc-volume.md`,
`manifest.json`, `results.json`, `shader-builds.json`, `no-private.json`,
`sources.json`, pinned source snapshots and native-load/validation logs.

### N12 — consecutive resource validation locality, 2026-10-24

One N12 node borrows the existing authoritative state-map entry for consecutive
resource events in `ValidateRecordedResourceStates`, shared by Close and queue
admission. Two stack pointers rebind on resource change; no persistent cache,
second state owner, allocation, lock, ABI/object-layout or GPU scheduling change.
All range/state/split/error checks and rollback remain. E map lookups become R
consecutive-resource-run lookups; pending-split search complexity is unchanged.
Existing runners add A→B→A valid/invalid local Close tests and an alternating
resource timing control, with identical final test PEs on both DLLs.

Scoped build and three DXMT_NO_PRIVATE_API syntax-only checks pass. All 34
correctness processes pass under Metal API/GPU validation, including split
order/replay/concurrency/isolation, allocation failures and exact GPU readback.
All 32 timing processes pass with validation disabled and HUD enabled. Four
paired runs retain 96 split/control/record samples and 240 ordinary-submit
samples per side. Median process p95 Close is 158.15→41.70us (-73.63%) for
consecutive same-resource events; alternating-resource Close is +0.16%, matching
the locality model. Same-resource total p95 improves 22.10%.

Ordinary-submit p95 rises 2.00%, with all four pairs positive; empty-transition
Close p95 rises 0.45us. These signals remain unresolved. Recording gains are not
attributed to a method outside the recording window. Accept only the measured
target-path improvement; historical/global performance gate stays OPEN /
INCONCLUSIVE. No capability promotion; EnhancedBarriersSupported stays FALSE.
Full no-private runtime, native Windows, D3D11 rendering and T3/T4 NOT_RUN.

Evidence: `../dxmt-test/n12-split-validation-cost-20261024/`:
`N12-split-validation-cost.md`, `manifest.json`, `validation-results.json`,
`perf-results.json`, `perf-aggregate.json`, `paired-comparisons.json`,
`perf-identities.json`, `no-private.json`, `exports.json` and raw logs.

### N12 — pending split removal cost, 2026-10-24

Base55e1849c. One N12 slice replaces completed pending-entry suffix erase with
at most one tail move and pop, guarded against Com self-move. Pending entries
are keyed by resource/subresource; storage order is not recording event order.
Existing states/splits remain sole owners. All pairing/range/state/error checks,
queue admission and commit/rollback remain. No new allocation, lock, cache,
source/header, ABI/object/resource layout, scheduling or capability change.
Linear find_if remains; changing search positions can add lookup cost.

Existing command-list benchmark adds peak32/512 independent-resource controls,
forward/reverse completion, 2,048 pairs and exact unique per-resource/per-round
GPU readback. Existing enhanced runner adds three distinct texture patterns,
END order1,0,2 across submissions and BEGIN Reset after fence. All43 final
correctness processes pass with Metal API/GPU validation, including mismatch,
concurrency, queue isolation and allocation failures. Scoped build, three changed
TU no-private syntax checks and six export identities pass. Full no-private
runtime, native Windows, D3D11 rendering and T3/T4 NOT_RUN.

Main48-process AB/BA/BA/AB series, median process p50/p95(us): pending512 Close
1262.075/1352.40→455.750/495.20 (-63.89%/-63.38%); total p95 -45.23%.
Pending32 Close p95 -39.43%. Reverse512 Close p50/p95 +4.10%/+1.34%, total
p50+5.22%: retain the order-dependent cost. Main record p50/p95+0.37%/-2.28%;
ordinary submit+7.24%/+1.05%; low-pending split submit p95+13.84% triggered
performance warnings. Independent16-process adjacent-only series with identical
DLLs/PEs yields ordinary+0.16%/-3.21%, split submit p95-17.76%. Both retained;
large adjacent regression is not stable across workload schedules, cause remains
unidentified. Accept target improvement only; global/historical performance gate
OPEN/INCONCLUSIVE. No recording speedup attribution or all-order scaling claim.

Each main split/control/record mode retains96 samples per side; ordinary240.
Adjacent series retains96 split and240 ordinary per side separately. Timing
validation0/0, correctness1/1, HUD1, fixed isolated Wine/device/cache. All64 timing
processes exit0; per-resource payload/readback and two-encoder/zero-staging-buffer
assertions remain. Actual macOS27.0.1/Xcode27.1/M1Max/Apple7/MSC4.0.1/Wine11/
clang23.1.1. EnhancedBarriersSupported remains FALSE; Device/FL12_2 gates OPEN.
User .gitignore unchanged. No push.

Evidence: `../dxmt-test/n12-pending-split-removal-20261024/`:
`N12-pending-split-removal.md`, `manifest.json`, `validation-results.json`,
`perf-aggregate.json`, `adjacent-perf-aggregate.json`, both paired comparisons,
`perf-identities.json`, `no-private.json`, `exports.json`, official spec snapshot
and raw logs. Pilot data is separate from final acceptance.

### N02 — typed-UAV reporting matches texture-view eligibility, 2026-10-24

Base8d2d51d5. Confirmed query/view contradiction: BGRA8 native TextureBufferRead
alone yielded UAV_TYPED_LOAD without a creatable typed UAV. Existing pure header
predicate now shares logical RGB96 exclusion, native Write and typed texel-buffer
Write/ReadWrite eligibility between Device reporting and texture-view admission.
Delete copied view predicate and repeated query write tests. Existing snapshot is
the only capability owner; no new state/cache/table/lookup/allocation/lock/native
operation, D3D12 source/header, ABI/layout or scheduling change. No promotion.

Same final PE baseline57/58 versus candidate58/58. Across58 query tuples only
format87 Support2 changes0x40→0; Support1 stays0x17cd3f2. Existing role fixture
explicitly enables BGRA UAV/RT resource flags, tests empty/seeded rejection and
legal UNORM/SRGB SRV/RTV identity; both DLLs67/67. Final18 processes:17 success,
1 expected baseline failure, all native DLL loads and Metal API/GPU validation
verified. Both DLLs pass typed/typeless nine-dimension GPU readback11..99 and
full-submit210 FULL/221 EMPTY GPU/descriptor/lifetime/Fence checks. BGRA-specific
GPU sampling/RT and exhaustive typed-UAV arithmetic are not claimed. Candidate
feature69, resource21 and typed/cast/counter/plane/command/barrier controls pass.
Scoped build and four changed-TU no-private syntax checks pass; full no-private
runtime, native Windows, D3D11 rendering and T3/T4 NOT_RUN. Six exports unchanged.

32-process AB/BA/BA/AB timing, median process p50/p95(us): format120000-query
batch3167.70/3242.10→3402.95/3466.40 (+7.43%/+6.92%); view10000-writes
1792.875/1863.90→1806.75/1872.60 (+0.77%/+0.47%); Recording
3583.75/3676.00→3040.75/3385.80 (-15.15%/-7.89%); ordinary Submit
108.90/140.45→111.425/143.60 (+2.32%/+2.24%). Format median rises in all
four pairs, triggering the warning: retain and accept the quantified correctness
cost (~1.96ns/query median,1.87ns/query from batch-p95 increment, not individual
call p95). Fixed guard and shared predicate, no new hot-path state. Recording gain
is not attributed to query/view code. Retain96 samples/side per format/view/record
mode and240 ordinary. Historical/global performance gates OPEN/INCONCLUSIVE.

Actual macOS27.0.1/Xcode27.1/M1Max/Apple7/MSC4.0.1/Wine11/clang23.1.1. Pinned
historical upstream a148782 all-one format TODO is not adopted. FL/SM/Options/
Tiers unchanged; EnhancedBarriersSupported FALSE. Plane MS_LOAD underreport and
complete Device/format/FL12_2 gates remain OPEN. User .gitignore unchanged; no push.
Evidence: `../dxmt-test/n02-uav-report-admission-20261024/`:
`N02-uav-report-admission.md`, `manifest.json`, `validation-results.json`,
`format-differential.json`, `shader-identities.json`, `no-private.json`,
`perf-aggregate.json`, `paired-comparisons.json`, `perf-identities.json`, official
source snapshots, pinned upstream source and raw logs.

### N02 — typed-UAV GPU load/store oracle, 2026-10-24

Base0ce5e12b. No production defect reproduced in the selected native Tier2 set;
fix confirmed test false-positive: buffer and texture copies aliased readback
at offset0. SAME external shader writes buffer0/texture255: original HEAD runner
exits0 falsely, corrected runner exits1 with both distinct values. Readbacks now
use offset0/512; add valid input/output transitions and checked completion/device
status. Existing sampler runner/HLSL adds phase constants, two dispatches separated
by UAV barrier, distinct 2x2 values, CPU before-load and physical after-store
oracles. Independent test-only byte decoder; no runtime capability table.
Three optional Meson variants reuse existing HLSL. No new source/header/runner.

Final22 GPU processes pass:18 formats (3 R32 plus15 additional-set candidates),
468 scalar load/store checks, and4 existing sampler modes. Three external shader
mutations fail as expected (load, store, alias); three invalid CLI forms exit2.
Adjacent feature69/69 and format58/58 pass. All GPU cases verify native stage DLL,
API/GPU validation1/1/HUD1 and no validation/aborted-buffer errors. Immutable DLL
b094fae2 is previous round's production candidate; test build regenerated version
metadata in build-tree DLLs, which were not substituted. Scoped runner build and
changed-TU no-private syntax check pass. Meson reconfigure and fixture dry-run
pass; equivalent source/profile/define DXC commands executed using matched Wine;
default fixture commands with older configured toolchain Wine NOT_RUN.

Apple Tier2+Tier1 and Microsoft typed-UAV snapshots bound safe candidates; fresh
M1Max native readWriteTextureSupport=2. Runtime GPU readback, not native support
alone, establishes this limited evidence. Packed/RG/SNORM combined-read_write
lowering remains UNVERIFIED; no unsafe dispatch. Vector shader research fixture
is not a complete additional-formats gate. FL/SM/Options/Tiers unchanged, including
TypedUAVLoadAdditionalFormats FALSE. No ABI/layout/runtime scheduling change.
Performance NOT_RUN (production unchanged); retain prior query p50+7.43%/
batch-p95+6.92% warning and OPEN global performance/Device/format/FL12_2 gates.
Full no-private runtime/native Windows/D3D11 rendering/T3/T4 NOT_RUN.
Actual macOS27.0.1/Xcode27.1/M1Max/Apple7/MSC4.0.1/Wine11/clang23.1.1;
fixed isolated prefix, user .gitignore unchanged, no push.

Evidence: `../dxmt-test/n02-typed-uav-gpu-20261024/`:
`N02-typed-uav-gpu.md`, `manifest.json`, `validation-results.json`,
`negative-results.json`, `baseline-alias.json`, `adjacent-results.json`,
`no-private.json`, `sources.json`, `machine.log`, scoped build/DXC and raw logs.

### N02 — packed/RG/SNORM typed-UAV execution, 2026-10-24

Base6e14e665. Resolve prior optional-format uncertainty using the existing
sampler oracle. Actual runtime cached AIR uses one mode3 texture handle for
load/store; this alone does not prove a format unsupported. Native CPU-only
Metal preflight accepts R32 RW, RG16_FLOAT write/RW and RGB10A2_UNORM write/RW;
no command-buffer commit exists in that probe. Legal RG16_FLOAT GPU pilot passes.

Final43 GPU success processes:39 formats/1004 scalar checks (18 controls468 plus
21 optional536), and4 original sampler modes. Optional RGB10A2 UNORM/UINT,
R11G11B10_FLOAT, RG32/RG16/RG8, RGBA/R SNORM and16-bit UNORM prove before-load
and independent physical after-store output on this device. Generalize existing
test-only decoder for component widths, normalization/sign and packed fields.
Packed UINT values exceed255 and cover alpha0..3. Existing HLSL/Meson add SNORM
and packed-UINT variants; no new runner/source/header/framework/runtime table.
Four packed/sign mutations and3 inherited shader mutations fail at exact CPU
comparisons;3 parser negatives exit2. Adjacent feature69/format58 pass. Every GPU
log verifies native stage DLL, API/GPU validation1/1/HUD1 and no validation/aborted
buffer error. Wrong-output shaders remain valid and in bounds.

The proposed unsupported-MSC inference is NOT REPRODUCED for these21 optional
formats; preserve the verified functionality instead of clamping capability bits
from static ReadOrWrite naming or the native Tier2 list. No production edit,
ABI/layout/scheduling/compiler-option or public FL/SM/Options/Tier change.
TypedUAVLoadAdditionalFormats remains FALSE; full gate not established here.
Scoped build/no-private changed-runner syntax pass; registered shader fixture
dry-run passes and equivalent DXC commands run with isolated matched Wine.
Default fixture commands with older configured Wine and full no-private runtime
NOT_RUN. Same immutable production DLL b094fae2; only build metadata regenerated.

Runtime log4.0.1 is the compiled API/header contract; cached AIR compiler metadata
is4.0.3. Actual macOS27.0.1/Xcode27.1/M1Max/Apple7/Wine11/clang23.1.1. Performance
NOT_RUN (production unchanged); prior query p50+7.43%/batch-p95+6.92% warning and
historical/global performance gates retained. A8/legacy formats, typed buffers/
arrays/3D, special values/exhaustive boundaries, Windows/other devices/D3D11/T3/T4
NOT_RUN. Device/format/FL12_2 remain OPEN. User .gitignore unchanged; no push.
Evidence: `../dxmt-test/n02-uav-access-contract-20261024/`:
`N02-uav-access-contract.md`, `manifest.json`, `validation-results.json`,
`negative-results.json`, `legacy-negative-results.json`, `adjacent-results.json`,
`native-preflight.json`, `cache-extract.json`, `no-private.json`, official source
snapshots, scoped build/DXC logs and raw logs.

### N02 — A8 alpha-only SRV/UAV and placed output, 2026-10-24

Base2282f19f. No A8 production mismatch reproduced. Existing sampler oracle now
expresses alpha-only channel3 presence, zero RGB defaults and independent raw
alpha-byte storage. Source SRV also A8, uploads128 to defeat constant-one sampling;
shader initializes four varying alphas, captures typed loads and halves alpha.
Compute load/store quantization tolerances from respective values; no complete
numeric conversion claim. Existing runner adds generic --placed, checks allocation
info/overflow, creates output at nonzero alignment offset, and releases heap after
resource. Footprint format/2x2 dimensions/row size/count/pitch/readback bounds checked.
Meson adds alpha variant in existing foreach. No new runner/source/framework.

Final12 normal GPU processes:200 scalar checks (A8 committed/placed40, R8 committed/
placed40, RG8_UNORM24, RGBA8_UNORM32, packedUINT32, RGBA16_SNORM32), plus4 original
sampler modes. A8 placed size/alignment/offset each65536. Five wrong-output shaders
fail exact CPU comparisons (alpha-from-red, skipped placed alpha store, wrong SRV
channel, wrong RGB default, constant-one sample);3 bad placed CLI forms exit2.
Feature70/70 and format59/59 pass, each adds one A8 query. Observed A8 Support1
0x274d3f0/Support2 0xc0, HRESULT0. Generic query test does not demand optional A8
UAV bits universally; GPU runner checks current view/load/store claims first.
All19 native-device logs verify immutable stage DLL, API/GPU validation1/1/HUD1
and no validation/aborted-buffer errors. Final22 runtime processes match expected
success/failure exits. Pilot and a stale external expected-count assertion are
separate from final results. Scoped build,2 changed-TU no-private syntax and alpha
fixture dry-run pass; equivalent cs_6_0 DXC command uses isolated matched Wine.
Default fixture command with older configured Wine/full no-private runtime NOT_RUN.

Same production DLL b094fae2; no production source/header/table/cache/state owner,
ABI/layout/scheduling or public FL/SM/Options/Tier change. Actual macOS27.0.1/
Xcode27.1/M1Max/Apple7/Wine11/clang23.1.1; compiled MSC API/header4.0.1, preceding
round's compiler AIR4.0.3. Performance NOT_RUN; prior query p50+7.43%/batch-p95
+6.92% warning and historical/global gates retained. A8 RTV/MSAA, other shapes/
stages/numeric edges, Windows/other devices/D3D11/T3/T4 NOT_RUN. Device/format/
additional-UAV/FL12_2 gates remain OPEN, Options bit FALSE. User .gitignore unchanged;
no push. Evidence: `../dxmt-test/n02-a8-uav-20261024/`: `N02-a8-uav.md`,
`manifest.json`, `validation-results.json`, `negative-results.json`,
`adjacent-results.json`, `no-private.json`, official sources and raw build/DXC/logs.

### N02 — distinct depth/stencil samples expose PSO mask loss, 2026-10-24

Base445a3903. The existing texture-dimensions runner adds optional writer VS/PS
fixtures, four one-bit SampleMask PSOs per DSV format and single-layer MSArray
DSVs. After zero clears,12 draws write distinct values to4 MS samples and two
MSArray layers. Both depth/stencil families run in MS and MSArray via reversal.
Writer PS variants independently use PSO SampleMask alone or explicit
SV_Coverage; the latter is a diagnostic control, not a production workaround.
Existing readback now transitions UAV to COPY_SOURCE, bounds completion to30s
and checks device removal. No new runner or src/d3d12 source/header.

A real production defect is reproduced: SampleMask-only depth and stencil
both FAIL exit1 at dimension6. Raw depth/stencil readbacks show every sample
holding the last draw's value98/109/140, while array layers remain distinct.
Changing only the PS to explicit coverage passes all48 distinct sample/layer
values across four family/role runs, including default channels. This separates
PSO sample selection loss from MS Load addressing. Static source forwards
SampleMask to AIRCONV but has no MSC IRCompilerSetSampleMask caller; the bundled
converter API documents an all-one default reset after compilation. Production
fix is deferred to the next single-node N07 PSO/compiler/cache slice. These two
red results are REAL_DEFECT_FAIL, not negative controls or a completed fix.

Final17 device processes: four distinct-coverage GPU PASS, two real defect
FAIL, four legal wrong-index/layer shaders detected by CPU mismatch, five
original GPU controls PASS, feature70/70 and format59/59 PASS. Two additional
parser/file rejects occur before device creation. All17 staged native-DLL logs
have API/GPU validation enabled and no GPU validation error. Scoped build,
changed-TU no-private syntax,13 final DXC compiles and five fixture-target dry
runs pass. Full no-private runtime, configured Meson shader command and T3/T4
NOT_RUN. Initial missing graphics tables and an overly strict external feature
log check were corrected in the fixture/harness, with original logs retained.

Production DLL b094fae2... unchanged; no owner/cache/ABI/format table or
FL/SM/Options/Tier change. Four component MS_LOAD underreports and full
Device/format gates remain OPEN; uniform-clear success cannot justify promotion.
Performance NOT_RUN for this test-only change; prior cost/regression signals
remain OPEN. Evidence: `../dxmt-test/n02-depth-ms-samples-20261024/`:
`N02-depth-ms-samples.md`, `results.json`, `raw-results.json`, `manifest.json`,
`shader-builds.json`, `negative-builds.json`, `no-private.json`, `sources.json`.

### N07 — MSC fragment PSO SampleMask and cache identity, 2026-10-24

Baseecf87c94. Fresh private-runtime depth/stencil originals both reproduce the
N02 failure before modification; both become green after forwarding canonical
SampleMask through existing classic/mesh graphics conversion, key and thunk.
MSC previously ignored the state while AIRCONV received it. The existing
conversion key now hashes normalized fragment mask and setter availability;
policy version11->12 excludes old incorrect conversions. Canonical PSO blobs
already encode SampleMask and retain their existing owner/format.

A flagged fragment-mask/vertex-layout union preserves native/PE64 size11960
and WOW64/PE32 size11872 and all measured offsets. WOW64 forwards the tagged
member. Native compilation calls optional IRCompilerSetSampleMask on each
non-default fragment conversion, including both compilation passes. Missing
setter rejects that PSO with E_NOTIMPL, while default all-one still works.
Only an internal optional-symbol observation bit changes, not public features.

Original/reversed depth/stencil, zero/default masks, explicit coverage,
coverage intersection and warm-cache readbacks pass. First matrix:2 original
red-to-green,19 directed/adjacent processes,2 missing-API controls;168 normal
sample/layer comparisons plus12 missing-API default checks. Wrong zero-reader
negative controls are detected. Warm runs each verify11 memory/6 persistent
hits. Pipeline persistence compute/graphics, stream/library and invalid cases,
feature70/70 and format59/59 pass. After adding the separate gated mesh caller,
final PE build and four original/subset smokes add48 passing comparisons.
Mesh execution remains unverified/gated; prior matrix DLL is retained separately.
All correctness device runs verify API/GPU validation; native paths checked by
dyld. No GPU validation error/abort. Scoped PE+Unix builds, postprocessing,
three final no-private syntax TUs, native/PE64/PE32 layout checks,16 DXC compiles
and five fixture registrations/dry-runs pass. Full no-private/WOW64 runtime,
configured Meson shader command and T3/T4 NOT_RUN.

Balanced4-process-order performance (validation0/0, HUD1) retains warnings:
default conversion-cold first8 PSOs29286.9->30429.65us(+3.90%); persistent-warm
18399.8->18421.95us(+0.12%). Recording p50/p95 -1.38%/-2.20%; ordinary Submit
93.6/103.0->94.1/114.5us(+0.53%/+11.17%). Submit tail causality remains OPEN;
no regression-free or speedup claim. Masked cold/persistent first8 costs
80161.2/18261.1us (one pair, distinct masks require distinct compilations).
No added draw/submit lock, allocation or scan; existing cache owns the extra
32-bit identity input. Earlier/global performance gates remain OPEN.

No new src/d3d12 source/header, cache manager, format table, root/binding ABI
or FL/SM/Options/Tier/format promotion. Wire layout preserved; internal C++
forwarding adds one value. PS-less/color-target/other-sample-count paths are
not claimed verified. Component MS_LOAD underreport and full Device gates OPEN.
Next: N13 ordinary Submit p95 investigation. Evidence:
`../dxmt-test/n07-sample-mask-20261024/N07-sample-mask.md`, `manifest.json`,
`red-results.json`, `green-results.json`, `results.json`, `final-results.json`,
`missing-results.json`, `abi-results.json`, `perf-aggregate.json` and raw logs.

### N13 — N07 Submit tail module attribution, 2026-10-24

Base960ec7b0; evidence-only slice. The original immutable SM5 full-submit
probe runs independently of the preceding PSO-cost/50000-copy workloads.
Four balanced process orders cross old/new D3D12 PE with old/new matched
Winemetal PE+Unix bridge; two additional orders compare byte-identical old
modules at different paths. Common DXGI, probe, compiler DLL and Wine loader
hashes are asserted before execution. Private Wine copies load the intended
native companion; the original n15 installation remains unchanged. Crossover
is limited to SM5 and does not validate the changed masked-SM6 protocol.

The initial matrix had different DXGI bytes and is excluded from attribution;
all initial logs/manifests remain under `rejected-dxgi-confound`. The corrected
24 processes exit0:4 with API/GPU validation and dyld path checks,20 with
validation0/0 and HUD1 for timing. All original descriptor mutation/lifetime,
residency counters and GPU oracles pass:5040 full cases plus5304 empty
submission/fence iterations. Correctness timings are excluded. QPC covers
ExecuteCommandLists only; compilation, fence waits and readback are outside.
Each process retains60 of70 rows per full scene and200 of221 empty rows;
p95 uses nearest rank. No derived or instrumented probe was introduced.

Median process p50/p95 us, four processes per crossed cell:

| D3D12 / bridge | ordinary | exact | fallback | empty |
|---|---|---|---|---|
| old / old | 94.350/105.600 | 98.050/106.850 | 118.125/128.200 | 18.675/23.000 |
| new / old | 93.650/102.400 | 97.000/106.450 | 117.950/130.950 | 18.600/23.100 |
| old / new | 95.675/106.250 | 99.425/117.200 | 119.325/138.800 | 18.675/22.600 |
| new / new | 94.275/104.750 | 97.800/107.200 | 117.375/131.850 | 18.500/22.900 |

Ordinary new/new versus old/old paired p95 changes are +2.31%, -6.07%,
-4.75%, +2.89%. Changing PE under old/new bridges gives paired medians
-1.64%/-2.46%; changing bridge under old/new PE gives +3.90%/+0.05%.
Same-binary A/A p95 changes +22.74%/+2.44% demonstrate variation, not a
universal noise bound. These descriptive comparisons do not establish a
stable positive module effect, prove scheduling/predecessor-workload causality,
or certify regression-free performance. Historical N07 +11.17% alert retained;
causality INCONCLUSIVE and global performance/Device gates OPEN.

No runtime/test source, owner, synchronization, ABI, resource layout or public
capability changes. No speculative queue optimization/rollback. Do not repeat
this matrix on unchanged code; another N13 patch needs a distinct reproducible
owner/call-cost observation. Next candidate: N02 component depth/stencil
MS_LOAD consistency through the existing sample/layer oracle. Actual host
remains macOS27.0.1/Xcode27.1/M1Max Apple7/Wine11 x64 Rosetta/clang23.1.1;
MSC runtime version not remeasured. Build, no-private runtime, native Windows
and T3/T4 NOT_RUN. Evidence: `../dxmt-test/n13-submit-tail-20261024/`
`N13-submit-tail.md`, `manifest.json`, `probe-source.json`, `environment.json`,
`results.json`, `aggregate.json`, `contrasts.json`, scripts and raw logs.

### N02 — component MS Load sample counts and dual readers, 2026-10-24

Base6fd5e260; test/evidence slice. Fresh component queries21/22/46/47 still
report Support1=0x1011a0/Support2=0 without MS_LOAD. The external bit-presence
gate is red for all four. No public bit change: N02 requires a separate
capability milestone for exposure increases. Raw custom-swizzle keys miss the
native capability table; blanket normalization would also incorrectly treat
component SRVs as depth targets and suppress SHADER_LOAD. Role separation
must be retained by any future narrow correction.

The existing texture-dimensions runner accepts final --samples=2|4|8, default4,
only with distinct depth/stencil writers. Resource and PSO counts, bounded
one-bit masks and sample loops use that value. Both DSV quality queries run
before an unsupported count exits4 without allocating textures. The existing
shader uses MS_SAMPLES and validates both textures' dimensions/sample count;
a mismatch returns before Load to avoid invalid indices. Distinct values and
default channels still detect sample/layer aliasing. Four2x/8x DXIL fixture
targets are registered beside the existing4x targets. No new runner or
src/d3d12 source/header, runtime owner/cache/format table or state.

Final accepted matrix:28 device processes,20 PASS and8 unsupported8x skips.
Sixteen component GPU cases cover2x/4x, depth/stencil, both D24/D32S8 families,
MS/MSArray and reversal, through both SM5/AIRCONV and SM6/MSC compute readers.
Writers remain SM6/MSC in both groups; SM5 graphics writers are not claimed.
Both DSV formats report quality1 at2x/4x and quality0 at8x on this host.
Ordinary/typeless GPU controls and feature70/format59 assertions pass. Another
15 final processes have expected exits: five GPU smokes, one unsupported8x,
old-runner CLI rejection and dimension mismatch, three legal wrong-count/
index/layer controls, four malformed CLI rejects. Positive component loops
check192 sample/layer values across matrix and final smokes. All device runs
verify staged DLL/native paths and API/GPU validation1/1/HUD1; no GPU validation
error or aborted buffer observed. Pilot runs before the early-return guard are
retained separately and excluded; accepted runs use final sources/PE/CSOs.

Scoped build, final changed-TU no-private syntax,17 shader compiles (9 DXIL,
6 DXBC,2 legal negative DXIL), four target registrations/dry-runs, changed-line
formatting and diff check pass. Corrected initial Meson CLI/target errors are
retained. Configured Meson shader execution, full no-private runtime, native
Windows and T3/T4 NOT_RUN. Production DLLs are byte-identical to N07 candidate;
performance NOT_RUN and historical warnings remain OPEN. Actual environment:
macOS27.0.1/Xcode27.1/M1Max Apple7/Wine11 x64 Rosetta/clang23.1.1; actual MSC
version not remeasured. FL/SM/Options/Tier, ABI/layout and scheduling unchanged.

Compute evidence now covers supported2x/4x through both backends. Public
underreport, Device and FL12_2 gates stay OPEN. Next distinct slice: dedicated
MS_LOAD milestone with pixel-stage readers and role-negative/hardware gates,
then review the narrow reporting correction; do not repeat this unchanged
compute matrix. Evidence: `../dxmt-test/n02-component-msload-20261024/`
`N02-component-msload.md`, `manifest.json`, `query-red.json`, `results.json`,
`final-results.json`, `shader-builds.json`, `negative-builds.json`,
`no-private.json`, environment/scripts and raw logs.

### N02 — component MS_LOAD pixel milestone and report fix, 2026-10-24

Base4fe09340; dedicated narrow format-report milestone after the preceding
compute prerequisites. Four component SRVs21/22/46/47 missed MS_LOAD because
their custom-swizzle keys were absent from the physical capability map.
CheckFeatureSupport now chooses the native key for custom plane components,
does one map lookup, keeps their logical role capabilities None and inherits
only native MSAA -> MS_LOAD. Fully typeless storage and real DSV roles remain
separate. No new capability table/cache/state/owner or runtime source/header.

Same final feature PE on old DLL twice:55 pass/4 fail each; corrected DLLs:
format59/59 and full feature70/70. Parent DSV MSRT gates each component's
expected MS_LOAD; role negatives forbid DSV/RTV/blend/MSRT/resolve/UAV/filter/
comparison/gather and Support2 bits. In the observed format fixture, exactly
four values change0x1011a0 ->0x5011a0, Support2 stays0. Final single-lookup
reports match the initial corrected trial. This closes the local component
underreport; it does not certify the whole Device/format/FL12_2 gate.

Existing texture runner adds --pixel-read, one1x color attachment and a
zero-seeded UAV, sharing CheckDimensions with compute. Pixel UAV is u1 after
RT0, compute remains u0. Matched SM5 VS/PS and matched SM6 VS/PS verify2x/4x,
depth/stencil, both families, MS/MSArray and reversed pairings. Six PS fixture
targets register2x/4x/8x;8x remains unsupported on this host. Initial compiler
u0/RT0 conflict and mixed-family PSO rejection are preserved as fixture pilots,
not runtime bugs; no workaround was added.

Initial corrected two-lookup trial:31 expected-exit processes, including2 red
queries,2 green query runs,16 pixel cases,4 legal wrong-count/index/layer/
no-oracle controls,2 compute controls,8x skip, mixed-family/CLI rejects and
plane/command-type regressions. Final single-lookup DLL adds9 successful
format/feature,4 pixel,2 compute and plane runs. Positive component checks
total216 sample/layer values across these accepted runs. Device correctness
processes verify staged D3D12/native paths and API/GPU validation1/1/HUD1;
no GPU validation error or aborted buffer observed. The no-oracle shader
leaves initialized zeros and fails at dimension0, ruling out stale output.
Both2x/4x qualities are1 and8x is0 for both DSV formats. No8x GPU claim.

Scoped build, final3-TU no-private syntax,17 final fixture compiles (8 pixel
readers,4 writers,2 compute,3 negative),6 target registrations/dry-runs,
format/diff checks and unchanged D3D12 export names/ordinals pass. Configured
Meson shader execution, full no-private runtime, native Windows and T3/T4
NOT_RUN. Actual host remains macOS27.0.1/Xcode27.1/M1Max Apple7/Wine11 x64
Rosetta/clang23.1.1; MSC actual version not remeasured. No production binding/
COM/wire ABI, layout, scheduling or FL/SM/Options/Tier/validated-flag change.

An initial extra-lookup trial measured query p50/p95+7.49%/+5.78% and ordinary
Submit+0.11%/+15.10%; its DLL/patch/logs are retained. Final one-lookup code,
four AB/BA/BA/AB pairs,24 processes, validation0/0/HUD1, median process us:
query120000 calls3393.00/3421.95 ->3174.90/3198.45 (-6.43%/-6.53%);
record50000 copies3037.05/3232.95 ->3016.40/3097.95 (-0.68%/-4.18%);
ordinary Submit96.325/121.25 ->96.425/109.10 (+0.10%/-10.02%). Retain24
of25 query/record batches and60 of70 ordinary rows, nearest-rank p95. All8
full-submit processes also pass1680 GPU/counter cases. Query checksum deltas
match the two changed component formats in that12-format workload. No new
runtime allocation, lock or scan; one existing lookup per format query.
Results do not prove Submit-tail causality or universal speedup; initial and
historical warnings/global performance gates stay OPEN. No unchanged matrix
repeat without a distinct reproducible owner/cost observation.

Evidence: `../dxmt-test/n02-msload-pixel-20261024/N02-msload-pixel.md`,
`manifest.json`, `red-results.json`, `validation-results.json`,
`final-results.json`, `report-delta.json`, `perf-aggregate.json`,
`extra-lookup-trial`, official sources, commands and raw logs. Other devices,
additional stages, full D24 quantization/filter/comparison/resolve semantics
and native differential remain unverified. Overall Device/FL12_2 gates OPEN.

### N12 — classic split ordering across subresource ranges, 2026-10-24

Base f4dfb92d. Microsoft Enhanced Barriers / Split Barriers requires no other
intervening barrier on the same resource. Disjoint subresources do not exempt
classic split pairs from that rule. This closes the earlier normative ambiguity
and supersedes the CPU-only disjoint positive candidate, not its historical log.
Five same-PE baseline negative modes fail: interleaved BEGIN, ordinary disjoint
transition, disjoint END, cross-list transition and cross-Execute transition.
Candidate rejects them at Close/admission and all five pass.

Existing transition validation now checks resource-level ordering once per
original split_index group; expanded subresources remain part of one barrier.
Normalized range/layout/access comparison moves out of the per-subresource loop.
One pending-vector lookup serves both checks, except a reordered first range
END may need a specific-subresource search. No second owner/state map, new
production file, allocation, lock, encoder or scheduler. ABI/layout unchanged;
six export names/ordinals match. Existing queue pending ownership is preserved.

The existing enhanced runner adds legal sequential mip pairs and six-subresource
GPU readback (3 mips x2 arrays, distinct values) within one Execute and across
Execute/reset. Three-resource pending range removal exercises swap-reordered
inherited entries and verifies GPU data. The single-lookup batch has27 expected
exits:5 baseline red,14 targeted candidate,4 full runners,4 two-sided performance
preflights. Final test-only range-removal/compiled-PE controls are separately
recorded. Scoped builds and both changed-TU no-private syntax checks pass.
Correctness verifies private loaded DLL/native paths and Metal validation1/1.
Full no-private runtime/native Windows/D3D11 rendering/T3/T4 NOT_RUN.

Final performance AB/BA/AB,18 processes, validation0/0/HUD1: median process
p50/p95(us), split Close37.80/40.20->38.85/42.60(+2.78/+5.97%);
split Submit91.05/121.60->92.65/163.50(+1.76/+34.46%);
split total538.95/590.30->532.25/617.30(-1.24/+4.57%);
ordinary recording3027.55/3122.30->3069.85/3141.70(+1.40/+0.62%);
ordinary Submit109.55/142.40->110.35/142.60(+0.73/+0.14%).
Split Submit paired p95 increases+11.30/+54.14/+32.98%, so performance acceptance
remains OPEN. Retain the initial extra-lookup trial(+17.70% split Submit p95);
one lookup did not eliminate the warning or establish its cause. No noise,
speedup, universal no-regression or unavoidable-cost claim. Next performance
work needs targeted split Submit phase attribution, not unchanged broad retries.

Actual macOS27.0.1/Xcode27.1/M1Max/Apple7/MSC API4.0.1/Wine11/clang23.1.1.
EnhancedBarriersSupported FALSE; FL/SM/Options/Tiers unchanged. Cross-queue
handoff, full enhanced/legacy UAV/aliasing interaction, native differential and
historical performance/Device gates remain OPEN. User .gitignore unchanged;
local commit only. Evidence: `../dxmt-test/n12-split-ranges-20261024/`:
`N12-split-ranges.md`, `design.txt`, `manifest.json`, `results.json`,
`no-private.json`, `sources.json`, official HTML/text snapshot,
`perf-identities.json`, `perf-results.json`, `perf-aggregate.json`,
`extra-lookup-trial/`, scoped build logs and raw correctness/performance logs.

### N13 — actual split submission validation boundary, 2026-10-24

Base ec44a617. Previous split Submit p95+34.46% remains OPEN; this round changes
only the existing command-list test runner and this ledger. New opt-in
`--split-cost phases` forwards the actual queue virtual validation call through
a scoped private-vtable timer. Slot derives from the current Itanium member
pointer, not a hardcoded index; matching private ABI is required. Empty-state
preflight and exactly1 call/submission guard the seam. Normal Close calls still
forward, and the patch restores on scope exit. No production DLL/ABI/owner/
layout/scheduling/capability change; staged DLLs match the prior N12 byte hashes.

Explicit `phases-delay` calibration inserts Sleep(1) only in that opt-in wrapper:
both DLLs pass25 rounds, validation minima1070.0/1542.8us and medians
1546.1/1545.4us. Excluded from performance. Existing changing GPU payload,
fence/device checks and exactly2 native blit encoders/round remain. Six
validation-enabled correctness processes pass: two-sided phases/calibration,
full command-list runner, enhanced six-subresource GPU readback. Scoped build
and changed-TU no-private syntax pass. Full no-private runtime, native Windows,
D3D11 rendering and T3/T4 NOT_RUN.

AB/BA/AB plain/phases controls,12 processes, validation0/0/HUD1, same PE and
dependencies.24 retained rounds/process,72/side/mode, median process p50/p95(us):
plain Submit87.70/122.40->88.20/136.60(+0.57/+11.60%);
profiled total90.35/137.30->90.80/146.00(+0.50/+6.34%);
validation35.95/38.60->36.55/39.10(+1.67/+1.30%);
post-validation suffix52.90/97.10->52.00/105.60(-1.70/+8.75%).
In candidate slow-total samples, validation36.6..40.6us and suffix78.3..126.7us.
Validation p95+0.5us does not explain the prior total p95+41.9us by itself.
Suffix includes descriptor collection, native factory/encoding/commit and return
cleanup, not exclusively Metal. Clocks measure wall time, not CPU self-time.

Plain paired p95 directions are+,+,-; profiled+,-,+. No noise bound, causal
native/OS claim or replacement performance acceptance. Instrumentation changes
call shape and adds clocks. Historical warning/global Device/performance gates
stay OPEN. Next meaningful measurement splits this exact suffix at existing
descriptor/native factory/commit boundaries; do not optimize validation or rerun
unchanged broad matrices based on the aggregate tail alone.
Actual matched host/prefix same as prior N12; shared Wine and user .gitignore
unchanged, EnhancedBarriersSupported FALSE, no FL/SM/Options/Tier promotion.
Evidence: `../dxmt-test/n13-split-phases-20261024/`: `N13-split-phases.md`,
`design.txt`, `manifest.json`, `identities.json`, `results.json`,
`no-private.json`, `perf-results.json`, `aggregate.json`, scoped build and logs.

### N13 — split Submit native suffix boundaries, 2026-10-24

Base20b2dc7a. Documentation-only outcome; production code and the tracked test
runner are unchanged. Detailed scoped diagnostic source, PE, reproduction
arguments and raw evidence are archived externally. External probe compile/link
and restored ordinary Meson test target build pass. Test-TU no-private syntax
passes; full no-private runtime, native Windows, D3D11 rendering and T3/T4 NOT_RUN.

Final detailed correctness13 processes pass with API/GPU validation1/1/HUD1:
both DLLs, four delay calibrations, error-options factory branch, full runner and
six-subresource GPU readback. Two pilot failures from an incorrect probe count
expectation are retained as rejected evidence. Actual sequence: two encoders,
six encodeCommands calls (two wait/two update/two stream), two ends. All original
calls forward; counts, phase partitions and changing GPU payload are checked.
No descriptor uses/native references occur in this workload.

AB/BA/AB three modes,18 performance processes, validation0/0/HUD1,24 retained
rounds/process. Median process p50/p95(us), before->candidate:
plain Submit95.70/155.40->92.10/131.40;
detailed total102.90/135.70->105.65/148.50;
collector0.70/0.80->0.70/0.90; native command-buffer factory3.50/5.00->3.50/5.70;
factory-end to commit-begin43.10/68.80->43.90/78.00;
native commit6.10/8.30->6.10/8.60;
nested encoder creation14.95/33.00->15.40/41.50.
Encoder creation contributes to slow samples on both DLLs; commands and residual
intervals also vary. These wall-time percentiles are not additive or CPU self-time.
Plain paired p95 directions+,-,-; detailed+,-,+. Instrumentation changes call
shape. No stable DXMT root cause, noise bound or replacement performance
acceptance is established; historical N12 p95+34.46% remains OPEN.

Do not remove valid encoder/fence boundaries or cache validation on this evidence.
Stop unchanged timing matrices; next distinct work can return to N12 cross-queue
split semantics under Device acceptance. Global Device/performance gates remain
OPEN, EnhancedBarriersSupported FALSE, no FL/SM/Options/Tier promotion.
Shared Wine and user .gitignore unchanged. Evidence:
`../dxmt-test/n13-split-native-20261024/N13-split-native.md`, `manifest.json`,
`identities.json`, `results.json`, `calibration.json`, `perf-results.json`,
`aggregate.json`, `split-native-probe.cpp`, `rebuild-commands.json` and raw logs.

### N12 — completed split pairs across queue handoff, 2026-10-24

Base192ea1a3. Existing enhanced runner adds opt-in `queue-handoff` and
`queue-handoff-reset`; no production change. Producer completes its texture
COPY_DEST->COPY_SOURCE split pair; consumer completes COPY_SOURCE->COMMON
pair then reads six subresources (3 mips x2 slices). END records first on one
CPU thread and BEGIN later on another; execution is BEGIN->END. First mode
queues consumer Wait/work before producer Execute, with no CPU completion wait
replacing the GPU dependency. Reset mode completes producer BEGIN, resets its
allocator/list, then submits END before signaling the consumer dependency.
Handoff fence is checked pending before Signal and completed after readback.

Both old/current N12 DLLs pass both modes with identical PE/dependencies and
verified staged native loader paths. Final10 correctness processes pass with
API/GPU validation1/1/HUD1, including full enhanced runner, range GPU/cross-Execute,
queue isolation, threaded-reversed and concurrent recording. External wrong-pixel
oracle intentionally exits1/readback FAIL; calibration controller passes and
retains its source/binary/rebuild arguments. Earlier9 passes retained separately.
No production defect reproduced. Existing concurrent test helpers did mutate a
shared copy-location field; both now use local copies, removing that harness
race. This is a static ownership correction, not a TSan result.

Scoped build and changed-test-TU no-private syntax pass; diff check passes.
Performance NOT_RUN (production unchanged); historical split Submit p95+34.46%
and global Device gates stay OPEN. Full no-private runtime, native Windows,
other queue types, cross-queue BEGIN/END half-pair matching and T3/T4 NOT_RUN.
This evidence covers complete pairs on each Direct queue; it does not establish
cross-queue half-pair semantics or full enhanced support. No public capability,
COM ABI, runtime ownership, layout or scheduling changes; new src/d3d12 files0.
Same actual M1Max/macOS27.0.1/Xcode27.1/Wine11/MSC API4.0.1 host, isolated n15
prefix; shared Wine and user .gitignore unchanged. Evidence:
`../dxmt-test/n12-crossqueue-20261024/N12-crossqueue.md`, `design.txt`,
`manifest.json`, `results.json`, `initial-results.json`, `no-private.json`,
`oracle-calibration.json`, rebuild arguments and raw validation logs.

### N12 — enhanced oracle interop and cross-queue halves, 2026-10-24

Base7fdb9d6c. Found a test validity defect: TestSplitExecutionOrder and
TestPendingTextureRemoval entered legacy COPY_DEST then switched directly to
enhanced barriers. Microsoft interop requires a COMMON domain boundary.
Both helpers now stay enhanced-only, using COMMON->COPY_DEST before upload;
no production change. Prior GPU evidence for these helpers has superseded
initial-layout validity; original logs are preserved. Current corrected tests
supply the bounded readback evidence instead of relying on those old setups.

New opt-in `cross-queue-half` completes producer BEGIN work, queues a fence Wait
on another Direct queue, then admits matching END there. Both immutable old/
current DLLs fail with0x887a0001 before native END encoding: pending split state
is queue-local, and consumer validation sees no BEGIN. These are two reproduced
failures, not correctness passes. Cross-Execute permission alone does not prove
cross-queue half-pair legality. Fixed VKD3D-Proton reference defers split work
until END but documents native-runtime issues at that revision; it is not a
native differential oracle. Normative status remains unresolved. Do not delete
unmatched-END validation or restore a device recording-pair container from this
observation. External report records the transactional ownership/dependency
requirements if cross-queue half-pair support is established.

Corrected same PE:14 correctness passes, two raw half-pair failures; controller
exit0 verifies these distinct outcomes. Both DLLs pass six-subresource range and
complete-pair queue handoff. Current DLL also passes ranged cross-Execute/Reset,
pending removal/reordered range, concurrent recording, queue isolation, full
runner and end-only/disjoint/global negative probes. API/GPU validation1/1/HUD1,
actual staged D3D12/native paths verified; no Metal validation errors. Scoped
build, changed-test-TU no-private syntax and diff check pass. Full no-private
runtime, native Windows, other queue types and T3/T4 NOT_RUN. Performance NOT_RUN
(production unchanged); historical p95+34.46% and global Device gates OPEN.
No COM ABI, owner, layout, scheduling or public capability changes; enhanced
FALSE, new src/d3d12 files0. Shared Wine and user .gitignore unchanged. Evidence:
`../dxmt-test/n12-split-queue-contract-20261024/N12-split-queue-contract.md`,
`design.txt`, `sources.json`, primary-source snapshots, `manifest.json`,
`results.json`, `no-private.json`, build and raw validation logs.

### N08 — shared texture descriptor extent limits, 2026-10-24

Base580d6931. Confirmed: eight max+1 texture descriptors returned finite copy
footprints. Existing ValidateTextureResourceDesc bounded Width by UINT_MAX but
omitted D3D12 dimension/array limits. Add required 1D/2D axes16384, arrays2048,
and 3D axes2048 limits in its existing switch; remove the weaker global bound.
All existing query/allocation/factory callers reuse this one descriptor validator.
No extra owner/cache/state, lock, allocation, encoder, COM ABI, resource layout or
capability change; new src/d3d12 files0. Planar remains unsupported, not promoted.

Existing resource runner:28 negatives and11 positive footprints (eight exact
maxima plus three existing controls). Same final PE: before metadata-only fails
all eight new rejection expectations, after passes. After also rejects actual
committed/placed output and null-output calls and allocation/Info1 queries.
Reserved2D invalid bounds reject E_INVALIDARG; unimplemented Reserved1D/3D
remain E_NOTIMPL. An inherited test expectation failure is preserved separately.
Exact maxima are metadata controls, not giant native allocations. Before invalid
cases are never sent to Metal. Adjacent RGBA 1D/2D/3D GPU copies pass both sides;
full resource runner passes after. All59 printed format reports match.

Final results: one expected red regression exit1; seven correctness passes,
two public-API cost-control passes, six performance processes. API/GPU1/1/HUD1
for correctness; actual staged D3D12 and native companion paths verified; no
Metal validation errors. Scoped library/test builds and four-TU no-private syntax
pass. Full no-private runtime, native Windows, maximum-size allocations, other
hardware and T3/T4 NOT_RUN. User .gitignore/shared Wine unchanged.

An initial benchmark used the N12 pre-fix DLL, introducing an unrelated baseline
confound; it is retained and excluded. Final before hash matches preceding
current-production DLL. Valid footprint query AB/BA/AB, validation0/0/HUD1,
25x3000 calls/process minus first batch,72 retained batches/side. Median process
p50/p95 of batch-average per-call costs(us):0.033100/0.034200->
0.034167/0.036000(+3.22/+5.26%, about+1.07/+1.80ns). Not individual-call p95,
no speedup/noise-bound claim. Keep the observed query cost increase and historical
Recording/Submit warnings OPEN; those benchmarks NOT_RUN. No mandatory guard is
removed for performance. Same actual M1Max/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1
host and isolated n15 prefix. Evidence:
`../dxmt-test/n08-texture-extents-20261024/N08-texture-extents.md`, `design.txt`,
`sources.json`, `manifest.json`, `results.json`, `no-private.json`,
`perf-results.json`, `aggregate.json`, external cost source/rebuild args and logs.

### N08 — preserve eligible explicit small MSAA alignment, 2026-10-24

`PopulateWMTTextureInfo` already checks the architecture-independent small-MSAA
eligibility contract. `GetResourceAllocationInfo1` then unconditionally raised
all MSAA requests to4MiB, including valid explicit64KiB requests; the shared
placed-resource admission consequently rejected legal small offsets/capacity.
One production condition now applies the automatic4MiB minimum only to zero
requested alignment. Explicit alignment still passes the existing eligibility
check and retains the native Metal alignment requirement. No new resource
owner/cache/state, source file, capability or command/Submit path change.

Red before: all4 eligible8x8 RGBA8/D32 2x/4x queries reported4MiB instead of64KiB.
After: Info/Info1 per-resource offsets/sizes, two-small batch, mixed-small/4MiB,
array2 query, automatic/explicit4MiB and oversized/4KiB-invalid controls pass.
The two-small batch reservation changes8MiB->128KiB. RGBA8 2x/4x each places two
textures at64KiB/128KiB in192KiB heap, releases the caller heap reference, clears
both red/green before either resolve, then waits on the GPU fence and checks
all pixels: four readbacks pass. This is allocation reservation/placement proof,
not a claim of64x lower committed native memory. D32 and array tests are query
only; no sample8 or exhaustive allocation claim.

One expected red; seven correctness runs plus two cost-control runs pass with
API/GPU1/1/HUD1 and staged DLL/native companion paths verified; no Metal validation
errors. Full resource runner passes and all59 format-contract rows are unchanged.
Library/test builds and four-TU no-private syntax pass; full no-private link/run,
native Windows, other GPUs and T3/T4 NOT_RUN. An external format-report parser
initially selected the wrong prefix; corrected comparison passes on retained
logs, without treating the parser failure as a runtime failure.

Matched valid public allocation-query AB/BA/AB, validation0/0/HUD1,25x3000 calls,
first batch dropped:72 batch averages/side. Median process p50/p95(us per call)
2.550020/2.594500->2.558035/2.611270(+0.31/+0.65%). These are batch-average tails,
not individual-call p95 and no speedup/noise-bound claim. Prior Recording/Submit
warnings remain OPEN; those unchanged paths NOT_RUN. User .gitignore/shared Wine
unchanged. Primary sources, baseline identity, red/green logs, exact benchmark
rebuild args and manifest: `../dxmt-test/n08-small-msaa-20261024/N08-small-msaa.md`,
`design.txt`, `sources.json`, `manifest.json`, `results.json`, `no-private.json`,
`perf-results.json`, `aggregate.json`.

### N02 — matched no-private Device runtime, 2026-10-24

Base3e5e4f8f. Previous no-private translation-unit syntax checks could not prove
PE/native link/deployment compatibility or actual configured feature gates.
Clean external x64 release build defines DXMT_NO_PRIVATE_API for host C/C++ and
native build C/C++; linked D3D12/DXGI/D3D11/WineMetal PE and Unix companion,
postprocessed Wine builtin, and reused existing PE runners in isolated Wine.
Compiler-command macro census covers D3D12(23),DXGI(5),D3D11(26),WineMetal PE(4),
Unix(4),Airconv(43) entries. Native cache private imports are absent in candidate
and present in private control; this pair is not an exhaustive private-API audit.

Only tests/dx12/dx12_feature_support.cpp adds runtime assertions for existing
no-private FL11_0 cap and OutputMergerLogicOp=FALSE. Same no-private test PE
rejects private DLL(FL11_1) as expected; matching no-private passes. Private test
variant still passes private Device contract(FL11_1/logicOp1). No production,
ABI, capability policy, resource layout, scheduling or src/d3d12 new file change.
SM6 cap stays6_0, Enhanced FALSE and advanced validation gates remain closed.

One expected configuration-mismatch red;15 validation-on zero exits, comprising
14 contract/readback passes and one no-resource SM6 dispatch SMOKE_ONLY; six
validation-off cost processes. Device identity/MSAA/InfoQueue/filter/lifetime,
interface contracts, multiple-fence ALL/ANY/cancellation/abandonment, InfoQueue
break/output and OOM rollback pass. GPU: full resource runner(including small
MSAA), D/S multi staging preservation/in-flight, SM5 graphics exact pixels,
DXIL cs6_0 root-UAV readback1234 and cost-control copy payloads pass. All59 format
rows unchanged. API/GPU1/1/HUD1 plus staged PE/native paths verified; no Metal
validation errors. Full no-private T3/T4, D3D11 rendering, native Windows,
other devices and extended stress NOT_RUN; no complete Debug Layer/FL claim.

Matched same-PE private/no-private configuration cost AB/BA/AB, API/GPU0/0/HUD1,
25 rounds/process,50k copies/round,first round dropped,72 retained rounds/side.
Median process p50/p95(us): Recording3061.95/3198.8->3027.15/3615.7
(-1.14/+13.03%); ordinary Submit22350.8/23034->22426.85/24279.9(+0.34/+5.41%).
Recording tail crosses warning threshold and stays OPEN; cause INCONCLUSIVE.
This is a configuration comparison, not performance attribution to test-only
changes. Historical Recording/Submit/split warnings remain OPEN; no rerun loop
or speculative runtime optimization. Close cost and per-process tails retained.

Initial private staging carried an older dxgi.dll; that entire cost matrix is
retained/excluded. Private runtime artifacts were rebuilt at current HEAD,
version headers and all four staged PE/native hashes verified, then red/format
checks and complete cost comparison rerun. An invalid no-work dry-run assertion
was corrected: version.h is always-stale; successful actual build/output hashes
are the evidence. Same actual M1Max/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1 host;
requested Xcode26 NOT_RUN. User .gitignore/shared Wine unchanged. Evidence:
`../dxmt-test/n02-no-private-runtime-20261024/N02-no-private-runtime.md`,
`design.txt`, `manifest.json`, `build-commands.json`, `macro-census.json`,
`results.json`, `perf-results.json`, `aggregate.json`, native imports and logs.

### N12 — preserve COMMON enhanced access dependencies, 2026-10-25

Base a037cdc0. Global/buffer/texture ordinary barriers confused raw COMMON
with NO_ACCESS because both lower to legacy zero for stage selection. The
existing shared emission lambda now uses raw access to preserve required
memory dependencies; texture stage selection/layout tracking and split owner
remain unchanged. No new production file/state/ABI/capability; Enhanced FALSE.

Existing command-list runner covers three scopes × COMMON/UAV/NO_ACCESS.
Same-encoder native oracle: COMMON0->1 packets, explicit UAV1->1, NO_ACCESS0->0.
Six exact GPU readbacks pass (also passed before; no corruption claim); three
no-resource controls are readback NOT_RUN. Full command-list/enhanced runners,
range/complete-pair queue handoff/removal, small-MSAA and Device contract pass.
Matched no-private focused/readback and FL11_0/logicOp0 contract pass. Staged
PE/native paths and API/GPU1/1/HUD1 verified. Cross-queue half legality remains
unresolved; Windows, other GPU, render-encoder COMMON, full T3/T4 NOT_RUN.

Final same-PE cost: API/GPU0/0/HUD1, observer off,1000 barriers/round,25 rounds,
AB/BA/AB,first round dropped,72 retained samples/cell/side. Required missing
barriers incur real CPU cost versus broken baseline; completion includes Wine
and fence latency, not isolated GPU time. All p50/p95 and controls retained;
COMMON recording p50 adds10.2/10.3/14.3ns per global/buffer/texture call;
1000-barrier Submit p50 adds34.15/34.38/31.82% versus broken baseline.
Overall/historical performance gates remain OPEN. See external report for
numbers, rejected harness/build/deployment attempts and explicit limitations.
User .gitignore/shared Wine preserved; focused local commit only, no push.
Evidence: `../dxmt-test/n12-common-access-20261024/N12-common-access.md`,
`design.txt`, `hypotheses.txt`, `manifest.json`, `sources.json`, `results.json`,
`perf-results.json`, `aggregate.json`, final build/runtime logs.

### N12 — ordinary NONE/access admission, 2026-10-25

Base2534edf8. Three ordinary enhanced barrier kinds accepted SYNC_NONE with
COMMON/COPY access. One shared side predicate enforces NONE->NO_ACCESS before
resource admission/emission; reverse implication deliberately not imposed.
Existing FailRecording/E_FAIL retained. No new file/state/lock/allocation/ABI,
layout/scheduling/capability policy change; Enhanced remains FALSE.

Existing enhanced runner27 cases: baseline twice12 invalid accepted/15 legal
pass; candidate twice27/27. Full enhanced/command-list, COMMON native9/six exact
GPU readbacks, range/complete-pair handoff/removal pass. Matched no-private
27/full-enhanced/native9/sixGPU/Device FL11_0+logicOp0 pass. API/GPU1/1/HUD1,
staged PE/native paths verified; no claim that CPU negatives are GPU tests.
Initial test expected E_INVALIDARG; existing recording policy is E_FAIL. Trial
retained, corrected same-PE red and candidate matrix rerun. No runtime HRESULT
change. Full sync/access, queue scopes/NONE execution lifetime, native Windows,
other devices/D3D11 rendering/T3/T4 NOT_RUN. Cross-queue half legality unresolved.

AB/BA/AB,25 rounds/first dropped,72 retained rounds/side/cell,API/GPU0/0/HUD1.
1000-barrier timing observer off; native counts/GPU data calibrated separately.
Recording p95 per-cell delta-5.00..+4.08%, Submit-6.88..+3.09%. Ordinary50000-copy
control p50/p95(us): Recording3473.75/3599.4->3239.35/3924.0(-6.75/+9.02%);
Submit22088.25/22393.8->22239/23752.8(+0.68/+6.07%). Tail observations and all
historical/global performance gates remain OPEN; no causal no-regression claim.
No unchanged rerun loop. Cost: fixed side comparisons in barrier recording only.
User .gitignore/shared Wine retained; local focused commit, no push.
Evidence: `../dxmt-test/n12-sync-none-20261025/N12-sync-none.md`, `manifest.json`,
`sources.json`, `design.txt`, `hypotheses.txt`, `results.json`, `perf-results.json`,
`aggregate.json`, `ordinary-perf-results.json`, `ordinary-aggregate.json`, logs.

### N12 — execution synchronization with NO_ACCESS, 2026-10-25

Basec51791ee. Shared ordinary emission elided both-NO_ACCESS barriers even with
nonempty before/after compute execution scopes. Existing lambda now considers
raw sync alongside access/layout for global/buffer/texture. Reuses conservative
native memory-barrier/pending path; no new encoder,owner/state/lock/allocation,
ABI/layout/queue policy/capability change. Enhanced remains FALSE.

Existing command-list fixture12 cases: baseline twice three mode3 native
failures/originalnine pass; candidate twice12 native pass. Six exact64-word GPU
readbacks pass both sides; six no-resource NONE/COMPUTE controls explicitly mark
readback NOT_RUN. Same-encoder two-dispatch packet/fence oracle confirms missing
native dependency restoration, not observed data-corruption repair or full
hardware execution-scope equivalence. Full command-list/enhanced,range/complete
per-queue-pair handoff/removal,Device contract pass. Matched no-private12/sixGPU/
full command-list+enhanced/FL11_0+logicOp0 pass. API/GPU1/1/Hud1,staged PE/native
paths checked; no Metal validation errors.

Cost: matched AB/BA/AB,25 rounds/first dropped,72 retained samples/side/cell;
1000-barrier recording batch-average/call,observer off,API/GPU0/0/Hud1. Restored
mode3 adds1000 required native commands versus broken baseline; stronger resource
barrier cost retained, no causal optimization claim. Ordinary50000-copy control:
record_us p50/p95 3220.45/3298.20->3131.30/3314.30 us (-2.77/+0.49%); submit_us p50/p95 22008.85/22754.00->22578.80/23098.40 us (+2.59/+1.51%).
All historical/global performance gates remain OPEN; completion includes Wine/
fence, not isolated GPU time. Full raw-sync/queue legality,NONE lifetime,aliasing
data-discard proof,render-specific execution,Windows/otherGPU/D3D11/T3/T4 NOT_RUN.
Cross-queue half-pair legality unresolved. User .gitignore/shared Wine unchanged;
local focused commit only, no push. Evidence:
`../dxmt-test/n12-execution-sync-20261025/N12-execution-sync.md`, `manifest.json`,
`sources.json`, `design.txt`, `hypotheses.txt`, `results.json`, `perf-results.json`,
`aggregate.json`, `ordinary-perf-results.json`, `ordinary-aggregate.json`, logs.

### N12 fragment constant-buffer synchronization — 2026-10-25

Base e99de894. Confirmed pixel corruption: shared render_stages_for_state omitted
Fragment from VERTEX_AND_CONSTANT_BUFFER, which also represents pixel CBVs.
Add that bit to the existing Fragment mask (25 -> 27); reuse legacy/enhanced/
pending owners. No runtime state/cache/allocation/lock, ABI, scheduling or public
capability change; Enhanced remains FALSE. Pure vertex use conservatively also
includes Fragment. One N12 mechanism; no new production source/header.

Existing command-list runner adds four same-encoder two-draw native/GPU cases:
enhanced CBV self, legacy CBV->UAV, UAV->CBV, non-pixel control. Separate SM5
reader/writer PSOs, root CBV b0/UAV u1; exact two pixels plus float4 buffer
readback and fence/device status. Same final PE baseline twice misses three
native Fragment scopes; CBV->UAV actually reads BLUE instead of GREEN before
the later UAV write. Candidate twice passes all four native/data cases. Control
scope remains25/27. Full command-list/enhanced,COMMON execution,complete queue
handoff,feature contract pass. Independent no-private build focused four/full
command-list+enhanced/FL11_0+logicOp0 pass. API/GPU1/1,Hud1,staged PE/native load
paths checked; no Metal validation errors. This is bounded correctness evidence.

Material performance OPEN: matched AB/BA/AB,25 rounds/dropfirst,72 retained/side,
observer off/API+GPU0/0/Hud1. 1000 consecutive CBV self barriers: Submit p50
411.25->6266.8 us; Execute-through-fence p50 3986.7->61682.4 us; recording/call
.0721->.10645 us. Completion includes CPU/Wine/event wait, not isolated GPU.
No intervening GPU work makes this redundant stress, not a representative frame.
Ordinary50000-copy recording/Submit p95 +.098%/-1.356%; Close2.0->2.4 us (+20%).
All historical/global cost gates remain OPEN. Future distinct N12 work may
coalesce adjacent native dependencies while preserving correctness/validation.

Rejected initialization,read-only fixture,SM5 u0 compile and stale benchmark
attempts retained/excluded; final benchmark source/exe hashes verified. Full
Windows/otherGPU/D3D11/T3/T4 and exhaustive queue/raw-sync/pending coverage NOT_RUN;
cross-queue half legality unresolved. User .gitignore/shared Wine unchanged.
Evidence: `../dxmt-test/n12-constant-stages-20261025/N12-constant-stages.md`,
`manifest.json`, `sources.json`, `results.json`, final `aggregate.json`,
`ordinary-aggregate.json`, build/runtime logs and excluded-trial directories.
Local focused commit only, no push; overall goal remains active.

### N12 adjacent native memory-barrier coalescing — 2026-10-25

Base9cf35826. Shared EmitMemoryBarrier emitted every contiguous native dependency
packet, causing redundant synchronization in the previous1000-CBV pressure case.
Use existing encoder cmd_tail to union adjacent render scope/after/before masks
or normalized compute scope. Any other command/pass ends adjacency.16 production
lines, O(1) predecessor check; avoids repeated packet allocation/native barriers.
No new owner/state/cache/scan/lock, production source/header, native ABI, encoder/
queue schedule, resource layout or capability change. Preserve all D3D12 input/
transition/split/alias validation and references; EnhancedFALSE/FL/SM unchanged.

Existing runner --adjacent-memory-barriers/default now checks two intervals
around three real same-encoder draws/dispatches. Baseline same final PE twice:
render modes0–2 count9/8,mode3 count11/10;compute9 nonempty count10/10;
three NONE controls0/0.13 redundancy
assertions fail,all10 GPU readbacks already PASS,six no-resource NOT_RUN. Candidate
twice16 native PASS:each nonempty interval1/1,NONE0/0;render scope7/stages27/27,
compute normalized scope3. Both pixels/float4 and six64-word buffer/texture RAW
readbacks correct; third dispatch verifies second transformation9*x+28. The
intervening GPU work retains separate dependencies. Broad-then-narrow scopes
require preserving earlier bits. External overwrite mutation is rejected by7
native union checks despite10 GPU data PASS;tracked production never replaced. Ordinary NON_PIXEL single
barrier control remains25/27. This round fixes cost,not a new pixel corruption.

Private full command-list/enhanced (including27 sync/access admission cases),
complete queue-handoff,feature contract PASS. Matched no-private focused16native/
10GPU,originalCBV4GPU,full command-list/enhanced,FL11_0+logicOp0 PASS. API/GPU1/1,
Hud1,staged PE/native paths checked; no Metal validation errors. Final source,
staged peers,benchmark lineage/no-private changed-TU macro verified by manifest.

Cost AB/BA/AB,25 rounds/dropfirst,72 retained/side,median process p50/p95;
observer off/API+GPU0/0/Hud1,same guarded benchmark PE.1000 CBV API calls produce
1000->1 native barriers with BOTH stages27/27. Submit p50/p95 6590.9/7475.9 ->
370.35/539.8 us (-94.38/-92.78%);Execute-through-fence65577.65/69321.1 ->
2713.15/3214.7 us (-95.86/-95.36%);recording/call .11635/.1223 ->.05785/.0631 us.
Completion includes CPU/Wine/event wait,not isolated GPU; synthetic adjacent
pressure,not game/frame speedup. Ordinary50000-copy recording/Submit p95
-.244%/-1.886%,Close p95 2.0->1.9 us; no causal ordinary speedup claim. Historical/
global cost gates remain OPEN. Full Windows/otherGPU/D3D11/T3/T4/exhaustive
raw-sync/queue/pending/alias-discard conformance NOT_RUN;cross-queue half legality
unresolved. Xcode27.1 actual,requested26 NOT_RUN. User .gitignore/shared Wine
unchanged;local focused commit only,no push;overall goal active. Evidence:
`../dxmt-test/n12-adjacent-barriers-20261025/N12-adjacent-barriers.md`,manifest,
sources/environment-actual/results JSON,final aggregate/ordinary-aggregate JSON,
mutation evidence;pre-union-strengthening matrices retained/excluded;
red/green/no-private/build/perf logs. Next candidate:N12 queue-specific raw-sync
admission with existing barrier oracle; no second mechanism started this round.

### N12 enhanced-barrier sync-domain/list-type admission — 2026-10-25

Baseb644d10a. Existing valid_barrier_sync_access checked only NONE=>NO_ACCESS;
nonzero undefined/queue-incompatible sync flags were accepted. Extend this one
pure predicate with type_: constexpr Copy/Compute/Direct allowed masks, reject
explicit bits outside the queue domain (even with ALL),no mask for Bundle.
All six global/buffer/texture side checks reuse it. Preserve aggregate shading
scopes on Compute and PREDICATION/EXECUTE_INDIRECT alias. Existing NONE/access,
split-phase/global-SPLIT/resource/layout guards retained. No stored masks/new
owner/state/cache/allocation/lock/scan,production file,ABI/queue/layout/capability
change. EnhancedFALSE/FL/SM unchanged. Errors follow existing FailRecording/
Close E_FAIL before split/resource bookkeeping;no device removal.

Existing enhanced runner --sync-scope-admission/default:41 finite bit-domain/
combination cases xDirect/Compute/Copy x3 barrier kinds x2 sides=738,plus18
nonempty Bundle cases. Fresh lists/NO_ACCESS/Common isolate sync admission;
invalid recordings never submitted. Same final PE baseline twice:246 legal and
66 existing invalid controls PASS,444 unexpected S_OK (210 undefined-domain,
216 queue-incompatible,18 Bundle). Candidate twice756 PASS;four failed-Close
sticky Reset/new-list-on-same-allocator/device-health controls PASS. Initial test
wrongly expected Reset recovery;Microsoft requires failed Close's error to
persist. Existing Reset correct;initial source/logs retained/excluded,oracle
corrected without N10 production change. Final760 assertions all PASS.

Private full enhanced (including27 NONE/access,split/ownership/interleaved),
full command-list,complete queue-handoff,feature contract PASS. Existing adjacent
oracle16native/10 exact GPU data PASS,six no-resource NOT_RUN. Matched independent
no-private760 assertions/16native+10GPU/originalCBV4GPU/full command-list+enhanced/
FL11_0+logicOp0 PASS. API/GPU1/1,Hud1,staged PE/native paths checked,no Metal
validation errors.31 final probe exits/source/stage/SDK hashes/changed-TU macro
verified. Legal raw scope admission is not full Enhanced Barrier conformance.

Cost AB/BA/AB,25 rounds/dropfirst,72 retained/side,median process p50/p95;
validation calibration thenobserver off/API+GPU0/0/Hud1. Unchanged tracked
command-list input/cost source+PE hashes checked;no stale artifact or unchanged
matrix retry.1000 validCBV API calls still ONE native barrier/stages27/27 BOTH
sides. Recording/call .0693/.0718 ->.05855/.0631 us;Submit366.05/544.9 ->
363.05/528.2 us;Execute-through-fence2602.35/3447.5 ->2650.5/3690.7 us
(p95+7.054%). Completion includes CPU/Wine/event wait,not isolated GPU time.
Ordinary50000-copy Recording p50/p95 3048.65/3300.4 ->3165.6/3492.8 us
(+3.836/+5.830%);Submit22209.5/22773.7 ->22482.8/23532.9 us (+1.231/+3.334%).
Close p95 2.1->2.1 us. Causes unproven;no validation-free/speedup/global cost PASS
claim. Current/historical performance warnings remain OPEN.

Full sync-access/layout/queue compatibility,NONE lifetime,pending/alias-discard,
actual RT/video,Windows/otherGPU/D3D11/T3/T4 NOT_RUN;cross-queue half legality
unresolved. Xcode27.1 actual,requested26 NOT_RUN. User .gitignore/shared Wine
unchanged. Evidence:`../dxmt-test/n12-sync-scope-20261025/N12-sync-scope.md`,
manifest/sources/environment-actual/final-selected-results JSON,red classification,
final aggregate/ordinary-aggregate JSON,build/runtime logs,initial-reset-expectation.
Local focused commit only,no push;overall goal active. Next candidate:N12
sync/access compatibility at the same owner before any capability promotion.

### N12 Copy/Resolve access and sync compatibility — 2026-10-25

Base 663a7c3d. Shared valid_barrier_sync_access admitted recognized sync bits
without transfer-access compatibility. Add COPY_SOURCE/DEST => ALL|COPY and
RESOLVE_SOURCE/DEST => ALL|RESOLVE, checking each family in combined masks.
Standalone SPLIT is deferred; existing phase/pair checks validate actual halves.
NONE=>NO_ACCESS and raw-bit/list-type admission unchanged. Same six checks,
no new owner/state/cache/file/ABI/native emission/capability change. Other access
families and full layout/queue compatibility remain OPEN; Enhanced remains FALSE.

Existing enhanced runner --transfer-sync-access/default adds144 Close assertions:
64 legal/80 invalid, both sides, four accesses and combined read masks. Resolve
uses global barriers to isolate access validation from resource/MSAA constraints;
copy also covers buffer/texture. Same final PE twice on immutable baseline:
62 unexpected S_OK,64 legal and18 existing invalid controls PASS. Candidate twice
all144 PASS. Full enhanced (including prior760 scope assertions/NONE/split),full
command-list,complete queue-handoff and feature contracts PASS. Adjacent native16
and exact GPU10 PASS,six no-resource readbacks NOT_RUN. Independent no-private
144/full enhanced/native16+GPU10/full command-list/FL11_0+logicOp0 PASS. API/GPU1/1,
staged DLL/native load paths checked,no Metal validation errors.30 probes checked;
source/PE/native hashes and no-private changed-TU macro verified. Private peer
DXGI/WineMetal remain immutable/matched to prior manifest on both sides;they are
not claimed identical to current build-directory PE copies. Initial overly strict
peer verifier failure retained;corrected lineage check passes,without sample retry.

Cost same PE,AB/BA/AB,3 processes/side,25 rounds/dropfirst,72 retained/side;
median process p50/p95,observer/API/GPU off,Hud1. Calibration both sides1000CBV
calls=>ONE native barrier/stages27/27,exact data PASS. Recording/call .0598/.0643
=>.06915/.0734 us (+15.635/+14.152%);Submit369.2/462.3=>363.05/506.2 us
(-1.666/+9.496%);Execute-through-fence2611.15/3186.9=>2618.85/3020.4 us
(+.295/-5.225%). Completion includes CPU/Wine/event overhead,not isolated GPU.
Ordinary50000-copy Recording3217.65/3507.4=>3610.55/3780.3 us
(+12.211/+7.781%);Submit22369.45/23093=>22478.2/22905.9 us (+.486/-.810%).
Close1.55/2.3=>1.65/2.2 us. Material recording/Submit-tail warnings retained;
causes unproven,including ordinary path without this validator. Correctness fix
has observed costs;current/historical performance gates OPEN,no free-validation
or performance PASS claim. All samples retained;no unchanged matrix retry.

Apple M1 Max/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1;requested Xcode26 NOT_RUN.
Full Enhanced conformance,NONE lifetime,pending/alias/discard,RT/video,Windows,
otherGPU/D3D11/T3/T4 NOT_RUN;cross-queue half-pair legality unresolved. User
.gitignore/shared Wine/prefix preserved. Evidence:
`../dxmt-test/n12-sync-access-20261025/N12-sync-access.md`,manifest/baseline/sources,
red-summary/results/aggregate/ordinary-aggregate JSON,build/runtime/verification
logs. Local focused commit only,no push;overall goal remains active.

### N12 invariant sync-domain decoding — 2026-10-25

Base d083da18. The previous recording warning reproduces before edits:
pressure.py seed reports +14.298% vs old663 in one process/side and exits1
for its >5% diagnostic warning; both GPU/native calibrations PASS. Disassembly
shows no extra out-of-line validator calls (Barrier1221 instructions/64 calls,
old1129/64),but queue-type mask selection duplicated for both sides in each kind
loop. Extract unchanged mask selection into barrier_sync_mask,compute once at
Barrier entry,pass invocation-local UINT32 to existing six side checks. Complete
validation body and mask constants byte-identical;no access/NONE/SPLIT/raw-domain
check removed. No cached state/owner/lock/allocation/file/ABI/native/capability
change. Candidate static Barrier1164 instructions/64 calls,one domain decode.
CopyBufferRegion172 instructions/13 calls all variants,callee addresses shift.
These are static counts,not executed instructions. Hoisting removes redundant
work;it does not prove the cause of every prior warning,especially ordinary-copy
recording that never invokes this predicate. Layout/Rosetta/cache causes OPEN.

Existing144 transfer assertions twice/full enhanced (prior760/NONE/split/state),
full command-list,complete queue handoff,feature contracts PASS. Adjacent native16
and exact GPU10 PASS,six no-resource readbacks NOT_RUN. Independent no-private
144/full enhanced/native16+GPU10/full command-list/FL11_0+logicOp0 PASS. API/GPU1/1,
staged DLL/native paths checked,no Metal validation errors.36 probe exits0;
source/equivalence/PE/native/no-private-macro/peer lineage verified. Existing tests
unchanged;no new runner. Immutable private peer PE match prior stage,not claimed
identical to current build-directory peer PE. User .gitignore/shared Wine/prefix
preserved;both builds pass with five pre-existing warnings.

Three-way rotating old663 diagnostic/d083 baseline/candidate,same PE/peers,
3 processes/stage,25 rounds/dropfirst,72 retained/stage/scenario,median process
p50/p95. First pressure rotation old/before precedes source edit;candidate follows
build+functional checks. Remaining pressure rotations and entire ordinary matrix
have no intervening builds;all seed/final samples retained. No unchanged retry.
Observer/API/GPU off,Hud1 for timing;validated calibration both sides1000CBV
calls=>ONE native barrier/stages27/27,exact data PASS. d083=>candidate pressure
Recording/call .0678/.0723=>.0607/.0648 us (-10.472/-10.373%);Submit377.05/479.1
=>356.8/480.7 us (-5.371/+.334%);Execute-through-fence2628.3/2948.7=>2618.2/3465.1
us (-.384/+17.513%). Completion includes CPU/Wine/event,not isolated GPU time.
Ordinary50000-copy Recording3614.05/3993.1=>3095.95/3259.6 us (-14.336/-18.369%);
Submit22166.2/22957.2=>22332.6/22900.7 us (+.751/-.246%);Close1.5/2.1=>1.6/2.1.
Recording vs old pressure-.164/+2.532%,ordinary-2.588/-2.123%;completion p95 vs
old+20.643%. Recording recovers in measured fixtures;completion-tail and all
historical/global performance gates remain OPEN. No game/whole-runtime speedup
or regression-free claim. Ordinary path causal attribution remains unresolved.

M1 Max/macOS27.0.1/Xcode27.1/Wine11/clang23.1 rechecked;existing Metal3/Apple7/
MSC4.0.1 identity retained. Requested Xcode26 NOT_RUN. FL/SM/EnhancedFALSE unchanged.
Full Enhanced/other access families/NONE lifetime/pending/alias/discard,RT/video,
Windows/otherGPU/D3D11/T3/T4 NOT_RUN;cross-queue half legality unresolved.
Evidence:`../dxmt-test/n12-sync-access-cost-20261025/N12-sync-access-cost.md`,
manifest/baseline/semantic-equivalence/code-summary/seed-diagnostic JSON,raw asm,
aggregate/ordinary-aggregate/perf-results JSON,build/runtime/verification logs.
Local focused commit only,no push;overall goal active. Next:N12 shared sync/access
compatibility with this hoisted input;tail/layout investigation needs stronger
causal evidence before further optimization.

### N12 shared access/sync/queue admission — 2026-10-25

Base8e408baf. The shared predicate checked sync domains and Copy/Resolve pairs,
but other access/sync and queue/access combinations reached lowering unchecked.
Unknown/mixed NO_ACCESS checks lived separately in lowering. Keep invariant sync
mask calculation and add one invocation-local supported-access mask. One predicate
now owns supported raw domains,unknown access,exclusive NO_ACCESS,NONE implication,
queue compatibility and complete requested-access coverage. Readonly32-entry/128B
sync-to-access table unions component/aggregate coverage;O(popcount(sync)) per side,
normally1 lookup,at most32 iterations,fast ALL/COMMON/NO_ACCESS/deferred SPLIT.
No allocation/lock/scan/cache/new state/owner/file/ABI/native-command/capability
change. Pure access-to-legacy lowering drops duplicate admission and six
unreachable video mappings. Unsupported RT access remains rejected;video is
incompatible with these queue types. Existing split-phase/matching guards retained.

Microsoft primary snapshot Access Bits Barrier Sync Compatibility + Command Queue
Access Compatibility + aggregate hierarchy used;do not copy apparent reverse-list
COMPUTE typo/omissions. NON_PIXEL inherits vertex/compute;DRAW inherits pixel,
including shading-rate access. New existing-runner --sync-access-compatibility:
33 access values x19 scopes x3queues x2sides=3762 global,78 combination and16
buffer/texture side assertions,total3856. Independent inverse access->required-sync
oracle;336 legal controls,unknown/RT/video/mixed-NO_ACCESS/partial-coverage/alias
cases included. Same final PE baseline twice:940 unexpected S_OK (348 sync/access,
584 queue-domain first violations,8 resource edges),336 legal/2580 already-invalid
PASS. Final candidate twice3856 PASS. Full enhanced adds previous144/760/NONE/
split/state/ownership cases;full command-list,complete queue handoff,feature
contracts PASS. Adjacent native16/exact GPU10 PASS,six no-resource readbacks NOT_RUN.
Independent no-private3856/full enhanced/native16+GPU10/full command-list/
FL11_0+logicOp0 PASS. API/GPU1/1,staged DLL/native paths checked,no Metal errors.
30 final probes/exits,source/PE/native/peer/macro/timing lineage verified.

Initial matrix passed but full test rejected an existing invalid positive discard
fixture:RT access paired PIXEL sync,texture lacked ALLOW_RENDER_TARGET. Preserve RT
discard coverage with RENDER_TARGET sync and correct texture flag;do not relax
validation. Wrong RT/pixel pair remains a negative matrix case. Initial source/PE/
logs/results retained in initial-discard-fixture,excluded;final same PE rebuilt and
rerun red/green twice. No performance samples existed before fixture correction.

Costs same PE,AB/BA/AB,3processes/side,25rounds/dropfirst,72retained/scenario/side,
median process p50/p95;observer/API/GPU off,Hud1,no concurrent builds/GPU probes,
no unchanged retry. Calibration both sides1000CBV API calls=>ONE native barrier,
stages27/27,exact pixels/buffer PASS. Recording/call .0598/.0644=>.0605/.0632 us
(+1.171/-1.863%);Submit325.6/438.4=>325.65/439.8 us (+.015/+.319%);
Execute-through-fence2242.5/2423.1=>2259.25/2381.8 us (+.747/-1.704%).
Completion includes CPU/Wine/event,not isolated GPU. Ordinary50000-copy Recording
3109.6/3207.5=>3189.6/3293.6 us (+2.573/+2.684%);Submit21908.75/22377.3=>
22197.25/22628.8 us (+1.317/+1.124%);Close1.4/1.9 unchanged. All costs reported,
no zero-cost/statistical-significance claim;historical/global performance gates OPEN.

M1 Max/Apple7,macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1/clang23.1 unchanged;
requested Xcode26 NOT_RUN. FL11_1/private,FL11_0/no-private,SM6_0,EnhancedFALSE
unchanged. Full resource/layout legality,NONE lifetime,sequential/pending/alias/
discard semantics,actual RT/video,Windows/otherGPU/D3D11/T3/T4 NOT_RUN;cross-queue
half legality unresolved. User .gitignore/shared Wine/prefix preserved. Evidence:
`../dxmt-test/n12-all-sync-access-20261025/N12-all-sync-access.md`,manifest/baseline/
sources/red-summary/red-classification JSON,aggregate/ordinary-aggregate/results,
build/runtime/verification logs and initial-discard-fixture. Local focused commit
only,no push;overall goal active. Next:resource/layout/access admission at this owner.

### 2026-10-25 — N12 resource/access/raw layout admission (G1)

Base9af126e8, one existing Barrier owner. Previously compatible queue/sync scopes
could admit accesses unsupported by a resource's kind/flags/heap; raw direct/
compute/video layout identities disappeared in legacy conversion. Add two pure
local helpers before lowering and split/state recording: one immutable resource
access mask shared by both sides, raw layout/access/queue check per texture side.
RT/DS/UAV/texture-SRV flags, buffer-vs-texture access, upload/readback domains;
Copy COMMON and direct/compute-specific layouts checked without alias erasure.
Buffer DENY_SHADER_RESOURCE remains compatibility no-op, simultaneous COMMON
UAV/RT and combined buffer read/write retained. One existing GetHeapProperties
per buffer barrier; reserved E_INVALIDARG falls back to descriptor domain, CUSTOM
not inferred from CPU visibility. No new src/header/state/cache/owner/ABI/native
command/lock/allocation/capability, no change to creation/converter/legacy owners.

Existing runner --resource-layout-access:60 authored cases x2 sides +31 SDK raw
layouts x3queues x2sides=306. Same final private PE baseline twice190 unexpected
accepts (66 resource/access/layout,124 queue-layout),116 accepted controls.
Candidate twice306 and independent no-private twice306 PASS. Both full enhanced
(includes prior3856/144/760/NONE/split/state),full command-list,features PASS;
adjacent native16/GPU10 PASS,6 no-resource readbacks NOT_RUN;real queue-handoff6
subresource readbacks PASS. API/GPU1/1,no Metal errors,32 final probe exits and
source/DLL/PE/native/peer lineage verified. Independent no-private PE matched to
its own build,not assumed byte-identical to private PE. First invalid deny-SRV
color fixture corrected to legal depth texture;initial/156/pre-format evidence
retained/excluded. Pre-format wrong queue-handoff harness route excluded,final
uses enhanced runner --split-order-probe queue-handoff. Shared split positives
now declare required UAV flags;dedicated no-flag negatives retained.

Matched costs AB/BA/AB,3processes/side,25rounds/dropfirst,72retained/scenario/side,
median process P50/P95. Buffer recording/call .0610/.0703=>.07015/.0790us
(+15.00/+12.38%,P50+9.15ns);Submit370.7/526.1=>372.55/472.6us;
Execute-through-fence2427.95/2707.3=>2442.1/2786.3us(+.58/+2.92%).
Ordinary50000-copy Recording3144.15/3304.0=>3235.05/3438.1us(+2.89/+4.06%),
Submit22330.55/22694.6=>22085.55/22542.1us;Close1.7/2.2=>1.65/2.4us.
Validation/observer off timing,real GPU payload checked;calibration both sides
1000barriers=>1nativepacket,stages27/27,pixels/buffer PASS. No isolated heap-query
cost or ordinary-copy causal attribution;texture-specific timing NOT_RUN.
Performance/history gates OPEN,not hidden by correctness.

M1 Max/Apple7,macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1/clang23.1 confirmed;
requested Xcode26 NOT_RUN. FL11_1/private,FL11_0/no-private,SM6_0,EnhancedFALSE
unchanged. Full operation-specific Resolve/SRS/resource eligibility,both-UNDEFINED
valid-but-unimplemented texture no-layout path,reserved/CUSTOM/placed runtime
matrix,lifetime/discard/alias/pending/cross-queue completeness remain OPEN or
NOT_RUN;Windows/otherGPU/D3D11/T3/T4 NOT_RUN. User.gitignore/shared Wine/prefix
preserved. Evidence:../dxmt-test/n12-resource-layout-20261025/N12-resource-layout.md,
manifest/baseline/sources/red-summary/results/aggregates/verification and build/
runtime logs. Local focused commit only,no push;overall goal active. Next:measure/
reduce admission cost first;then texture no-layout recording/split/submit semantics.

### 2026-10-25 — N12 borrow immutable heap properties for admission (G1)

Base3c2abaa5. Prior resource/layout admission added9.15ns/call measured warning.
Hypotheses:COM property-copy cost;duplicate GetDesc(falsified for buffers by
existing scope fast path);code placement/system movement(unresolved control).
Existing resource base gains internal const GetHeapPropertiesView,Buffer/Texture
borrow owned heap_props_ or nullptr when reserved. Barrier replaces20B COM result
initialization/copy/HRESULT with one scalar pointer-return virtual query. Same
resource mask/flags/upload/readback/CUSTOM/reserved/sync/layout/split/state/native
semantics retained. No copied/cached state,new field,lock,allocation,reference,
lifetime extension or src/header file. Existing property owner remains resource;
view used only in current call. Public COM getter source and buffer getter bytes
unchanged. Query appended after old resource methods:PE vtables prove COM0..14
and resource-base0..33 stable;two implementation destructor slots shift behind
new query and all d3d12 units rebuilt. Public exports/data layout unchanged.
Static Barrier1340=>1336 instructions,59calls both;COM getter15 unchanged,view5;
ordinary CopyBuffer162/13 unchanged(excluding padding;not cycle proof).

Existing enhanced test adds8 reserved-buffer side assertions:source/dest/SRV
accepted,UAV without flag rejected,public heap query E_INVALIDARG required.
Prior306+8=314 PASS baseline/private/no-private. Both full enhanced(prior3856/
144/760/NONE/split/state),full command-list,features PASS. Both adjacent native16/
GPU10 PASS,6 no-resource readbacks NOT_RUN;queue-handoff6 GPU subresources PASS.
Pressure A/B calibration1000calls=>1nativepacket,stages27/27,pixels/buffer PASS.
API/GPU1/1 correctness,no Metal errors;29 final probe exits/hashes/paths verified.
Independent no-private matched its own DLL/native/PE. Changed-line clang-format/
scoped builds PASS;144 warning occurrences/backend,none on changed lines.
Initial internal-slot placement trial retained/excluded;after query append DLLs
rebuilt and all final correctness/timing repeated,not an unchanged retry.

Same-PE AB/BA/AB,3processes/side,25rounds/dropfirst,72retained/scenario/side,
median process P50/P95. Buffer recording/call .07055/.0821=>.06455/.0740us
(-8.50/-9.87%,P50-6ns);Submit375.15/456.7=>362.15/486.9us(-3.47/+6.61%);
Execute-through-fence2460.85/2647.9=>2366.05/2744.1us(-3.85/+3.63%).
Ordinary50000-copy Recording3249.3/3439.1=>3096.8/3254.4us(-4.69/-5.37%);
Close1.8/2.5=>1.6/2.3us;Submit22255.35/23042=>22257.35/23014.2us.
Recording improvement supports avoiding the property copy,not exclusive causal
attribution:ordinary control also moves though it never calls new getter.
Completion includes CPU/Wine/event,not isolated GPU. Tail warnings/historical/
global performance gates OPEN;no-private/texture-specific timing NOT_RUN.

Environment refreshed macOS27.0.1/Xcode27.1/M1 Max Apple7/MSC4.0.1/Wine11/
clang23.1;Xcode26 NOT_RUN. FL11_1/private,FL11_0/no-private,SM6_0,EnhancedFALSE
unchanged. Both-UNDEFINED valid-but-unimplemented path,full resource/Resolve/SRS/
split/lifetime semantics,Windows/otherGPU/D3D11/T3/T4 remain OPEN/NOT_RUN.
User.gitignore/shared Wine/prefix preserved. Evidence:
../dxmt-test/n12-admission-cost-20261025/N12-admission-cost.md,manifest/baseline/
results/aggregates/disassembly/vtable-symbols/warnings/environment/verification/
build/runtime logs and pre-append-trial. Local focused commit,no push;goal active.
Next:first both-UNDEFINED texture no-layout recording/split/submit;tail-cost cause
only with a distinct reproducible measurement slice.

### 2026-10-25 — N12 texture access-only layout preservation (G1)

Base6dbf90e6. Both UNDEFINED texture barriers now use an ordered access-only
resource event. Existing split/range/lifetime checks retained; local validation
and commit preserve layout, including unknown first layout. Shared predicate
used at recording and Execute boundary; memory-only split halves carry no layout
across full flush. That cross-Execute behavior is a specification inference;
Windows native validation NOT_RUN. DISCARD plus both UNDEFINED remains backend
unsupported, not established D3D12 illegality. No new src/header file, owner,
cache, public ABI or capability promotion. Event stride32 unchanged in final PE.

Final26-case CPU red repeated twice on baseline; GPU/split red reproduced.
Both private/independent no-private:26 recording assertions, full enhanced and
command-list/features PASS; new native4/GPU2 PASS (2 no-resource NOT_RUN), existing
adjacent native16/GPU10 PASS (6 NOT_RUN), seven access-only split modes and queue
handoff each6 exact GPU subresources PASS. API/GPU validation1/1;48 expected exits,
staged PE/DLL/source hashes verified. Initial21-case trial retained/excluded.
GPU data does not introspect global logical layout; commit branch also reviewed.

Private same-PE AB/BA/AB,72 retained/side/scenario. Pressure Recording/call
.0724/.0797=>.0666/.0766us;Submit372.9/476.1=>357.0/488.4us;
Execute-through-fence2415.45/2691=>2381.55/2656us (P50/P95).
Ordinary50000-copy Recording3048.3/3165.8=>3105.1/3249.3us;
Close1.45/2=>1.5/2.1us;Submit22284.5/22671.9=>22290.05/22883.5us.
Calibration1000 API=>1nativepacket,stages27/27,pixel/buffer PASS. Buffer/control
costs do not isolate this new texture path or prove causal speedup. Tail/global
performance gates OPEN;no-private/texture CPU timing NOT_RUN.

M1 Max/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1;Xcode26/Windows/otherGPU/
D3D11/T3/T4 NOT_RUN. FL11_1/private,FL11_0/no-private,SM6_0,EnhancedFALSE
unchanged. Full N12 resource/Resolve/SRS/split/lifetime coverage OPEN.
User.gitignore/shared Wine/prefix preserved. Evidence:
../dxmt-test/n12-no-layout-20261025/N12-no-layout.md,manifest/results/sources/
red-summary/aggregates/transition-stride/build/runtime/verification logs.
Local focused commit,no push;goal active. Next:first-use/submit layout
reconciliation;distinct reproducible tail-cost diagnosis.

### 2026-10-25 — N12 explicit enhanced layouts versus legacy promotion (G1)

Basef751d053. Confirmed:known COMMON wrongly admitted explicit enhanced
COPY_SOURCE/COPY_DEST/SHADER_RESOURCE LayoutBefore because shared validation
applied legacy promotion unconditionally. Final36-case same PE baseline twice:
12 incorrect accepts/24 controls each;private/no-private36 PASS. Both ordinary
and split enhanced reject mismatch;legacy promotion controls stay accepted.
New cross-list negative:both Close S_OK,baseline Execute S_OK;final admission
DXGI_ERROR_INVALID_CALL before translation. Initial unknown recording layout is
still deliberately deferred;no guessed global state seeded at Close.

Existing ordered event gains immutable enhanced_barrier provenance in padding;
only legacy events can use COMMON promotion. Same validator at Close/Execute;
no new state owner/cache/lock/scan/native encoder/barrier/public ABI/src file.
Final event stride32 unchanged. Validation apply225=>227 static nonpadding
instructions/18calls;Barrier1355/59 unchanged;split668=>673/46;Close154/13
unchanged. Initial typed-flag candidate added out-of-line append;constant true
for all enhanced split events removes per-event type query and restores prior
inlining. Full prior candidate/tests/costs retained/excluded;final45 probes
repeated after runtime change. Initial24-case red fixture retained separately.

Both backends:three new legal COMMON-copy/split modes each6 exact GPU
subresources PASS (same-list,batch,reversed CPU recording);four adjacent split/
queue modes each6 PASS;full enhanced/features PASS. Adjacent native16/GPU10
PASS,6 no-resource readbacks NOT_RUN;access-only native4/GPU2 PASS,2 NOT_RUN.
Correctness API/GPU1/1,no Metal errors;staged/build PE/DLL hashes verified.
Pressure calibration1000API=>1nativepacket,stages27/27,pixel/buffer PASS.

Private same-PE AB/BA/AB,72 retained/side/scenario,P50/P95 us:
Pressure Recording/call .06745/.0785=>.07085/.0781;
Submit388/532.3=>353.4/469.5;Execute-fence2443.8/2700.3=>2375.35/2820.6.
Ordinary50000-copy Recording3143.55/3260.4=>3327.55/3508.7;
Close1.7/3.2=>1.95/2.4;Submit22365.15/22870.5=>22371.95/22724.6.
Recording warnings +5.04% pressure P50 and +5.85/+7.62% ordinary P50/P95
remain unresolved;completion P95+4.46%. INCONCLUSIVE,not a speedup claim.
Static CopyBufferRegion162/13/entry address unchanged but call targets move;
counts do not prove harmless noise or causality. Tail/global performance gates
OPEN;no-private/new texture CPU timing NOT_RUN. Read-only inspection occurred
during timing;no concurrent builds/correctness GPU probes.

Environment refreshed:M1 Max Apple7/1007,macOS27.0.1,Xcode27.1,MSC4.0.1,
Wine11,clang23.1.1. Xcode26/Windows/otherGPU/D3D11/T3/T4 NOT_RUN.
FL11_1/private,FL11_0/no-private,SM6_0,EnhancedFALSE unchanged. Device QI
unchanged. Full mixed-domain/global decay/queue-specific layout/Resolve/SRS/
N12 coverage OPEN. User.gitignore/shared Wine/prefix preserved. Evidence:
../dxmt-test/n12-layout-promotion-20261025/N12-layout-promotion.md,manifest/
results/red-summary/sources/static-cost/aggregates/environment/verification/
build/runtime logs,initial-24,typed-flag-trial. Focused local commit,no push;
goal active. Next:isolate shared recording cost;global first-use/legacy decay
reconciliation only with a complete execution-ordered oracle.

### N12 recording-cost isolation — 2026-10-25

Documentation-only follow-up to `0813773e`; no runtime, test-runner, ABI,
owner, cache or capability change. External record:
`../dxmt-test/n12-recording-isolation-20261025/N12-recording-isolation.md`.
Four validated calibration processes and 64 timed processes cross frozen
`f751d053`/`0813773e` DLLs between two directories, including same-DLL controls.
All 68 processes pass their real readback checks. Each scenario/variant has
16 processes, 384 retained rounds. Process/block-level log-ratio adjustment
for directory and order estimates pressure recording P50 +5.98% (exploratory
95% interval +4.61% to +7.28%) and ordinary P50 +6.54% (+5.43% to +7.68%).
Both have eight of eight mixed-DLL pairs slower with B. Pressure also has a
-3.52% directory effect; this is not solely a directory or noise explanation.
Ordinary P95 +0.20% (-5.87% to +7.23%) does not establish the previous +7.62%
tail warning as a stable DLL effect; that historical warning remains recorded.

Pressure timing removes the native observer; ordinary timing retains its
newBuffer allocation counter. Ordinary `native_buffers=0` denotes zero new
buffers, not zero copies, and its uninstalled encoder observer is NOT_RUN.
API/GPU validation is enabled in calibration and disabled for timed processes.
The initial incorrect harness counter expectation is retained and excluded.
Standalone actual IsSameDevice P50 changes only +0.49%; this does not explain
the full recording regression. Native profiling yields opaque Rosetta JIT PCs.
Guest RIP sampling maps leaf PCs to DLL symbols but A has one sampling error
(exit 1), while B exits 0; it is diagnostic evidence, not a passed paired gate
or causal attribution. No speculative identity/cache/alignment patch is made.
Performance acceptance remains OPEN. Next: isolate full recording components
and classify/repeat guest sampling errors. Exact commands, hashes, calibration,
raw timing, null controls, rejected trials and profiling limits are external.

### N12 shared ownership-query cost — 2026-10-25

On `fda27428`, the shared `IsSameDevice` helper queried both the external
child's canonical owner and the already-live internal Device's own IUnknown.
`MTLD3D12DeviceImpl::QueryInterface` returns its primary Device for IUnknown;
all helper callers supply the internal Device type. Borrow that controlling
pointer, retaining the child's canonical query, failure handling and owned
reference. This removes one QI/AddRef/Release pair per check, with no cache,
new owner, ABI change or bypass of null/foreign-device validation. Existing
feature tests now assert the DXMT primary/controlling-IUnknown invariant;
the assertion passes on baseline and both candidate build variants. Device5–10
remain grouped and legacy interfaces remain rejected. New src/d3d12 files: 0.

External evidence: `../dxmt-test/n12-owner-borrow-20261025/N12-owner-borrow.md`.
Both private/no-private builds and all 42 final processes pass: 18 validated
correctness/calibration processes, 16 GPU timing processes and eight CPU-only
helper/component probes. Full command-list tests retain foreign allocator
rejection. Enhanced admission covers 36 cases per tested build; cross-Execute,
legal COMMON batch, queue handoff and adjacent/access-only GPU readbacks pass.
The real-helper CPU probes additionally cover null, own/foreign resources,
canonical child query, GetDevice failure and balanced reference counts.
These CPU/feature checks are not counted as GPU oracles. All 500 rounds in
the 20 GPU timing/calibration processes validate their changing data output.

Fixed-stage ABBAABBA, four processes per DLL/scenario, 25 rounds/drop first:
ordinary recording median process P50/P95 is 3300.825/3420.9 -> 1879.5/2229.8
us per 50,000 copies (-43.06%/-34.82%); pressure is .0747/.0847 ->
.061875/.07185 us per barrier (-17.17%/-15.17%). All four adjacent paired P50
comparisons improve in both scenarios. Actual helper P50 is 27.9044 -> 13.7695
ns (-50.65%); separate QI/GetDevice/two-query controls remain within 0.67%.
Two helper checks predict 28.27 ns saved/copy; the full path saves 28.43 ns.
Pressure timing removes its observer; ordinary retains the native newBuffer
allocation counter (zero allocations, encoder observer NOT_RUN).

This demonstrates removal of redundant hot-path work; it does not establish
the cause of the earlier ~6% binary-linked regression. Preserve that unresolved
attribution and historical tail warnings. Pressure/ordinary Submit P95 changes
+5.37%/+3.48%, with mixed paired signs/outliers: Submit/completion performance
acceptance remains INCONCLUSIVE and the broader G1 gate remains OPEN.
FL/SM/Options are unchanged. Next: N13 Submit tail/use-residency attribution.
Revert this focused change if the internal controlling identity contract changes.

### N13 ordinary-copy submission phase attribution — 2026-10-25

Evidence-only slice on `4288a30b`; no production/test source, ABI, owner,
state, synchronization, capability or scheduling change. Frozen A=`0813773e`
and B=the DLL tested/committed as `4288a30b` use identical native peers and
one staged path. The external current-runner derivative reuses the scoped
validation/collector/native observer and forwards all original calls. This
measures ordinary 1/1000/50000-copy scaling, not the earlier split/SM5 scenes.
Record: `../dxmt-test/n13-ordinary-phases-20261025/N13-ordinary-phases.md`.

36 successful processes each validate 25 changing GPU payloads (900 rounds):
12 API/GPU-validation calibrations and 24 timed processes. Correctness packet
walks confirm exactly 1/1000/50000 copies; timed modes never walk packets or
clock individual copies. Profiled Execute has one validation, collector,
factory, encoder and commit, three bridge encode calls (wait/stream/update),
and one end. Four injected boundary-delay calibrations pass. Four additional
omitted-submit oracle controls exit1 on stale readback as expected on both
DLLs and both original/external runners; these are EXPECTED_FAIL, not timing
passes. Two failed external source-generation trials are retained/excluded.

ABBA, alternating counts/plain-profile order; each DLL/count/mode retains
48 rounds in two processes. Median process profiled Submit/stream P50 at
50000 copies is A=23354.05/23211.175 us, B=23312.15/23140 us. Per-row stream
share medians are 99.31%/99.32%; eight slowest-row examples retain 98.96–99.22%.
Validation/collection remain about 1–3 us. Stream slope from 1000 to50000 is
.4645/.4627 us/copy. The bridge already sends a complete stream in one Unix
call; this is not 50000 PE/native crossings. The measured interval includes
transition/decoder/Metal API/scheduling, not GPU time or CPU self-time.

Plain 50000-copy Submit P50/P95 A->B=22611.375/23284.1 ->23437.675/24762.9 us
(+3.65%/+6.35%); one-copy=34.725/70.9 ->55.025/126.95 us (+58.46%/+79.06%).
These observations and historical warnings remain OPEN, not dismissed as noise.
Profiling perturbs calls/pacing; two processes per cell do not establish a
stable DLL effect or replace acceptance timings. Factory/encoder fixed costs
matter more at one copy. No descriptor/validation optimization, copy deletion,
fence batching or backend migration is justified by this bulk result.
No-private runtime/production build/native Windows/D3D11/T3/T4 NOT_RUN here.

Stop unchanged Submit matrices. Native intervention would need thread CPU and
finer decoder/API attribution. Next architectural work: select an unclosed N06
binding ABI invariant after checking existing SM5/SM6 evidence, without redoing
completed N09 mutation/replay/index slices. Broader G1/performance gates remain
incomplete; all feature claims and the user's staged .gitignore stay unchanged.

### N06 global root-signature DWORD admission — 2026-10-25

G1 slice on `dfa5828c`: `MTLD3D12RootSignatureImpl::Initialize` checked only
parameter count, accepting over-budget global signatures. Two baseline runs
each wrongly accepted 14 of 42 RS1.0/1.1 creation cases; 28 controls passed.
No invalid signature was bound or submitted. The existing validation loop now
checks constants=N DWORDs, table=1, root CBV/SRV/UAV=2 with subtraction before
addition, before native sampler allocation/layout publication. Local roots
remain exempt from the global64 budget; their separate record-size admission
and existing parameter-count restriction remain outside this slice.

Do not equate DWORD cost with padded qwords: 64 tables/one-DWORD parameters
legally occupy64 qwords; mixed61 constants+CBV+table costs64 and occupies33.
No new state/owner/cache, stage-specific validator, source file, COM/WineMetal
ABI, resource layout, command scheduling or capability change is introduced.
Record and primary-source hashes:
`../dxmt-test/n06-root-budget-20261025/N06-root-budget.md`.

Both private/no-private builds pass. Existing descriptor_ownership runner's
42 creation cases and full ownership regression pass in both configurations.
16 original compute-runner GPU readbacks (two builds, SM5/Airconv and SM6/MSC,
root constants/CBV/SRV/UAV) pass. Eight additional external existing-runner
derivative processes validate exact64 cost (62 constants+root UAV), two input
values777/999, both DLLs and backends, with actual GPU readback. These values
change across processes, not within one recording. All correctness processes
enable Metal API/GPU validation; no validation errors/command aborts found.
34 total processes:32 successful, two expected baseline failures, including
four creation timings. One unused-executable staging failure is retained and
excluded. Exact commands, loaded paths and hashes are in results.json.

Creation timing ABBA uses identical PE/native peers, two processes/DLL/cell,
25 retained 100-call batch means/process, ten legal case/version cells.
1000 retained means cover100000 timed Create+Release calls. Largest observed
increase: RS1.0 padded64 P50/P95 1259/1313 ->1355.5/1435 ns (+7.66/+9.29%).
This is added checked arithmetic in existing O(parameters) creation validation;
no draw/dispatch/submit work is added. No speedup or universal tail claim.
Recording/Submit performance is NOT_RUN; historical N12/N13 warnings stay OPEN.
Actual Xcode27.1, requested26 NOT_RUN. Full N06 graphics/mesh/RT ABI, local-size
validation, native Windows and T3/T4 remain OPEN/NOT_RUN. FL/SM/Options and the
user's staged .gitignore remain unchanged. Next: parameter-specific root
argument bounds/reconciliation with existing GPU fixtures. Revert this focused
commit if legal global admission regresses; broader G1 remains incomplete.

### N06 parameter-specific root argument admission — 2026-10-25

G1 slice on `c63db037`: the old direct-write guard bounded only the whole
staging/payload, permitting odd-constant padding, adjacent parameters and
wrong parameter types. Missing signatures/indices/null data were silently
ignored. Two calibrated baseline runs each wrongly accepted52 of96 direct
cases. The indirect factory separately accepted18 of32 wrong type/range
cases. Invalid recordings/signatures were never executed on the GPU.

The root owner's offset vector is replaced by one immutable parameter layout
(source qword, type, exact writable DWORD count). No second parameter array
or per-command parser/cache is added. One checked-subtraction predicate now
serves all12 direct setters and four indirect root-update argument kinds.
Indirect validation precedes MSL source generation; duplicated later index
checks are removed. Direct errors use existing FailRecording/Close E_FAIL,
without device removal; indirect errors return E_INVALIDARG. These are DXMT
diagnostic policies for invalid API use, not a universal Windows HRESULT claim.
Failed Close continues to prohibit Reset with the same error. Two initial
test-calibration runs had an incorrect recovery expectation and are excluded.

Both private/no-private builds pass. Existing descriptor_ownership runner
passes96 direct and32 indirect cases, full ownership/metadata tests and the
prior42-case root-budget gate in both configurations. Final ownership PEs
were rebuilt/retested after removing unrelated formatting.28 successful GPU
processes cover SM5/SM6 partial constants, root CBV/SRV/UAV, graphics address
mutation, draw ExecuteIndirect and MSC RS1.1 descriptor tables. The external
current-compute-runner derivative records two dispatches in one list:
initial{100,200,300}, single middle update777 ->4531; partial{9,11} at offset1
preserves first100 ->204. Both backends/baseline and both candidate builds
pass; two omitted-update/dispatch controls fail the readback oracle as expected.
45 runtime processes:40 successful and five expected failures;41 enable Metal
API/GPU validation and four setter timings disable it. No Metal validation
errors/command-buffer aborts. Exact loaded paths/hashes, commands and primary
Microsoft sources: `../dxmt-test/n06-root-argument-ranges-20261025/N06-root-argument-ranges.md`.

ABBA setter timing: same PE/native peers, RS1.1, six operations x graphics/
compute, two processes/DLL/cell;25 retained1000-call batch means/process,
1200 means/1.2M timed calls. Most median increases are1–3 ns; largest is
graphics UAV7.90->13.25 ns (+67.72%), P958.5->13.8 ns (+62.35%). Retain this
observed cost of O(1) type/range checks; no speedup/full-performance PASS claim.
Metadata records grow4->12 bytes, same vector; no setter lock/allocation or
new native call. Recording/Submit and historical N12/N13 performance acceptance
remain OPEN/NOT_RUN here. The private CPU record format changes and consumers
are rebuilt; public COM/WMT/MSC binding ABI, serialization and capabilities
remain unchanged. Native Windows, requested Xcode26 (actual27.1), D3D11/T3/T4,
indirect root-update GPU execution and complete N06/DXR/local admission remain
NOT_RUN/OPEN. Next: global/local root and PSO/root-binding reconciliation.
Revert this focused commit on valid binding regressions; G1 is incomplete.

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

### N06 global/local root-signature admission — 2026-10-25

G1 slice on `efc80ba4`: same-device ownership did not establish root kind.
Two baseline runs each wrongly accepted28 of50 RS1.0/1.1 cases; adding genuine
D3DSetBlobPart embedded roots produced32 bad accepts of58 in two more runs.
Both empty and nonempty local roots entered compute/graphics bindings, explicit
compute/graphics PSOs, compute streams and root-updating indirect signatures;
local flags combined with graphics flags were accepted too. Invalid objects
were created/closed for diagnosis, never dispatched or submitted.

The existing root owner now retains canonical flags instead of a derived
resource-direct-indexing bool. Inline kind/direct-index queries serve existing
consumers. Binding rejects local roots before publishing them to64-qword global
staging; indirect admission rejects before MSL generation. Shared pipeline-cache
descriptor admission covers explicit compute/graphics/stream/library roots;
mesh and RT global-root consumers also check kind. The shared embedded-global
helper checks the existing raw header's flags before publishing its pointer.
No second parser/state/cache, new source module, COM/WMT/MSC interface,
capability, resource layout or GPU scheduling change. Internal CPU flags field
changes from bool to enum; affected consumers were rebuilt.

Private and no-private builds pass. Existing descriptor_ownership runner's58
kind cases and full ownership/metadata/budget/range regression pass in both
configurations; final PEs are rebuilt/retested after removing unrelated format
changes. Eight GPU processes pass: SM5/Airconv and SM6/MSC partial constants
(4531 then204 in one recording), graphics draw ExecuteIndirect, MSC RS1.1
resource tables(1554), each configuration.22 runtime processes:18 success and
four expected baseline failures;18 enable Metal API/GPU validation and four
cost processes disable it. No Metal validation error/command-buffer abort.
Initial harness unloaded the compiler while blobs remained alive; its crash
is retained/excluded, fixed in the fixture, and is not DXMT failure evidence.

ABBA uses identical PE/native peers, two processes/DLL,50 retained1000-call
batch means/domain/process,400 means/400000 alternating-root binds. Compute
P50/P95(ns)18.7/19.9->19.5/20.4(+4.28/+2.51%); graphics18.5/19.0->18.9/20.1
(+2.16/+5.79%). One O(1) bit test per changed root bind, no new hot-path lock,
allocation, scan or native call. Recording/Submit NOT_RUN; historical N12/N13
warnings remain OPEN. Evidence, primary-source hashes and exact commands:
`../dxmt-test/n06-local-root-capacity-20261025/N06-root-kind.md`.

This closes global/local misuse, not local capacity: existing64-parameter cap
and missing4064-byte local-record admission remain OPEN, with no capacity lift
before this boundary. Mesh/RT runtime, native Windows and T3/T4 NOT_RUN;
requested Xcode26 NOT_RUN(actual27.1). Broader G1/N06 remains incomplete.
User staged .gitignore unchanged; local commit only, no push.

### N06 local-root record capacity — 2026-10-25

G1 slice on `5f6f4ede`, following global/local admission protection. The old
64-parameter guard rejected legal local layouts while missing the separate
local byte budget. Two baseline104-case runs each fail32 cases:22 false
rejections and10 oversized accepts, covering RS1.0/1.1. A final identical-PE
baseline run repeats32 failures after formatting. No invalid root is submitted.

The existing creation-validation loop now applies parameter-count/global DWORD
limits only to global roots. Local roots accumulate ordered record bytes:
constants4-byte aligned, descriptors/table handles8-byte aligned, bounded by
MAX_SHADER_RECORD_STRIDE(4096)-SHADER_IDENTIFIER_SIZE(32). Capacity division
precedes DWORD multiplication; UINT_MAX/1<<30 constants are rejected without
wraparound. No persistent footprint field, parser, extra scan or module.
Global64-DWORD cost and existing CPU padded-qword layout remain distinct.
4064 record bytes can legally occupy509 or1016 internal qwords; validation must
not confuse these layouts. Static samplers cost no local argument bytes;
unbounded table ranges still contribute one8-byte table handle.

Both private/no-private builds pass. The existing root-budget gate grows42->104
cases, adding local exact/over/overflow,65/508/1016 parameter and order/padding
cases. Both configurations pass104 budget cases,58 root-kind cases and full
ownership/range/metadata regression. Large legal local objects are also
rejected at compute/graphics global binding before staging (Close E_FAIL,
device remains S_OK). Eight adjacent GPU processes pass SM5/SM6 partial constant
updates(4531 then204 in one list), graphics ExecuteIndirect and MSC RS1.1 table
readback1554 across both configurations.23 runtime processes:20 successful,
three expected baseline failures;17 API/GPU-validation-on and six timing-off.
No Metal validation error/command-buffer abort. Exact commands and hashes:
`../dxmt-test/n06-local-record-20261025/N06-local-record.md`.

Creation-only ABBA: identical PE/native peers, two processes/DLL, four common
legal RS1.1 cells,25 retained100-create/release batch means/process/cell;
400 retained means/40000 calls. Global constants64 P50/P95(ns)473/505->481/567
(+1.69/+12.28%); retain tail warning, attribution INCONCLUSIVE. Other cells'
P95:local33CBV1685->1751(+3.92%), local1016constants423->421(-0.47%),
global64padded1656->1680(+1.45%). Two candidate-only processes add200 means/
20000 calls for newly admitted layouts; P95(ns)1016 constant parameters27112,
508CBVs11321,508tables33537, mixed4064-byte record576. Old rejection cost is
not a legitimate speedup baseline. New arithmetic is O(parameters) creation
work; no bind/draw/submit change. Recording/Submit NOT_RUN, historical warnings
remain OPEN; no overall performance acceptance claim.

This closes local creation capacity admission, not DXR execution or a native
shader-record ABI proof. Local MSC layout/RT dispatch, native Windows, other
GPUs and T3/T4 NOT_RUN; broader N06/G1 remains incomplete. No COM/WMT/MSC ABI,
resource layout, schedule or public capability change. Requested Xcode26
NOT_RUN(actual27.1). User staged .gitignore/shared Wine preserved; focused local
commit only, no push. Revert this commit if legal global admission regresses.

### N06 implicit PSO root admission — 2026-10-25

One creation-admission slice on07e6279c. Microsoft root-signature contract:
explicit PSO root overrides embedded roots; without one every present shader
must embed a matching root. MSC compute/graphics/mesh incorrectly tolerated
missing-root E_FAIL. Existing extraction helper now returns E_INVALIDARG for
absence; callers propagate all failures. Graphics uses its selected root
fields instead of duplicate nullable reference state. Existing matching and
local/global admission remain; no new parser/cache/state/module/ABI.

Identical final external matrix PE on baseline reproduces6 bad SM6 accepts/42;
formal container runner reproduces exactly2 bad accepts. Private/no-private
builds pass, each passing42 SM5+42 SM6 RS1.0/1.1 matrix cases and full container/
ownership regression (104 capacity,58 kind cases). Formal cases stay in existing
container runner; ownership source unchanged. Default compute now creates/binds
an explicit empty root. Persistence creates/passes/binds roots for graphics
PSO/stream and creates a separate root on its second device; graphics fixtures
opt into input assembly. Two initial persistence fixture failures remain logged
and excluded, not claimed as passing tests.

Eight implicit-root GPU processes cover compute777 and graphics0xff00ff00,
both backends/configurations. Zero-constant oracle exits1 as expected. Four
no-resource compute runs are SMOKE_ONLY; two persistence runs pass live and
rehydrated compute/graphics execution.38 recorded processes:36 accepted outcomes
(31 success, five expected failures), two excluded fixture failures.28 accepted
API/GPU-validation-on runs plus eight timing-off; no accepted Metal validation
error/command-buffer abort. Actual commands/hashes/logs and source snapshot:
`../dxmt-test/n06-implicit-root-20261025/N06-implicit-root.md`.

Creation-only ABBA per backend, four legal RS1.1 compute/graphics embedded/
explicit cells,320 retained10-call batch means/3200 calls. Embedded P95(us):
SM5 compute284.20->295.41(+3.94%), graphics710.11->748.20(+5.36%);
SM6 compute401.24->412.15(+2.72%), graphics986.20->1015.67(+2.99%).
Explicit P95 delta:SM5 compute-6.06%, graphics-4.14%; SM6 compute+0.31%,
graphics+2.87%. Retain SM5 embedded-graphics tail warning OPEN, attribution
INCONCLUSIVE. Existing cache reused; first-per-case timings are not cold-driver
latency. No new per-bind/draw/submit scan/allocation/lock; Recording/Submit
NOT_RUN and historical warnings remain OPEN. No overall performance claim.

This closes missing-root admission, not all root/resource compatibility or
mesh/RT execution. Mesh compiled only; GS runtime/native Windows/other GPUs/
T3/T4 and requested Xcode26 NOT_RUN(actual27.1). Public capabilities unchanged;
broader N06/G1/FL12_2 incomplete. User staged .gitignore/shared Wine preserved;
focused local commit only, no push.

### N06 root-container caller extent — 2026-10-25

On55c0b771, both public root deserializers discarded DataSize and trusted the
DXBC header size. Identical final ownership PE on baseline reproduces20 bad
accepts/24 advertised-length cases: RS1.0/1.1 x legacy/versioned APIs accept
zero,1,31,one-byte-short and >UINT_MAX lengths for a fully backed valid blob.
The backing bytes remain intact, so the baseline is a deterministic API failure
without depending on a process crash. Exact-size controls remain valid.

Existing root-signature module now shares GetRootSignatureData using the
existing CDXBCParser::ReadDXBC with the actual caller extent. Null/over32-bit,
malformed outer framing, absent RTS0 and undersized raw headers fail before
raw deserialization. Device creation validates before its owned blob copy,
then passes the extracted raw slice for immediate owned deserialization; no
borrowed pointer persists, second container scan or new object owner is added.
Both exported APIs use the same helper. No vendored parser or shared Airconv
code is modified. This is caller-extent admission, not complete hardening:
nested raw payload offsets/counts and the vendored framing parser's own
malformed-index arithmetic remain OPEN for the next bounded-decoder slice.

Private/no-private builds and full ownership/container regression pass; the
existing ownership runner adds24 extent cases and input-mutation/owned-descriptor
checks, plus exact device creation for both versions. External derivative adds
42 guard-page/null/over32-bit checks per configuration across all three entry
points, with no guard-page fault. Eight adjacent SM5/SM6 implicit-root compute/
graphics GPU processes return777/0xff00ff00; two persistence runs pass live and
rehydrated execution.21 processes:20 success, one expected baseline failure;
17 API/GPU-validation-on, four timing-off. No unexpected runtime exit, Metal
validation error or command-buffer abort. Initial macro-qualification build
error is preserved and corrected; final builds pass. Commands/source/PE/DLL
hashes and Microsoft primary-source snapshots:
`../dxmt-test/n06-root-container-20261025/N06-root-container.md`.

Same-PE creation/deserialization ABBA: RS1.0/1.1 x legacy/versioned/device,
600 retained100-call batch means/60000 create-release calls. P95(ns), in that
API order: RS1.0 358.55->362.75(+1.17%),318.10->325.10(+2.20%),
479.10->494.65(+3.25%); RS1.1 309.00->312.95(+1.28%),
318.55->351.50(+10.34%),485.65->488.00(+0.48%). Retain RS1.1 versioned
P95 warning OPEN, attribution INCONCLUSIVE; its P50 decreases305->299ns.
One existing O(container chunks) framing pass remains; extra constant-time
extent/minimum checks, no bind/draw/submit scan/lock/allocation. Recording/
Submit NOT_RUN, historical warnings OPEN; no overall performance acceptance.

M1 Max/Apple7, macOS27.0.1, MSC4.0.1/Wine11, actual Xcode27.1; requested26,
native Windows/other GPUs/mesh/RT/T3/T4 NOT_RUN. Public capabilities/ABI/
root argument layout unchanged. Broader N06/G1/FL12_2 incomplete. User staged
.gitignore/shared Wine preserved; local commit only, no push.


### N06 raw root payload bounds — 2026-10-26

On dbce115d, the shared RootSignatureDeserializer accepted unchecked nested
payload spans. Final same-PE baseline safe subset: 28 failures/40 checks;
two minimal static-sampler probes also reproduce invalid S_OK. Existing decoder
now bounds offsets/counts/alignment before pointer formation, checks allocation
multiplication and discriminant/flag wire values, preserves modern SDK bits,
and accepts empty ranges without false OOM. Empty-array offsets remain bounded.
Microsoft DXC pinned serializer/validator support the latter contract; no new
parser, owner, module, public ABI or binding layout. INV-06/12/15.

Private/no-private builds pass. Each configuration passes168 raw-root cases
and existing container/ownership regression.19 final candidate processes pass,
16 API/GPU-validation-on: eight SM5/SM6 compute/graphics GPU runs read back
777/0xff00ff00; two persistence runs pass live/rehydrated execution; D3D11
boundary checks are CPU creation/rejection only. Native ASan/UBSan passes20149
checks, including10000 deterministic mutations per version. Initial build and
misconfigured harness attempts remain excluded; commands/hashes/source snapshots:
`../dxmt-test/n06-raw-root-20261025/N06-raw-root.md` and final-manifest.json.

Final-source creation ABBA covers18 cells,1800 retained100-call batch means,
180000 timed calls. Largest P95 delta is +7.21%
(174.00->186.55ns;
version=1,kind=2,api=1; mapping/report above).
Retain tail warnings OPEN, attribution INCONCLUSIVE, no overall perf acceptance.
Checks add O(parameters+ranges) work inside existing decode loops; no new
bind/draw/submit scan/lock/allocation. Recording/Submit NOT_RUN; prior warnings OPEN.

M1 Max/Apple7/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1. Requested Xcode26,
native Windows/other GPUs/mesh/RT/T3/T4 NOT_RUN. Scalar semantic completeness
and outer vendored DXBC index arithmetic remain OPEN. Capabilities unchanged;
N06/G1/FL12_2 incomplete. User .gitignore is currently unstaged and preserved;
focused local commit only, no push.


### N14 concurrent multiple-fence notification — 2026-10-26

On8d8c6f33, Device attachment B concurrency coverage gap confirmed: existing
multi-wait test signals sources serially. Runtime defect Not Reproduced.
Existing command-list runner adds opt-in --multiple-fences-concurrent;
ALL/ANY x pre-registered/racing-registration x16 rounds x8 groups =512 groups
per configuration. Four CPU Signal workers release together. ANY includes a
fifth unsatisfied fence; subsequent rewind/signal must not re-notify. Existing
scoped IAT observer counts forwarded SetEvent calls and owned event handles.
Exactly512 notifications/duplicates/closes observed in each configuration.

Private/no-private builds and four validation-on CPU processes pass: new
concurrency checks plus existing null/duplicate/wide/six-OOM/96-abandonment
cases. No production change, new runner framework or public capability/ABI.
This is bounded opt-in concurrency evidence, not exhaustive interleaving/TSan
or fullT4 proof. GPU readback/performance NOT_RUN for this test-only slice;
historical warnings/Device/G1/FL12_2 remain OPEN. Actual Xcode27.1; requested26
and native Windows NOT_RUN. User .gitignore preserved; local commit/no push.
Commands/hashes/timing ownership table and source reference:
`../dxmt-test/n14-concurrent-groups-20261026/N14-concurrent-groups.md`.


### N02 InfoQueue FIFO retention cost — 2026-10-27

On47d89d73, message semantics pass but vector front erasure moves all retained
messages per full-queue append and repeatedly during shrink (quadratic).
Existing InfoQueue owner now uses deque/pop_front; insertion still precedes
counter update/eviction, with the same mutex/COM identity/filters. No extra
state/index/cache/owner/new source file. Append/eviction amortized O(1), shrink
O(n); retrieval scans remain. Microsoft SetMessageCountLimit FIFO contract kept.

Private/no-private builds pass.12 final validation-on processes cover full
feature/InfoQueue, retention order/counters/zero/unlimited/reuse, debugger output,
OOM and four SM5/SM6 GPU readbacks777. Existing11 allocation-failure sites plus
five new container-growth sites pass per config: nonempty counts101/203/305,
rollback2/2/1, original counters and first/last messages survive then recover.

Same-PE ABBA retention16 samples/cell/variant,1024 append calls/sample;
16384-capacity append P50/P95(ns)19092.25/19396->62.60/67.38;
shrink-to16 P50/P95(ns)148145500/151071000->35700/49200.
Copy Recording48 samples/variant initially+7.58%/+13.55% P50/P95;
ordinary copy Submit+0.65%/+1.01%. One bounded B-A-A-B copy-only control
removes retention workload from sequence: Recording+3.25%/-3.08%,
Submit+2.53%/+1.34%. Keep both observations; Recording warning OPEN,
attribution INCONCLUSIVE, no overall performance acceptance. Eight accepted
copy processes validate200 changing payloads; no speculative runtime workaround.
Old full-submit fixture exits1 on baseline before N13_FULL; excluded, cause
not established. Three successful pilots outside final comparison retained.

Actual M1Max/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1. Requested26/native
Windows/other GPUs/D3D11 rendering/fullT3/T4 NOT_RUN. Public ABI/capabilities/
resource layout/scheduling unchanged; complete debug layer/Device/G1/FL12_2
not claimed. User .gitignore preserved; local commit/no push. Commands/hashes,
primary source, all28 process records and performance scope:
`../dxmt-test/n02-infoqueue-retention-20261026/N02-infoqueue-retention.md`.


### N03 shared DXBC container bounds — 2026-10-27

On342ecf6e, static inspection found inclusive-end extent comparisons and
index-size narrowing in ReadDXBC. The existing shared parser now bounds the
index count by remaining bytes, checks contiguous offsets before pointer
construction, and advances exclusive ends only after subtraction-based checks.
NextOffset starts within ContainerSizeInBytes and remains within it after each
header/payload advance. Minimal correction in the existing Microsoft-derived
owner avoids a second D3D12 parser; no allocation, lock, state or ABI added.
Zero-size chunks, trailing bytes and failure-state semantics are preserved.

Private/no-private PE/native builds and targeted validation-on execution pass:
existing shader-container negatives and168 raw-root cases per configuration;
six additional legal metadata-boundary cases per configuration; SM5 andSM6
compute readbacks777 in both; D3D11 SHDR/SHEX acceptance and DXIL/hybrid/no-code
rejection. Final container binaries rerun after guarding empty test payloads.
These are bounded compatibility/regression checks, not exploit reproduction,
full parser fuzzing, alignment-contract verification or exhaustive N03 proof.

Cost remains O(blob count), no per-draw/submit path change. Performance not
remeasured; existing performance warnings remain OPEN. M1Max/macOS27.0.1,
Xcode27.1/Wine11/MSC4.0.1; requested Xcode26/native Windows/other GPUs/fullT3/T4
NOT_RUN. No capability promotion; Device/G1/FL12_2 remain incomplete.
Report, commands and artifact hashes:
`../dxmt-test/n03-container-bounds-20261027/N03-container-bounds.md`.


### N12 parallel texture-range split recording — 2026-10-27

On1b4d7513, Device attachment D coverage gap confirmed: prior concurrent mode
records one subresource, while full-range/access-only/COMMON-layout modes use
serial or reversed recording. Runtime defect Not Reproduced. Extend existing
TestSplitExecutionOrder mode predicates with range-concurrent,
access-only-concurrent and common-layout-concurrent. Each records three mips
and two array slices. Both recording threads now Close their own lists before
joining; HRESULT storage is separate and read only after join. Existing local
copy-location ownership and ordered BEGIN->END submission remain unchanged.

Private/no-private scoped builds and final ten validation-on processes PASS:
three new modes, existing single-subresource concurrent and queue-handoff in
each configuration. Fifty exact GPU subresource readbacks, distinct per-index
byte values; capability probe stays FALSE. Ten pre-format runs also pass and
are kept separately. Final source rebuilt and final runner hashes verified.
This is bounded scheduled concurrency coverage, not guaranteed overlap of every
operation, exhaustive interleaving/TSan or complete Enhanced Barrier proof.

No production source, public ABI, owner, capability, layout or scheduling
change. Performance NOT_RUN for test-only work; prior warnings remain OPEN.
M1Max/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1; requested26/native Windows/other
GPUs/fullT3/T4 NOT_RUN. Device/G1/FL12_2 remain incomplete. User .gitignore
preserved, local commit/no push. Exact commands/results/hashes/report:
`../dxmt-test/n12-parallel-ranges-20261027/N12-parallel-ranges.md`.


### N06 root-deserializer COM identity — 2026-10-27

Onbf5be300, both root-deserializer types rejected IUnknown and left unsupported
QI output unchanged. Existing templated QueryInterface now clears output and
accepts IUnknown alongside Base through the same ref_and_cast/reference owner.
No object layout, public COM/WMT/MSC ABI, cache, binding or capability change.
The 4064-byte local-record admission was already fixed; not repeated here.

Existing shader-container runner adds shared COM checks for RS1.0/1.1 and both
factories: repeated identity, typed roundtrip, unsupported/null output, factory
IUnknown and queried-interface survival after original release. Same final PE
baseline exits1 with all four groups FAIL. Both candidate configurations pass
all four in CPU and full-container runs;40/168 existing raw cases also PASS.
Four SM5/SM6 GPU readbacks777 PASS. Nine total process records: expected baseline
failure, two CPU candidate checks, six validation-on container/compute checks.
Scoped builds/diff pass, final staged/build hashes match; no Metal errors.

No performance rerun for this O(1) off-hot-path query fix; prior warnings OPEN.
M1Max/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1; requested26/native Windows/other
GPUs/fullT3/T4 NOT_RUN. Complete N06/G1/Device/FL12_2 not claimed. User .gitignore
preserved; local commit/no push. Microsoft COM contract, commands/hashes/report:
`../dxmt-test/n06-deserializer-identity-20261027/N06-deserializer-identity.md`.


### N06 root-serialization allocation failure — 2026-10-27

On5c25510c, existing segment initialization dereferenced a failed malloc before
its caller's null check, and vector/blob bad_alloc could escape serialization.
The existing shared SerializeVersionedRootSignatureImpl now guards memset and
translates bad_alloc to E_OUTOFMEMORY after segment RAII unwinds. Existing output
preservation/error-output initialization retained; no new owner/source/header,
public ABI, binding, scheduling or capability change.

Existing command-list runner adds opt-in root-serialization-allocation-failure
for legacy RS1.0 and versioned RS1.0/1.1. Driver-local thread-scoped injection
covers malloc and optional calloc (compiler folded malloc+memset into calloc).
Both configurations pass13 allocation failures per mode:78 failures total,
each followed by byte-identical healthy serialization. Same final test PE with
frozen d143af64 DLL exits1 on escaped bad_alloc at mode0/allocation1. Baseline
DLL is retained from the previous slice, not a fresh5c25510c rebuild. Initial
malloc-only pilots missed segment sites; corrected harness results retained
separately. This does not measure exact frees or establish leak absence.

Private/no-private scoped builds pass. Concurrent commits4f9596fc/e4681cee
are preserved; both rebuilt/staged and the full matrix rerun after each. Final
evidence uses e4681cee, with earlier runs retained separately. Later uncommitted
winemetal NSError edits are preserved and outside this verification. Eleven final
processes comprise the
expected baseline failure and ten candidate successes: two CPU OOM processes,
eight validation-on adjacent InfoQueue/container/compute processes. Existing
InfoQueue11+5 failure sites and168 raw-root cases pass per configuration;
four SM5/SM6 GPU readbacks777 pass. Loader/native paths and final build/stage
hashes checked; no observed Metal validation errors. Existing build warnings
remain OPEN. No performance rerun for off-hot-path creation; prior warnings OPEN.

M1Max/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1; requested Xcode26/native Windows/
other GPUs/fullT3/T4 NOT_RUN. Full serializer input/extent validation and complete
N06/G1/Device/FL12_2 remain OPEN. User .gitignore preserved; local commit/no push.
Commands, baseline provenance, source/artifact hashes and report:
`../dxmt-test/n06-serialize-oom-20261027/N06-serialize-oom.md`.


### N14 fence factory allocation boundary — 2026-10-27

On426092a9, CreateFence allocated its COM object outside Initialize's bad_alloc
handler; object allocation could escape HRESULT, while State/listener failures
returned E_OUTOFMEMORY without clearing caller output. Same final test PE on
freshly built baseline confirms these failures for Fence/Fence1 and the existing
null-output validation path. Move the existing catch to the shared factory,
initialize output before allocation, and remove the narrower duplicate catch.
Existing Com/State/removal-listener ownership remains; no new source/header,
object/state, public ABI, capability or GPU scheduling change.

Existing command-list runner adds opt-in fence-creation-allocation-failure:
three modes x three allocation sites x two configurations =18 failures handled,
each followed by successful creation, initial-value/CPU-rewind checks, unsupported
IID/null-output checks, and final fence/device Release==0. Fresh device per
injection covers object, State and listener-vector allocations. This proves
bounded observable lifetime recovery, not exact free-count/heap-leak absence.

Private/no-private scoped builds and11 final validation-on processes pass their
expected outcomes: one baseline exit1, ten candidate exit0. Adjacent ALL/ANY/
null/duplicate/rewind/wide/six-OOM/abandonment checks,512 concurrent groups per
configuration, removal/backpressure/destruction, and two depth/stencil GPU copy
readback rounds per configuration pass. Existing removal helpers are exposed
through one opt-in argument, avoiding a full unrelated runner traversal. Final
source/PE/DLL/native hashes and staged loader paths verified; no observed Metal
validation errors. Existing deprecation warnings remain.

O(1) output initialization and relocated exception boundary are off recording/
submit paths; performance NOT_RUN, prior warnings OPEN. M1Max/Apple7/macOS27.0.1/
Xcode27.1/Wine11/MSC4.0.1; requested26/native Windows/other GPUs/fullT3/T4 NOT_RUN.
Shared-fence interoperability and complete N14/Device/G1/FL12_2 remain OPEN.
User .gitignore preserved; focused local commit, no push. Commands, primary
CreateFence API reference, evidence classification and hashes:
`../dxmt-test/n14-fence-create-oom-20261027/N14-fence-create-oom.md`.


### N14 fence creation flag admission — 2026-10-27

On93433e53, CreateFence stored arbitrary flags and returned success although
CreateSharedHandle/OpenSharedHandle are E_NOTIMPL and the fence owns a local
software timeline. Same final PE baseline confirms SHARED creation succeeds
then export returns0x80004001; unknown masks are also accepted. Existing factory
now clears output, rejects unknown bits with E_INVALIDARG and recognized but
unimplemented shared/cross-adapter/non-monitored modes with E_NOTIMPL before
allocation. Microsoft documents NON_MONITORED=0x4; bundled headers omit it, so
the existing source uses a named constant without modifying SDK headers.

This is truthful unsupported admission, not shared-fence implementation or
feature completion. Nonzero recognized combinations use the project's current
unsupported result; exact Windows flag-combination precedence is NOT_RUN.
D3D11's shared-event/KMT path is not copied into D3D12: sharing that event alone
would not share the authoritative rewind value and latched wait registrations.
The full cross-process ownership/notification protocol remains OPEN.

Private/no-private scoped builds pass. Eleven final validation-on processes:
one expected baseline exit1 and ten candidate exit0. Each configuration checks
24 output/null-output cases,11 allocation-free rejection cases and ordinary
fence recovery; existing9 creation-OOM sites, multiple-wait6 OOM sites, removal/
backpressure/destruction and two depth/stencil GPU copy rounds pass. Final
source/PE/DLL/native hashes and loader paths checked; no observed Metal validation
errors. One earlier baseline run is retained separately. No new production file,
owner, public ABI, layout, scheduling or FL/SM/Options bit change.

Two constant-time factory checks; no recording/submit work added. Performance
NOT_RUN and prior warnings OPEN. M1Max/Apple7/macOS27.0.1/Xcode27.1/Wine11/
MSC4.0.1; requested26/native Windows/other GPUs/fullT3/T4 NOT_RUN. N14/Device/G1/
FL12_2 remain incomplete. User .gitignore preserved; local commit/no push.
Commands, source references, hashes and evidence:
`../dxmt-test/n14-fence-flags-20261027/N14-fence-flags.md`.

### N09 — typed-buffer format admission, 2026-10-27

Base4b4e70d2. Native format mapping was incorrectly treated as texel-buffer
eligibility. Existing buffer SRV/UAV entrypoints now reject depth/stencil
component views and consult the existing immutable native TextureBufferRead/
Write/ReadWrite capabilities before view creation/publication. No new table,
owner/cache/lock, production file, ABI, scheduling or public capability change.
RAW R32_TYPELESS and structured UNKNOWN keep their existing path.

Same final PE baseline rejects the test at BGRA UAV: descriptor published plus
Metal unsupported-write message. This one negative baseline uses documented
validation nslog mode; no illegal GPU dispatch. Both candidates use default
assert mode and pass70 checks each:54 empty/seeded rejections including bytewise
descriptor and generation preservation,12 legal typed,4 RAW/structured controls.
Full ownership, typed-view, texture-role, counter and feature regressions pass
in both configurations. R32_UINT typed SRV/UAV GPU readback1554 succeeds through
SM5/SHEX and SM6/DXIL on baseline/private/no-private, six runs total. A missing-SRV
shader produces777 and fails. Four existing depth/stencil GPU copy rounds pass.
Final22 validation-on processes:20 successes and2 expected failures. DLL/native
loader paths and hashes checked. Early unsafe baseline timeout, unproven RGB96
test expectation, test-edit build failures and driver-parser errors are retained
but excluded from final acceptance. Both final builds pass.

AB/BA/AB/BA8 processes,10000 typed-view writes/batch,96 retained samples/side:
median process p50/p95 us2164.225/2229.150 ->2245.775/2305.400 (+3.77%/+3.42%).
One snapshot lookup per typed view; approx8.155ns/write median-batch increment.
No speedup claim; existing recording/submit warnings and globalperf gate OPEN.
No draw/submit code change. BGRA legal private-buffer SRV inherits ShaderWrite
in shared native view creation: observed and still OPEN, explicitly excluded
from this admission slice. RGB96 typed-buffer shader semantics and missing typed
BUFFER query bits remain UNVERIFIED/OPEN; no speculative restriction/promotion.

M1Max/Apple7/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1/clang23.1.1. Requested
Xcode26/nativeWindows/otherGPUs/D3D11rendering/fullT3/T4 NOT_RUN. Device/G1/
FL12_2 incomplete; EnhancedBarriersSupported and additional typed-UAV Options
remain FALSE. User .gitignore preserved, local commit/no push. Evidence:
`../dxmt-test/n09-buffer-format-20261027/N09-buffer-format.md`, manifest.json,
results.json, shader-identities.json, performance.json and raw logs.


### N09 — read-only typed-buffer native usage, 2026-10-27

Base6d9f18e3. Legal BGRA SRV inherited ShaderWrite from managed/private
allocation flags in shared Buffer::prepareAllocationViews. Same final PE baseline
reports API success but native Metal unsupported-write validation. Baseline
observation alone uses nslog, with no illegal GPU dispatch. Existing descriptor
now carries read_only in its existing cache key; D3D11/D3D12 derive the restriction
from immutable native capability data. Writable formats retain SRV/UAV sharing.
GpuReadonly allocation/tracking flags remain unchanged. No new owner/cache/lock,
production file, public capability or PE/native ABI change. Internal descriptor
layout changes; matching shared consumers rebuilt.

Both scoped builds pass. Each configuration passes72 format cases and8 cache
checks. BGRA DEFAULT-buffer GPU readback matches four channel values through
SM5/SHEX and SM6/DXIL. D3D11 managed/private/immutable inputs each run two passes,
including rotated UpdateSubresource data on mutable inputs:24 comparisons per
configuration. R32 controls read1554 in6 baseline/private/no-private backend runs.
Wrong-channel SM6 mutation fails both candidates as expected. Existing D3D11
backend boundary, D3D12 ownership/roles/formats/counter/feature controls and4
depth/stencil GPU copy rounds pass. Final31 validation-on processes comprise28
candidate processes (26 success,2 expected wrong-output failures),1 baseline
expected native error and2 baseline R32 successes. No candidate Metal API/GPU
validation errors; loader paths, container chunks and source/PE/DLL/native hashes
checked. Preliminary runs and removed test-link error excluded from final counts.

AB/BA/AB/BA8 processes,10000 writes/batch,96 retained samples/side: process-median
p50/p95 us2167.025/2269.800 ->2245.000/2332.050 (+3.60%/+2.74%). Observed overhead,
not a speedup; approx7.80ns/write median difference. Existing cache adds one
boolean comparison, D3D11 adds one capability lookup at view creation; D3D12
reuses its lookup. No draw/submit work added. D3D11 creation/first-view cost and
recording/submit/presentation performance NOT_RUN; global performance gate OPEN.

M1Max/Apple7/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1/clang23.1.1. Requested26,
native Windows/other GPUs/fullT3/T4/broadD3D11rendering NOT_RUN. BGRA SRV usage
resolved; typed BUFFER report/RGB96/Device/G1/FL12_2 remain incomplete. No public
capability promotion. User .gitignore preserved; local commit/no push. Evidence:
`../dxmt-test/n09-buffer-usage-20261027/N09-buffer-usage.md`, manifest.json,
results.json, shader-identities.json, performance.json and raw logs.


### N09 — preserve 64-bit buffer-view range admission, 2026-10-27

Baseef12e6e0. Public SRV/UAV FirstElement is UINT64 but shared MakeBufferSlice
accepted UINT, discarding high bits before overflow/resource-range checks. A
256-byte buffer view at2^32 therefore aliased element0 and published a descriptor.
The helper preserves UINT64 through validation, then narrows only after the
existing nonzero-stride and32-bit byte-range checks prove representability.
Four typed/RAW/structured callers share the fix. No new table/state/owner/lock,
allocation, production file, public ABI/layout/capability or scheduling change.
Existing internal32-bit BufferSlice limits remain; >4GiB view support is not added.

Same final PE: baseline144 cases with48 high-bit failures, both candidates144/144.
Three view modes xSRV/UAV xempty/seeded destination x12 ranges verify exact valid
slice/owner/type and bytewise+generation preservation on rejection. No invalid
view is dispatched. UINT64 multiplication and end-range overflow are covered.
Existing GPU runner gains typed/structured offset modes at last element63.
SM5 typed and dual-backend structured read1665 on baseline/private/no-private
(9 successful GPU runs). Missing-SRV mutation reads777 and fails both candidates.

New independent OPEN defect: SM6 typed offset reads0 on baseline and both
candidates;3 runs explicitly EXPECTED_FAIL, not GPU semantic PASS. Clean Metal
validation does not establish correct output. MSC typed-view binding/offset
contract is the next priority. RGB96 buffer execution and typed BUFFER reporting
remain UNVERIFIED/OPEN; no claim is promoted to mask these gaps.

Both scoped builds pass. Nearest buffer formats/cache, counter, full ownership,
feature, BGRA SM5/SM6 GPU readbacks and4 depth/stencil copy rounds pass. Final31
validation-on processes:25 successes,1 baseline truncation failure,3 existing
SM6 typed-offset failures,2 deliberate wrong-output failures. No observed Metal
API/GPU validation errors. Loaded DLL/native/PE and final source hashes checked.
Preliminary diagnostic failures kept separately. D3D11/shared production unchanged;
D3D11 runtime/raw-view GPU/native Windows/other GPUs/fullT3/T4 NOT_RUN.

AB/BA/AB/BA8 processes;10000 writes/batch;96 retained samples/side. Median process
p50/p95 us2310.225/2349.850 ->2244.375/2327.050 (-2.85%/-0.97%). No regression
observed in this microbenchmark, no causal speedup claim. No recording/submit work
added; Recording/ordinarySubmit p95 and global performance gate remain OPEN.
M1Max/Apple7/macOS27.0.1/Xcode27.1/Wine11/MSC4.0.1/clang23.1.1; requested26
unavailable. Device/G1/FL12_2 incomplete; public FL/SM/Options unchanged.
User .gitignore preserved; focused local commit/no push. Evidence and commands:
`../dxmt-test/n09-buffer-range-20261027/N09-buffer-range.md`, manifest.json,
results.json, performance.json, official source snapshots and raw logs.

### N11 — WriteBufferImmediate recording and lifetime, 2026-10-27

Base136b56ca. Replace unsupported stub with existing PreBlit/native buffer-copy
recording. Device-local VA checks null/alignment/logical bounds/mode; Close returns
E_INVALIDARG, or E_OUTOFMEMORY for payload allocation failure. Rename/reuse query
recording HRESULT as shared recording error. Immutable values use at most argument
arena first64KiB, then encoder-owned64KiB chunks. Existing resource_refs retains
chunks/destinations through recording/submission/completion. No new src/d3d12
file, scheduler, binder, resource-state authority or public ABI.

DIRECT/COMPUTE/COPY GPU readbacks, caller mutation, overlap, replay,49157-write
chunk overflow, allocation-failure injection, pending-reset rejection and released
application refs pass private/no-private. Dispatch→WBI→Dispatch restores root/PSO;
4 graphics cases preserve earlier pixels across encoder split. Invalid tests
include foreign VA/bundle. Wrong-value oracle fails both at word255. Existing
command-list lifecycle, adjacent barriers, feature and depth/stencil copy pass.
30 verified processes:28 success,2 expected oracle failures; reporting correction
rechecks8 candidate anchors and2 baseline failures. Production hashes unchanged;
no observed Metal API/GPU validation errors. Logs/hashes outside repository.

Conservative marker fallback uses existing fences; native overlap/crash-progress
NOT_VERIFIED. COPY_DEST is caller contract; no new command state validator.
Public WBI mask stays NONE; no FL/SM/Options promotion. Native Windows/other GPUs/
SM6 interleaving/sparse/fullT3/T4 NOT_RUN.

Performance OPEN:512 writes vs baseline prefilled copy,24 samples/side, recording
p50/p95 us49.55/55.90->288.60/301.10; submit409.10/472.05->432.375/476.65.
10000-copy control ABBA/BAAB,20 samples/side: recording380.25/404.40->490.75/518.40
(+29.1%/+28.2%,~11ns/copy median), submit4807.20/5047.60->4760.95/4969.50.
Recording regression repeats; root cause unresolved despite unchanged hot-path
call sequence. No unrelated optimization/zero-cost claim. M1Max/Apple7/macOS27.0.1/
Xcode27.1/Wine11/MSC4.0.1/clang23.1.1; requested Xcode26 unavailable.

Next priority: DXIL Append/Consume MSC counter binding ABI, conversion, GPU
counter/payload differential and submission lifetime; GS details/Int64/Native16
in differential acceptance. Existing SM6 typed-offset wrong-zero stays OPEN.
Device/G1/FL12_2 incomplete. Evidence: ../dxmt-test/n11-write-immediate-20261027/
N11-write-immediate.md, slice.md, manifest.json, results.json, performance.json,
official snapshots and raw logs. User .gitignore preserved; local commit/no push.

### N06 — MSC Append/Consume counter binding, 2026-10-27

Base38668511. Baseline rejects bufferUpdateCounter at conversion; gate-only
external diagnostic then fails safe ABI preflight (texture ID0). Existing
Airconv counter pointer was present, MSC counter texture absent. Extend existing
BufferViewDescriptor cache with optional byte range: one R32Uint element at the
counter offset, with relative MSC offset0 (ABI field only8 bits). Keep full UINT64
counter offset. Descriptor counter owner/key copies and resets with existing
heap mechanics. Existing recording/submission read-write resource uses retain
both native counter buffer and texture. Remove blanket Append/Consume rejection;
other gates and public FL/SM/Options unchanged. No new src/d3d12 module/owner.

Final private/no-private:28 processes,24 success,4 deliberate wrong-value failures.
SM5/SM6 Append→Consume GPU payload/count/guards pass counter offsets0/4096/8192,
payload FirstElement0/3/8, distinct cached range IDs, copied CPU heap destruction,
post-Close mutation, counterless replacement, fenced reuse and application object
release during gated submission. MSC submission retains2 read-write counter
texture uses. Conversion-only probe explicitly separates GPU NOT_RUN. Full
ownership, existing Airconv counter/residency, D3D11 BGRA24-value controls and
feature checks pass. Earlier format/range controls pass. No final Metal API/GPU
validation/native assertion/aborted-command errors. DXC opcode70 +/-1, MSC CLI4.0.3
metallibs/reflection and runtime API4.0.1 distinguished. M1Max/Apple7/macOS27.0.1/
Xcode27.1/Wine11; requested Xcode26 unavailable.

Final ABBA cost,24 samples/side,20000 cached writes:counterless p50/p95 us
2596.70/2683.20->2481.25/2554.00;counter-bearing3333.05/3428.30->3947.95/4018.90
(+18.4% median,~30.7ns/write). No zero-cost/global-performance claim. No counter
submit/GPU timing claim. Graphics/mesh/RT, dynamic counter arrays, same-resource
payload/counter, non-default heaps, sparse/>4GiB offsets, allocation fault injection,
native Windows/other GPUs/fullT3/T4 NOT_RUN. GS/Int64/Native16 differential remains
next; SM6 typed-offset wrong-zero and WBI recording-cost regression remain OPEN.
Device/G1/FL12_2 incomplete. Evidence:../dxmt-test/n06-append-consume-20261027/
N06-append-consume.md,slice.md,results.json,performance.json,conversion/reflection,
primary snapshots and logs. User .gitignore preserved; local commit/no push.

### N20 — Int64/Native16 differential acceptance, 2026-10-27

Initial base33b85f49; final production build base982e39fd includes concurrent
Airconv/DXBCParser commits. Extend existing dx12_compute_sm6 runner and numeric
HLSL fixtures with external input/reference bytes; old smoke modes preserved.
DXIL container magic no longer mislabels the execution backend: inspect actual
PSO backend. No production source changes/new src/d3d12 files/capability promotion.

Deterministic independent CPU reference:two256-record datasets per type. Actual
DXIL retains i64 arithmetic/division/remainder/shifts/comparisons and i16/half
arithmetic,2-byte Native16 structured storage. DXC cs_6_0/cs_6_2 with native16
flag; MSC CLI4.0.3 conversion/metallibs/reflection and runtime API4.0.1 separated.
Runtime checks actual MSC backend and reflected1x1x1 group size. Offline automatic
TLAB reflection is not evidence for the explicit runtime root-signature layout.

Final private/no-private matrix:26 processes,18 success,8 deliberate wrong-reference
failures detected at pass0/byte32. Eight positive root/table differential processes
each check3 passes x256 records:24576 Int64 or8192 Native16 result bytes plus64
guard bytes/pass. Nonzero input/output offsets16/32, post-Close descriptor copy,
unchanged closed-list replay, fenced reset/rerecord, pending allocator Reset
rejection and gated submission after releasing application refs pass. Eight old
numeric smoke controls and two feature-contract controls pass. No observed final
Metal API/GPU validation/native assertion/aborted-command errors; hashes and loaded
paths recorded. Both Meson differential fixture targets build in both configurations.

M1Max/Apple7/macOS27.0.1/Xcode27.1/Wine11/MSC runtime4.0.1/clang23.1.1; requested
Xcode26 unavailable. Performance NOT_MEASURED:this slice changes tests only; single
DXC invocation times are diagnostic, not a benchmark. No full Int64/Native16 claim:
native Windows/other GPUs/NaN payloads/all operations/atomics/graphics stages/full
T3/T4 NOT_RUN. int64_validated/native16_validated remain false. GS fine-grained
differential is next; typed-offset wrong-zero,WBI recording cost and cached-counter
descriptor cost remain OPEN. Device/G1/FL12_2 incomplete. Evidence and commands:
`../dxmt-test/n20-numeric-differential-20261027/N20-numeric-differential.md`,
slice.md,manifest.json,results.json,fixtures.py,validate.py,conversion/reflection
and logs. User .gitignore preserved; focused local commit/no push.

### N20 — GS primitive assembly and submission differential, 2026-10-27

Base97f85de4. Extend existing dx12_graphics_sm6 and geometry_sm6.hlsl; optional
Meson fixtures for five GS input arities. No production changes/new src/d3d12
files/capability promotion. Previous one-pixel tests do not distinguish vertex
ordering, missing emissions or strip connections. New CPU oracle compares every
byte of a256x3 RGBA8_UINT target. Nonlinear vertex values, VertexID, InstanceID,
PrimitiveID and GS-visible root constant contribute to the output. Each input
primitive emits two strips separated by a clear guard row; primitive1 emits none.

Final private/no-private matrix:78 processes,72 success,6 deliberate wrong-reference
failures at pass0/byte0.54 positive differential processes cover8 input topologies
(point/list and strip line/line adjacency/triangle list and strip/triangle-list
adjacency),nonindexed/R16/R32 indices,VB/IB offsets32/16 and draw/base offsets0/2.
Each checks3 passes x3072 bytes;66 primitives x2 draw instances crosses reflected
32-primitive mesh group capacity. Closed-list replay,fenced reset/rerecord,constant
snapshot,pending Reset rejection and gated release of VB/IB/RT/PSO/root/allocator/
list refs pass.10 prior GS GPU smoke controls,2 feature controls,2 GS-instance
rejections,2 stream-output rejections and2 topology-mismatch rejections pass.
Rejection checks are admission-only/GPU_NOT_RUN, not feature support evidence.

Runtime MSC API4.0.1 backend/geometry config verified; offline CLI4.0.3 converts
15 VS/GS/PS variants with reflection. Vertex output32 bytes,GS max primitives32,
instance count1. Actual DXIL primitive/instance IDs,emit/cut and root loads saved.
Offline automatic root layout remains separate from runtime explicit roots.
Initial diagnostic mismatches were CPU-oracle errors:Draw offsets do not add to
VertexID;odd triangle strips keep the leading vertex first. Corrected using
Microsoft functional spec8.16/13.10 and its GS-input diagrams; all final cases
use the corrected oracle and nonlinear input values. No observed final Metal
API/GPU validation/native assertion/aborted-command errors; artifact hashes and
loaded paths checked. M1Max/Apple7/macOS27.0.1/Xcode27.1/Wine11/clang23.1.1.

Bounded GS acceptance complete; full GS feature coverage remains OPEN. Triangle
strip adjacency,strip-cut indices,point/line GS output,multiple output streams,
GS instancing execution,SO execution,tessellation+GS,indirect GS,viewport/layer
semantics,native Windows/other GPUs/fullT3/T4 NOT_RUN. Performance NOT_MEASURED
(tests only). Device/G1/FL12_2 and existing performance/typed-offset issues OPEN.

User review backlog after numeric/GS acceptance:MSC actual VB stride variants;
indirect sparse-slot vs compact-table mapping;empty-root zero upload;post-indirect
modified VB/IB/root state reset+dirty flags. Corresponding regressions:padding/
dynamic stride,sparse slots,empty root,indirect then direct draw/dispatch. These
four findings remain deferred and unfixed in this slice. Evidence and commands:
`../dxmt-test/n20-gs-differential-20261027/N20-gs-differential.md`,slice.md,
manifest.json,results.json,validate.py,reflection/DXIL,primary snapshots and logs.
User .gitignore preserved; focused local commit/no push.


### N07 — MSC bound vertex strides, 2026-10-27

User review finding1: ordinary MSC input layouts inferred a packed stride from
attribute extent, ignoring VBV.StrideInBytes. Preserved baseline packed12 passes;
padded vertex/instance/indexed/pure-draw-indirect cases fail with the same runner.
Prior numeric/GS differential rounds were completed before this implementation.

Existing graphics PSO owns immutable native recipe, retained VS/PS functions and
mutex-protected active-stride pipeline cache. Original packed pipeline seeds it;
unused slots do not affect keys. Stride0 uses Constant stepping and a nonzero
Metal descriptor extent; per-instance step0 is normalized to Constant. Nonzero
instance divisors preserved. Full input-range validation precedes VB mutations.
Command-list GraphicsVertexStride dirty state tracks actual active stride
changes; unchanged strides avoid cache lookup/lock. Stride-only binds emit only
the native pipeline, preserving existing depth/rasterizer state authority.

Existing RenderEncoderData retains private refs to recorded graphics PSO owners
until allocator encoder destruction/reset, preserving native variants across
switches, Close/replay and queued submission. Native functions/variants remain
owned by the PSO. Pure Draw indirect restores the selected variant after its
resolver. Recording failures propagate through existing Close failure handling.
Six existing production files modified; new src/d3d12 files=0. SM5→AIRCONV and
SM6→MSC remain fixed; public FL11_1/SM6.0/Options/Tiers unchanged.

Final private/no-private matrix:28 processes,26 success and2 deliberate wrong
oracles correctly rejected. Fourteen positive stride processes each compare all
4096 integer bytes across initial execution,replay and fenced reset/rerecord.
Covers slot3,float3 plus padding16/32,zero stride,instance divisor2/0,reversed R16,
pure Draw indirect and two PSOs released after Close before execution/replay.
Pending allocator Reset rejected; list/allocator/PSO refs dropped during gated
submission, with resource inputs retained through fence completion. Four GS
point/triangle-strip-u16 controls compare3072 bytes three times each. Ordinary
MSC,selected AIRCONV list/indexed/indirect/GS,loaded pipeline-cache compute/graphics
readback and feature contracts pass. All accepted cases enable API/shader
validation; final logs have no validation diagnostics or native assertions.
Full SM5 --all has one existing Null texture access diagnostic also reproduced
with baseline; those broad runs are not validation-clean passes.

Five interleaved performance samples,4096 draws,validation/HUD off,readback on:
fixed-stride warm CPU median0.243011→0.221457us/draw (P950.252859→0.264125);
CPU submit6174.3→5928.2us. P95 increases4.5%; no speedup/regression conclusion from five samples. Candidate
changing16/32/16/0 each draw costs0.281085us/draw (P950.292473),submit6326.2us.
Native cache miss can hitch:prevalidation observed56.288ms first-four outlier;
warmed final first-four median2739.0us does not establish fully cold compiler
latency. Cache grows with distinct active stride tuples; no eviction/cap added.
No extra encoder/barrier or unchanged-stride draw lock. Concurrent/many-stride
stress,other GPUs,Windows reference,isolated GPU profiling,fullT3/T4 NOT_RUN.

Evidence:`../dxmt-test/n07-vertex-stride-20261027/N07-vertex-stride.md`,
manifest.json,results.json,baseline-and-candidate.json,final-v6-* logs,
perf-v6-raw.json/perf-v6-summary.json and build logs. M1Max/Apple7,
macOS27.0.1(26A434),Xcode27.1(27A9275;requested26 unavailable),Wine11 x64/Rosetta,
clang23.1.1,MSC runtime4.0.1. User .gitignore excluded. Full goal remains OPEN.
Next N11 shared indirect slice retains findings2–4:sparse compact VB slots,
zero-length root upload and post-indirect modified-state reset/dirty tracking.


### N11 — ExecuteIndirect compact VB ABI and post-state, 2026-10-27

Closes user review findings2–4 after the N20 numeric/GS and N07 stride rounds.
Workflow node is N11; prior next-step N10 label corrected. Five existing production
files change; new src/d3d12 files=0. Existing signature owns modified VB slot and
root DWORD masks. Existing resolver receives the active PSO mask and writes
popcount(lower active slots),skipping inactive slots. CPU tables allocate only
active entries. Paired internal CPU/MSL command data adds vertex_slot_mask;
offset80/size88 checked. No public/WineMetal ABI or backend changes.

Existing command-list helper clears signature-modified VB/IB/root DWORD state,
marks graphics/compute root and VB dirty,and preserves untouched values. IB clear
reuses the existing setter. MaxCount0 also resets state and skips ICB allocation;
GPU count0 resets state after encoding. Empty root uploads and absent sampler
uploads are skipped. Root-updating work without a bound root fails recording;
compute signatures with VB/IB updates reject E_INVALIDARG. No new executor,
lifetime owner,reset allocation/lock,encoder or barrier.

Preserved baseline control passes; sparse slot3 reads old VB; empty-root Close
fails; IB remains bound; graphics/compute direct work reuses old constants.
Final private/no-private matrix:36 processes,34 success,2 wrong-oracle negatives.
Twenty-six positive new-mode processes execute three exact readbacks each,
covering sparse3,inactive31 plus later PSO31,empty VB/IB roots,partial constants,
NULL CBV/SRV/UAV uploads,unmodified views,graphics/compute,CPU/GPU zero counts,
closed-list replay,fenced reset/rerecord and pending-reset rejection. Admission
checks reject invalid compute VB signature and missing roots without device loss;
Close-failed lists retain E_FAIL on Reset and are replaced before allocator reuse.
Four MSC stride/pure-indirect processes,two selected AIRCONV controls and two
feature controls pass. All final logs are clean under Metal API/shader validation.

Proof boundaries:root NULL view reset is verified in actual uploaded native
argument bytes without NULL shader dereference. NULL IB address is observed in
a later GPU-zero-count indirect command,then valid nonindexed direct work runs.
Preliminary nil-index direct draw assertions and invalid failed-list Reset test
expectations are retained as diagnostic history,not accepted passes.

Five interleaved private-runtime perf samples:non-updating control first record
566.6→618.6us(+9.2%),warm rerecord40.3→40.6us(+0.7%),native encode41.6→44.5us/submit
(+7.0%). INCONCLUSIVE; no performance improvement or regression-free claim.
Same control counts across3 submissions before/after:6 render,0 compute,3 blit,
3 ICB. Root updating graphics has the same counts;compute0/3/6 encoders,3 ICB.
No isolated GPU/full-game timing. Reset cost O(modified slots+DWORDs),no new
per-draw lock or heap walk. Predication,upper root mask/multiple active sparse
slots,overlap/concurrency/stress,Windows,other GPUs,fullT3/T4 NOT_RUN.

Evidence:`../dxmt-test/n11-indirect-bindings-20261027/N11-indirect-bindings.md`,
manifest.json,results.json,final-v2-* logs,perf-raw.json/perf-summary.json,
accepted-artifacts and build logs. Same M1Max/macOS27.0.1/Xcode27.1/Wine11/clang23.1.1/
MSC4.0.1 environment. User .gitignore excluded. Public FL11_1/SM6.0/Options/Tiers
unchanged;MSC updating/emulated-GS indirect gates remain. Full goal stays OPEN.


### N04 — AIRCONV PointStream primitive count and size, 2026-10-27

First of user's new six findings,base8e809273. Current baseline D3D12 and D3D11
ordinary/three-instance GS produce blank output. Point Emit increments
next_write_vertex,but epilogue reads untouched primitive_count. Existing converter
now selects point emitted count,retaining line/triangle accounting and point Cut
no-op. Count-only repair exposed missing point size (unintended coverage);reuse
existing mesh OutputPointSize/CreateSetMeshPointSize1.0,matching D3D11.3 spec3.4.6
one-pixel points. No duplicate counter,backend,queue,binder or lifetime owner.

AIRCONV_VERSION27→28 invalidates persisted bad programs through existing cache
version authority. Same-directory upgrade retains cache_27 and adds cache_28 with
three matching keys;correct candidate and warm reload pass. Actual cached AIR
baseline has primitive_count0;candidate uses next_write_vertex and point_size1.
LLVM23 disassembly is inspection only;production uses configured LLVM15.

Existing geometry fixture and GS differential runner gain SM5 PointStream via
external D3DCompile helper;existing D3D11 boundary runner renders same blobs.
Full private/no-private matrix40 processes:32 success,8 deliberate wrong oracles.
24 positive point processes compare6144 bytes three times each:66 input points,
32-primitive group boundary,0/1/2 Emit,RestartStrip,draw instances2,GS instances1/3,
nonindexed/R16/R32,PrimitiveID/VertexID/GSInstanceID,phases,replay/fenced reset,
pending-reset rejection and D3D12 submission lifetime. D3D11 phase0 proves empty
output. Eight AIRCONV/MSC/persistence/feature controls pass. Final logs have no
Metal API/shader diagnostics or native assertions. Final timing-only/CLI-format
runners additionally repeat instance3/R32 smokes;functional PEs archived.

Two existing AIRCONV production files modified;new src/d3d12 files=0. Public
FL/SM/Options/Tiers unchanged. Native stages own physical loader/ntdll and isolated
winemetal.so to avoid loading old symlink targets;loaded paths/hashes checked.
Prior Wine stages/native bridges and user .gitignore preserved.

PSO conversion/native creation n=3,validation/HUD off:median30869.2→32543.6us,
P9532957.7→111319us. INCONCLUSIVE (+5.4% median,first candidate native-cache outlier);
no speedup claim. Incorrect baseline zero-point GPU timing is not comparable.
No new per-draw CPU lock/encoder/barrier/allocation;point size adds an AIR output
write per Emit. Max emission/other instances/topologies/MSAA/Windows/other GPUs,
fullT3/T4 NOT_RUN. Full goal OPEN. Evidence:
`../dxmt-test/n04-gs-pointstream-20261027/N04-gs-pointstream.md`,manifest/results,
IR/cache-upgrade proof,perf data,build/validation logs and accepted artifacts.

Prioritized remaining review queue (not closed by this round):N11 graphics stage
layout root rebinding;SO complete-primitive allocation/order/strip expansion;
D3D11 zero-complete TS patch guards in both draw paths (avoid unsafe GPU baseline);
AIRCONV triangle-strip adjacency signed lane/mask correction;MSC TS point/line
validation/output. Requested feature gaps remain OPEN implementation work:MSC GS
instancing,D3D12 GS+TS,and GS/MSC SO combinations. Existing rejection paths are
limits,not proof of implemented support. Next recommended slice:N11 stage binding.


### N11 — Graphics stage layout root rebinding, 2026-10-27

Closes user finding2,base1c7c25b5. Existing SetPipelineState compares the old/new
root binding layout:backend ABI,vertex versus object/mesh route,and MSC TS extra
HullDomain binding. A changed layout dirties GraphicsRootArguments/Signature;
logical roots and repeated same-signature no-op remain. Existing DescriptorHeaps
dirty handling already rebinds native heaps/tessellator tables;no duplicate flag
or new binding authority added. Same-layout PSO switches keep root uploads clean.
PreDraw reuses its AIRCONV root lambda for vertex/fragment and removes unreachable
MSC mesh branches inside the AIRCONV binder. One production file;new src/d3d12
files=0;no cache/native ABI/lifetime owner or public capability changes.

Existing command-list runner/stages fixture covers AIRCONV ordinary→GS,MSC
ordinary→GS/TS/mesh/AS+mesh,GS→TS,cross-backend transitions,redundant same-RS calls,
and same-layout GS/mesh/AS switches. Two root-value epochs keep the signature,
descriptor table and static sampler unchanged through each sequence. A native
safety observer checks actual root bytes,table address,static sampler encoding,
MSC heap handles/offsets and TS runtime tables before forwarding each draw.
Baseline missing/stale stage roots are observed and invalid batches suppressed:
GPU transition output is NOT_RUN. Baseline isolated-encoder control passes GPU
output,proving fixtures independently. An early expectation that root-only repair
would miss heap bindings was disproved;existing unconditional heap dirty state
was confirmed and the redundant candidate flag removed before final acceptance.

Final private/no-private matrix52 processes:43 success,9 expected negatives
(7 guarded baseline failures,2 wrong-image oracles). Stage modes compare81 exact
GPU images across initial execution,closed-list replay and fenced reset/rerecord;
application references remain held through completion. Pending allocator Reset
rejects while queue is gated. Adjacent indirect root/compute/sparse/empty-root
cases compare24 readbacks;stride/pure-indirect compare12 images. AIRCONV and
feature controls pass. All52 final logs clean of native/API/shader diagnostics.

Five interleaved same-layout4096-draw private samples,validation/HUD off:
first recording median1.18633→1.19578us/draw(+0.8%);warm rerecord0.312769→0.311938
(-0.3%);native encode2328.7→2261.5us/submit(-2.9%,15 submissions). INCONCLUSIVE,
no speedup/regression-free claim. Two root sets and2 render encoders per submission
remain equal. GS/mesh/AS same-layout sequence needs only6 root sets across2 root
value epochs. O(1) layout comparison per changed PSO;changed layouts use existing
root/static-sampler uploads. No new per-draw lock,heap walk,encoder or barrier.
PSO creation and isolated GPU/full-game timing not measured by this microbenchmark.

Evidence:`../dxmt-test/n11-stage-bindings-20261027/N11-stage-bindings.md`,
manifest.json,final-results.json,final-v2-* logs,perf-v2 data,accepted-artifacts,
accepted-source.diff and build logs. Same M1Max/Apple7,macOS27.0.1,Xcode27.1
(requested26 unavailable),Wine11/Rosetta,clang23.1.1,MSC4.0.1 environment.
Directly-indexed heap shader consumption,early application-reference release,
Windows,other GPUs,fullT3/T4 NOT_RUN. User .gitignore excluded. Full goal OPEN.
Next priority:SO complete-primitive allocation/order/strip semantics,then zero
complete D3D11 TS patch guard,adjacency strip lanes and MSC TS point/line output.
MSC GS instancing,D3D12 GS+TS,and GS/MSC SO feature gaps remain OPEN.


### N11 — Ordered complete-primitive VS stream output, 2026-10-28

Closes new user finding3 within the existing AIRCONV ordinary-VS,stream0/slot0,
no-raster SO range;base108a8cc1. Baseline point control passes,but a triangle in
a buffer smaller than one complete primitive writes partial data and advances
the cursor;cut-strip ordering also fails. D3D11 bare SO was rejected by its old
no-attachment gate. One shared output mechanism now uses a per-draw GPU cursor
snapshot and complete-primitive budget,deterministic input occurrences,manual
index fetch,strip/list expansion,winding/leading-vertex preservation and adjacency
discard. Gaps/padding remain untouched. D3D11 uses the existing Buffer/BufferAllocation
owner for its cursor,ordered ST reset via rename,and append-preserving restoration.

Existing internal library supplies preparation and bounded-work hierarchical index
prefix kernels. Direct cut strips use the existing compute/render passes;ordinary
SO adds a preparation draw and buffer barriers. GPU indirect counts/IBV updates
cannot size prefix scratch on the CPU. Reuse the existing compute ICB allocator,
executor and command-signature decoder:parallel occurrence kernels for non-cut
work;an ordered bounded-memory workgroup plus native ICB barriers for cut strips.
Shared storage derives from the legal SO declaration,not the GPU vertex count.
32/96/256-thread cases pass;all1..128 component counts fit32KiB. Cut-free chunks
skip scans;capacity division is outside chunk loops. No CPU readback executor,
second binder,timeline or src/d3d12 source/header file introduced.

Graphics PSOs retain an immutable SO compilation recipe and lazily create indirect
entries/pipelines under the existing PSO mutex. Signature SO resolver compilation
is lazy too. Existing encoder retention owns graphics PSOs and signatures in both
physical encoder kinds. Paired CPU/MSL compute-command data now112 bytes (VB64,
SO template96),SO draw data160 bytes (base112,capture136,indices152). Static asserts
and actual GPU signature-update cases verify ABI. A multi-entry library exposed
the existing MetallibWriter's single list-size prefix;native Xcode two-function
output proves per-entry sizes. The writer now emits each size independently;
single-entry serialization retains the same layout. AIRCONV28→29 invalidates old
programs. Public FL/SM/Options/Tiers unchanged;GS/MSC SO gates stay explicit.

Validation caught a real shared IA dependency:table length was unused,and compute
SO read beyond a VB. The existing pull helper now checks the format extent against
the view length and supplies missing-component defaults,keeping specified32-bit
IA address wrapping. Preliminary matching output with validation errors is NOT
accepted. D3D11 prefix preparation also supplies the same bound as capture.

Final private/no-private acceptance94 processes:90 success,4 wrong-oracle negatives.
222 SO payload/canary/cursor comparisons across initial submission,replay and fenced
reset/rerecord cover all ordinary IA topologies including adjacency and patches,
nonindexed/R16/R32,instances,cut/disabled-cut,short/tail/full buffers,gaps,append,
root/static state preservation,predication,CPU/GPU zero counts,multiple indirect
commands,sparse VBV/IBV/root updates,empty roots and indirect→direct work.96/32-thread
wide declarations and66019-index three-level prefixes pass. D3D11 deferred replay,
explicit same-offset reset and unbind/rebind append pass. Known D3D12 resources,
PSO/root/signature application references are released under a queue gate;alternate
buffers referenced only by GPU arguments stay held to completion. Pending allocator
Reset rejects. Sixteen nearest ABI/binding/GS/stride/persistence/feature controls
pass;all accepted Metal API/shader diagnostics clean. Mesh-ICB probe is a CPU-side
validation rejection on Apple7,not a supported route;compute ICB barrier probe
executes1000 ordered increments correctly.

Performance:five interleaved private samples,validation/HUD off. Correct3-point
control warm record41.4→46.0us;GPU timestamp span1440.21→1826.75us. Pipeline creation
median11305.9→20568.9us,large overlapping P95;no regression-free or speedup claim.
Candidate32771-input×3 instances:direct/indirect point warm GPU1815.185/429.2505us,
CPU102.6/149.6us;direct/ordered-indirect cut strip1902.92/3276.355us,CPU138.4/167.5us.
Ordered strip is a measured cost boundary;prior same-device median4443.955us is
retained as diagnostic before optimization. Timestamp spans include sampling/pass
boundaries,not isolated shader or full-game throughput. First-use conversion/resolver
cost is separate from warm rerecord. No new lock on unaffected Draw paths or full-heap walk:SO cache locks
are per SO indirect invocation;metadata allocation is per draw/call,cut summaries
O(index count),ordered group storage O(declared components). T3/T4/other GPUs/Windows,
DrawAuto,SO queries,multiple buffers/streams,rasterized SO and requested GS/MSC SO
feature combinations remain OPEN/NOT_RUN,not implied by this bounded repair.

Evidence:`../dxmt-test/n11-stream-output-20261027/N11-stream-output.md`,manifest,
accepted-results,accepted-* logs,accepted artifacts,source diff,perf-accepted raw/
summary,native metallib layout and hardware probes. Round began2026-10-27 and
finished2026-10-28;M1Max/Apple7,macOS27.0.1,Xcode27.1(requested26 unavailable),Wine11
x64/Rosetta,clang23.1.1,native LLVM15,MSC4.0.1. Prior Wine/native stages and user
.gitignore preserved. Full goal OPEN. Next user priority:D3D11 zero-complete TS
patch guards,then GS adjacency lanes and MSC TS point/line output;requested feature
gaps remain implementation work.

### D3D11 TS incomplete patch dispatch (2026-10-28)

Direct and indexed tessellation draws now return before parameter allocation for
zero complete patches; indirect draws share the corrected GPU marshal, which
writes zero dispatch for zero complete patches or instances. Zero control-point
(non-patch) topology is rejected in both paths. Full patches with trailing
incomplete control points retain floor division. Group-limit multiplication is
64-bit; no public capability, shader ABI, backend or owner changes. The existing
Apple7+ UINT64_MAX group limit remains, so this is not general work throttling.

Baseline direct/indexed/instanced variants were intercepted before Metal with
1,073,741,824 groups. A standalone run of the actual baseline GPU marshal read
(536870912,1,1) for one or two vertices at four threads/patch without executing
that indirect dispatch. The candidate passed all ten GPU marshal boundary cases.
Native-loaded D3D11 passed 34 private and 22 no-private image/encoding cases,
with Metal API and GPU validation, plus two three-pass SO controls. Inherited
Wine builtin resolution ran old implementation despite the PE pathname and is
excluded; the accepted override is `d3d11,d3d12,dxgi=n;winemetal=b`.

Five interleaved warm samples of 1000 normal direct draws plus Flush: CPU submit
median 72.3→68.9 us; subsequent copy/map completion wait 12741.3→14652.5 us,
noisy wall-clock values, not GPU timestamps. Cold/other GPU/per-application
performance NOT_RUN. Checks and products are O(1), with no new allocation, lock
or encoder boundary. Evidence, commands, environment and limitations:
`../dxmt-test/n11-zero-ts-patch-20261028/N11-zero-ts-patch.md`. Remaining user
priorities: GS adjacency and MSC TS point/line, then the requested feature gaps;
the overall goal remains open.

### AIRCONV triangle-strip adjacency assembly (2026-10-28)

For strips without index cut, the shared object/mesh shader assembly now maps
primitive-zero previous adjacency to payload lane 31 in both mesh loads and the
object validity mask. Tail groups reject incomplete interior triangles, and an
odd dangling input no longer replaces the last adjacency. Group offsets use
64-bit intermediates. AIRCONV cache version 29→30; no public capability, ABI,
resource ownership or submission changes. An isolated cache-29 database gained
cache-30 entries for the same keys and the corrected GPU result, confirming old
shader results are not reused.

Baseline D3D11/D3D12 GPU tests read vertex 2 instead of 1 for the official
10-input example; D3D11 also emitted nonexistent tail primitives. Final D3D11
private/no-private passed 32 image cases, four additional no-private-EXE cases,
and rejected one wrong oracle. Final D3D12 private and no-private each passed
18 cases and rejected one wrong oracle. Tests compare all six GS inputs and
additively count each primitive across indexed/nonindexed, two draw instances,
GS instances, odd tails and group boundaries, with Metal API/GPU validation.
Two ordinary PointStream controls passed. D3D12 here tested a single submission
and fence completion, not replay or delayed release. CPU model covers all counts
0–999 and samples 32-bit boundaries, not a substitute for a huge GPU draw.

Five interleaved warm samples of 1000 draws had CPU submit medians 72.4→75.5 us;
Copy/Map waits were noisy and not GPU timestamps. Baseline emitted 56 GS
invocations versus the correct 36, so GPU work is unequal and no GPU performance
conclusion follows. No extra lock, allocation or encoder boundary; this topology
adds fixed validity checks and 64-bit indexing. Old shaders require one-time
reconversion. Full games, cold compilation and other GPUs remain NOT_RUN.

Strip cut/restart still needs cross-group segment boundaries, winding and
primitive-ID prefixes and is explicitly OPEN, as are the separately requested
GS/TS/SO combinations. This node does not claim arbitrary restarted strips.
Evidence and commands: `../dxmt-test/n11-triangle-adj-20261028/N11-triangle-adj.md`.
Next requested item: MSC tessellation point/line output validation.

### MSC tessellation topology validation: line fixed, point OPEN (2026-10-28)

The previous PSO always validated the tessellator output as triangle. The
existing HS reflection now selects the matching point/line/triangle geometry
input; invalid types and mismatched HS/DS still reject. Legal isoline/line now
creates and runs. The required enum header was already tracked at the requested
`include/metal_shader_converter/ir_tessellator_output_primitive.h` path and is
referenced there. No conversion ABI or feature-level promotion.

Final private/no-private: 16 cases covering line nonindexed/R16/R32, 8×8 exact
image, CW/CCW triangle controls, HS/DS mismatch and point rejection. Additional
final binary checks include point-vs-line mismatch and two wrong-oracle line
images, which correctly failed. Three submissions exercise replay and a fenced
reset/rerecord; application resource release under the gate was not tested.
Metal API/GPU validation and native PE/dylib path checks were clean.

**Point is not complete.** An externally retained diagnostic build without its
guard produced three point outputs for tri factor1 but domain-coordinate sum
(1,2,0) rather than (1,1,1); tri factor2 produced 15 points rather than the 7
reference vertices. The actual runtime's generated AIR includes a point-passthrough index-to-zero
fallback on a tessellator flag; this is a plausible duplicate mechanism, not a
complete proof of the internal cause. The production PSO returns E_NOTIMPL for
point to prevent silent incorrect rendering. Point fixtures and failing oracles
are retained for a future general unique-point solution that respects existing
MSC ownership/ABI and is validated across factors, domains and lifecycle.

Five interleaved warm triangle-control PSO creation samples: baseline/candidate
medians 9198.5/8224.5 us, overlapping and noisy; no per-draw or GPU performance
claim. This change only adds a constant-time validation switch at PSO creation.
Detailed reproduction, diagnostics, unsafe diagnostic artifact, exact hashes,
performance and limits: `../dxmt-test/n11-msc-ts-output-20261028/N11-msc-ts-output.md`.
Point output, GS/TS/SO combinations and strip restart remain OPEN.

### MSC point topology: isolated AIR diagnostic (2026-10-28)

The requested enum header remains tracked in `include/metal_shader_converter/`
(the include resolves to that file). The production point-output guard remains.
In a separate test-only runtime cache, changing the generated passthrough's
index-to-zero fallback to the current vertex index corrected the tri factor-1
coordinate sum for submission, replay and reset. It did not make factor 2
unique: the observed sum/count was approximately (5,5,5,15), versus the seven
reference points' approximately (7/3,7/3,7/3,7). The seventh point is the
interior center; the earlier six-point expectation was incorrect. An
identity-repack control retained the original failure. Thus the index fallback and the repeated tessellated stream require
separate treatment; neither this experiment nor a second field test establishes
a general deduplication algorithm. No-op LLVM/AIR reassembly also caused a
Metal compiler XPC interruption in this environment, with its cause unknown.
No binary mutation was added to the product. Point and other previously listed
feature gaps remain OPEN. Exact artifacts, hashes, controls and limits:
`../dxmt-test/n11-msc-ts-point-20261028/N11-msc-point-diagnostic.md`.

### Corrected factor-2 point oracle (2026-10-28)

The earlier six-vertex factor-2 expectation omitted the center. The Microsoft
D3D11 reference tessellator, as preserved in the Mesa source mirror, creates
six boundary vertices and one interior center for an integer tri patch with
all factors 2. A new optional `tess-point-coord-factor2` probe uses the seven
points' fixed-fraction coordinate sum and count, with 0.0001 float tolerance.
This aggregate oracle detects these observed failures but does not establish
the complete vertex set or domain shader side effects.

The guard-removed diagnostic DLL fails this oracle with (2,11,2,15); the
shipping DLL explicitly rejects the PSO. The private and no-private guard
controls and line-image controls passed; line exercised three submission,
replay and reset passes with Metal validation. A factor-1 control
failed on the original diagnostic DLL and passed all three passes with the
isolated index-only experiment. That experiment remains outside product code.
A per-stream-index diagnostic observed 15 emitted entries with seven distinct
quantized color codes; those colors do not prove exact coordinates. Point
support remains OPEN. Reference, commands, raw logs and limits:
`../dxmt-test/n11-msc-ts-point-20261028/N11-msc-point-diagnostic.md`.

### MSC point input topology and stream flags (2026-10-28)

A test-only MSC proxy set `IRCompilerSetInputTopology` after enabling GS/TS
emulation. Both point and patch settings preserved the factor-1 and factor-2
point failures; their four generated point-domain AIR modules were byte-identical
with the default conversion. The line-image control passed in each setting.
This experiment sets a topology on all emulation-stage compilers and does not
exclude other per-stage configurations. Setting point globally also broke an
ordinary triangle pipeline, so it is not a safe general substitution.

For the factor-2 triangle diagnostic, probing the two tessellator flag fields
showed that field 3's low bit selected six distinct boundary color codes while
omitting the center; field 4 also selected duplicate boundary codes. This
quantized, single-factor observation does not establish general flag semantics.
Neither bit alone provides the required unique-point stream. Point output and
previously listed feature gaps remain OPEN; no product code was changed.
Reproduction, hashes, validation and limitations:
`../dxmt-test/n11-msc-ts-point-topology-20261028/N11-input-topology-and-flags.md`.

### MSC point factor-2 per-position regression (2026-10-28)

The optional `tess-point-grid-factor2` probe separates the six boundary points
and the center into seven image rows. Additive floating-point output checks the
full domain-coordinate vector and raster count per row, with a zero sentinel.
A seven-point ordinary point-list control passed three submission/replay/reset
passes in private and no-private configurations; a deliberately wrong reference
and an eighth point duplicating the center failed as intended. Metal API/GPU
validation and PE/native path checks were enabled. Both line controls and point
rejection controls passed.

The test-only DLL without the guard failed: row counts for one tessellated draw
were (1,10,1,1,1,1,0), omitting the center and repeating one boundary point.
This records rasterized outputs, not domain-shader invocation side effects.
Actual point support and broader factor/domain/lifetime coverage remain OPEN.
Reproduction, preliminary controls and exact artifacts:
`../dxmt-test/n11-msc-ts-point-grid-20261028/N11-point-grid.md`.

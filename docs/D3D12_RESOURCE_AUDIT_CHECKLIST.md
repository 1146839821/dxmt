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

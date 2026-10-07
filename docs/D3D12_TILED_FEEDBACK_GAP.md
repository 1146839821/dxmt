# Tiled raw/structured feedback production gap

Baseline cc665176. Task Analysis: prioritize a real Tier 2 implementation gap,
not another complete matrix rerun. Packed array tails are not the next missing
feature: the existing packed audit records Tier 2's rejection of substandard
array mips. Do not widen that normative boundary. Single-slice packed-tail
translation still requires compatible D3D/Metal tail boundaries and byte sizes.

The next identified gap is raw/structured sparse access status. Current texture
feedback reaches AIR sparse residency and StoreFeedback canonicalizes the opaque
status for CheckAccessFullyMapped. InstLoadRaw and InstLoadStructured have no
feedback destination or residency input; ordinary pointer loads cannot manufacture
a Metal texture sparse-access status. The Tier 2 gate therefore correctly marks
this path DESIGN_REQUIRED / BLOCKED_BY_ARCHITECTURE.

Hypothesis: a shader-visible mapping sideband can represent the residency of the
actual byte footprint of a raw/structured access. This requires more than accepting
an opcode: identify every 64KiB tile touched by the instruction's components and
view origin, guard OOB/overflow, then return mapped status only when every touched
tile is mapped. TGSM and ordinary committed buffers must remain distinguishable.
Do not substitute a constant success flag or a CPU-time snapshot for GPU ordering.

Required implementation seams: resource mapping state ownership; descriptor/root
transport of sideband identity and view-relative offsets; AIR raw/structured
operation decoding/lowering; queue-ordered sideband updates matching sparse mapping;
completion-owned lifetime across repeated submission and cross-queue remapping.
DXIL needs an independent MSC-compatible solution; DXBC support alone cannot close
Tier 2. Keep feedback consumers rejected until all these seams are connected.

Expected effect: real mapped/NULL/mixed byte-footprint feedback without changing
shader-family routing or promoting TiledResourcesTier. Risk: stale mapping
generations, tile-straddling vector loads, allocation churn and unsafe dynamic
root provenance. First proof must include mapped and NULL accesses with identical
numeric zero data (to prevent value-based status inference), a tile boundary
straddle, nonzero view origin, and remap ordering. This is source-backed task
preparation, not implementation or GPU acceptance.

## Byte-footprint prerequisite

Added sparse_buffer_footprint.hpp: bounded component-mask geometry computes
unique resource-relative 64KiB tile indices after view-origin translation.
Per-component four-byte accesses are checked independently; masked-out bytes do
not create tiles. OOB, empty/invalid masks and arithmetic overflow reject without
publishing a partial result. No residency/status decision is made here.
Native unit tests pass in both configured builds, including all 15 nonempty masks,
view-origin boundary crossing, unaligned component crossing, mask selectivity,
invalid view bounds and overflow. Main self-review confirms the maximum output
capacity is four components times two tiles; end-address arithmetic is bounded
by the validated resource/view range. Queue-ordered GPU mapping sideband,
descriptor transport, actual AIR/DXIL lowering and GPU mapped/NULL oracles remain
unimplemented. This prerequisite is not production feedback admission or Tier 2
qualification; current shader feedback rejection remains unchanged.

## Native queue write prerequisite

The current sparse-mapping handle is an MTL4CommandQueue. SDK-confirmed
MTL4ComputeCommandEncoder fillBuffer writes permit GPU-ordered sideband updates
on that queue. The opt-in native sparse_sideband_queue prototype brackets writes
with ResourceState-to-Blit consumer ordering and Blit-to-All device visibility,
retains command allocator/resource/residency through a shared-event completion,
and alternates two independent bytes over eight phases. Both builds compile and
both native runs pass (`sparse-sideband-queue-{normal,np}.log`).

This proves the buffer-write primitive and completion visibility only. No sparse
mapping operation or shader feedback consumer is in this prototype; the producer
ResourceState dependency has no matching mapping workload yet. It must not be
credited as atomic map/status coupling, cross-queue remap correctness or production
feedback admission. Next connect actual mapping operations and sideband ownership
before adding the optional Wine boundary and AIR descriptor transport. The Metal
resource/synchronization skills guided explicit residency, stage barriers and
allocator lifetime; the native target is not built by default.

## Actual mapping prerequisite result

The native target now allocates a two-page placement sparse buffer plus a
compatible placement heap and includes both in its residency set. Each phase
maps one virtual page, unmaps the other, alternates the physical heap page,
then writes sideband bytes and a phase-distinct payload on the same queue.
ResourceState-to-Blit uses ResourceAlias visibility; dependent buffer readback
uses an explicit Blit-to-Blit device barrier. Both native builds pass eight
phases (`sparse-sideband-map-{normal,np}.log`), with numeric 0x40+phase data checks
in addition to sideband bytes. Resources and allocator survive the GPU signal.

This supersedes the earlier no-mapping probe only for native mapping/write
ordering. The probe serially waits each phase and copies only the mapped page;
NULL-page shader access, cross-queue overlap, raw/structured status, descriptor
transport, ABI versioning and production integration remain unfinished.
Main self-review: uses SDK tile units for bufferRange/heapOffset; checks support,
residency, exact independent per-phase data and status expectations; no CPU bitmap
write substitutes for GPU updates. No production admission, prefix deployment,
game test or capability promotion follows. These prerequisites are ready for a
local checkpoint commit, not completion of the sparse-feedback task.

## Optional native/Wine sideband entry point

Hypothesis: reuse the proven queue write primitive through an independently
appended Unix call (199), preserving the old mapping ABI. Expected effect:
GPU-written one-byte-per-64KiB-tile mapping state without a CPU snapshot.
Risk: asynchronous object lifetime, overlapping operation ordering and mixed
runtime versions. Validation: compile both variants and run the shared native
helper with numeric mapping/readback checks before integrating shader consumers.

The new entry point returns submission success/failure, prepares command buffer,
allocator and private per-command residency before changing mappings, validates
range arithmetic, and orders overlapping sideband writes. A completion feedback
block retains the command, allocator, residency and resource objects. Callers
must serialize the mapping queue and supply preceding resource-state barriers
and cross-queue synchronization. The internal contract requires a compatible
64KiB placement sparse buffer/heap; it is not a general Metal sparse API.

The native probe now calls the same helper as the Unix entry. No production
D3D12 caller imports the new export yet: integration must dynamically resolve it
for old-runtime compatibility. Resource-side ownership, copy-mapping updates,
descriptor transport, AIR/DXIL lowering and shader feedback admission remain
unfinished. No capability promotion.

Validation result: both reconfigured full builds pass, both native probes pass
the eight mapping/remapping data and sideband oracles, range/NULL-input rejection
and a final NULL-heap whole-buffer unmap. The same shared helper also passes a
standalone MRC build/run, matching the Unix runtime's ownership mode. Both
cache-only Wine probes dynamically resolve the export and pass eight GPU-written
sideband phases through the PE/Unix call. Both host suites pass 13/13.
Evidence: `dxmt-reconciliation.ZLDvwE/sideband-api-{build,probe-build,native-build,
wine}-{normal,np}.log` and `sparse-sideband-api-mrc-native.log`.

The first Wine attempt found the new export missing despite the app-local DLL
containing it; updating the isolated runtime's PE counterpart alongside its
Unix `.so` made the probe pass. Match both halves when deploying; app-local
path output alone does not prove the builtin export table was refreshed.
No game/prefix DLL replacement, game launch, process management or push occurred.

Main self-review checks appended table order (including WOW64), fixed-width
pointer transport, submission failure propagation, overflow-safe tile bounds,
no map mutation before object preparation, and completion-owned MRC lifetime.
The tests do not prove error-injection recovery, overlapping-map last-wins,
cross-queue remap overlap or shader status semantics. Next connect resource-side
ownership and both update/copy mapping paths before descriptor/AIR integration.

## Resource-owned GPU mapping sideband

Task Analysis / Hypothesis: reserved buffers need a stable, resource-owned
allocation updated in mapping queue order, including CopyTileMappings. CPU
bookkeeping is only used to retain copied backing heaps, never to reconstruct
the shader mapping bytes. Expected effect: immutable sideband identity for
future descriptor transport, with GPU update/copy semantics. Risk: transient
copy allocation and heap lifetime, old runtime pairing, copy failure publication.
Validation: both builds, shared native mapping/data/copy probe, public D3D12
queue update/copy probe inspecting GPU-written bytes only after a fence.

The copied byte range is staged on the GPU before destination writes. Unix call
200 preserves prior table indices; copied backing heaps are included in its
completion-owned residency/lifetime. Reserved buffers initialize sideband to
unmapped once before GPU use, register residency without registering a D3D VA,
and enable this path only when both optional exports resolve. Existing older
runtime mapping behavior remains available without a sideband. Failed native
submission does not publish destination CPU mapping bookkeeping. Descriptor,
AIR/DXIL lowering and feedback admission remain unchanged and unfinished.
Validation results: reconfigured normal/no-private full builds pass. Both native
probes pass mapping/remap/copy byte and payload checks, including a right-shifted
overlapping mapping's real page data. The shared helper also passes the MRC
standalone probe. Both public D3D12 Wine probes pass eight update/copy phases
plus overlapping copies in both directions, with GPU-written mapping bytes
checked after the main-queue fence. Both host suites pass 13/13. Native API
validation runs explicitly report Metal API Validation Enabled and pass without
API errors in both variants. Shader validation and feedback shader tests were
not run because shader transport/lowering is not yet connected.

An isolated older matched winemetal PE/Unix pair with neither new export passes
the normal build's legacy probe (export absence, queue completion, resource
creation and CPU bookkeeping only). This does not qualify the older runtime's
GPU sparse data or overlapping-copy semantics. Current PE/Unix runtime halves
must remain matched. Evidence is under `dxmt-reconciliation.ZLDvwE`:
`resource-sideband-{build,overlap-build,final-build,wine,native,api-validation}-{normal,np}.log`,
`resource-sideband-legacy-normal.log`, and `sparse-sideband-copy-mrc-native.log`.

Main self-review: stable resource ownership and one-time zero initialization;
inaccessible native pointers use the existing native updateContents bridge for
initialization without adopting CpuPlaced external-memory lifetime (32-bit
runtime is not tested here);
no new mandatory imports; fixed 48/64/80-byte appended thunk layouts; overflow-safe
range validation; private completion-owned residency for copied backing heaps,
byte scratch and mapping scratch; native failure before CPU mapping publication.
The Metal resource/synchronization skills guided explicit lifetime and barriers;
the validation skill guided API-only diagnostics. No game/prefix deployment,
process management, push, shader feedback admission or capability promotion.
The prerequisite producer barrier also now retains its own command allocator
through completion instead of releasing it immediately after commit.
Allocation-error injection, multi-queue overlap and full shader matrices remain
unqualified. Next connect descriptor/root transport and actual access lowering.

[D3D12 CopyTileMappings](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-copytilemappings)
requires snapshot-like results when source and destination overlap. The new
native copy helper stages both mapping and sideband in that case, rather than
depending on an undocumented Metal overlap guarantee. The native probe checks
the shifted mapping's actual page data; the D3D12 probe checks both overlap
directions' GPU-written bytes. This is distinct from legacy compatibility,
whose probe checks export absence, resource creation, queue completion and
CPU bookkeeping only, not GPU sparse status or overlap mapping correctness.

Final residency review: capture unique old backing heaps under the resource
mapping lock, retain their private COM identity until native submission, then
keep them in native completion-owned residency until the remap finishes. Copy
retains both copied source heaps and replaced destination heaps. Update uses
an independent retained variant (Unix call 201 / 64-byte layout); the previously
published call 199 remains ABI-compatible. Sideband allocation requires the
retained-update and copy exports. Mutable completion containers explicitly
release transient objects at callback time. Fresh final normal/no-private builds,
native API validation, MRC and public D3D12 Wine API validation runs all pass
with alternating distinct backing heaps. Host suites again pass 13/13 each.
Evidence: `resource-sideband-retained-build-{normal,np}.log`,
`resource-sideband-wine-api-validation-{normal,np}.log`,
`resource-sideband-api-validation-{normal,np}.log`, and
`sparse-sideband-copy-mrc-native.log`. In-flight old/new heap overlap remains a
broader GPU regression matrix item, not inferred from this serial phase probe.

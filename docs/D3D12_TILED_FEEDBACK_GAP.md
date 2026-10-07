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

## AIR descriptor transport

Hypothesis: an immutable resource-level header in AIR raw/structured descriptor
word 3 can carry resource base VA, actual byte size and the live GPU mapping
array. View origin is then pointer minus resource base, preserving nonzero
views without per-view CPU mapping snapshots. Evidence: AIR buffer descriptors
are four qwords, with word 2 reserved for UAV counters and word 3 unused; MSC
has a separate three-qword entry and must remain untouched. Expected effect:
real descriptor transport with allocation-owned header/bitmap lifetime.
Risk: stale descriptor reuse, padded reserved allocation size, auxiliary resource
residency and partially published VA metadata. Validation: inspect raw and
structured views, counter/MSC invariance, overwrite/copy behavior and retained
allocation metadata in the isolated resource probe. Root shader transport,
feedback decoding/lowering and admission remain unfinished.

Implementation: allocation-owned header and mapping byte references are
immutable before VA publication; failed initialization does not unregister an
unpublished primary allocation. AIR raw/structured SRV/UAV entries populate
word 3, preserve UAV counter word 2, and clear both auxiliary words when an
ordinary/null SRV replaces the entry. MSC entries remain exactly three qwords.
Descriptor copy retains the source allocation and its auxiliary references.
Existing record/submission resource fan-out declares header and bitmap read-only
outside the heap/VA locks. Root residency is prepared, but root shader transport
has not been added. Static descriptor flags, pending-use eligibility and live
submission reread policy are unchanged.

Validation: both reconfigured full builds and host suites (13/13 each) pass.
Both cache-only Wine descriptor probes, with API validation visibly enabled,
check nonzero raw SRV and structured UAV origins, actual 65552-byte resource
width versus 131072-byte sparse allocation, counter preservation, MSC entries,
descriptor copy/overwrite, and allocation-owned header/bitmap after public
resource/heap release. Both prior public sideband update/copy/overlap probes
also pass. Evidence: `sparse-descriptor-{build,wine,sideband-regression}-{normal,np}.log`
under `dxmt-reconciliation.ZLDvwE` and the build host test logs.

Main self-review checks acyclic allocation ownership, publish ordering, header
size/offset assertions, counter/MSC separation, ordinary/null overwrite reset,
and read-only auxiliary resource declaration. The binding-model skill guided
the independent AIR/MSC layout; API validation guided diagnostics. No feedback
shader was compiled or executed: the protocol probe is CPU-visible descriptor
and lifetime evidence, not a shader access-status oracle. Raw/structured feedback
opcodes still fail closed in SM50Initialize; no Tier 2 or capability promotion,
game deployment, process management or push occurred. Root transport, actual
byte-footprint lowering, DXIL support and GPU feedback matrices remain required.
# Raw/structured feedback decoder checkpoint

## GPU mapping lookup lowering checkpoint

### Compute root feedback transport

Hypothesis: an independent submission-owned VA lookup table can serve root
feedback without changing application root arguments or ExecuteIndirect stride.
Expected effect: direct compute root SRV/UAV feedback uses the same bitmap
lowering as descriptor tables. Risk: omitted residency/lifetime, binding-slot
collisions, excessive lookup cost, and ICB buffer inheritance differences.

The AIR compute root binding now adds a private buffer input at slot 8 only
when a root SRV/UAV consumes feedback. Its qword count is followed by 24-byte
VA/size/header rows; a bounded GPU loop finds the containing allocation and
computes remaining bytes. The host conservatively marks AIR compute pipelines
with any buffer-feedback consumer, snapshots registered allocations per encoder
at submission, fills a separate shared buffer outside the registry lock, declares
table/main/auxiliary resources, and keeps table plus source references until
completion. Application root argument and MSC layouts/strides are unchanged.
Table-only feedback pipelines currently also incur the snapshot/table cost;
exact root-only host eligibility and linear-search performance remain to refine.

Normal/no-private full builds and 15 host tests pass. Both runtimes pass root
SRV and root UAV raw/structured GPU data/status oracles at VA + 65532 over four
serial mapping alternations; table-UAV regressions also pass. Evidence:
`dxmt-reconciliation.ZLDvwE/root-feedback-{srv,uav}-{normal,np}.log` and
`root-feedback-table-regression-{normal,np}.log`. No API-validation run or
inflight/multiqueue/VA-alias lifecycle qualification is claimed here.

At this checkpoint graphics/geometry/tessellation root feedback still rejected
compilation; the later graphics transport below changes that boundary. The
compute ICB transport removes its earlier conservative recording gate;
no tiled Tier2/feature-level promotion occurred.

### Indirect compute root feedback transport

Hypothesis: a submission-owned resolver payload can supply slot 8 to compute
ICBs without mutating recorded commands or application indirect arguments.
Expected effect: root-updating ExecuteIndirect uses the direct compute VA table.
Risk: replacing the resolver binding loses implicit allocator-heap residency;
payload ownership, ICB binding and repeated submission must remain isolated.

The private compute resolver payload grows from 120 to 128 bytes, with the
table address at offset 120 (host assertions and matching MSL declaration).
Application ByteStride, root UploadQwords and MSC TLAB layouts are unchanged.
Submission replay copies the payload into a retained shared buffer, replaces
only its resolver binding, and explicitly declares the original allocator GPU
heap read/write: the resolver still writes root arguments into that heap.
AIR root-updating ICBs explicitly bind the table at slot 8. MSC routing is
unchanged, and recorded nodes/payloads remain immutable.

Diagnostic negative evidence: both inline and shared payload carriers without
the original heap declaration failed data/status outputs and the independent
dispatch sentinel. Changing the carrier alone did not fix execution. Adding
the heap declaration made the same oracle pass. Temporary diagnostics were
removed before final builds; the shared carrier gives submission ownership,
not a measured performance improvement.

Normal/no-private full builds and 15/15 host tests each pass. Both runtimes
pass indirect root SRV/UAV raw/structured data/status checks through four serial
mapping alternations. The argument stream uses VA + 65532 while recording
stages VA + 65536, so missed root updates fail independently. Output includes
a nonzero dispatch sentinel to detect nonexecution. The normal SRV final run
passes with explicit Metal API Validation enabled. Existing ordinary indirect
root VA remap regression passes with 317/619 in both runtimes.
Evidence: `dxmt-reconciliation.ZLDvwE/root-feedback-indirect-{srv,uav}-{normal,np}.log`,
`root-feedback-indirect-ordinary-regression.log`, and
`root-feedback-indirect-ordinary-np.log`.

This tests MaxCount=1 with CPU-uploaded arguments and GPU resolver/ICB execution,
not GPU-produced arguments, count buffers, predication, concurrent resubmission,
inflight remap or multiqueue lifetime. Graphics/GS/tessellation and DXIL feedback,
VA alias coverage, full sparse matrices and allocation/lookup performance remain
open. No game/prefix deployment, process management or push was performed.

### Graphics root feedback transport

Hypothesis: reuse the compute VA/header table for render encoders and make its
private AIR input available to every graphics stage. Expected effect: root
loads with feedback no longer fail solely because they are outside compute.
Risk: synthesized GS/tessellation functions share input signatures, render ICBs
do not inherit bindings with root updates, and residency stage masks must cover
object/mesh consumption as well as ordinary vertex/fragment consumption.

Root binding setup now defines slot 8 for any stage with a consuming root
SRV/UAV. FunctionSignatureBuilder already deduplicates ArgumentBindingBuffer
inputs by location, so combined signatures reuse the same input index. AIR
graphics reflection accumulates the existing buffer-feedback flag with an
allocation-failure boundary. PreDraw marks the encoder; submission reuses the
VA snapshot/table lifetime and binds vertex/object/mesh/fragment buffers, with
resource-use stages extended to pre-raster when emulation is present. Application
root and MSC layouts stay unchanged. Like compute, host eligibility currently
includes table-only consumers and its performance cost remains unmeasured.

The earlier compiler-stage restriction is removed. At this checkpoint
root-updating render ExecuteIndirect still failed recording; the transport
below replaces that temporary gate. No capability or Tier 2 promotion follows
from this change.

Normal/no-private full builds and 15/15 host tests each pass. New `--pixel`
SRV/UAV probes execute a full-screen triangle in a 1x1 viewport/scissor without
color attachments, writing 12 independently checked UAV words. Raw/structured
loads use root VA + 65532, zero mapped/NULL payloads, four serial mapping
alternations and a nonzero execution sentinel. All four probes pass. No-private
UAV additionally passes with explicit Metal API Validation enabled and no API
errors observed. Evidence: `dxmt-reconciliation.ZLDvwE/root-feedback-graphics-{srv,uav}-{normal,np}.log`.

VS/GS/HS/DS consumption and shared synthesized signatures are source-reviewed,
not GPU-qualified by the PS probe. Stage-specific GPU oracles, render ICB
transport, DXIL feedback, inflight/multiqueue/alias matrices and performance
remain open. The binding integration review preserves AIR/MSC separation;
API validation does not establish complete GPU memory correctness.

### Render ICB root feedback transport

Hypothesis: apply the compute submission-owned payload pattern to the render
resolver, then explicitly bind slot 8 in generated AIR vertex/fragment ICBs.
Expected effect: root-updating ordinary indirect draws consume the same table
as direct graphics. Risk: lost implicit allocator-heap residency, stale payloads
on resubmission, and interference with MSC private replay markers.

The private render payload now has 184 bytes, with root_feedback_table at
offset 176 and matching host assertions/MSL fields. Application indirect stride,
root UploadQwords and MSC TLAB layouts are unchanged. When any root/VB/IB update
disables ICB buffer inheritance and AIR feedback is present, recording stores
a resolver marker. Submission snapshots the VA table and creates a retained
shared payload copy. Replay declares the original heap read/write for vertex
and fragment consumption, changes only the copied binding, rejects conflicting
or unmatched markers, and leaves original commands/payloads untouched. AIR ICBs
explicitly bind slot 8 for vertex and fragment; MSC branch routing is unchanged.
The former root-feedback recording rejection is removed. Existing GS/tessellation
indirect signature limitations are not bypassed by this ordinary render change.

GPU oracle `--pixel --indirect` uses two root updates followed by Draw with
32-byte application stride, MaxCount=1 and CPU-uploaded arguments. Recording
VA + 65536 differs from argument VA + 65532; four serial remaps, independent
data/status expectations and the nonzero dispatch/draw sentinel remain active.
Normal/no-private SRV/UAV pass; normal UAV additionally passes with explicit Metal API
Validation enabled and no API errors observed. Both full builds and 15/15 host
tests each pass; no-private indirect compute UAV regression also passes.
Evidence: `dxmt-reconciliation.ZLDvwE/root-feedback-render-icb-*`.

This does not qualify DrawIndexed, VB/IB-only updates, count buffers, predication,
GPU-produced arguments, multiple commands, concurrent resubmission, inflight
remap or VS/GS/HS/DS feedback consumption. Those matrices and DXIL feedback
remain open; no aggregate capability promotion or game deployment occurred.

### Vertex-stage root feedback oracle

The `--vertex` probe moves feedback consumption and the independent 12-word
data/status/sentinel output into a VS compiled as vs_5_0. Only SV_VertexID 0
writes the output, avoiding repeated writes from other vertices. No pixel shader
is present; the VS returns a triangle position after the feedback checks. Both
direct Draw and root-updating ExecuteIndirect use graphics roots. The latter
retains the different recorded/argument VAs and 32-byte draw stream, checking
actual vertex ICB slot 8 rather than inferring it from the PS oracle.

Normal/no-private full builds and 15/15 host tests each pass. Both runtimes'
direct and indirect SRV/UAV GPU probes pass all four serial remaps. Evidence:
`dxmt-reconciliation.ZLDvwE/root-feedback-vertex-*`.
These are zero-payload raw/structured root-load tests, not complete stage or
sparse qualification. No new runtime implementation was necessary for the
tested VS path. GS/HS/DS and synthesized shared-signature consumption remain
unqualified, alongside the outstanding indirect/lifetime/DXIL/performance
matrices. No API/shader-validation run is claimed for this checkpoint.

### Geometry-stage root feedback oracle

The `--geometry` probe independently compiles the consuming GS as gs_5_0. A
single input triangle invokes the GS once; it performs the raw/structured loads,
writes the 12-word data/status/sentinel output, then appends three vertices with
explicit SV_Position semantics. The helper VS consumes no feedback and no PS
is present. This exercises the AIRCONV GS synthesized path rather than ordinary
VS/PS binding. The first fixture used a bare float4 stream and was rejected by
D3DCompile for missing output semantics before DXMT execution; the committed
fixture uses a semantic-bearing structure.

Normal/no-private full builds and 15/15 host tests each pass. Both runtimes pass
GS root SRV/UAV GPU checks across four serial mapping alternations at VA + 65532,
with zero mapped/NULL payloads and independently expected status and execution
sentinel. Evidence: `dxmt-reconciliation.ZLDvwE/root-feedback-gs-*`.
The tested direct GS consumer required no further runtime implementation change.

Root-updating GS indirect signatures remain unsupported and the probe rejects
that combination explicitly rather than reporting false coverage. HS/DS,
joint VS/GS feedback/shared-input deduplication, stream output, other topology
and instance combinations, inflight/multiqueue lifetime, DXIL and the broader
sparse/performance matrices remain open. No API/shader-validation run, complete
geometry conformance or game acceptance is claimed here.

### Hull/domain-stage root feedback oracles

`--hull` and `--domain` independently compile the consuming stage as hs_5_0 or
ds_5_0, pairing it with a non-consuming companion. The fixture reuses the
repository's triangular, three-control-point, integer-partitioned tessellation
shape and factors of one. Hull control point 0 writes the 12-word output; the
domain stage writes only at the exact corner (1,0,0), avoiding repeated writes
from the other invocations. No PS is present. Data, mapping status and nonzero
execution sentinel are checked independently after a fence, so these checks
prove HS/DS consumption rather than inferred PS or GS success.

Normal/no-private full builds and 15/15 host tests each pass. Both runtimes'
HS/DS root SRV/UAV probes pass all four serial remaps at VA + 65532. Evidence:
`dxmt-reconciliation.ZLDvwE/root-feedback-tess-*`.
The tested direct consumers require no additional runtime implementation change.

HS fork/join or patch-constant feedback consumers, joint-stage feedback/input
deduplication, other tessellation domains/partitioning/factors, multiple patches,
instances, indirect root updates, inflight/multiqueue/alias semantics, DXIL and
the broader sparse/performance matrices remain open. No API/shader-validation
run, full tessellation conformance or ROTTR visual/performance acceptance is
claimed by these zero-payload root-buffer status tests. The CLI explicitly
rejects conflicting stage selectors and unsupported tessellation indirect mode.

### Exact root-feedback host eligibility

Hypothesis: shader-wide BUFFER_FEEDBACK reflection over-admits table-only
consumers, causing avoidable submission VA snapshots, tables and private ICB
payloads. Expected effect: request root transport only for a visible consuming
root SRV/UAV, without changing table feedback or ordinary indirect VA retention.
Risk: reflection exposes range IDs rather than full register/space; a host-only
slot comparison can incorrectly suppress SM5.1 root consumers.

New read-only AIR query SM50UsesRootBufferFeedback matches decoded consuming
resources against root descriptors by register, space, type and stage visibility.
It uses the same matching conditions as root binding setup, not reflection
range IDs. The query validates DXBC length and serialized parameter/table/sampler
extents and alignment before deserialization, returns 0/1 or -1, and does not
mutate the shader. Compute and all graphics stages query their resolved explicit
or embedded root; query failure rejects PSO initialization rather than silently
disabling transport. Existing root argument, reflection and MSC ABIs are unchanged.
The PE/native bridge appends entry 202 in both tables with a size-checked wow64
payload; matched new winemetal PE/native binaries are required. wow64 execution
is source-reviewed, not runtime-tested here.

Table-only direct feedback now avoids the root VA/table submission path.
Indirect root VA snapshots/retention remain independently enabled when needed;
only unnecessary feedback table/private payload work is suppressed. No FPS,
allocation count or full performance benefit has been measured.

The native query oracle deliberately uses range IDs different from registers,
nonzero space, visibility/type/usage mismatches, a real descriptor table, RS1.0,
no parameters and invalid lengths/payloads. An initial empty-table fixture was
rejected by the existing deserializer and was replaced with a valid populated
table. Both full builds and 16/16 host tests pass. Normal table SRV, indirect
compute/PS root UAV, indirect VS root SRV, and direct GS/HS/DS root SRV GPU
regressions pass. No-private table SRV, indirect compute root UAV and direct DS
root UAV also pass; this is a targeted regression subset, not a full stage matrix.
Evidence: `dxmt-reconciliation.ZLDvwE/root-feedback-select-*`.
SM5.1 matching is tested against decoded records, not a compiled SM5.1 GPU
fixture; broad visibility/alias/concurrency/indirect-emulation and sparse matrices
remain open. No API/shader-validation run or capability promotion is claimed.

### AIR geometry/tessellation indirect count and predication

Hypothesis: reuse the existing GPU predication-count filter and gate marshal
dispatch output from a GPU count pointer, removing the unconditional count/
predication rejection without a CPU readback. Expected effect: single-command
GS/tessellation ExecuteIndirect honors count and predicate. Risk: changing the
shared D3D11 marshal ABI, losing count-buffer residency or missing compute-to-
render synchronization, and stale output falsely passing skipped-draw checks.

The original 32-byte GS/TS task layouts and entry names remain intact. Their
arithmetic is shared with new counted entries consuming a private 40-byte task
(base task plus count GPU pointer). A zero count emits zero mesh dispatch;
nonzero count uses the existing arithmetic. Command-list PSO caches distinguish
counted/ordinary functions. Count/predicate support retains MaxCommandCount=1
and non-updating signatures; no root/VB/IB update or multi-command rejection is
bypassed. Predication uses the existing compute filter before PreDraw and the
existing encoder/fence transition. The marshal declares the actual source count
or filtered allocator buffer read-only for the vertex stage.

The `--counted` fixture uses a 16-byte non-updating Draw stream with count values
0/1/0/UINT_MAX; `--predicated` independently tests predicate rejection, count
rejection, execution and predicate rejection with count UINT_MAX. Each phase
clears output before setting the predicate, and skipped draws must leave all
12 words zero, including the execution sentinel. Active phases must match the
independent sparse data/status expectations. Count/predicate uploads are CPU
written between completed submissions; consumption and filtering are GPU-side.

Both full builds and 16/16 host tests pass. Normal GS/HS/DS count-only and
predicate/count SRV probes pass. No-private GS count-only, GS/HS predicate/count SRV and DS
predicate/count UAV pass; DS additionally passes with explicit Metal API
Validation enabled and no API errors observed. Evidence:
`dxmt-reconciliation.ZLDvwE/emulation-{count,predicate}-*`.

DrawIndexed, count offsets, GPU-produced counts, predicate-without-count-buffer,
NOT_EQUAL_ZERO, concurrent/inflight updates, D3D11 runtime marshal regression,
multi-command execution, indirect root updates and broad topology/tessellation
matrices remain unqualified. No shader-validation, full predication/geometry/
tessellation conformance, capability promotion or game acceptance is claimed.

Selective root-transport prerequisite: raw/structured decoder uses now accumulate
`buffer_feedback` on the corresponding SRV/UAV only when the status destination
is non-NULL. AIR argument reflection publishes this as the previously unused
bit 8, `MTL_SM50_SHADER_ARGUMENT_BUFFER_FEEDBACK`, without expanding either
reflection structure. Ordinary loads and NULL status do not request the flag.
This provides usage information for a future submission VA/header table without
forcing one on every buffer shader. Root lookup/table binding itself is not yet
implemented. Expanded decoding tests cover raw/structured, SRV/UAV, normal/
feedback, and NULL/non-NULL status combinations. Normal/no-private full builds
and all 15 host tests pass; focused tests also pass after formatting. Reflection
flag publication is source-reviewed, not independently runtime-asserted here.
No capability promotion or public root/MSC ABI change occurred.

Indirect root residency prerequisite: submission snapshots already strongly
retain registered primary allocations, but their encoder fan-out previously
declared only the primary buffer. It now also declares attached sparse header
and mapping allocations as read-only for render/compute encoders, after the
registry lock is released. Primary buffers retain their conservative read/write
usage. The submission's source allocation reference owns both auxiliary
allocations through completion; no root/MSC ABI or indirect stride changed.
Normal/no-private full builds, all 15 host tests, and existing indirect root VA
same-address-remap oracles pass (317/619) in both runtimes. These ordinary-buffer
oracles are regression evidence, not a GPU oracle for the new auxiliary branch.
Root shader header lookup and sparse indirect feedback execution remain open.
Evidence: `dxmt-reconciliation.ZLDvwE/indirect-sparse-aux-{build,remap}-{normal,np}.log`.

UAV execution follow-up: the buffer feedback oracle now accepts `--uav`, compiles
RWByteAddressBuffer/RWStructuredBuffer feedback at u1/u2, creates matching raw and
structured UAVs, and uses UAV resource states. Disassembly confirms raw and
structured feedback instructions actually read u1/u2. All four alternating
mapping phases pass for both normal and no-private runtimes; default SRV mode
also passes again in both. Evidence is in
`dxmt-reconciliation.ZLDvwE/buffer-feedback-uav-wine-{normal,np}.log` and
`buffer-feedback-uav-srv-regression-{normal,np}.log`. This validates serial
compute UAV reads only, not concurrent reads/writes or UAV feedback in other
stages. Root-header transport remains open: AIR root argument uploads currently
share their stride with ExecuteIndirect, so extending their layout requires
coordinated direct/indirect changes rather than appending bytes only at direct
binding sites. MSC root entries remain unchanged.

GPU oracle follow-up: `tests/dx12/dx12_buffer_feedback.cpp` now executes raw
Load/Load2 and structured Load with feedback in both normal and no-private
cache-only Wine runtimes. Native D3DCompile disassembly is checked for
`ld_raw_s`, `ld_structured_s`, and `check_access_fully_mapped`, preventing ordinary
loads from standing in for feedback coverage. The two views start at byte 65532;
two scalar addresses select opposite tiles and Load2 crosses the 64KiB boundary.
Each of four serial phases alternates which tile maps the same physical page,
initializes the mapped page to zero, and checks all eleven GPU outputs after
queue completion. Mapped and NULL data are both zero while status distinguishes
them; the cross-boundary status is false in every phase. Both builds pass.
Evidence: `dxmt-reconciliation.ZLDvwE/buffer-feedback-wine-{normal,np}.log`.
This is SRV compute data/status execution evidence, not inflight/multiqueue,
UAV/root/DXIL, ordinary/OOB, all masks/swizzles, or full Tier2 qualification.
No game/prefix DLL replacement or process restart occurred.

Hypothesis: the retained resource header and queue-ordered GPU mapping bytes
can provide raw/structured status independently of payload values. Expected
effect: descriptor-table feedback loads no longer stop at decoding. Risk:
incorrect component footprints or speculative zero-address loads could corrupt
status or fault; validation starts with typed-pointer LLVM verification before
GPU execution qualification.

The converter now branches on the live header address, reads its four immutable
qwords, derives the view origin from the payload pointer, and checks every source
component selected by destination mask/swizzle. It uses the same i32 word-index
arithmetic as data lowering, checks view/resource/tile bounds, and branches before
reading a volatile mapping byte. A zero runtime header represents an ordinary
buffer; missing compiler-side header support (root/D3D11/TGSM) still rejects
feedback explicitly. Out-of-view components do not inspect any tile. That status
policy still requires GPU/reference qualification, as do structured overflow and
offset cases. The implementation does not infer mapping from payload zeroes.

Normal/no-private full builds and all 15 host tests pass. The new LLVM verifier
test covers all 15 nonempty component masks and missing-header rejection; it
does not execute the generated shader or prove mapping status values. Its first
run crashed because its LLVM context omitted the production typed-pointer
setting; adding `setOpaquePointers(false)` fixed the fixture. Fresh focused
decode/IR reruns pass in both builds after the x86 execution wait cleared.
Root header transport, GPU shader data/status oracles, DXIL feedback, and the
full tiled-resource qualification matrix remain open. No capability promotion,
game deployment, or game performance/visual acceptance occurred here.

Reference: [D3D11.3 resource access specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm),
sections 22.4.10 and 22.4.12 define mask/swizzle-selected buffer components and
optional status; section 5.9.4.5 defines mapped-status consumption.

Shader binding follow-up: AIR descriptor-table raw/structured SRV and UAV
bindings now load qword 3 (32-byte descriptor stride) into
`BufferDescriptor::SparseFeedbackHeader` and propagate it to the converter's
read handle. D3D11, TGSM, and root descriptors retain a null compiler-side field;
a table descriptor can instead carry the runtime integer zero for an ordinary
buffer. Lowering must distinguish these cases and branch before dereferencing
zero. CBV/counter and MSC three-qword descriptor layouts are unchanged.
Normal/no-private full builds and 14 host regressions pass. This is source and
build evidence only: the existing tests do not execute a shader header load,
and non-NULL feedback compilation still fails explicitly. Root transport,
header dereference, byte-footprint checks, and GPU bitmap lookup remain open.

Hypothesis: normalize feedback loads into the existing raw/structured load IR
while retaining the separate status destination, so later residency lowering
can share the data-load implementation without losing status semantics.
Evidence: feedback variants insert a status operand before the address operands.
Expected effect: correct operand indices and resource-read tracking, including
NULL status destinations. Risk: decoding alone must not admit uninitialized
status outputs. Validation: explicit lowering failure precedes every data-load
path when a status destination is present; NULL status uses ordinary load lowering.

The decoder now handles `LD_RAW_FEEDBACK` and `LD_STRUCTURED_FEEDBACK` and retains
their optional integer status destination. Non-NULL status still produces an
explicit unsupported-feature compile failure: header reads, GPU bitmap lookup,
root-header transport, and shader execution acceptance remain unfinished.
This checkpoint does not expose tiled-resource Tier 2 or prove sparse NULL data
semantics. The new host test covers both load shapes, ordinary/feedback variants,
NULL/non-NULL status, shifted immediate addresses, register identity, alignment,
and SRV read tracking. Normal and no-private full builds and all 14 host tests
pass; UAV/TGSM and GPU shader feedback cases remain outside this test's scope.

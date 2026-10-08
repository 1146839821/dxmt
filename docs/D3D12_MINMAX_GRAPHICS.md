# MinMax graphics integration

## Task Analysis

### Current Branch / Baseline / Local Commits Since origin/feat/d3d12

`feat/d3d12-1`, clean at `5dad7e5`; read-only remote baseline `e147c710`.
111 local commits since that baseline. Previous turn added GPU-produced argument/
count evidence; this turn targets the next graphics production prerequisite.

### Current State / Existing Implementation / Relevant Files

MinMax lowering and private-root binding currently feed compute only. The shared
preparation inspector rejects pixel DXIL input and output, conversion hardcodes
compute, and pair resolution ignores pixel-only root visibility. Graphics PSO,
recording and submission still have no MinMax integration. Relevant files:
`d3d12_typed_origin.cpp`, `d3d12_minmax.hpp`, `d3d12_typed_origin_root.cpp`, shader
converter, graphics pipeline, command list and queue.

### Existing Tests / D3D12 Contract / Missing Pieces

Compute has focused preparation/PSO/binding/GPU probes. Graphics requires matching
pixel conversion, compiler-root layout for all participating stages, stage-visible
application descriptors, static/live snapshots, resource lifetime and real pixel
readback. Compiler success alone cannot prove reduction filtering or draw support.

### DXBC / AIRCONV Impact / DXIL / MSC Impact / Shared Runtime Impact

Keep DXBC on AIRCONV and DXIL on MSC. Reuse existing DXIL SampleLevel/SampleGrad
lowering, validator and compiler-root construction; introduce explicit artifact
stage rather than relabeling compute bytecode. Runtime graphics admission remains
closed until the private binding/PSO replay integration is actually implemented.

### Hypothesis / Evidence / Expected Effect

The existing lowering is stage-neutral for explicit LOD/gradients, but the host
envelope and resolver are compute-only. Source inspection confirms hardcoded
compute program kind and ALL visibility. Stage-aware preparation/conversion and
pair visibility remove these prerequisites without duplicating the IR algorithm.

### Risks / Minimal Implementation Plan

Reject invalid/mismatched stages, preserve typed-origin compute restriction, avoid
ambiguous stage-visible ranges, preserve output on failure and stage-distinct cache
keys. First implement and validate pixel compiler artifacts plus actual native
render PSO creation; then integrate graphics recording/submission/private bindings.
These are consecutive steps toward full graphics support, not a replacement scope.

### Validation Plan / Capability Impact

Both reconfigured full builds; focused validated pixel SampleLevel/SampleGrad
preparation, fragment metallib/native render PSO, negative stage/visibility checks
and compute regressions. Pixel GPU semantics follow after runtime integration;
do not claim pixel readback or full graphics support from this compiler step.
No capability/Feature Level, backend, game/prefix DLL or Steam changes.

## Task Result

Implemented the compiler prerequisite: explicit Compute/Pixel artifact stage,
input/output DXIL kind validation, stage-visible descriptor/static-sampler
resolution, pixel-root-deny rejection and fragment MSC conversion. The existing
compute wrapper remains compute-only; typed-origin preparation remains compute
only. Reused the existing lowering and augmented-root implementation without
changing native IR algorithms or backend routing. Existing conversion cache keys
already include shader stage. Graphics PSO admission and draw replay are unchanged.

Both `build` and `build-no-private` were reconfigured, focused optional targets
built, and full default builds completed before DLL staging. Final evidence:
`/Users/zhangbo/.cache/dxmt-minmax-fragment.JPtMH8`. Twelve final process runs
passed (six per configuration): SampleLevel fragment, SampleGrad fragment,
compute preparation, root regression, GPU-produced indirect count and typed-origin
multi-command readback. The two fragment probes exercise ALL/PIXEL visibility
and dynamic/static samplers: 16 actual native render PSO creations total, **no
graphics draw or pixel readback**. Negative stage, visibility, deny-root and
failure-output-preservation checks passed. The compute count probes performed
six GPU submissions total with counts 0/1/7 and MaxCommandCount 2; typed-origin
GPU readbacks also passed. Five host suites passed in each configuration.

Final runs use task-owned DXMT shader caches; initial shared-cache lock warnings
are not the final evidence. The no-private Metal framework cache fallback is
unchanged. Matching native runtime hashes were checked before execution. No game
deployment, prefix DLL modification, Steam restart or full GPU matrix rerun.

Standards self-review against `5dad7e5`: shared implementation, default compute
compatibility and failure preservation checked; no blocking finding. Spec
self-review: compiler prerequisite is partial graphics integration, not MinMax
or FL12_0 closure. These are main-agent reviews, not independent reviews; the
code-review skill's independent-agent procedure was unavailable. `git diff
--check` passed. Local commit only, no push.

Next production work remains graphics PSO variants, compiler-root/private
binding integration, recording/submission lifetime and descriptor snapshots,
then real minimum/maximum pixel readback. Implicit LOD/bias, broader operations,
cube/aniso/feedback and full qualification remain separate open gaps. No feature
declaration or FL gate was promoted.

## Runtime binding follow-up — Task Analysis

Current branch `feat/d3d12-1`, HEAD `666f684`, baseline `e147c710`; clean before
this follow-up. Hypothesis: compute's descriptor capture/materialization can be
shared with standard vertex/pixel graphics without duplicating private state.
Evidence: the recorder currently retains a concrete compute PSO and filters all
tables to ALL visibility; materialization reads the compute PSO's Texture.Load
guard. This prevents a pixel variant from using it and omits vertex-only tables.

Expected effect: stage-aware binding artifacts plus a common recording entry
point enable graphics snapshots/materialization while existing compute callers
and replay keep their compute pipeline metadata. Standard graphics captures ALL,
VERTEX and PIXEL tables; compute keeps ALL only. Risks: borrowed artifact lifetime,
static/volatile semantics, filtering unrelated stages, accidental graphics
indirect admission and changed compute behavior. Validate focused pixel binding
materialization plus existing GPU compute/typed-origin regressions in both builds.
This is runtime groundwork toward graphics replay, not completed draw support or
a replacement for production graphics PSO selection and real pixel readback.

## Runtime binding follow-up — Task Result

### Branch / Baseline / Local Commit

`feat/d3d12-1`; read-only baseline `origin/feat/d3d12` (`e147c710`),
merge-base `85bb2dd2`. Parent `666f684` (112 local commits since baseline).
This follow-up is the local `feat(d3d12): share MinMax graphics binding snapshots`
commit; its final hash is reported in the task response.

### Changed Files / Implementation

`d3d12_minmax_pipeline.hpp` separates immutable binding metadata from compute
pipeline state. `d3d12_minmax_dispatch.hpp/.cpp` share recording/materialization
with a retained pipeline base and stage-qualified binding artifact. Compute
retains its wrapper and replay metadata. Pixel captures ALL/VERTEX/PIXEL tables;
hull-only tables are not dereferenced. Pipeline kind/backend checks reject
mismatches; mesh/geometry/tessellation/SO and graphics indirect remain unsupported.
Pixel deny-root flags are rejected. Texture.Load guards come from PSO metadata,
not a caller-provided override (tightened during self-review).

### DXBC / AIRCONV Impact / DXIL / MSC Impact / Shared Runtime Impact

No DXBC/AIRCONV changes or backend fallback. DXIL/MSC compiler algorithms, native
ABI and reflection are unchanged. Shared PE recording/materialization preserves
static recording snapshots, unique volatile submission resolution, private
submission-owned descriptor/TLAB storage and strong resource/sampler retention.
Production graphics PSO selection and command replay are not yet connected.

### Tests Added / Tests Run / Runtime Results

`dx12_minmax_fragment.cpp` and its optional Meson target now exercise the real
shared recorder/materializer with an ordinary application graphics PSO. Tests
include vertex-only retention, unbound hull-only exclusion, pipeline-stage
rejection with unchanged output, static vs volatile texture/sampler replacement,
ordinary static samplers, distinct submission buffers and immutable templates.
The test-owned artifact stays alive through both synchronous materializations;
production artifacts must be PSO-owned.

Both builds were reconfigured; optional targets and final full default builds
completed before staging. Matching native runtime hashes were verified. Final
evidence: `/Users/zhangbo/.cache/dxmt-minmax-graphics-binding.UcURiV`,
`review-final-*`, `review-full*`, `review-host*`. Eight final process runs passed:
two fragment operations, GPU-produced MinMax indirect counts and typed-origin
multi-command readback in each configuration. Fragment probes cover 16 binding
cases, 32 submission materializations and 16 explicit private native render PSO
creations (plus their ordinary application graphics PSOs). **No graphics draw or
pixel GPU readback.** Existing MinMax count probes pass six actual compute GPU
submissions with counts 0/1/7; typed-origin GPU readbacks pass. Five host suites
pass in each configuration. No full mandatory matrix, Windows oracle, Metal
validation, game, performance or tessellation acceptance was run.

### Known Limitations / Capability Status / Feature Level Impact

Graphics integration: PARTIAL. Graphics GPU semantics: UNVERIFIED. Full MinMax
and FL12_0 closure remain open. No capability/SM/FL declaration was promoted;
FL11_1 reporting is unchanged, FL12_0/FL12_1 are not enabled by this work.
Graphics variant construction, recording selection, render submission replay and
real reduction pixel readback remain required, followed by the broader operation
and resource qualification gaps already recorded above.

### Review / Git Status / Push Status / Next Recommended Task

Standards main-agent self-review against parent `666f684`: shared implementation,
immutable artifact borrowing and output preservation checked; no remaining
blocking finding. Spec main-agent self-review: the full graphics objective is
still incomplete and is not replaced by materialization tests. Independent
review agents are unavailable; these are not independent-review results.
`git diff --check` passed. Only task-owned files staged; local commit, NOT PUSHED.
No game/prefix DLL deployment or Steam/process restart. Next: construct the
graphics PSO variant and connect render replay to this shared binding path, then
verify actual minimum/maximum pixel output.

## Graphics replay integration — Task Analysis

Branch `feat/d3d12-1`, clean HEAD `7dd0894`, baseline `e147c710` (113 local
commits). Existing stage-aware compiler and shared descriptor materializer are
not selected by PreDraw or replayed by the render queue. Hypothesis: retain the
ordinary native render description and original VS/PS, compile a PSO-owned
pixel variant using the shared augmented root for both stages, and replace a
recorded marker with submission-owned PSO/TLAB commands. Static reduction roots
need eager private compilation; ordinary roots can select lazily under the
existing explicit DXC-directory opt-in. No executable-family fallback.

Evidence: current PreDraw binds ordinary root layouts and sampler validation;
render queue replays allocator nodes directly. Expected effect: real standard
graphics MinMax SampleLevel/SampleGrad draws, including static and volatile
descriptor snapshots, without modifying immutable command nodes. Risks: shared
VS/PS root offsets, graphics-state preservation, PSO switching, borrowed lifetime,
mixed render streams and unsupported indirect/emulation. Implement PSO variant,
recorded markers, submission cloning/lifetime, then focused actual pixel readback
and compute regression in both builds. Broader GPU matrices follow production
gap closure. Capabilities/FL/SM, native ABI, AIRCONV and game deployment unchanged.

## Graphics replay integration — Task Result

### Branch / Baseline / Local Commit

`feat/d3d12-1`; read-only baseline `origin/feat/d3d12` (`e147c710`),
parent `7dd0894` (113 local commits). This is the local
`feat(d3d12): integrate MinMax graphics draw replay` commit; final hash is
reported in the task response. NOT PUSHED.

### Changed Files / Implementation

Graphics pipeline/device interfaces and `d3d12_minmax_pipeline.hpp` now expose
a PSO-owned pixel MinMax variant. Standard graphics retains the original VS/PS
and value-owned native render description. Lazy variants compile VS and lowered
PS against the same augmented compiler root; static reduction roots compile
eagerly and require the explicit DXC-directory opt-in. Root/application pipeline
cache provenance remains original. Native vertex input, attachments, blending,
raster/MSAA and other stored render state are reused, not reconstructed from
defaults. Transient native function/archive pointers are not borrowed in the
saved description. Cached variants reject a different compiler directory.

`d3d12_command_list.cpp`, command encoder and MinMax dispatch metadata record
private draw markers and the shared descriptor/TLAB snapshots. Root buffer uses
remain explicit; private application tables are not independently reread by the
ordinary pending-use path. Ordinary draws restore their PSO and application TLAB.
`d3d12_command_queue.cpp` clones only passes containing private draws, replaces
markers with PSO plus vertex/fragment TLAB bindings, declares resources resident
and retains submission bindings until GPU completion. Allocator-owned command
nodes/templates are not patched. Ordinary passes keep direct single-call replay.

### DXBC / AIRCONV Impact / DXIL / MSC Impact / Shared Runtime Impact

No AIRCONV changes or executable-family fallback. Existing mixed-family and
stage validation remain enforced. DXIL classification records sampler-dependent
operations (sample/gather/LOD query) so pixel-only lowering cannot silently run
vertex sampling through point-encoded reduction descriptors. Such VS variants
remain rejected pending actual pre-raster lowering. Native ABI/IR lowering is
unchanged. Shared runtime now consumes the stage-aware binding implementation
from the prior commit in actual render submissions. The MSC compilation and
integration skills informed the same-root VS/PS layout, native vertex-fetch
preservation and submission residency/lifetime contract.

### Tests Added / Tests Run / Runtime Results

`dx12_minmax_fragment.cpp` now uploads distinguishable 2x2 textures, renders to
a 4x4 target and checks every RGBA pixel after D3D12 texture copy/readback.
SampleLevel/SampleGrad cover ALL/PIXEL visibility, dynamic samplers, static root
MINIMUM/MAXIMUM samplers, static/volatile application descriptors and two
submissions of the same closed list. Dynamic cases also switch to an ordinary
linear draw in the same render pass, checking 128/128 control pixels against
the private extrema (16/240 or replacement 32/224). Descriptor-static mutation
is a defensive snapshot probe, not evidence that D3D12 permits changing static
descriptors before re-recording. Templates and command-node links are checked
unchanged after both submissions; overlapping submissions are not tested.

Negative probes cover absent static opt-in, stage/visibility/root deny flags,
directory mismatch, real sampling VS rejection and graphics ExecuteIndirect
recording rejection. The latter list is not submitted; `Close` must return
`E_FAIL`. `minmax_fragment.hlsl` adds a sampling-VS negative fixture. All four
shader fixtures were freshly built with repository DXC for the final runs.

Both configurations were reconfigured, optional targets built and final full
default builds completed before task-owned DLL staging. Evidence:
`/Users/zhangbo/.cache/dxmt-minmax-render.WnEX31`, final `review-*` logs and
`dxc-*` logs. Matching native runtime hashes were verified. Eight final process
runs passed: two fragment operations, existing GPU-generated MinMax indirect
count and typed-origin multi-command readback per configuration. Fragment runs
cover 16 graphics cases and **32 real MinMax draw submissions**, plus 16 ordinary
linear control draws; each case also performs a separate input upload submission.
Compute count regression passes six GPU submissions with counts 0/1/7 and
MaxCommandCount 2. Typed-origin GPU readbacks pass. Five host suites pass in each
configuration. No shader/compiler warning in the reviewed optional builds.

### Known Limitations / Capability Status / Feature Level Impact

Bounded 2D float SampleLevel/SampleGrad direct graphics: DXMT_LOCAL_PASS.
Full MinMax capability: PARTIAL. Graphics indirect (including non-updating),
pre-raster sampling, emulation, implicit Sample/Bias, broader dimensions/views,
cube/aniso/feedback and default-path complete qualification remain open. Direct
indexed draw, graphics root VA updates, overlapping/in-flight reuse, Metal API/
shader validation and native Windows oracle were not qualified by this probe.
No full mandatory GPU matrix, game, performance or tessellation acceptance run.
FL11_1 reporting unchanged; FL12_0/FL12_1 are not enabled; capability and SM
declarations unchanged. Bounded success does not satisfy the full promotion gate.

### Review / Git Status / Push Status / Next Recommended Task

Standards main-agent self-review against `7dd0894`: ownership, saved native
pointer clearing, exhaustive replay node sizes, marker validation, static/live
snapshot isolation and ordinary-state restoration checked; no remaining blocking
finding. Spec main-agent self-review: the full graphics/MinMax objective remains
incomplete; unsupported cases reject instead of falling back or raising gates.
Independent review agents are unavailable. `git diff --check` passed. Only task
files committed locally, NOT PUSHED; no game/prefix DLL deployment or Steam kill.
Next production gap: MinMax graphics indirect replay, beginning with inherited
non-updating draw/indexed draw, then GPU-generated counts and root-update TLABs.

## Inherited graphics indirect — Task Analysis

Branch `feat/d3d12-1`, clean baseline `6409b9f`, remote baseline `e147c710`;
114 local commits. Production gaps remain the priority; this step is not full
MinMax or FL12_0 closure.

Hypothesis: non-updating DRAW/DRAW_INDEXED can inherit the existing submission
private vertex/fragment TLAB if the GPU resolver restores the selected private
PSO. Evidence: allocator ICB construction inherits buffers only without root,
vertex or index updates, inherits PSO, and currently restores the application
PSO after a resolver draw using vertex slot 30. Private TLAB uses slot 2.
Expected effect: connect bounded indirect graphics without duplicating descriptor
reads, lowering, residency or render replay.

Risk: updating signatures disable inheritance and use ordinary per-command root
layouts. Keep all such MinMax signatures rejected. Retain PSO-owned artifacts
through the existing recording snapshot; do not patch allocator nodes. DXBC
AIRCONV and DXIL MSC routing, static/volatile flags and feature bits stay intact.

Plan: return the selected graphics variant from PreDraw and pass it to allocator
PSO restoration; enable only non-updating signatures. Validation: real pixel
readback for DRAW and DRAW_INDEXED, count clipping/zero, repeated closed-list
submissions, ordinary draw restoration and existing direct/compute regressions;
normal/no-private reconfigure and full builds before task-local staging.
Main-agent Standards/Spec self-review; no independent reviewer available.
GPU-produced counts, root-update TLABs, full matrices, game/performance and
tessellation acceptance remain subsequent work; no capability promotion.

## Inherited graphics indirect — Task Result

### Implementation / Backend and Runtime Impact

PreDraw returns its selected graphics variant only after successful ordinary
graphics preparation. ExecuteIndirect admits MinMax only without root/VB/IB
updates and passes that variant to the allocator. The resolver uses vertex slot
30, leaving submission-private vertex/fragment TLAB slot 2 inherited by the ICB;
its restore command now selects the matching private native PSO. The allocator
also rejects a private variant with an updating signature. Existing private
draw snapshots retain PSO-owned variants and descriptor generations through
submission completion; no additional descriptor reads or replay specialization,
allocator mutation, native ABI change or duplicate lowering is introduced.
DXBC remains AIRCONV, DXIL remains MSC; mixed-family/emulation restrictions and
capability/feature-level declarations are unchanged. MSC integration guidance
informed checking inherited binding slots and matching root/pipeline layout.

### Tests Added / Tests Run / Runtime Results

The existing fragment probe now runs direct, inherited DRAW and DRAW_INDEXED
with 32-bit indices. SampleLevel/SampleGrad, ALL/PIXEL visibility, static/volatile
descriptor ranges and static/dynamic reduction samplers run in both variants.
Indirect arguments start at byte 16, count at byte 112 and indices at byte 128;
MaxCommandCount is 2 with count values 0/1/7. Counts are CPU-written upload
data, not GPU-produced evidence. Every target pixel is checked; zero count
preserves clear pixels. Same closed lists are resubmitted with live descriptor
replacement; ordinary linear control draws follow private indirect execution
in the same pass. Original argument templates and render-node links must remain
unchanged. Root-constant updates and VB-updating signatures are negative recording
probes (`Close` returns E_FAIL), never submitted. Source fixture transitions were
corrected to the combined pixel/non-pixel read state used across repeat probes.
Static descriptor mutation remains defensive snapshot evidence, not legal D3D12
application mutation semantics.

Both configurations were reconfigured, optional targets and full default builds
completed before final task-cache staging. All four HLSL fixtures were freshly
compiled using repository DXC, and matching native runtime hashes verified.
Evidence: `/Users/zhangbo/.cache/dxmt-minmax-render-indirect.J62a3J`, `final-*`
and `dxc-*` logs; reconfigure logs under `/tmp/dxmt-minmax-render-indirect-*`.
Four final fragment processes pass 32 indirect cases / 96 GPU submissions
(32 zero-count submissions), plus 16 direct cases / 32 submissions. This count
is not a count of distinct GPU semantics. Existing compute GPU-produced count
0/1/7 and typed-origin multi-command readbacks pass in both configurations.
Eight final runtime processes pass; five host suites pass per configuration.
Reviewed optional builds have no compiler warnings; final fragment logs have
no resource-state warnings. Expected negative recording failures remain logged.

### Known Limitations / Capability Status / Feature Level Impact

Bounded inherited DXIL/MSC pixel MinMax DRAW/DRAW_INDEXED: DXMT_LOCAL_PASS.
Root-update graphics private TLABs remain unimplemented and rejected. GPU-produced
graphics arguments/counts, in-flight overlap, pre-raster sampling, emulation,
implicit operations and broader resource/view/filter matrices remain open.
No Metal API/shader validation, native Windows oracle, game/performance or fresh
tessellation acceptance was performed. Full MinMax remains PARTIAL; FL12_0 and
FL12_1 promotion gates are not satisfied or changed.

### Review / Git Status / Push Status / Next Recommended Task

Main-agent Standards review against `6409b9f`: successful-only variant output,
ICB inheritance/restore, artifact retention, independent static/live materialization,
updating-signature rejection and immutable recording checked; no blocking finding.
Main-agent Spec review: closes inherited indirect production wiring only, not
the full capability or mandatory GPU matrices. Independent review unavailable.
`git diff --check` passes. Only task files are committed locally, NOT PUSHED;
no game/prefix DLL deployment or process manipulation. Next production gap:
graphics indirect root-update private TLAB reflection/materialization; add focused
GPU-generated argument/count evidence with that integration before broad matrices.

## Graphics indirect root updates — Task Analysis

Branch `feat/d3d12-1`, clean baseline `92c677b`, remote baseline `e147c710`,
115 local commits. Existing inherited graphics indirect and compute root-update
paths pass focused GPU tests. Missing production piece: graphics augmented-root
reflection, private per-command TLAB storage and resolver payload rebinding.

Hypothesis: the existing GPU graphics resolver can consume the same private
template scheme as compute. Evidence: it already copies templates and patches
inline constants / root buffer addresses using supplied reflected offsets, and
binds both vertex and fragment TLABs. Current materializer clones compute data
only, render replay does not patch resolver data, and recording rejects updates.
Expected effect: root-updating DRAW/DRAW_INDEXED uses submission-specific tables
and state without modifying allocator-owned payloads or rereading descriptors.

Risks: payload ABI differs by stage; accidentally using ordinary reflection,
shared writable TLABs, losing unchanged constants or restoring the wrong PSO
would corrupt output. Plan: stage-checked shared payload sizing/copy, record
render resolver binding, patch cloned render replay node and use augmented root
offsets; retain existing indirect root-VA residency and root-state reset behavior.
VB/IB updates and emulation remain rejected. DXBC AIRCONV / DXIL MSC routing,
static/live descriptor capture, native ABI and capability bits stay unchanged.

Validation: root-constant partial updates with distinct commands and preserved
constant, DRAW/DRAW_INDEXED pixel readback, count zero/clipping, ordinary-state
restoration, immutable payload/template/replay nodes, both configurations' full
builds plus compute/typed-origin regression. Broader root-VA and GPU-produced
graphics arguments/counts follow focused integration, not broad matrix-first
work. Main-agent Standards/Spec review; full MinMax and FL12 gates remain open.

Focused validation refinement: include CBV/SRV/UAV root address updates in the
same production integration, using two distinguishable command payloads and a
real default-heap UAV buffer. This verifies all admitted root update kinds rather
than crediting constant-only output as buffer-address evidence.

## Graphics indirect root updates — Task Result

### Branch / Baseline / Implementation / Changed Files

`feat/d3d12-1`, starting at `92c677b`, remote baseline `e147c710`.
Command list and allocator now admit root-only updating signatures with MinMax,
return the resolver binding node and encode parameter offsets from the augmented
compiler-root reflection. Ordinary graphics and compute paths keep their existing
layouts. The original resolver payload is attached immutably to the private draw
snapshot. MinMax materialization shares stage-checked payload sizing/copy between
compute and render, checks payload/binding consistency and multiplication bounds,
and allocates submission-specific writable per-command TLABs. Render replay
patches only its copied resolver buffer node to the cloned payload, validates
that every patch is consumed and restores the matching private graphics PSO.
Original payloads/templates/command links are not changed.

Runtime files: allocator header/source, command list/queue, MinMax dispatch
header/source. Test files: fragment C++/HLSL. Documentation: this file and closure
status ledger. Existing registered-buffer retention/residency for indirect root
VAs and root-state reset are reused. This does not repair same-address VA remaps
or change native payload ABI. Static/live descriptor generations are still
captured once per binding; no additional heap-lock fan-out is introduced.

### DXBC / AIRCONV Impact / DXIL / MSC Impact / Shared Runtime Impact

No shader backend fallback, AIRCONV shader change or mixed-family admission.
DXIL remains MSC. MSC integration guidance informed inline constants, absolute
root/table addresses, reflection offsets and submission storage ownership.
The shared host runtime now connects private graphics root-updating ICBs to
the existing resolver; VB/IB-updating signatures and emulation remain rejected.

### Tests Added / Tests Run / Runtime Results

New HLSL modes consume a two-DWORD root constant block and root CBV, raw SRV and
raw UAV buffers. Partial updates change only DWORD 0 (7/11), preserving DWORD 1
(31). CBV values are 13/17, SRV 19/23, UAV 5/9. Two distinct root-updating
commands therefore produce blue 75/91 while ordinary following draws observe
the updated constant reset to zero and preserved 31, with explicitly rebound
baseline buffers (blue 68). CBVs use 256-byte-aligned upload addresses; UAV uses
a real default-heap UAV buffer initialized through a separate upload list.
DRAW and DRAW_INDEXED consume CPU-provided counts 0/1/7 with MaxCommandCount 2,
nonzero argument/count offsets and repeated closed-list submissions. Every pixel
and original payload/template/command link is checked. This is UAV root-address
read evidence, not a new UAV-write/ordering qualification.

The first buffer-probe run stopped on fixture allocator Reset, before root-update
execution: retained encoders prohibit allocator reset. Use a separate upload
allocator/list instead; production allocator guards are unchanged. Final runs
have no resource-state warnings or pixel failures. VB updates still reject at
recording, while the former root-update rejection probe is replaced by real GPU
execution. The constants-only fixture also passed an initial normal-build run.

Both configurations were reconfigured and optional plus full default builds
completed before each task-local staging. Fresh repository-DXC shader fixtures
and matching native runtime hashes verified. Evidence:
`/Users/zhangbo/.cache/dxmt-minmax-render-roots.RFSNQ7`, `final-*`, `review-*`,
`dxc-*` logs; reconfigure/first-build logs under `/tmp/dxmt-minmax-render-roots-*`.
Eight final fragment processes pass: two operations with root-buffer updates and
two baseline operations per configuration. They include **32 new root-update
cases / 96 GPU submissions** plus direct/inherited regressions (352 graphics
submissions total, including zero-count execution). Counts are execution counts,
not independent semantic matrix cells. Existing compute GPU-produced count
and typed-origin multi-command readbacks pass in both configurations: twelve
final runtime processes pass. Five host suites pass per configuration. Reviewed
optional targets have no compiler warnings.

### Known Limitations / Capability Status / Feature Level Impact

Bounded DXIL/MSC pixel MinMax root-updating DRAW/DRAW_INDEXED: DXMT_LOCAL_PASS.
Full MinMax remains PARTIAL. Graphics GPU-produced argument/count evidence,
overlapping in-flight reuse, same-address root-VA remap, UAV writes/ordering,
pre-raster sampling, emulation, implicit Sample/Bias and broader views/filter
coverage remain open. No complete mandatory matrix, Windows oracle, Metal
API/shader validation, game/performance or fresh tessellation acceptance run.
FL11_1 reporting is unchanged; FL12_0/FL12_1 gates remain unmet and not promoted.
No capability or Shader Model declaration changes.

### Review / Git Status / Push Status / Next Recommended Task

Main-agent Standards self-review against `92c677b`: stage/payload consistency,
size overflow, reflection reuse, strong retention, patch consumption, immutable
closed-list storage and root-state restoration checked; no blocking finding.
Main-agent Spec self-review: production supports admitted root update kinds;
focused GPU evidence is bounded and not full MinMax/FL12 acceptance. Independent
review unavailable; code-review two-axis methodology used as a main-agent
fallback. `git diff --check` passes. Task files committed locally, NOT PUSHED;
no game/prefix DLL writes or process manipulation. Next production gap: DXIL
pixel MinMax implicit Sample/SampleBias lowering, then remaining pre-raster and
resource semantics; GPU-produced graphics count evidence and broad matrices
remain required before final qualification.

## DXIL implicit pixel sampling — Task Analysis

Branch `feat/d3d12-1`, clean baseline `f016a6b`, remote baseline `e147c710`;
116 local commits. Existing production graphics direct/indirect/private bindings
support explicit SampleLevel/SampleGrad. Native DXIL binding qualification rejects
Sample/SampleBias; AIRCONV already has independent derivative/gradient semantics.
Relevant files: native dxil_minmax binding/lowering, fragment HLSL/C++ probes,
native IR probe and capability ledger. Do not merge executable families or ABI.

Hypothesis: pixel Sample/SampleBias can normalize to explicit gradients before
the runtime reduction branch, then reuse existing isotropic LOD and tap lowering.
Evidence: SampleGrad already applies sampler bias, clamps and ordinary/reduction
branches. DXIL specifies Sample opcode 60, SampleBias 61, coarse derivatives
83/84 and separate clamp operands. Bias order is sampler bias, instruction bias,
then sampler/resource/instruction clamps. Sources:
[DXIL specification](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst),
[D3D filtering contract](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
Expected effect: connect actual pixel implicit sampling without duplicating
LOD/footprint lowering, private-root reflection or submission residency.

Risks: derivatives inside newly divergent branches, array layer derivatives,
wrong bias/clamp order, unused DXIL operation declarations and stale native
overlays. Plan: require pixel shader metadata for new operations, emit only
spatial coarse derivatives before injected branching, normalize to SampleGrad
and SampleLevel, apply additive instruction bias after runtime sampler bias,
and remove unused normalized declarations. Existing unsupported handle/feedback/
cube/aniso paths stay closed. Runtime/backend/capability declarations unchanged.

Validation: fresh DXC assembly/validation and MSC render PSO plus real pixel
readback for Sample/SampleBias, ordinary and reduction paths, distinguishable
mips/bias, existing explicit/root-update and compute regressions. Native IR
negative stage/provenance tests; normal/no-private configure/full builds and new
matching native overlays before runtime tests. Broad matrices/game acceptance
remain required later. Main-agent Standards/Spec self-review, no push.

### Ordinary MSC sampler bias follow-up — Task Analysis

The original bias=1 GPU fixture fails only the subsequent ordinary MSC draw:
it returns mip 0 instead of mip 1. Keeping sampler bias=1 and changing only
instruction bias to 2 passes. Descriptor metadata matches the bundled MSC
IRDescriptorTableSetSampler ABI. The compiler enables TextureMinLODClamp but
omits the documented SamplerLODBias compatibility flag. Hypothesis: enabling
that flag restores ordinary dynamic sampler bias rather than changing the
oracle or routing ordinary draws through private lowering. Expected effect:
ordinary SampleBias observes both biases; explicit SampleLevel and private
lowering retain their existing behavior. Risk: double bias or stale caches.
Use one shared default flag mask, retain existing caller override behavior,
verify cache hashing already includes the mask, and rerun original bias=1 plus
Sample/explicit/root-update/compute GPU regressions in both configurations.

The added nonzero-bias SampleLevel regression invalidated the initial assumption
that explicit LOD should ignore sampler bias. The ordinary MSC result correctly
selects mip 1, while private SampleLevel still selects mip 0. Microsoft explicitly
states that sample_l honors MIPLODBIAS:
[sample_l contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sample-l--sm4---asm-).
Refined implementation: apply runtime sampler bias to every admitted sample,
including explicit SampleLevel, before clamps. Update the new oracle to the
specified mip 1 result; existing zero-bias explicit tests remain unchanged.
Also exercise nonzero-bias SampleGrad to detect omitted or double bias.

## DXIL implicit pixel sampling — Task Result

### Branch / Baseline / Implementation

`feat/d3d12-1`, parent `f016a6b` (116 local commits since read-only baseline
`e147c710`, merge-base `85bb2dd2`). Native qualification now admits pixel-only
float Sample/SampleBias for the existing finite 1D/2D/3D and array binding shapes.
Spatial coarse derivatives are emitted at the original call site, before the
new runtime sampler branch; array-layer coordinates are not differentiated.
The existing SampleGrad isotropic LOD and SampleLevel reduction/ordinary tap
lowering are reused. Runtime sampler bias applies to every admitted operation,
instruction bias follows for SampleBias, then existing clamps apply. Unused
normalized DXIL declarations are removed before regeneration/validation.

Ordinary MSC compilation now enables the bundled SamplerLODBias compatibility
flag alongside TextureMinLODClamp, with one shared default mask used by capability
configuration, conversion/cache keys and native fallback. Explicit nonzero caller
masks retain their override behavior. No thunk structure layout or backend ABI
changed. DXBC remains AIRCONV; DXIL remains MSC. No fallback, mixed-family shader
pipeline or feature/SM/FL declaration change was introduced.

### Tests / Runtime Evidence

Both configurations were reconfigured; focused optional targets and full default
builds completed before task-cache staging. Final source/native binaries match
isolated overlay hashes. Evidence:
`/Users/zhangbo/.cache/dxmt-minmax-implicit.6V0wqp`, final `accepted-*` logs,
`level-fix-targets*` and `level-fix-full*`; original failing and bias=2 diagnostic
logs are retained separately, not counted as acceptance.

Twenty final runtime process runs pass: sixteen graphics (eight per configuration)
and four compute/typed-origin regressions. Graphics includes new Sample/SampleBias,
nonzero-bias SampleLevel/SampleGrad, prior zero-bias explicit operations and
root-buffer indirect updates. These perform 608 draw submissions (excluding
texture-upload submissions), checking full 4x4 RGBA readback, static/live descriptor
behavior, immutable closed-list replay, direct/DRAW/DRAW_INDEXED, zero/clipped
CPU-provided counts and subsequent ordinary PSO/binding restoration. New mip
fixtures distinguish sampler bias, instruction bias and minimum/maximum results.
Static-descriptor mutation is a defensive snapshot probe, not a claim of legal
application mutation under D3D12 static-descriptor promises.

Compute regressions retain GPU-produced counts 0/1/7 and typed-origin multi-command
full-buffer readback. Four native implicit transforms pass, including sixteen
negative stage/missing-metadata/opcode/status checks with unpublished output on
failure, derivative placement and removed intrinsic declarations. Both native
base IR probes pass; actual production preparation also regenerates/validates DXIL
and creates MSC render PSOs. Five registered host suites pass per configuration.
Existing macOS 27 Managed-storage deprecation and libunwind linker warnings remain.

### Review / Limits / Next Work

Main-agent code-review Standards axis against `f016a6b`: shared algorithm/default
mask, failure publication, sampler descriptor ABI, cache hashing, unchanged
residency/lifetime and immutable replay checked; no remaining blocking finding.
Spec axis caught the missing ordinary compiler flag and private explicit-LOD bias;
both are fixed and covered without reducing the oracle. This is main-agent
two-axis self-review, not independent-agent review. `git diff --check` passes;
task files committed locally, no push. No game/prefix DLL deployment or Steam/
wineserver manipulation occurred.

Qualification remains bounded: GPU fixtures cover 2D float pixel sampling, not
the complete dimension/view/clamp/address/filter matrix. Cube, anisotropy,
comparison, gather, residency feedback, dynamic/nonuniform provenance, pre-raster
sampling and broader graphics emulation remain open. GPU-produced graphics
arguments/counts, same-address root-VA remap, in-flight overlap, game/performance/
tessellation acceptance and mandatory FL12_0 matrices are not claimed. Continue
production pre-raster/resource gaps before broad qualification; MinMax and FL12_0
remain partial and the overall development goal remains active.

## Vertex sampling and cross-stage pairs — Task Analysis

Branch `feat/d3d12-1`, clean parent `a2359be`, baseline `e147c710`, 117 local
commits. Previous task is progress: implicit pixel production lowering, two bias
repairs, fresh GPU evidence and local commit. Next production gap: graphics PSO
explicitly rejects sampling VS; preparation and conversion accept Compute/Pixel
only. Existing private point/ordinary arrays and CBV use stage-local pair indices,
so simply removing the VS guard would alias vertex/pixel pairs incorrectly.

Hypothesis: assign disjoint pair intervals in one shared augmented root/TLAB,
lower each sampled stage against that interval, and resolve each interval with
its own application visibility. Evidence: current materializer already retains
ALL/VERTEX/PIXEL tables, binds slot 2 to both stages and handles shared direct/
indirect replay. Native finite-pair lowering only needs register/CBV ordinal
rebasing; resource range IDs remain stage-local. Expected effect: standard VS/PS
sampling uses the existing recording/snapshot/submission/residency pipeline,
including inherited and root-updating indirect draws, without another ABI.

Plan: add Vertex compiler stage and explicit pair offset/total metadata. A tagged
layout option in the existing reserved transport word preserves its 64-byte
layout and makes old runtimes reject new requests rather than silently ignore
them. Zero retains old single-stage behavior. Discover/validate sampled stages,
then reprepare both against a shared interval when both sample. Resolve their
bindings independently, concatenate stage-labelled locations, and compile every
participating stage against the same reflected root. Keep implicit derivatives
pixel-only, no GS/HS/DS/mesh/SO admission or backend fallback. Support vertex-only
sampling with an ordinary pixel stage and fragment-less standard graphics.

Risks: wrong ordinary sampler half, CBV range size, duplicate register numbers
with stage-separated application bindings, deny-root flags, eager static PSO and
lazy dynamic variants, stale native overlays and loss of ordinary state after
indirect replay. Validate native rebasing/invalid intervals, stage/visibility
failures and actual vertex-sampled pixel colors alongside PS reduction, static/
volatile tables, ordinary draw restoration, direct and indirect paths in both
builds. Reconfigure/full builds before staging; focused regressions and main-agent
Standards/Spec self-review before local commit, no push. Broad pre-raster/emulation
and mandatory matrices remain open; do not promote capability or feature level.

## Vertex sampling and cross-stage pairs — Task Result

### Branch / Baseline / Changed Implementation

`feat/d3d12-1`, parent `a2359be` (117 local commits since baseline `e147c710`,
merge-base `85bb2dd2`). Standard graphics now prepares sampled vertex and pixel
stages separately, allocates disjoint pair intervals, resolves stage-specific
application bindings and compiles both against one reflected augmented root.
Vertex-only sampling and a missing pixel shader are no longer rejected by the
standard graphics variant factory. Unsampled stages are recompiled unchanged
against the same root. Both eager static-sampler and lazy dynamic variants use
this shared preparation. The existing render replay and indirect resolver are
reused; no second submission/materialization algorithm was introduced.

Native lowering rebases private t/s registers and CBV rows while preserving local
DXIL range IDs. The ordinary sampler half begins at the shared total pair count;
the private CBV size covers that entire layout. The tagged reserved transport word
keeps the existing 64-byte structure size/offsets, with zero retaining prior local
behavior. This is a semantic extension, not a new struct layout. Prior runtimes'
nonzero-reserved rejection makes this fail closed by source inspection; no old-
runtime cross-version execution was performed. Invalid tags, counts and intervals
are rejected before publishing artifacts. Pair offset/total join conversion cache
keys. The legacy Pixel binding-variant tag still identifies render replay, while
per-pair stage labels identify actual Vertex/Pixel visibility and deny-root checks.

No DXBC/AIRCONV routing change, mixed-family pipeline or fallback. No SM, feature
declaration, FL gate, native descriptor/state structure or heap-lock/residency
policy change. Compute retains its original default stage and local pair layout.
Vertex implicit Sample/SampleBias remains disallowed; admitted VS operations are
explicit SampleLevel/SampleGrad through the existing finite-pair lowering.

### Tests / Runtime Evidence

Normal/no-private were reconfigured, optional targets built and full default builds
completed before isolated cache staging. Final evidence:
`/Users/zhangbo/.cache/dxmt-minmax-vertex.LH73mW`, `final-targets*`,
`accepted-full*`, `accepted-*`, `final-*-prepare/compute/typed.log`, `host-*`.
Native overlay hashes match the built libraries. No game/prefix DLL deployment or
Steam/wineserver process manipulation occurred. Existing Managed-storage
deprecation and libunwind reexport warnings remain, unrelated to these source edits.

Thirty-six final runtime process runs pass: twelve new graphics, sixteen graphics
regressions, two preparation/export probes, two root-signature probes and four
compute/typed-origin GPU regressions. Graphics checks perform 928 draw submissions
(excluding texture uploads), all full 4x4 RGBA readbacks. New cases cover combined
VS/PS and VS-only SampleLevel/SampleGrad, static/volatile application tables,
minimum/maximum static samplers, two submissions of immutable closed lists,
direct/DRAW/DRAW_INDEXED, counts 0/1/7 clipped to MaxCommandCount 2 and ordinary
PSO/TLAB restoration. Vertex samples are independently visible in the blue channel;
PS slot replacement does not replace the static VS slot. Same t0/register-space
bindings with PIXEL/VERTEX visibility resolve to different physical slots and
independent private intervals. Invalid ALL-plus-stage duplicate root layouts are
not substituted for that legal stage-separated case.

Forty focused cases additionally create/select depth-only application/private
MinMax PSOs without a fragment shader. These prove pipeline creation and variant
selection only, **not depth GPU output**. Existing explicit/implicit/bias and
root-only updating graphics readbacks remain green; new cross-stage root-updating
semantics are not independently qualified by those old pixel-only regressions.
Compute GPU-produced counts 0/1/7 and typed-origin multi-command full-buffer
readback pass. Ten native IR invocations pass: four shared-layout transforms
(register halves, CBV size/rows, LLVM verification), four implicit transforms
with prior negative checks, and two base IR probes. PE export probes exercise
valid tagged rebasing plus malformed layouts and unchanged output on failure.
RS1.0/1.1 dynamic/static/unbounded root reflection and existing volatile conversion
pass. Five registered host suites pass in each configuration.

### Review / Limitations / Next Production Work

Main-agent code-review Standards axis against `a2359be`: shared algorithms, bounded
tagged transport, range-ID/ordinal separation, root reflection/cache keys, pinned
PSO artifacts, exception/output preservation and unchanged static/live residency
checked; no remaining blocking finding. Spec axis: removing only the VS guard
would have been incorrect; actual cross-stage lowering and GPU evidence are
present. The new VS-only binding probe initially expected a PS replacement to
change a static VS descriptor; its oracle was corrected to the binding contract,
with both stages still independently verified. Independent agents unavailable;
these are main-agent two-axis reviews. `git diff --check` passes. Task files
committed locally, no push; the overall goal remains active.

This closes bounded standard VS MinMax production wiring, not all pre-raster
sampling. GS/HS/DS/mesh/SO emulation, complete dimensions/views/address/filter/
clamp/feedback qualification, GPU-produced graphics counts, cross-stage updating
indirect qualification, overlapping submissions and root-VA remap remain open.
No game/performance/tessellation or mandatory FL12_0 matrix acceptance was run.
Next production gap: integrate MinMax stage-private bindings with the existing
MSC pre-raster emulation pipeline. Full MinMax and FL12_0/FL12_1 remain unqualified.

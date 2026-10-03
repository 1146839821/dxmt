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

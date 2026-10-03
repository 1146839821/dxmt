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

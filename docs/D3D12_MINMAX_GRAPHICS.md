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

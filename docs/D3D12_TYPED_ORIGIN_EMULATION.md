# Task Analysis

## Current Branch

`feat/d3d12-1`.

## Baseline

`49dfa26`: shared typed-origin shader records are available for VS/PS/GS/HS/DS;
graphics runtime selection still rejects emulation.

## Local Commits Since origin/feat/d3d12

135 commits at the start of this task; no push is authorized.

## Current State

Clean worktree at task start. Native VS/PS origin variants are implemented; GS and tessellation
need private companion PSOs, shared argument bindings and draw-config replay.

## Existing Implementation

MinMax already reconstructs private companion geometry/tessellation pipelines
from retained application attachments and fresh converted-stage reflection.

## Relevant Files

`d3d12_pipeline_graphics.cpp`, `d3d12_typed_origin_pipeline.hpp`,
`d3d12_typed_origin_binding.cpp`, `d3d12_command_queue.cpp`.

## Existing Tests

Typed-origin pre-raster conversion, native depth and VS/PS draw fixtures;
MinMax geometry/tessellation fixtures. Conversion alone is not draw acceptance.

## D3D12 Contract

Honor stage visibility and range flags. Static descriptors are captured at
recording; only volatile slots are reread at submission. Preserve application
root identity and ordinary draw restoration.

## DXBC / AIRCONV Impact

No shader routing or AIRCONV change.

## DXIL / MSC Impact

Reuse the existing companion pipeline constructor for origin variants; shared
records must use one TLAB across object/mesh and HS/DS bindings.

## Shared Runtime Impact

Private pipeline reflection must also drive companion draw dispatch. Resource
retention must cover object/mesh/fragment without extending heap lock scope.

## Missing Pieces

Stage-aware preparation, shared origin records, capture of active stage tables,
object/mesh binding, private draw-config replay, and actual GPU readback.

## Hypothesis

Existing MinMax companion construction can be shared without changing native
VS/PS semantics; origin emulation can consume the same reflection contract.

## Evidence

The existing factory only consumes converted shaders, retained attachments and
an output PSO/config. It does not consume MinMax descriptors or pair records.

## Expected Effect

Avoid a second companion construction implementation and keep configuration
validation identical between private shader transformations.

## Risks

Stage-local record indexing, missing HS/DS TLAB bindings, stale draw reflection,
and incomplete resource stage masks can silently corrupt GPU output.

## Minimal Implementation Plan

Share the private emulation result and construction first. Connect typed-stage
preparation, materialization and replay next; then exercise public draw readback.

## Validation Plan

Reconfigure and build normal/no-private variants before runtime staging. Run
host tests and existing private graphics regressions; new emulation acceptance
requires direct/indexed GPU readback with independent stage contributions.

## Capability Impact

None. FL12_0/12_1 and TypedUAVLoadAdditionalFormats remain disabled.

# Task Result

The result below covers the shared-state extraction at `ba37b1f`, not the
subsequent runtime integration.

## Branch

`feat/d3d12-1`.

## Baseline

`49dfa26`.

## Local Commit

The containing commit extracts shared private graphics pipeline state only.

## Changed Files

Added `d3d12_private_graphics_pipeline.hpp`; updated MinMax and typed-origin
graphics variant headers and the graphics pipeline constructor; added this record.

## Implementation

Both private graphics variants now share PSO ownership and companion reflection
configuration. The existing MinMax factory consumes this shared result type.
Its conversion, validation and companion creation behavior is unchanged.

## DXBC / AIRCONV Impact

None; routing unchanged.

## DXIL / MSC Impact

Reusable private companion construction; typed-origin emulation selection is
still rejected pending stage preparation and replay integration.

## Shared Runtime Impact

Internal variant layout changed; all consumers rebuilt together. No exported
Wine/Metal ABI, descriptor observation or resource retention change.

## Tests Added

None. This extraction does not justify expanding the test matrix.

## Tests Run

Both build directories reconfigured; both full default builds passed. Host
tests passed 5/5 per build. Independent cache-only Wine overlays ran existing
depth, VS/PS, pixel, root, static/live compute, multi-live indirect compute and
MinMax CubeArray bias fixtures successfully on both builds.

## Runtime Results

400 typed-origin draws and their 400 ordinary restoration draws passed across
the two builds and legacy/modern fixtures. This is existing native coverage,
not new GS/HS/DS draw acceptance. Evidence: cache directory
`/Users/zhangbo/.cache/dxmt-private-graphics.mu6GAC`.
Staged D3D12 SHA1 matched build outputs: normal
`792e8704c9ad57aa3ae5738cd45da2c52687282f`, no-private
`5334e7aedf203701b42b14ae8308f1ac31d473c7`.

## Standards Review

Main review: shared fields retain defaults and RAII ownership; field access
remains type-safe after multiple inheritance. Independent Standards review found
no actionable code issues; clarified the historical worktree statement.

## Spec Review

Main review: bounded prerequisite only. No root/descriptor contract or
capability change; the broader runtime task remains open. Independent Spec review
found no bounded-scope defects and confirmed the unfinished runtime requirements.

## Known Limitations

Typed-origin GS/HS/DS runtime stage preparation, shared TLAB materialization,
object/mesh replay and independent real draw readback remain unfinished.

## Capability Status

No promotion.

## Feature Level Impact

FL12_0/12_1 remain disabled.

## Git Status

Only the files listed above changed before local commit.

## Push Status

No push.

## Next Recommended Task

Connect shared-record typed-origin preparation and object/mesh submission replay
to the shared factory, then validate actual geometry/tessellation draws.

# Runtime Integration Task Analysis

Baseline `ba37b1f`, branch `feat/d3d12-1`, 136 local commits since the recorded
remote baseline; clean worktree at task start. Shared companion result is now
available; typed graphics selection still rejects emulation.

Hypothesis: assigning global records across all present typed stages permits a
single coherent TLAB to drive the existing companion PSO and dispatch paths.
Evidence: companion requires object/mesh argument bindings plus the HS/DS bind
point for tessellation; shader lowering already supports global record intervals.
Expected effect: correct typed buffer FirstElement/NumElements in real GS/HS/DS
draws without fallback, capability promotion or descriptor-lock expansion.
Risk: aliasing per-stage register identities, missing active ordinary-stage
tables, stale dispatch reflection or native VS/PS TLAB regression.

Plan: prepare present VS/PS/GS/HS/DS stages, assign shared offsets, resolve each
against stage visibility and application root identity; materialize one TLAB;
bind object/mesh/fragment and patch companion draw config through the shared
private result. Keep mesh/SO/indirect graphics and MinMax combinations rejected.
Validation: normal/no-private configure and full builds, existing host/GPU
regressions, public GS/tess direct/indexed readback with stage-distinct data,
static/volatile/RS1.0 descriptors, and ordinary restoration. DXBC/AIRCONV and
advertised feature levels remain unchanged.

# Runtime Integration Task Result

## Branch

`feat/d3d12-1`.

## Baseline

`ba37b1f` (shared private pipeline state).

## Local Commit

The containing commit connects the typed-origin companion runtime and supports
absent-PS depth-only geometry/tessellation pipelines. No push.

## Changed Files

Graphics pipeline preparation, typed binding variant/capture/materialization,
private render replay, vendored companion header, existing depth fixture, new
`typed_origin_emulation_depth.hlsl`, this record and the closure checkpoint.

## Implementation

Present typed VS/PS/GS/HS/DS stages receive globally offset records in one private
CBV. Each stage resolves its own application visibility and embedded-root
identity through shared binding validation. Ordinary stages are converted
against the same compiler root and included in descriptor-table capture.
Native VS/PS retain stage-local records and existing split-TLAB behavior.

Emulation submissions bind the shared TLAB on object/mesh/fragment and additionally
on the HS/DS bind point for tessellation. Resources use object/mesh/fragment
residency masks. The shared private PSO reflection config replaces the application
config on both direct and indexed companion commands; ordinary PSO commands
clear the private replay state.

The first real fixture exposed two existing absent-PS blockers: both companion
constructors required a fragment function, and D3D12 initialization explicitly
rejected tessellation without PS. Both are removed without synthesizing a PS.
Explicitly supplied fragment libraries still require successful function lookup;
acquired C++ fragment functions retain balanced guarded release paths.

## DXBC / AIRCONV Impact

No shader-family routing or AIRCONV changes. DXBC stays on AIRCONV.

## DXIL / MSC Impact

GS and triangle HS/DS typed origins now reach actual companion draws. DXIL stays
on MSC. Existing mesh/SO/indirect graphics, predication and MinMax-combination
origin restrictions remain; no shader-family fallback is added.

## Shared Runtime Impact

Static snapshots remain recording-owned and are not reread. Unique live slots
are resolved/retained under the existing heap lock; allocation and encoder
fan-out remain outside it. Capture/layout decisions now use named variant
queries. No exported thunk ABI or capability declaration changes.

## Tests Added

Extended the public depth fixture with geometry/tessellation, explicit/deployed
compiler and ordinary-control modes. Stage-visible same-register `t0` tables use
different origins/counts and stage weights. Initial and replacement hand-calculated
depths expose wrong global intervals, OOB reads and table aliasing. Both direct
and indexed draws are tested; indexed input includes a nonzero start and guards.

Mixed typed VS and ordinary StructuredBuffer GS/HS/DS stages test active ordinary
table capture, including legal volatile resource/origin/count changes. GS/HS/DS
deny-root negative pairs include matching positive GPU controls. A second ordinary
emulation draw in the same encoder must restore depth 0.75.

## Tests Run

Both build directories were reconfigured. Final focused and full default builds
completed successfully; host tests passed 5/5 each. Twenty-four fresh emulation
processes passed across both builds, SM6.0/6.6 and explicit/deployed/ordinary modes.
Existing native depth/VS-PS/pixel, root, static/live compute, multi-live indirect
compute and MinMax CubeArray bias regressions passed on both builds. Four additional
MinMax geometry/tessellation processes passed with real fragment stages.

## Runtime Results

The emulation runs passed 672 typed or mixed-stage depth draws and 672 ordinary
restoration draws; ordinary-control runs passed another 128 ordinary draws.
Existing native regressions added 400 typed draws and 400 restoration draws.

Evidence directory: `/Users/zhangbo/.cache/dxmt-origin-emulation.9ZNtHs`.
Final authoritative build logs are `config-final-*`, `focused-final-*`,
`full-final-*`, `host-final-*`; GPU logs are `gpu-*`, `reg-*`, `minmax-*`.
Initial failed probes and unsuccessful target-name invocations are retained but
are not qualification evidence.

Final staged/build SHA1 matched: normal D3D12
`2ace6151e6d9472699e0602508cd0105586b8802`, no-private D3D12
`3b8b0000622664b0d732c7c39022531ae75d6fba`; native winemetal normal
`da4a3a55add166cd56f936a799621f36249f716b`, no-private
`7f56e03c3bcc060029e2bd79b2f8b1a01c89e7ed`.

The controlled baseline uses `ba37b1f` D3D12 in an isolated Wine overlay with the
new native companion solely to separate its null-fragment blocker. Both builds
pass ordinary GS, reject typed GS during recording, and reject ordinary/typed
tessellation at PSO creation because of the old explicit PS requirement.
The initial all-old companion probe also fails ordinary geometry PSO creation.
An earlier app-local-only baseline was invalid because the runtime-directory DLL
took precedence; its logs are not used as baseline evidence.

## Standards Review

Main self-review and independent Standards review complete. Shared binding
validation, an explicit passed fixture object, and named capture/layout queries
resolve the initial maintainability findings. No remaining concrete findings.
MSC integration/compilation and metal-cpp ownership skills guided companion
bindings, reflection configuration and optional fragment cleanup.

## Spec Review

Independent Spec review found the initial identical-range oracle inadequate.
Different origins/counts and stage weights resolve it; enumerating record
assignments yields the expected value only for the correct mapping. Added mixed
ordinary stages and GS/HS/DS deny pairs. Final review reports no remaining
bounded-scope correctness findings. No illegal static-descriptor mutation test
was added: static no-reread is source-inspected, not a timing GPU claim.

## Known Limitations

This is bounded depth-only triangle GS/HS/DS acceptance, not a full graphics
matrix. Typed emulation PS, broader topology/format/provenance/lifetime,
embedded-root/wrong-visibility GPU cases, indirect graphics, SO and mesh remain
unqualified or unsupported as previously documented. No game restart, benchmark
or fresh ROTTR tessellation acceptance was performed. Compiler packaging/default
distribution remains separate work.

## Capability Status

TypedUAVLoadAdditionalFormats remains FALSE; no promotion.

## Feature Level Impact

FL12_0/12_1 remain disabled. The four production workstreams and seven complete
GPU qualification categories remain open.

## Git Status

Only the listed task files changed before local commit; verify clean afterward.

## Push Status

No push, merge or cherry-pick.

## Next Recommended Task

Per user priority, first audit the 17 divergent `feat/d3d12` commits for semantic
reconciliation, especially residency correctness and merge risks. User-provided
target is `bad6756a5d55be975a123253b36ad3f495e19c51`, merge-base
`85bb2dd2a2a74fa4ae0138b3b9a6165208a4e27b`; verify current refs/counts before
classifying each commit as covered, partially covered or missing. Audit does
not itself authorize merge/cherry-pick/push.

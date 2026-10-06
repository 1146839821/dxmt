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

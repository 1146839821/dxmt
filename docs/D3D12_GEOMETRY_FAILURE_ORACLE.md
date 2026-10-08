# Task Analysis

## Current Branch

`feat/d3d12-1`; clean working tree at task start.

## Baseline

Task `ab00071`; integration `origin/feat/d3d12`, merge-base `85bb2dd`.

## Local Commits Since origin/feat/d3d12

Compute, ordinary VS/PS and HS/DS invocation oracles retained.

## Current State

GS compiler-failure invocation evidence remains missing; isolation PARTIAL.

## Existing Implementation

AIRCONV initializes VS/GS/PS, compiles PS, then combined VS/GS mesh and
object stages for two strip and three index variants. MSC converts VS/PS/GS
with two calls per stage. Mixed-family and wrong-stage validation precedes
compiler entry.

## Relevant Files

Read-only production graphics/converter; backend-failure probe, Meson,
gate/host tests and reused `shader_backend_stages.hlsl` GS fixture.

## Existing Tests

11 compute, 20 ordinary graphics and 22 tessellation modes; 25 host tests.

## D3D12 Contract

GS failure stops without alternate backend; mixed/wrong-stage rejection has
zero compiler calls. Require exact HRESULT, PSO presence and ordered traces.

## DXBC / AIRCONV Impact

Test-only combined geometry imports identify both VS/GS shader handles.
Initialization, mesh-compile and object-compile failure cases; real control.

## DXIL / MSC Impact

Extend stage identity to GS and inject stage-specific first/second-pass errors.
Use real VS emulation/stage-in, PS and GS conversion; no generated success.

## Shared Runtime Impact

None planned; no production fault hooks, compiler or encoder edits.

## Missing Pieces

GS invocation oracle. Mesh/library and GPU GS draw semantics remain open.

## Risks

Real PSO may fail after compilation in library/reflection/native pipeline;
do not relax assertions or mistake compilation for GPU correctness. Paired
handles and mesh/object entry points must remain distinguishable.

## Minimal Implementation Plan

Hypothesis: reuse source-linked wrappers for both combined AIRCONV imports.
Evidence: geometry compiles mesh then three object variants for each strip.
Expected effect: exact failure prefixes and zero opposite compiler calls.
Risk: stage handles, shader fixture compatibility, real pipeline creation.
Reuse existing VS/PS and GS fixtures; add mandatory GS oracle to the gate.

## Validation Plan

Reconfigure/full build normal/no-private; verify fixture targets; Meson/host
tests and full Wine gates under API/Shader Validation. Verify runtime/binary
provenance, restore normal, independent Standards/Spec review, local commit.

## Capability Impact

None: no Feature Level or GS GPU-correctness promotion. No push.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

`origin/feat/d3d12`; task baseline `ab00071`, merge-base `85bb2dd`.

## Local Commit

Final handoff supplies the actual hash of the commit containing this report.

## Changed Files

Backend-failure probe, Meson, FL12 gate/host tests, this report and gate guide.
No production sources or existing shader fixtures changed.

## Implementation

Wrap actual source-linked combined AIRCONV Geometry/Vertex imports. Traces
identify initialized VS/GS handles and distinguish mesh/object compilation.
Control requires two mesh and six object calls, after PS compilation.
Selected GS initialization, mesh and object faults stop at exact prefixes.
MSC GS stage/pass faults retain real VS emulation/stage-in and PS work.
Every mode checks exact HRESULT, PSO presence/nullness and ordered trace;
mixed-family/wrong-stage modes require no compiler calls.

Share existing fresh-process, disabled-cache and staged executable/runtime
provenance machinery. The gate requires GS evidence but retains isolation
PARTIAL for mesh/library paths. Handle-stage lookup shared with tessellation.

## DXBC / AIRCONV Impact

Reuse GS HLSL through D3DCompile SM5.0. Inject E_FAIL at initialization,
combined mesh compilation or first object compilation. No MSC calls accepted.
No compiler lowering changes or production fault hooks.

## DXIL / MSC Impact

Reuse GS SM6.0 fixture. GS invalid-DXIL, unsupported, memory and second-pass
errors retain E_INVALIDARG, E_NOTIMPL, E_OUTOFMEMORY and E_NOTIMPL. No AIRCONV
calls accepted. Positive control delegates to real conversion/pipeline creation.

## Shared Runtime Impact

None. Import wrapping is test-linked, not installed DLL-internal interception.
Independently deploy matching normal/no-private runtime for verification.

## Tests Added

13 modes: two real controls, two mixed-family and two wrong-stage rejections,
three AIRCONV failures and four MSC failures. Two new host tests enforce GS
evidence and all modes; existing executable provenance tests cover GS too.

## Tests Run

2026-10-01, Apple M4, x64 Wine, MSC API 4.0.1. Both configurations reconfigured;
full release builds succeeded and reused fixture targets verified current.
Meson 3/3 each; Python gate 27/27. Runtime full gates use API/Shader Validation
before device creation. Diff check PASS.

## Runtime Results

Both gates: provenance PASS; GS 13/13, HS/DS 22/22, ordinary VS/PS 20/20 and
compute 11/11 PASS. Real AIRCONV control: three initializes/nine compiles;
real MSC control: six converts. Both return S_OK and non-null PSO. Existing
feature, validation/container/stage and min/max probes PASS. Typed-UAV DXIL
matrix remains FAIL; FL12 gates FAIL (exit 1). Normal installed runtime restored
after no-private testing and matching provenance verified PASS.
No GPU draw/readback/game/FPS acceptance claimed.
Ignored receipts: `build/fl12-gs-failure-normal.json` and
`build-no-private/fl12-gs-failure-no-private.json`.

## Self-review

Independent Standards/Spec review against `ab00071`.
Spec: zero scoped findings. Real controls, paired handles, distinct mesh/object
failures and exact rejection/failure traces satisfy the bounded contract.
Standards: zero bugs/hard violations; two optional heuristic smells (grouped
fixture paths/null placeholders and repeated mode generation). Retain bounded
baseline reuse; no broader test-interface refactor this round. Final handoff
confirms actual commit/status bookkeeping.

## Known Limitations

x64 MinGW source-linked oracle, not public DLL-internal interception or Native
Windows comparison. Control creates real PSO but does not submit GS draws.
Forced mesh/object compile failures exercise the first strip/index variant;
the control requires all variants but later-variant failure injection and
variant argument equality are not validated. Mesh/library paths remain open.

## Capability Status

PARTIAL overall; bounded GS invocation contract DXMT_LOCAL_PASS.

## Feature Level Impact

FL11_1 unchanged; FL12_0 FAIL; FL12_1 FAIL. No capability declaration changes.

## Git Status

Final handoff confirms actual local commit and worktree status.

## Push Status

NOT PUSHED.

## Next Recommended Task

Mesh-stage/compiler-failure invocation evidence, with real controls and
no-fallback assertions; keep library and GPU correctness separate.

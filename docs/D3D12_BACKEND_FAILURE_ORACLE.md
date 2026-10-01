# Task Analysis

## Current Branch

`feat/d3d12-1`; preserve dirty `include/native/directx`.

## Baseline

Read-only `origin/feat/d3d12`; task baseline `9496673`, merge-base `85bb2dd`.

## Local Commits Since origin/feat/d3d12

Reviewed the local sequence: volatility/tessellation, FL gates and isolation,
min/max rejection, typed UAV audit/view repair, offline DXIL round-trip,
origin lowering and static aliases. No repeats or capability promotion.

## Current State

Master Prompt sections 14/43 prioritize backend isolation. Existing gate
explicitly records the missing compiler-failure/no-fallback invocation oracle.
Offline alias evidence does not close this prerequisite.

## Existing Implementation

Compute PSO initialization classifies before routing. DXIL calls MSC conversion
and returns failure; DXBC initializes AIRCONV then compiles. Existing synthetic
validation and public PSO regressions do not count compiler invocations.

## Relevant Files

`d3d12_pipeline_compute.cpp`, `d3d12_shader_converter.cpp`,
`tests/dx12/meson.build`, `dx12_fl12_gate.py`, existing isolation tests.

## Existing Tests

Shader validation/container/stage matrix; gate Python tests; both build variants.

## D3D12 Contract

DXBC must only invoke AIRCONV, DXIL only MSC. A selected compiler failure must
return failure without another backend attempt. Invalid family/stage must reject
before compiler invocation. This tests routing, not GPU shader semantics.

## DXBC / AIRCONV Impact

Test-only interception of frontend initialization and compilation calls.

## DXIL / MSC Impact

Test-only interception of MSC conversion calls; preserve selected failure code.

## Shared Runtime Impact

None planned. Compile the actual production compute/converter sources into the
probe, intercept imports at test link time; do not add production fault switches,
counters, exported diagnostics or environment-controlled compiler failures.

## Missing Pieces

Invocation assertions, forced failures and positive controls reaching actual
production call sites; provenance-bound gate registration.

## Risks

Import wrapping can miss DLL-internal calls; explicitly exercise the probe-linked
production factory, not the uninstrumented DLL factory. Controls must show the
hooks are reached. Caches must not hide MSC calls. Scope is compute only;
graphics/tessellation/library invocation closure remains PARTIAL.

## Minimal Implementation Plan

Hypothesis: link-time wrapping can observe production compute routing without
changing production behavior. Evidence: direct AIRCONV/MSC imports at the
compute/converter call sites. Expected effect: deterministic no-fallback
failure signatures. Risk: import symbol interception/linkage or cache bypass.
Implement one bounded compute probe, integrate its evidence without declaring
full backend isolation PASS, independently review before local commit.

## Validation Plan

Reconfigure/build normal and no-private variants; valid DXBC/DXIL controls;
forced AIRCONV initialization/compile and MSC failures with exact call counts;
wrong-family/stage rejection; gate unit regressions; stable runtime provenance;
diff check and Standards/Spec review. No GPU dispatch/game claim.

## Capability Impact

None: FL11_1 ceiling and all capability/Shader Model bits unchanged.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

`origin/feat/d3d12`; task baseline `9496673`.

## Local Commit

The commit containing this report; its hash is supplied in the final handoff.

## Changed Files

New `tests/dx12/dx12_backend_failure.cpp`, `tests/dx12/meson.build`,
`dx12_fl12_gate.py`, `test_dx12_fl12_gate.py`, this report and the FL12 gate guide.

## Implementation

Link actual production compute, converter and persistence sources into the
x64 probe, wrapping imported AIRCONV initialize/compile and MSC conversion
function pointers. The positive controls call real compilers. Failure cases
check exact HRESULT, null PSO and exact backend counts, including zero calls
to the opposite backend. Test-local noncompute factory guards invalidate any
unexpected graphics/mesh call. Device identity validation uses real canonical
COM identities; an explicit empty root stays alive through PSO creation.
No production source or diagnostic ABI is changed.

Gate execution records every staged executable hash and requires it to match
the initial/final probe hash; missing/mixed provenance cannot hide execution
FAIL. Build variant verification includes the test-linked compute/converter
compile commands. Wine helpers exposed inherited output-pipe EOF delay; the
runner now captures to a temporary file, preserving timeout/exit/marker checks
without waiting for background helper handles to close.

## DXBC / AIRCONV Impact

Real initialization/compile positive control; forced initialization and compile
failures. Each returns E_FAIL with zero MSC calls. No AIRCONV compiler change.

## DXIL / MSC Impact

Real two-pass compile positive control. Invalid DXIL, unsupported shader,
out-of-memory and second-pass failure retain E_INVALIDARG, E_NOTIMPL,
E_OUTOFMEMORY and E_NOTIMPL respectively, with zero AIRCONV calls.
Each mode starts a fresh process; persistent shader cache disabled.

## Shared Runtime Impact

None. Test-linked copies and bounded guards are not installed as production
DLLs. Existing runtime deployment is used only to validate the fixtures.

## Tests Added

11 compute invocation cases per variant: two real PSO positive controls, six
forced failures, two wrong-stage rejections and one empty-container rejection.
Wrong-stage/empty cases require zero compiler calls. Five new Python gate tests
cover partial isolation, failed-call aggregation, executable provenance,
file capture status/markers and a real inherited-helper-handle regression.

## Tests Run

2026-10-01, Apple M4, x64 Wine, MSC API 4.0.1. Both variants reconfigured,
full release builds succeeded. Meson 3/3 passed independently; Python gate
suite 21/21 passed. Full gates executed independently with Metal API and
Shader Validation enabled before device creation. `git diff --check` passes.

## Runtime Results

Both normal/no-private reports have build/runtime provenance PASS and all
11 invocation cases PASS. Existing feature, shader validation/container/stage
and min/max rejection contract probes PASS. Mandatory typed-UAV matrix remains
FAIL on DXIL; both FL12 gates remain FAIL (expected runner exit 1).
Compute invocation requirement PASS; full backend isolation PARTIAL.

Initial AIRCONV cases stopped before compiler entry without an explicit root;
not accepted. After supplying the root, a Wine helper pipe timeout was also
not accepted. Final file-capture full-gate runs supersede both attempts.
Ignored evidence: `build/fl12-backend-failure-normal.json` and
`build-no-private/fl12-backend-failure-no-private.json` contain per-case output,
hashes and final provenance. Normal installed runtime was restored and verified
PASS after no-private testing. No dispatch/readback/game/FPS acceptance claimed.

## Self-review

Standards: one P2 executable-provenance finding, fixed by staged initial/final
hash conjunction and unit tests. Final source review has no actionable findings.
Spec: zero actionable findings; compute-only scope and missing graphics
invocation closure are explicit. Runner capture follow-up reviewed separately;
real helper-handle and status-matrix regressions added.

## Known Limitations

Import-wrapping probe is x64 MinGW only, not an interception of DLL-internal
compiler calls. Public DLL PSO/container regressions run alongside the actual
source-linked factory probe. Null-resource/empty-root compute only; graphics,
HS/DS/GS, library/mesh paths, root binding and GPU correctness are not closed.

## Capability Status

PARTIAL overall. Bounded compute invocation contract: DXMT_LOCAL_PASS.
No Native Windows oracle or full backend-isolation PASS is claimed.

## Feature Level Impact

FL11_1: unchanged. FL12_0: FAIL, not promoted. FL12_1: FAIL, not promoted.
No Shader Model, typed UAV, ROV, tiled or conservative capability change.

## Git Status

Task files only; dirty `include/native/directx` preserved. No binaries staged.

## Push Status

NOT PUSHED.

## Next Recommended Task

Extend the invocation oracle to ordinary VS/PS graphics and mixed-family
rejection, then HS/DS failure paths. Keep native min/max and general MSC typed
buffer alignment/lowering blockers explicit; resume mandatory tessellation
and raster/format gates without promoting partial capability evidence.

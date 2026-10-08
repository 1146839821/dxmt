# Task Analysis

## Current Branch

`feat/d3d12-1`; working tree clean at task start.

## Baseline

Task baseline `6819c90`; read-only integration baseline `origin/feat/d3d12`,
merge-base `85bb2dd`.

## Local Commits Since origin/feat/d3d12

Reviewed recent compute failure isolation and preceding typed-buffer/static
alias validation. Preserve existing capabilities, backend isolation and gates.

## Current State

Compute failure oracle passes; graphics failure invocation evidence remains
missing in Master Prompt section 14 and the current gate.

## Existing Implementation

Graphics PSO creation validates all stages/families before compilation. Both
ordinary backends compile VS then PS and immediately return compiler failures.
Existing import wrappers observe compute calls but graphics factories are
test-local fail-closed guards, not real graphics source-linked paths.

## Relevant Files

Production `d3d12_pipeline_graphics.cpp` (read only), existing
`dx12_backend_failure.cpp`, gate runner/unit tests and Meson.

## Existing Tests

11 compute invocation cases, public container/stage regressions, 21 Python
gate tests; normal/no-private full-gate reports.

## D3D12 Contract

DXBC VS/PS must only invoke AIRCONV; DXIL VS/PS only MSC. A selected-stage
failure must stop compilation and return its error without trying the other
backend. Mixed-family and wrong-stage PSOs must reject before compilation.

## DXBC / AIRCONV Impact

Test-only stage-aware initialization/compile failure wrappers; no lowering change.

## DXIL / MSC Impact

Test-only stage/pass-aware converter failure wrappers; preserve real positive
controls. No backend fallback or generated AIR rewriting.

## Shared Runtime Impact

No production edits planned. Link actual graphics sources into the existing
probe and remove graphics/mesh guard stubs; retain real device identity checking.

## Missing Pieces

Ordinary graphics VS/PS ordered-call signatures and exact error/PSO assertions.
HS/DS/GS, mesh/library, GPU draw semantics and complete capability closure remain
out of scope, even though their production code is linked for dependencies.

## Risks

Global call totals can conflate VS/PS or compiler passes; require ordered stage
records. Stage handles must be classified before initialization and compilation
records must identify the initialized handle, not assume ordinal call positions.
Caches must not suppress calls; each mode runs in a fresh process with persistent
shader caching disabled. Source-linked evidence does not intercept DLL internals.

## Minimal Implementation Plan

Hypothesis: reuse the test-only import wrappers with stage-aware fault selection
and ordered traces to close the ordinary graphics invocation gap.
Evidence: source validates families first and sequentially compiles VS/PS.
Expected effect: deterministic VS/PS no-fallback signatures.
Risk: stage identity, source dependencies, shader fixture compatibility.
Add isolated no-resource VS/PS fixtures, positive controls, first/second-pass
failures, mixed-family and wrong-stage rejection; keep overall isolation PARTIAL.

## Validation Plan

Reconfigure normal/no-private; compile fresh fixtures; full builds and Meson
tests; 11 compute plus graphics invocation cases in full gates, independent
runtime/variant and staged binary provenance. Metal API/Shader Validation enabled
before device creation. Standards/Spec self-review and diff check before local
commit, no push. Restore normal runtime after no-private testing.

## Capability Impact

None: no Feature Level, Shader Model, typed-UAV, ROV, tiled or raster promotion.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

`origin/feat/d3d12`; task baseline `6819c90`.

## Local Commit

The commit containing this report; final handoff supplies its hash.

## Changed Files

`tests/dx12/dx12_backend_failure.cpp`, `meson.build`,
`backend_failure_graphics.hlsl`, `dx12_fl12_gate.py`,
`test_dx12_fl12_gate.py`, this report and `D3D12_FL12_GATE.md`.

## Implementation

Link the actual production graphics factory into the existing x64 import-
wrapping probe. Remove test-local graphics/mesh guards. Identify AIRCONV
compilation stages by the initialized shader handle; identify MSC stage and
query/materialization pass. Inject faults only at the selected VS or PS stage.
Require exact ordered traces, HRESULT and PSO presence/nullness. Mixed-family
and wrong-stage cases require no compiler calls. Positive controls use real
compilers. No production fault hooks, runtime ABI changes or fallback.

Share invocation aggregation with the compute oracle, retaining staged binary
hashes, initial/final provenance and execution-FAIL precedence. Build variant
verification now also checks the test-linked graphics source's compile flags.

## DXBC / AIRCONV Impact

Real VS/PS control and selected-stage initialization/compile failures. Failures
return E_FAIL; no MSC call is accepted. No compiler lowering changes.

## DXIL / MSC Impact

Fresh no-resource SM6.0 VS/PS fixtures. Selected-stage invalid DXIL,
unsupported shader, memory and second-pass failures preserve E_INVALIDARG,
E_NOTIMPL, E_OUTOFMEMORY and E_NOTIMPL. No AIRCONV call is accepted.

## Shared Runtime Impact

None. Source-linked probe factories are test-only, not installed DLL changes.
Normal runtime restored after independent no-private testing; provenance PASS.

## Tests Added

20 graphics cases per variant: two real controls, two mixed-family rejections,
four wrong-stage rejections, four AIRCONV failures and eight MSC failures.
Two new Python tests require all graphics cases and preserve failed/missing
graphics evidence. Existing executable-provenance tests cover both oracles.

## Tests Run

2026-10-01, Apple M4, x64 Wine, MSC API 4.0.1. Both variants reconfigured;
full release builds and fresh VS/PS fixture builds succeeded. Meson 3/3 in
each variant; Python gate suite 23/23. Full gates ran independently with
Metal API/Shader Validation enabled before device creation. Diff check PASS.

## Runtime Results

Normal and no-private: build/runtime provenance PASS, all graphics 20/20 and
compute 11/11 invocation cases PASS. Existing feature, shader validation,
container/stage and min/max rejection probes PASS. Mandatory typed-UAV matrix
still FAIL on DXIL; both full gate commands exit 1 and FL gates remain FAIL.
Backend isolation remains PARTIAL. No GPU draw/readback/game/FPS acceptance.

Ignored receipts: `build/fl12-graphics-failure-normal.json` and
`build-no-private/fl12-graphics-failure-no-private.json`, including per-case
traces, HRESULTs and staged executable/runtime hashes.

## Self-review

Standards and Spec reviewed independently against task baseline `6819c90`.
Standards: zero actionable findings. Spec: zero implementation/scope findings;
handoff must confirm actual local commit and clean status rather than treating
precommit report wording as a receipt. Final handoff supplies that evidence.

## Known Limitations

x64 MinGW import wrapping observes actual source-linked production call sites,
not installed DLL-internal interception. Ordinary no-resource VS/PS PSO
creation only. HS/DS/GS, mesh/library failure paths and GPU semantics remain
unverified; linking their source does not validate those paths.

## Capability Status

PARTIAL overall. Ordinary VS/PS invocation contract: DXMT_LOCAL_PASS.
No Native Windows oracle or complete isolation PASS is claimed.

## Feature Level Impact

FL11_1: unchanged. FL12_0: FAIL, not promoted. FL12_1: FAIL, not promoted.
No Shader Model, typed-UAV, ROV, tiled or raster capability changes.

## Git Status

Task changes locally committed after verification; final handoff reports status.

## Push Status

NOT PUSHED.

## Next Recommended Task

HS/DS tessellation compiler-failure invocation evidence, with real controls
and stage-aware no-fallback assertions; keep GPU correctness separate.

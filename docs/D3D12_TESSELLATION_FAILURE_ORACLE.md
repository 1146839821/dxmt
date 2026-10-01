# Task Analysis

## Current Branch

`feat/d3d12-1`; working tree clean.

## Baseline

Task baseline `54b5a06`; integration baseline `origin/feat/d3d12`.

## Local Commits Since origin/feat/d3d12

Recent compute and ordinary VS/PS invocation oracles retained unchanged.

## Current State

Ordinary VS/PS/CS isolation has bounded invocation evidence. Tessellation
failure invocation coverage remains missing; full isolation stays PARTIAL.

## Existing Implementation

AIRCONV initializes VS, HS, DS, then PS; compiles PS, combined HS/DS domain,
then combined VS/HS hull for three index variants. MSC converts VS, PS, HS,
DS with two passes per stage. Families/stages validate before compiler entry.

## Relevant Files

Production graphics/converter sources read only. Extend backend-failure probe,
Meson, gate and unit tests; reuse `shader_backend_stages.hlsl` HS/DS fixtures.

## Existing Tests

20 graphics and 11 compute invocation cases; 23 host gate tests; full gates.

## D3D12 Contract

Selected HS/DS failures stop compilation without invoking the other backend.
Mixed-family/wrong-stage tessellation rejects before compilation. Exact error
and null PSO required; positive controls must use actual compiler paths.

## DXBC / AIRCONV Impact

Wrap combined tessellation Hull/Domain imports in the test only; record both
initialized handles' stages. No shader lowering changes.

## DXIL / MSC Impact

Extend stage identity to Hull/Domain; inject first/second-pass failures by
stage. Reuse fresh HS/DS SM6.0 fixtures and real VS stage-in emulation.

## Shared Runtime Impact

None planned. No installed DLL fault hooks or encoder/binding changes.

## Missing Pieces

HS/DS invocation oracle, including initialization and combined compilation.
GS/mesh/library, actual tessellated draws and complete capability closure
remain outside this task.

## Risks

Wine SM5 HS/DS compilation may be unavailable. Real control PSO can fail
later in reflection/library/pipeline creation; report that layer, never relax
the oracle or replace a real compiler with synthesized success.

## Minimal Implementation Plan

Hypothesis: existing source-linked wrappers can observe the real HS/DS path.
Evidence: dedicated combined AIRCONV imports and MSC stage-specific calls.
Expected effect: exact ordered failure prefixes with zero opposite calls.
Risk: stage-handle identity, compiler fixtures and real emulation control.
Reuse ordinary VS/PS fixtures and existing HS/DS HLSL; add bounded tess modes
with independent normal/no-private runtime and binary provenance.

## Validation Plan

Reconfigure/build both variants and fresh fixtures; Meson and host unit tests;
full gates under API/Shader Validation; restore normal runtime; independent
Standards/Spec review and local commit only, no push.

## Capability Impact

None. No Feature Level or tessellation GPU-correctness promotion.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

`origin/feat/d3d12`; task baseline `54b5a06`; integration merge-base `85bb2dd`.

## Local Commit

The commit containing this report; actual hash supplied in final handoff.

## Changed Files

Backend-failure probe, Meson, FL12 gate and host tests; this report and gate
guide. Production source and reused HS/DS HLSL remain unchanged.

## Implementation

Test-only import wrappers now observe AIRCONV combined Domain and Hull
compilation. Traces identify both initialized shader handles, not call ordinal.
Hull/Domain MSC calls have stage-specific failure selection and pass traces.
Extend existing graphics probe with HS/DS shaders and patch topology; real
compilers and real production graphics factory are retained. Exact ordered
prefix, error and PSO presence/nullness determine PASS; any opposite-backend
call invalidates the trace. Three AIRCONV hull variants are required in control.

Gate runs each mode in a fresh process with persistent shader cache disabled.
Reuse existing staged executable/runtime hash checks and FAIL precedence;
HS/DS evidence is mandatory, not sufficient for whole-isolation PASS.

## DXBC / AIRCONV Impact

Reuse existing HS/DS HLSL through D3DCompile SM5.0, and no-resource ordinary
VS/PS controls. Selected initialization or combined compile failures return
E_FAIL. Real control creates the tessellation PSO with four initializes, five
compiles, zero MSC calls. No compiler lowering or native pipeline changes.

## DXIL / MSC Impact

Reuse existing SM6.0 HS/DS fixtures and ordinary VS/PS fixtures. Real control
converts all four stages with eight calls and zero AIRCONV calls; emulated VS
stage-in and tessellation PSO creation succeed. Selected HS/DS invalid DXIL,
unsupported, memory and second-pass failures return E_INVALIDARG, E_NOTIMPL,
E_OUTOFMEMORY and E_NOTIMPL respectively.

## Shared Runtime Impact

None. Import wrapping is test-linked, not production DLL interception. Runtime
deployment is only for independently verified normal/no-private test runs.

## Tests Added

22 tessellation modes: two real controls, four mixed-family rejections, four
wrong-stage rejections, four AIRCONV failures and eight MSC failures.
Rejection modes require zero compiler calls. Two host tests enforce mandatory
tessellation evidence and all modes; executable provenance covers this oracle.

## Tests Run

2026-10-01, Apple M4, x64 Wine, MSC API 4.0.1. Both configurations reconfigured,
full release builds succeeded; existing HS/DS fixture targets verified current.
Meson 3/3 each and host gate 25/25. Both full gates run with API/Shader
Validation enabled before device creation; diff check PASS.

## Runtime Results

Normal and no-private full gates: provenance PASS; tessellation 22/22, ordinary graphics 20/20,
compute 11/11 PASS. Existing feature, validation/container/stage and min/max
probes PASS. Typed-UAV DXIL matrix remains FAIL; FL12 gates FAIL (exit 1).
Normal installed runtime restored afterward; matching provenance PASS.

Receipts: ignored `build/fl12-tess-failure-normal.json` and
`build-no-private/fl12-tess-failure-no-private.json`. Per-case compiler traces,
HRESULT and staged executable/runtime hashes retained. Initial control attempt
with an unstaged HLSL file returned UNVERIFIED; configure-file staging fixed
the missing input, and only subsequent real positive controls were accepted.

## Self-review

Independent Standards/Spec reviews against `54b5a06`.
Standards: zero hard findings; two optional heuristic smells (repeated mode
expansion and grouped HS/DS/HLSL paths). Retained narrow baseline reuse rather
than widening this test-only task with a fixture abstraction/refactor.
Spec: zero scoped findings; positive controls and compiler prefixes satisfy
the bounded contract. Final handoff supplies actual commit/status evidence.

## Known Limitations

x64 MinGW source-linked import oracle, not installed DLL-internal compiler
interception or Native Windows comparison. No draw/readback/game/performance
test: real PSO success does not prove tessellated GPU semantics. GS,
mesh/library invocation coverage and complete capability closure remain open.
Combined hull failure exercises the first index variant only; the real control
requires all three variants but later-variant failure injection is not covered.

## Capability Status

PARTIAL overall. HS/DS invocation contract: DXMT_LOCAL_PASS in accepted runs;
no complete backend-isolation PASS claimed.

## Feature Level Impact

FL11_1 unchanged; FL12_0 FAIL; FL12_1 FAIL. No capability declaration changes.

## Git Status

Final handoff confirms actual local commit and working-tree status.

## Push Status

NOT PUSHED.

## Next Recommended Task

GS combined compiler-failure invocation evidence, with real controls and
stage/handle-aware no-fallback traces; keep GPU correctness separate.

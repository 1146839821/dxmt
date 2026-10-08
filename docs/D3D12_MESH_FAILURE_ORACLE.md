# Task Analysis

## Current Branch

`feat/d3d12-1`; clean working tree at task start.

## Baseline

Task `cf62ae0`; integration `origin/feat/d3d12`, merge-base `85bb2dd`.

## Local Commits Since origin/feat/d3d12

Compute, ordinary graphics, tessellation and GS invocation oracles retained.

## Current State

Mesh/AS and library failure invocation evidence remains missing; isolation
PARTIAL. Existing mesh production factory is already source-linked in probe.

## Existing Implementation

Native mesh accepts DXIL only. It validates MS/AS/PS before conversion, then
converts MS, optional AS, optional PS with two passes each. No AIRCONV path.

## Relevant Files

Production graphics/converter/persistence read only; backend failure probe,
gate/host tests, existing mesh MS/AS/PS fixture targets and gate guide.

## Existing Tests

66 invocation cases and 27 host tests; existing native mesh SM6 probe.

## D3D12 Contract

Selected MS/AS/PS error must stop immediately without alternate compiler.
DXBC native stages and incorrect DXIL stages reject before conversion.
Exact HRESULT, PSO presence/nullness and ordered traces required.

## DXBC / AIRCONV Impact

Observe zero AIRCONV calls even for DXBC stage rejection; no lowering changes.

## DXIL / MSC Impact

MS/AS stage identity and selected first/second-pass error injection. Real
controls with/without AS, using existing SM6.5 fixtures and explicit root.

## Shared Runtime Impact

None planned. Test-only source-linked native factory; no production fault hooks.

## Missing Pieces

MS/AS invocation oracle. Pipeline library and GPU mesh draws remain open.

## Risks

Real MS/AS payload, reflection and pipeline compatibility may fail after
conversion. Do not synthesize compiler success or relax control acceptance.
Internal factory evidence does not cover public stream parsing itself.

## Minimal Implementation Plan

Hypothesis: existing MSC wrappers can observe native mesh stage/pass failures.
Evidence: production converts MS, AS, PS sequentially and returns failures.
Expected effect: exact stopping prefixes and zero AIRCONV calls.
Risk: fixture compatibility, correct factory data defaults and capability gates.
Add independent mesh probe modes and mandatory gate evidence; retain PARTIAL.

## Validation Plan

Reconfigure/full build both variants, current fixture targets, Meson/host
tests and full Wine gates under API/Shader Validation. Bind runtime/executable
provenance, restore normal, Standards/Spec review, local commit without push.

## Capability Impact

None. No mesh tier/SM/Feature Level or GPU-correctness promotion.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

`origin/feat/d3d12`; task baseline `cf62ae0`, merge-base `85bb2dd`.

## Local Commit

Actual hash supplied in final handoff for the commit containing this report.

## Changed Files

Backend-failure probe, FL12 gate/host tests, this report and gate guide.
Production sources, Meson targets and existing mesh fixtures unchanged.

## Implementation

Call the actual source-linked native mesh factory with explicit root and
existing MS/AS/PS fixtures. Identify MSC Mesh and Amplification stages;
stage/pass-specific faults stop at exact ordered prefixes. Require exact
HRESULT, PSO presence/nullness and trace equality. Two real controls cover
optional AS. Empty MS, wrong DXIL stage and DXBC native slots reject with
zero compiler calls. The mixed DXBC PS fixture has a valid Pixel stage, so
family rejection is not conflated with a PS stage error. SM5 has no native
MS/AS stages; DXBC VS containers test the DXIL-only contract for those slots.

Reuse existing fresh-process, disabled persistent cache, staged executable
and runtime provenance checks and FAIL precedence. Mesh evidence mandatory;
isolation PARTIAL still discloses pipeline-library failure invocation gap.

## DXBC / AIRCONV Impact

Zero AIRCONV calls accepted. DXBC native-stage fixtures reject E_NOTIMPL
before any compiler call. No parser, lowering or production hook change.

## DXIL / MSC Impact

Real MS+PS control has four conversions, MS+AS+PS six. Selected stage invalid
DXIL, unsupported shader, memory and second-pass failures retain E_INVALIDARG,
E_NOTIMPL, E_OUTOFMEMORY and E_NOTIMPL. No fake metallib/PSO success supplied.

## Shared Runtime Impact

None. The test calls native factory directly, not public stream parsing.
Root reference in stream data cleared before raw root/device release.

## Tests Added

21 mesh modes: two controls, empty MS, three wrong-stage rejections, three
DXBC rejections and twelve selected MS/AS/PS compiler failures. Two host tests
require mesh evidence/all modes; executable provenance checks cover mesh too.

## Tests Run

2026-10-01, Apple M4, x64 Wine, MSC API 4.0.1. Both configurations reconfigured,
full release builds succeeded; MS/AS/PS fixtures compiled from unchanged
sources. Meson 3/3 each; Python 29/29. Full gates under API/Shader Validation
before device creation; diff check PASS.

## Runtime Results

Both initial real controls PASS: S_OK, non-null PSO, exact MSC traces and zero
AIRCONV calls. Both full gates: provenance PASS, mesh 21/21 and existing
66/66 invocation cases PASS; feature, validation/container/stage and min/max
probes PASS. Typed-UAV DXIL matrix remains FAIL, keeping FL12 gates FAIL
(exit 1). Normal installed runtime restored afterward and provenance verified
PASS. Initial build used an
incorrect nonexistent Mesh pipeline enum; fixed to the existing Graphics
pipeline type before accepted builds/tests. No GPU draws/readback/game/FPS.
Ignored receipts: `build/fl12-mesh-failure-normal.json` and
`build-no-private/fl12-mesh-failure-no-private.json`.

## Self-review

Independent Standards/Spec review against `cf62ae0`.
Spec: zero scoped missing/wrong/extra findings. Standards: zero hard findings;
one optional repeated MSC fault-decoding heuristic. Retained narrow baseline
reuse; no broader decoder refactor this round. Ownership/ref balancing and
real compiler delegation reviewed. Final handoff confirms actual commit/status.

## Known Limitations

x64 MinGW source-linked import oracle, not public DLL-internal interception
or Native Windows comparison. Real native pipeline creation only, not payload
execution, DispatchMesh/readback, public stream parsing, library-cache loads
or complete capability closure. DXIL shader-library/ray-tracing compilation
is also outside this probe; pipeline-library caching and shader libraries
must not be conflated. No optional-PS absence control this round.

## Capability Status

PARTIAL overall; bounded native invocation contract DXMT_LOCAL_PASS.

## Feature Level Impact

FL11_1 unchanged; FL12_0 FAIL; FL12_1 FAIL. No mesh/SM declaration changes.

## Git Status

Final handoff confirms actual commit and working-tree status.

## Push Status

NOT PUSHED.

## Next Recommended Task

Pipeline-library load/cache-hit/miss compiler invocation evidence. Keep public
stream parsing and GPU native-mesh semantics separate.

# Task Analysis

## Branch / baseline / current state

`feat/d3d12-1`, clean task baseline `0073150`; integration
`origin/feat/d3d12`, merge-base `85bb2dd`. Prior 156 invocation modes and
37 host tests retained. Overall isolation PARTIAL, FL12 gates FAIL.

## Existing implementation / relevant files / evidence

`ConvertD3D12LibraryShader` calls the same internal MSC conversion path with
library permission and a requested export. It validates bytecode policy before
the compiler, performs sizing/materialization, maps backend errors to HRESULT,
and caches only successful conversion. Raytracing state-object candidate-stage
probing continues after candidate conversion failures and can return E_NOTIMPL;
that separate caller contract is not covered by converter tests.
Reuse source-linked converter/import oracle and six-stage ray library fixture.
Change test, gate and docs only. Existing ray-stage direct MSC probe can load
all six metallibs but does not observe production converter failure routing.

## Hypothesis / expected effect / risk / minimal plan

Hypothesis: library converter errors never enter AIRCONV or poison successful
retry/cache reuse. Expected effect: exact stage/export/pass traces for RayGen,
Miss, ClosestHit, AnyHit, Intersection and Callable controls/failures; same-process
retry requires both real passes, subsequent fault-armed conversion hit no calls.
Risk: second-pass failure leaves allocated output storage even though no valid
conversion is published. Assert HRESULT and backend/entry validity, not empty
storage; callers must honor failure. Real successful output must load into Metal
and contain its returned function. No fake success or production fault hooks.

Add stage/export-aware MSC import observation. Per stage: real control and
invalid/unsupported/memory/materialization failures followed by retry/cache hit.
Add legacy, ordinary DXIL, empty entry and payload-qualifier precompiler rejection.
Make evidence mandatory while explicitly retaining state-object isolation gap.

## Backend / shared runtime / capabilities / missing pieces

DXIL library uses MSC only. DXBC and ordinary DXIL rejection must call neither
compiler. No production lowering, cache, ABI, residency or capability changes.
State-object failure propagation and stage probing, fused hit-group compilation,
GPU tracing, native Windows, gameplay/FPS remain separate. No full isolation,
FL12/SM/raytracing-tier promotion; FL11_1 unchanged.

## Validation plan

Independent normal/no-private reconfigure/full builds, six-stage and rejection
fixtures, Meson/host tests, full Wine gates with API/Shader Validation before
device creation and executable/runtime provenance. Restore normal and verify.
Parallel Standards/Spec review against `0073150`; Task Result and local commit
without push. No state-object or GPU success inferred from metallib creation.

# Task Result

## Implementation / evidence

Added 34 mandatory production-converter invocation modes: six ray stages times
real control, invalid input, unsupported shader, memory and materialization
failure; four precompiler rejection modes. Import interception observes actual
stage, export and pass. No production fault hooks or AIRCONV fallback introduced.
Failed conversion checks HRESULT and invalid backend/entry; partial storage
after materialization failure is not mistaken for valid output. Same-process
retry requires both real MSC passes, then fault-armed cache reuse requires zero
compiler calls and identical metallib bytes/entry. Every successful output loads
as a real Metal library and resolves its returned function.

Existing 156 invocation modes retained. New host checks make missing/failed
library evidence mandatory and retain executable/runtime hash verification.
No production code, cache policy, ABI, capability or Feature Level change.

## Validation

Independent normal/no-private reconfigure and full builds PASS; fixtures built
in each configuration. Host tests 39/39 PASS; Meson 3/3 PASS per configuration.
Normal full gate: 190/190 invocation modes PASS, including all 34 new modes;
build/runtime provenance PASS. Metal API and Shader Validation enabled before
device creation for every invocation; no validation errors observed in them.
No-private full gate likewise: 190/190 invocation modes PASS, provenance PASS,
API/Shader Validation enabled in all 190 cases and no validation errors observed.
Both full runners exit 1 because the existing typed-UAV DXIL matrix FAIL and
remaining FL requirements keep the FL12 gates FAIL; this is not an oracle failure.
Existing six-stage direct MSC probe PASS in both configurations. Installed normal
runtime restored and provenance reverified PASS before commit.

Ignored receipts: `build/fl12-shader-library-normal.json`,
`build-no-private/fl12-shader-library-no-private.json`, corresponding
`shader-library-gate.log`, sync/restore logs and Meson test logs.

Initial ad-hoc control execution omitted the fixture staging override and was
UNVERIFIED for missing mode-named files; correcting runner inputs yielded six
real controls PASS. No production repair or trace expectation relaxation.

## Self-review

Independent Standards/Spec review against `0073150`, including this document.
Standards: zero hard violations, two nonblocking heuristics (repeated fault/error
mapping and adjacent host-test status assertions). Retain the bounded extension
without unrelated test restructuring. Spec: zero scoped implementation findings;
runtime acceptance pending at review time resolved by the results above.
MSC-compilation skill guided real six-stage output checks; Metal-validation skill
guided environment before device creation. Final diff check PASS.
Compiler/Metal-function acceptance
does not establish raytracing GPU semantics.

## Known limitations / next task

Overall backend isolation remains PARTIAL: state-object candidate-stage probing
and caller error propagation are not covered. GPU tracing, fused hit groups,
native Windows, gameplay and FPS remain unverified. Existing typed-UAV DXIL
matrix failure and other missing FL requirements keep both FL12 gates FAIL.
FL11_1 and advertised capabilities unchanged.
Next: bounded state-object candidate-stage/failure routing oracle, preserving
the distinction between converter HRESULT and caller probing/error contract.
Final handoff confirms actual local commit and clean tree. NOT PUSHED.

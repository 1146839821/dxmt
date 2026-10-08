# Task Analysis

## Branch / baseline / current state

`feat/d3d12-1`, clean task baseline `cee5c82`; integration
`origin/feat/d3d12`, merge-base `85bb2dd`. Prior stage and compute/ordinary/
tessellation library oracles retained. Isolation PARTIAL, FL12 gates FAIL.

## Existing implementation / files / tests

Graphics library retains PSO only in memory and serializes metadata; cold load
calls the production graphics factory. Direct GS oracle already observes
AIRCONV VS/GS/PS initialization, PS compilation and paired VS+GS mesh/object
variants, or MSC VS/PS/GS conversions. Shared library helper provides real DLL
seed, source-linked cold rebuild and same-library retry. Reuse existing GS,
VS/PS and HLSL fixtures; change tests, gate and documentation only.
Baseline: 141 invocation modes, 35 host tests and public persistence regression.

## Hypothesis / evidence / expected effect / risk

Hypothesis: GS library retained hits compile nothing; metadata reload uses
the correct backend and failed rebuilds remain retryable. Evidence: production
library lifecycle and existing paired VS+GS import wrappers. Expected effect:
exact GS init, first mesh/object compile failure prefixes, HRESULT/nullness,
same-library retry and fault-armed zero-call identity hit.
Risk: successful VS/PS MSC conversion is cached before GS fails, so retry must
convert only GS. AIRCONV retry must require every mesh/object variant again.
DLL seed must not warm source-linked conversion cache. Do not fabricate PSOs,
metallibs or compiler success, or add production fault hooks.

## Contract / minimal plan

Extend shared library helper with GS fixture setup. Add retained/reload/missing/
key mismatch controls per backend, AIRCONV GS init/mesh/object failures and
MSC GS invalid/unsupported/memory/materialization failures. Assert exact trace,
HRESULT/null output, no alternate compiler, same-library retry and retained
identity. Make evidence mandatory and preserve all previous modes.

## Backend / runtime / capability / missing pieces

DXBC stays AIRCONV; DXIL stays MSC. No production cache, ABI, residency,
synchronization or capability changes. Shader-library/raytracing failure
invocations, library1 stream interception, race stress, Native Windows and
GPU draw/readback/gameplay remain separate. Failure injection targets the
first mesh/object variant, not every later variant. No full isolation/FL/SM
promotion; FL11_1 unchanged.

## Validation plan

Independent normal/no-private reconfigure/full build and current fixture
targets; Meson/host tests, full Wine gates with Metal API/Shader Validation
before device creation and executable/runtime provenance. Public persistence
regressions both variants, restore normal and verify. Parallel Standards/Spec
review against `cee5c82`; full Task Result and local commit without push.

# Task Result

## Branch / baseline / local commit / changed files

`feat/d3d12-1`; task baseline `cee5c82`, integration `origin/feat/d3d12`,
merge-base `85bb2dd`. Actual containing commit hash supplied in final handoff.
Changed backend-failure executable, FL12 runner/host tests, guide and this
report. Production source, Meson linkage and fixture sources unchanged.

## Implementation / tests added

Extend shared real-library lifecycle with GS setup. Fifteen fresh-process
modes: retained/reload/missing/key mismatch for each backend, AIRCONV GS
initialization/first mesh/first object failure, and MSC GS
invalid/unsupported/memory/materialization failure. DLL seed creates a real
PSO and metadata outside executable compiler interception, leaving its cache
cold; setup and deserialization require empty trace. Real source-linked
library/factory handles cold loads. Exact stopping trace, HRESULT/null output
and zero alternate-backend calls are mandatory. Disable fault and retry in
the same library, then re-arm it and require identical retained PSO with no
new compiler calls. No fabricated compiler success, metallib or PSO.

Two host tests require the new evidence and preserve failed cases even when
runtime hashes disagree; executable provenance tests include GS library.
All prior modes remain mandatory. Overall isolation remains PARTIAL with the
shader-library/raytracing gap disclosed; no feature-level closure inferred.

## DXBC / AIRCONV impact

Full cold/retry trace: VS, GS, PS initialization; PS compilation; two groups
of one paired VS+GS mesh and three paired VS+GS object compiles (12 events).
GS initialization failure stops at event 2, first mesh failure at 5 and first
object failure at 6. E_FAIL/null PSO, no MSC calls. Retry requires the complete
trace anew. Later mesh/object variant-specific failure injection is outside
scope; successful controls still require all variants. No lowering change.

## DXIL / MSC impact

Full cold trace: VS, PS, GS query/materialization (six events). Invalid,
unsupported, memory and materialization failures preserve E_INVALIDARG,
E_NOTIMPL, E_OUTOFMEMORY and E_NOTIMPL with no AIRCONV calls. Successful VS/PS
stay in process-local conversion cache; same-library retry requires only GS
query/materialization. Subsequent retained hit adds no compiler calls.
Conversion reuse is not native binary PSO persistence or a performance result.

## Shared runtime impact

None. Test-only executable imported compiler wrappers; real production library,
factory and DLL seed. Existing COM RAII. Persistent shader cache disabled;
production process-local cache left intact.

## Tests run / runtime results

2026-10-01, Apple M4 / x64 Wine / MSC API 4.0.1. Independent normal and
`DXMT_NO_PRIVATE_API` reconfigure/full builds succeeded; existing VS/PS and GS
fixture targets checked. Meson 3/3 per variant, host tests 37/37. Both full
gates launched with Metal API/Shader Validation before device creation. New
GS library 15/15 and prior invocation 141/141 per variant PASS.
Build/runtime/executable provenance PASS; no Metal validation error diagnostics
observed in new GS library cases. First accepted full runs passed without
production repair or weakened trace expectations.

AIRCONV first object failure stops after six events; same-library retry
requires all 12 events, including two mesh and six object compiles. MSC GS
materialization failure stops at six events; retry adds only GS query and
materialization. All subsequent retained hits add zero compiler calls.

Existing public persistence regression PASS both configurations with root-UAV
compute and ordinary SM6 graphics fixtures; it does not substitute for GS
GPU execution or GS compiler interception. Feature, shader validation,
container/stage and min/max probes PASS. Existing typed-UAV DXIL matrix FAIL
keeps both FL12 gates FAIL and full runner exit 1. Normal installed runtime
restored afterward and provenance reverified PASS. No new draw/readback,
gameplay or performance acceptance claimed.

Ignored receipts: `build/fl12-geom-library-normal.json`,
`build-no-private/fl12-geom-library-no-private.json` and corresponding
`geom-library-public.log` files.

## Self-review

Independent parallel review against `cee5c82`. Standards: zero hard
violations; two nonblocking heuristics (duplicated geometry expectations and
positional fixture-path clump). Retain narrow adjacent-test reuse instead of
unrelated restructuring. Spec: zero implementation findings; pending runtime
acceptance during review resolved by the results above before commit.
No unresolved scoped findings. Final diff check PASS. MSC-compilation skill
guided real emulation controls; Metal-validation skill guided environment
before device creation. No fabricated success or capability promotion.

## Known limitations / capability / Feature Level impact

GS library PSO creation only, not GPU execution, Native Windows, race stress,
library1 stream interception, shader-library/raytracing, gameplay or FPS.
VS/PS failure injection within GS library is not added; direct GS and ordinary
VS/PS probes remain separate. Overall isolation PARTIAL; bounded invocation
acceptance only. FL11_1 and advertised capabilities unchanged. FL12 gates
remain FAIL due to existing typed-UAV DXIL matrix failure.

## Git / push / next task

Final handoff confirms actual commit and clean working-tree status. NOT PUSHED.
Next: inspect shader-library/raytracing compiler entry points and add bounded
failure-isolation evidence; do not conflate DXIL shader libraries with PSO
pipeline-library caching or declare GPU semantic acceptance.

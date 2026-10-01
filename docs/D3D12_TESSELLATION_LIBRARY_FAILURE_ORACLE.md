# Task Analysis

## Branch / baseline / current state

`feat/d3d12-1`, clean task baseline `b501c75`; integration
`origin/feat/d3d12`, merge-base `85bb2dd`. Prior stage and compute/ordinary
graphics library oracles retained. Overall isolation PARTIAL, FL12 gates FAIL.

## Existing implementation / relevant files / existing tests

Production graphics library stores metadata and an optional retained PSO.
Deserialization rebuilds through the graphics factory. Existing direct HS/DS
oracle establishes AIRCONV initialization, PS compilation, paired HS+DS domain
and VS+HS hull compilation; MSC converts VS, PS, HS, DS in order. The library
helper already source-links real persistence/factory/converter implementations
and seeds through the real DLL to keep its executable converter cache cold.
Change only backend probe, gate/host tests and documentation; reuse existing
VS/PS, HS/DS and HLSL fixtures. Baseline: 121 invocation modes, 33 host tests.

## Hypothesis / evidence / expected effect / risk

Hypothesis: tessellation library retained hits call no compiler, metadata reload
rebuilds in the correct backend, and failed rebuilds remain retryable. Evidence:
the source-linked graphics factory, paired AIRCONV wrappers and process-local
MSC conversion cache. Expected effect: exact HS/DS failure stopping prefixes,
HRESULT/null output, same-library retry and fault-armed retained identity hit.
Risk: MSC success before the failed stage is cached; HS failure retries HS+DS,
DS failure retries only DS. Do not reset or bypass that production cache.
AIRCONV retries require the full combined compile trace. Controls must create
real metallibs and PSOs; no synthetic success or production fault hooks.

## Contract / minimal implementation plan

Extend shared library helper with HS/DS fixture setup and patch topology.
Add retained/reload/missing/key-mismatch controls for both backends and selected
HS/DS initialization/compile (AIRCONV), invalid/unsupported/memory/second-pass
(MSC) failures. Assert ordered traces, backend isolation, HRESULT/nullness,
same-library retry and subsequent retained PSO identity without more calls.
Make evidence mandatory in the gate; preserve prior compute/ordinary modes.

## Backend / runtime / capability impact and missing pieces

DXBC remains AIRCONV, DXIL remains MSC. No runtime ABI, residency, synchronization,
compiler cache policy or capability changes. Library1 stream interception,
GS library failures, shader-library/raytracing, concurrency, GPU execution and
gameplay remain separate. Overall isolation PARTIAL; no SM or FL promotion.

## Validation plan

Reconfigure/full-build normal and no-private independently, check current fixture
targets, Meson/host tests, full Wine gates with API/Shader Validation before device
creation and executable/runtime provenance. Public persistence regressions both
variants, restore normal and verify. Parallel Standards/Spec review against
`b501c75`, record Task Result, local repository-style commit without push.

# Task Result

## Branch / baseline / local commit / changed files

`feat/d3d12-1`, task baseline `b501c75`; integration `origin/feat/d3d12`,
merge-base `85bb2dd`. Actual containing commit hash supplied in final handoff.
Changed backend-failure executable, FL12 runner/host tests, gate guide and
this report. Production source, Meson definitions and fixtures unchanged.

## Implementation / tests added

Extend shared library lifecycle with HS/DS fixtures and patch topology.
Twenty fresh-process modes: retained/reload/missing/key mismatch per backend,
selected HS/DS AIRCONV initialization/compile failures and MSC
invalid/unsupported/memory/materialization failures. Real DLL seed creates
PSO/cache metadata without warming executable converter cache. Setup and
deserialization require empty import trace. Actual source-linked production
library/factory rebuilds metadata-only entries; no production fault hooks.
Failed loads require expected HRESULT/null output and exact stopping prefix.
Retry in the same library with fault disabled, then re-arm a fault and require
the retained PSO interface identity with no additional compiler calls.

Two host tests make evidence mandatory and preserve a failed mode even with
inconsistent runtime hashes; executable provenance test includes new oracle.
Prior compute/ordinary graphics library modes remain required. Gate keeps
overall isolation PARTIAL and explicitly discloses GS library and shader-library
failure evidence gaps rather than treating this as full isolation acceptance.

## DXBC / AIRCONV impact

Full cold/retry trace: VS, HS, DS, PS initialization; PS compile; one HS+DS
domain compile; three VS+HS hull compiles. HS initialization stops at call 2,
DS initialization at 3; domain compile failure at 6, hull compile at 7.
Failures preserve E_FAIL/null PSO and zero MSC calls. Real success delegates
to AIRCONV and real pipeline creation. No parser/lowering change.

## DXIL / MSC impact

Full cold trace: VS, PS, HS, DS query/materialization, eight ordered calls.
Selected HS or DS errors stop at that stage/pass, preserving E_INVALIDARG,
E_NOTIMPL, E_OUTOFMEMORY or E_NOTIMPL and zero AIRCONV calls. Successful prior
stages remain in the production process-local conversion cache: HS failure
retry converts HS+DS (four calls), DS failure retry converts only DS (two).
Library PSO retention and shader conversion reuse are distinct assertions.

## Shared runtime impact

None. Test-only imported compiler interception at source-linked call sites;
real DLL seed and cached metadata. Persistent cache disabled for probes; the
MSC process cache is not reset or bypassed. Existing COM RAII ownership.

## Tests run / runtime results

2026-10-01, Apple M4 / x64 Wine / MSC API 4.0.1. Both normal and
`DXMT_NO_PRIVATE_API` independently reconfigured and fully built. Existing
VS/PS and HS/DS fixture targets checked. Meson 3/3 each; host tests 35/35.
Full gates ran with Metal API/Shader Validation before device creation.
New HS/DS library 20/20 and prior invocation 121/121 per variant PASS.
Build/runtime/executable provenance PASS. No Metal validation error diagnostics
observed in new cases. All new modes passed their first accepted full run;
no production repair or weakening of expected traces was needed.

AIRCONV hull compilation failure stops at the first VS+HS call; retry contains
the full one domain/three hull compile sequence. MSC DS materialization
failure stops after eight calls; retry adds only the two DS calls. Retained
hits add no compiler calls in both backends. Positive controls create real PSOs.

Existing public pipeline-persistence regression PASS both configurations using
root-UAV compute and ordinary SM6 graphics fixtures; this public regression
does not itself supply an HS/DS invocation or GPU execution oracle. Feature,
shader validation, container/stage and min/max probes PASS. Typed-UAV DXIL
matrix remains FAIL, other typed-UAV subcases PASS. Both full runners exit 1
and FL12 gates remain FAIL. Normal installed runtime restored and reverified
against the normal build, PASS.

Ignored receipts: `build/fl12-tess-library-normal.json`,
`build-no-private/fl12-tess-library-no-private.json`, and corresponding
`tess-library-public.log` files. No new gameplay/FPS or GPU semantic acceptance.

## Self-review

Independent Standards review: zero hard violations; two optional heuristics
(duplicated expected traces and HS/DS/source fixture-path parameter clump).
Keep narrow adjacent-test reuse rather than unrelated restructuring.
Spec review: zero implementation findings; its one pending validation/Task
Result evidence item is resolved by the accepted results above before commit.
No unresolved scoped findings. Final diff check PASS. MSC-compilation skill
guided real emulation compiler controls; Metal-validation skill guided launch
environment before device creation. No fabricated metallib/PSO success.

## Known limitations / capability / Feature Level impact

HS/DS library failure/retry PSO creation only, not GPU draw/readback,
tessellation visual quality, gameplay/FPS, Native Windows, library1 stream
interception or race stress. VS/PS failure injection inside tessellation
library is not added; ordinary VS/PS and direct tessellation probes remain.
GS library and shader-library/raytracing failures remain separate. Overall
isolation PARTIAL, bounded local invocation acceptance only. FL11_1 and
capabilities unchanged; FL12 gates remain FAIL due to existing typed-UAV DXIL.

## Git / push / next task

Final handoff confirms actual commit and clean working-tree status. NOT PUSHED.
Next: GS pipeline-library combined mesh/object failure and retry traces;
keep shader-library/raytracing and GPU tessellation acceptance separate.

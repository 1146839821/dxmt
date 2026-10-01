# Task Analysis

## Branch / baseline / current state

`feat/d3d12-1`, clean task baseline `750a24c`; integration
`origin/feat/d3d12`, merge-base `85bb2dd`. Prior local compute, ordinary,
tessellation, geometry, mesh and compute-library oracles are retained.
Backend isolation PARTIAL; both FL12 gates FAIL.

## Existing implementation / relevant files / tests

Production `LoadGraphicsPipeline` validates and hashes the descriptor, then
returns a retained PSO or calls the graphics factory when the entry has no
PSO. Serialization stores metadata, not native pipeline binaries. Existing
backend probe already source-links persistence, graphics and converter code.
Reuse its real DLL seed technique and library lifecycle; change only tests,
gate and documentation. Existing coverage: 101 invocation modes, 31 host tests
and public pipeline-persistence regression.

## Hypothesis / evidence / expected effect / risk

Hypothesis: ordinary VS/PS library hits compile nothing; deserialized entries
rebuild in the correct backend and survive failed rebuilds. Evidence:
`PipelineLibraryEntry`, `LoadGraphicsPipeline`, `LoadPipeline` and metadata-only
serialization. Expected effect: exact stage/pass stopping traces, HRESULT,
null output, same-library retry and retained PSO identity evidence.
Risk: successful VS conversion before a PS failure warms the process-local
MSC conversion cache, so the retry should compile only PS, not fabricate a
second VS conversion. Keep the production cache policy unchanged. Seed in the
real DLL to avoid warming the source-linked cache before the cold load.

## Contract and minimal implementation plan

Extend the existing library lifecycle helper with ordinary graphics setup;
retain all compute modes. Real DLL PSOs, cached metadata and production library
only; no fake compiler success or production fault hooks. Add AIRCONV VS/PS
initialization/compile failures and MSC VS/PS query/materialization failures.
Exact ordered traces must reject the other backend. Controls, missing name
and valid descriptor key mismatch remain mandatory. Failed rebuild retries
on the same library, then a fault-armed hit returns the same PSO without calls.

## Backend / shared runtime / missing pieces / capability impact

DXBC uses AIRCONV; DXIL uses MSC. No production change, ABI, residency,
synchronization or capability declaration change. Ordinary VS/PS library only;
HS/DS, GS library rebuilds, library1 stream interception, cache races,
shader-library/raytracing and GPU draws remain separate. Overall isolation
stays PARTIAL and FL11_1 unchanged; no FL12 or SM promotion.

## Validation plan

Reconfigure and full build normal/no-private independently; current VS/PS
fixture targets, Meson/host tests and full Wine gates with Metal API/Shader
Validation before device creation. Bind executable and installed-runtime
provenance. Public persistence regression both variants; restore normal and
verify. Parallel Standards/Spec review against `750a24c`, record results,
local repository-style commit, no push. FPS/gameplay are not acceptance here.

# Task Result

## Branch / baseline / local commit / changed files

`feat/d3d12-1`, task baseline `750a24c`; integration `origin/feat/d3d12`,
merge-base `85bb2dd`. Actual containing commit hash supplied in final handoff.
Changed backend-failure executable, FL12 runner/host tests, gate guide and
this report. Production sources, fixtures and Meson definitions unchanged.

## Implementation / tests added

Extend the existing library lifecycle helper for ordinary VS/PS, retaining
all compute modes. Share real DLL seed, metadata serialization/reload,
same-library retry and retained-hit checks. Twenty fresh-process modes:
retained, reload, missing name and key mismatch for each backend; VS/PS
AIRCONV initialization/compile failures and MSC invalid, unsupported, memory
and materialization failures. Two host tests require the new oracle and
preserve a failing mode despite inconsistent runtime hashes. Existing
executable provenance test includes the new oracle. Gate keeps all-library
isolation PARTIAL and explicitly discloses HS/DS/GS and shader-library gaps.

## DXBC / AIRCONV impact

Ordinary cold/retry trace is `air.init.vs;air.compile.vs;air.init.ps;air.compile.ps`.
Selected-stage failures stop at the appropriate prefix and return E_FAIL,
null PSO, zero MSC calls. Initialization and compilation success delegate to
real AIRCONV; no production hook or shader lowering change.

## DXIL / MSC impact

Ordinary cold trace is `msc.vs.query;msc.vs.materialize;msc.ps.query;msc.ps.materialize`.
Invalid, unsupported, memory and materialization failures preserve E_INVALIDARG,
E_NOTIMPL, E_OUTOFMEMORY and E_NOTIMPL, null PSO, zero AIRCONV calls. When PS
fails, the successful VS remains in the converter's process cache; same-library
retry requires only `msc.ps.query;msc.ps.materialize`. When VS fails, retry
requires all four calls. A fault-armed subsequent library hit returns the same
PSO with no additional compiler calls. This is not a native binary PSO cache
or a measured performance claim.

## Shared runtime impact

None. Source-linked production library and factory inside the test executable;
only its compiler import call sites are wrapped. Real DLL seed uses separate
conversion cache and is explicitly outside instrumentation. Setup and
deserialization must have empty trace. COM ownership uses existing RAII.

## Tests run / runtime results

2026-10-01, Apple M4 / x64 Wine / MSC API 4.0.1. Normal and
`DXMT_NO_PRIVATE_API` independently reconfigured and fully built; current
ordinary VS/PS fixture targets checked. Meson 3/3 each and host tests 33/33.
Both full gates launched with Metal API and Shader Validation before device
creation. New ordinary graphics library 20/20, prior invocation 101/101 per
variant PASS. Build/runtime/executable provenance PASS. No Metal validation
error diagnostics observed in the new library cases.

Existing public pipeline-persistence regression PASS both configurations
with root-UAV compute and ordinary SM6 graphics fixtures. Feature,
shader-validation, container/stage and min/max probes PASS. Typed-UAV DXIL
matrix remains FAIL (other typed-UAV subcases PASS); full runner correctly
returns 1 and FL12 gates remain FAIL. No GPU draw/gameplay/FPS claim from
the new invocation tests. Normal installed runtime restored afterward and
build/installed-runtime provenance reverified PASS.

Ignored receipts: `build/fl12-graphics-library-normal.json`,
`build-no-private/fl12-graphics-library-no-private.json`, and corresponding
`graphics-library-public.log` files. All 20 new modes passed their first
accepted full run in both variants; no production fix or acceptance weakening.

## Self-review

Independent parallel review against `750a24c`. Standards: zero hard
violations; two nonblocking duplication heuristics (ordinary expected traces
and backend failure-mode matrix). Preserve explicit adjacent-test conventions
and shared library lifecycle rather than broad oracle refactoring this round.
Spec: zero scoped missing/incorrect/extra findings. Final diff check PASS.
MSC-compilation skill guided real backend controls; Metal-validation skill
guided environment configuration before device creation. No fake compiler
success, native PSO-cache performance claim or capability promotion.

## Known limitations / capability status / Feature Level impact

Ordinary `LoadGraphicsPipeline` only, not HS/DS/GS library failures,
library1 stream interception, cache concurrency, shader-library/raytracing,
Native Windows comparison, draw/readback, gameplay or FPS. No full isolation
or GPU semantic promotion. FL11_1 and advertised capabilities unchanged.
Overall backend isolation PARTIAL; FL12_0 and FL12_1 remain FAIL due to the
existing typed-UAV DXIL matrix failure. Bounded local invocation acceptance
must remain distinct from full feature-level acceptance.

## Git status / push status / next task

Final handoff confirms actual commit and working-tree status. NOT PUSHED.
Next: HS/DS pipeline-library failure/retry traces, including combined AIRCONV
compilation; GS library and DXIL shader-library/raytracing remain separate.

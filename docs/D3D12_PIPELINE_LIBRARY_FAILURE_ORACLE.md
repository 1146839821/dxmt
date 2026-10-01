# Compute pipeline-library invocation oracle

## Task Analysis

### Branch, baseline and current state

`feat/d3d12-1`, clean at task start `b125dd8`; integration baseline
`origin/feat/d3d12`, merge-base `85bb2dd`. Local compute, graphics,
tessellation, geometry and native mesh invocation oracles are retained.
Overall backend isolation is PARTIAL; both FL12 gates remain FAIL.

### Existing implementation and relevant files

Read-only production `d3d12_pipeline_persistence.cpp`, compute factory and
shader converter. Changes are limited to the backend-failure executable,
FL12 runner, host tests and documentation; existing Meson source linkage and
compute SM6 fixture are reused. Existing coverage has 87 invocation modes
and 29 host tests, plus public pipeline-persistence regressions.

### Contract, implementation plan and validation

- Hypothesis: a retained pipeline-library entry returns its PSO without shader
  compilation; a deserialized entry must rebuild, and a failed rebuild must not
  poison the entry or switch shader backends.
- Evidence: `PipelineLibraryEntry` retains a COM PSO only in memory;
  `SerializeLocked` writes names, keys and cache metadata. `LoadPipeline` calls
  the production PSO factory outside the library lock when no PSO is retained,
  and installs the PSO only after successful construction and key verification.
- Expected effect: executable evidence for compute retained hits, metadata-only
  reloads, missing/mismatched keys, backend failures and same-library retry.
- Risk: warming the executable converter cache during setup would conceal the
  cold rebuild. Seed PSOs must therefore use the real DLL factory; only the
  source-linked library rebuild has wrapped compiler imports. Assert zero
  wrapped calls during setup. No production fault hooks or fake cached PSOs.
- Validation: exact ordered per-phase compiler traces, HRESULT, output pointer
  and retained COM identity; independent normal/no-private builds, provenance,
  existing regressions and Metal validation. Review Standards and Spec before
  a local commit, without pushing.
- Scope: compute library only. Graphics-library, shader-library/raytracing,
  GPU execution and performance are not established by this oracle. No feature
  level or shader-model capability promotion.

### Backend and shared-runtime impact

DXBC continues through AIRCONV; DXIL continues through MSC. Fault injection
is confined to executable import wrappers. No production ABI, descriptor,
residency, synchronization, cache policy or resource-lifetime change.

### Missing pieces and capability impact

Compute cache-path invocation evidence is the bounded missing piece this
task closes. Graphics-library rebuild, DXIL shader-library/raytracing and
GPU semantic matrices remain separate. FL11_1 and advertised capabilities
are unchanged; no FL12_0/FL12_1 promotion is planned.

## Task Result

### Branch, baseline and local commit

`feat/d3d12-1`; task baseline `b125dd8`, integration
`origin/feat/d3d12`, merge-base `85bb2dd`. Actual containing commit hash is
supplied in the final handoff, avoiding a self-referential hash in this file.

### Changed files and implementation

`tests/dx12/dx12_backend_failure.cpp`, `dx12_fl12_gate.py`,
`test_dx12_fl12_gate.py`, this report and `D3D12_FL12_GATE.md`.
No production source, fixture source or Meson linkage changes.

The real DLL creates the seed PSO, so its converter cache does not warm the
executable's source-linked converter. The actual production library stores
the PSO and metadata. Setup and deserialization must leave the import trace
empty. Retained loads require the same interface pointer and no compiler
calls with a compiler fault armed. Serialized loads drop the original library
and seed, then rebuild through the source-linked production factory. Missing
name and valid-but-different NodeMask keys reject E_INVALIDARG without a
compiler call. Rebuild failures require null output and exact stopping traces;
disable the fault, retry on the same library, require real successful
compilation, and re-arm a fault to verify the subsequently retained hit.

Fourteen fresh-process modes: six AIRCONV and eight MSC. Two host tests make
the library evidence mandatory and preserve a failing mode even if runtime
hash consistency is missing. Existing executable provenance tests now cover
this oracle; compile-flag provenance also checks persistence production source.

### DXBC / AIRCONV impact

Real reload trace `air.init.cs;air.compile.cs`; initialization and compilation
failures preserve E_FAIL, with no MSC calls. Same-library retries require the
full trace anew. No shader lowering change or persistent-cache dependency.

### DXIL / MSC impact

Real reload trace `msc.cs.query;msc.cs.materialize`; invalid DXIL, unsupported,
memory and materialization failures preserve E_INVALIDARG, E_NOTIMPL,
E_OUTOFMEMORY and E_NOTIMPL respectively, with no AIRCONV calls. Failed
conversions do not prevent a fresh two-pass retry on the same library.

### Shared runtime impact

None. Test-only executable import interception; real compiler success,
metallib and PSO creation. COM references use existing RAII ownership. DLL
seed calls are explicitly outside the invocation instrumentation boundary.

### Tests run and runtime results

2026-10-01, Apple M4 / x64 Wine / MSC API 4.0.1. Normal and
`DXMT_NO_PRIVATE_API` independently reconfigured and fully built successfully;
compute and public persistence fixture targets checked. Meson 3/3 per variant,
host Python 31/31. Both full Wine gates ran with Metal API and Shader
Validation enabled before device creation. Library 14/14 and prior invocation
87/87 per variant PASS; build/runtime/executable provenance PASS. No Metal
validation-error diagnostics were observed in the new library cases.

Typed-UAV DXIL matrix remains FAIL; FL12_0 and FL12_1 remain FAIL and the full
runner correctly exits 1. Feature, container/stage, shader validation and
min/max contract probes PASS. Receipts (ignored build outputs):
`build/fl12-library-failure-normal.json` and
`build-no-private/fl12-library-failure-no-private.json`.

The existing public `dx12_pipeline_persistence.exe` also passes both variants
using root-UAV compute and ordinary graphics SM6 fixtures. The first
no-private attempt was UNVERIFIED because its optional fixtures were absent;
after building those existing targets the accepted fresh run PASS. Public
logs are `build/library-public-regression.log` and its no-private counterpart.
Normal installed runtime restored and provenance verified PASS afterward.

### Self-review

Independent parallel Standards and Spec reviews against `b125dd8`.
Standards: zero hard violations; two nonblocking duplication heuristics in
fault decoding and mandatory-oracle host assertions. Retain explicit narrow
tests consistent with adjacent oracles instead of broad refactoring.
Spec: zero missing, incorrect or scope-creep findings in the bounded task.
`git diff --check` PASS. MSC compilation and Metal-validation skills guided
real-backend controls and validation configuration; no success was fabricated.

### Known limitations and capability status

Compute `LoadComputePipeline` only, not graphics library failure injection,
library1 stream-load interception, cache-race stress, native binary PSO cache,
performance, native Windows comparison or shader-library/raytracing. Public
regression evidence does not substitute for a graphics-library invocation
oracle. Overall backend isolation PARTIAL; this bounded contract
DXMT_LOCAL_PASS. No gameplay, FPS or new GPU semantic acceptance claimed.
FL11_1 and capability declarations unchanged; FL12_0 FAIL, FL12_1 FAIL.

### Git / push status and next task

Final handoff confirms the actual commit and clean working-tree status.
NOT PUSHED. Next: graphics pipeline-library retained-hit/reload/failure/retry
invocation evidence; keep DXIL shader-library/raytracing as a distinct task.

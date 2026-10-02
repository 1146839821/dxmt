# FL12 evidence gate and backend isolation

## Task analysis

Scope: first task in the supplied FL12_1 master prompt, not FL12 promotion.
Work branch: `feat/d3d12-1`. Read-only reference: `origin/feat/d3d12`
at `e147c710eabc0a8d1530f5d2e21d18b0eb114be4`.
Local starting point: `3b47b7f`; common ancestor `85bb2dd`.

The reference branch contains stage/family validation and regression fixtures.
Reuse those narrow changes without merging its unrelated residency/performance
changes or replacing the local AIRCONV tessellation implementation.
The dirty DirectX header submodule is not part of this task.

- Hypothesis: explicit executable family and shader kind eliminate ambiguous
  routing and reject wrong-stage shaders before either compiler.
- Evidence: classification previously preferred DXIL when both executable
  families existed; optional shader absence was represented as AIRCONV.
- Expected effect: DXBC only reaches AIRCONV; DXIL only reaches MSC;
  absent, ambiguous, unsupported and ordinary/library inputs are distinct.
- Risk: HS/DS and mesh optional stages could reject valid existing pipelines.
- Validation: helper contracts, real PSO positive/negative cases, extended
  GS/HS/DS mixed-family cases, normal and no-private builds and Wine runs.

No-private validation found an existing cache thunk naming bug:
`WMTSetMetalShaderCachePath` in the Unix disabled branch collided with the
public Windows signature. Both Unix branches must implement
`_WMTSetMetalShaderCachePath(void *)`; the disabled branch still returns
unsuccessful cache-path setup. This does not enable a private API.

## Running the ledger

Reconfigure and build with the repository's cross file and existing options.
The host unit test is registered with Meson:

```sh
meson setup --reconfigure build
meson compile -C build
meson test -C build dx12-fl12-gate-unit --print-errorlogs
```

Build the non-default DXIL fixtures:

```sh
meson compile -C build dx12_compute_sm6_fixture \
  dx12_shader_embedded_compute_fixture dx12_shader_embedded_graphics_vs_fixture \
  dx12_shader_embedded_graphics_ps_fixture dx12_shader_embedded_graphics_mismatch_fixture \
  dx12_shader_backend_gs_fixture dx12_shader_backend_hs_fixture \
  dx12_shader_backend_ds_fixture dx12_shader_backend_library_fixture \
  dx12_backend_failure_vs_fixture dx12_backend_failure_ps_fixture \
  dx12_mesh_sm6_ms_fixture dx12_mesh_sm6_as_fixture dx12_mesh_sm6_ps_fixture
```

Use a consistent Wine loader/server for fixture compilation and execution;
the configured toolchain Wine and the user's game Wine can differ in protocol
version. Do not run them concurrently against the same prefix.
Run the gate after deploying the matching Unix `winemetal.so`:

```sh
python3 tests/dx12/dx12_fl12_gate.py --build-dir build \
  --wine /absolute/path/to/wine --variant normal --output build/fl12-gate.json
```

For a no-private build, define `DXMT_NO_PRIVATE_API` in **both cross and native**
C/C++ arguments (`c_args`, `cpp_args`, `build.c_args`, `build.cpp_args`).
Build and deploy its matching Unix runtime independently, then run with
`--build-dir build-no-private --variant no-private`.
Restore the normal Unix runtime after this validation.

The runner stages newly built d3d12/dxgi/winemetal DLLs with each executable,
disables persistent shader caching, and records the staged copies' SHA256
values per probe. Build hashes must remain consistent before/after probes.
It verifies the relevant cross/native compile commands and installed Unix
runtime before and after probes. A variant label alone is not evidence.
Exit 0 requires **both** gates PASS; exit 1 with successful probes and FAIL
gates is an expected capability refusal, not a regression-test failure.

## Coverage and honesty

Each requirement records PASS, PARTIAL, FAIL, BLOCKED_BY_ARCHITECTURE or
UNVERIFIED. API minima that are satisfied remain PARTIAL without the complete
GPU semantic matrix. Missing fixtures, skips and missing compile evidence do
not pass. Probe failures remain explicit FAIL, including an isolated feature
query failure.

The ledger covers FL12_0 resource binding, tiled resources, typed UAV formats,
min/max filtering, logic operations, raster/format matrices, both shader
backends, tessellation and geometry/stream output. FL12_1 depends on all
FL12_0 requirements plus both ROV paths, conservative raster Tier 1 and the
complete FL12_1 contract. Tiled raw/structured and multi-tile packed-mip ABI
gaps are recorded separately from declaration failures; see
`D3D12_RESOURCE_AUDIT_CHECKLIST.md` and
`D3D12_RESOURCE_AUDIT_ROUND2_2026-09-12.md`.

Backend regression covers missing/duplicate/hybrid executable chunks, unknown
kinds, wrong VS/CS slots, ordinary/library isolation and mixed VS/PS/GS/HS/DS.
Successful PSO creation is **not** proof of GPU shader correctness.
Classification helpers are synthetic contract tests, not compiler invocation
instrumentation. All validation/container/stage probes are mandatory evidence;
missing probes become UNVERIFIED. The isolation requirement remains PARTIAL
pending instrumented malformed/ambiguous precompiler rejection traces; see
`D3D12_BACKEND_ISOLATION_AUDIT.md`. The compute failure/invocation oracle added
in `D3D12_BACKEND_FAILURE_ORACLE.md` now observes test-linked production compute
call sites. The follow-up `D3D12_GRAPHICS_FAILURE_ORACLE.md` adds ordinary
VS/PS ordered invocation traces and mixed/wrong-stage precompiler rejection.
The next `D3D12_TESSELLATION_FAILURE_ORACLE.md` adds HS/DS initialization,
combined AIRCONV compile and MSC per-stage/pass failure traces.
`D3D12_GEOMETRY_FAILURE_ORACLE.md` adds GS initialization, combined VS/GS
mesh/object compile and MSC per-stage/pass failures.
`D3D12_MESH_FAILURE_ORACLE.md` adds native MS/AS/PS ordered conversion failures
and DXBC/wrong-stage rejection. `D3D12_PIPELINE_LIBRARY_FAILURE_ORACLE.md`
adds compute retained hits, metadata reload and failed-rebuild/retry traces.
`D3D12_GRAPHICS_LIBRARY_FAILURE_ORACLE.md` adds ordinary VS/PS library
retained hits, metadata reload and selected failure/retry traces.
`D3D12_TESSELLATION_LIBRARY_FAILURE_ORACLE.md` adds HS/DS library
combined AIRCONV and MSC selected-stage failure/retry traces.
`D3D12_GEOMETRY_LIBRARY_FAILURE_ORACLE.md` adds GS library paired mesh/object
and MSC failure/retry traces. `D3D12_SHADER_LIBRARY_FAILURE_ORACLE.md` adds
six ray-stage library converter export/pass failures and retry/cache traces.
`D3D12_STATE_OBJECT_FAILURE_ORACLE.md` adds six-stage candidate probing,
AH/CH stage hints and failed-publication/retry/cache traces.
`D3D12_STATE_OBJECT_ADDITION_FAILURE_ORACLE.md` adds same-parent failure/retry/cache
and immutable/inherited identifier/stack checks. `D3D12_RAY_SYNTHESIS_FAILURE_ORACLE.md`
adds lazy dispatch/intersection synthesis failures and same-object retry/retained
state. `D3D12_RAY_METAL_FAILURE_ORACLE.md` adds PSO/table/function-handle failures
and a transactional publication repair. `D3D12_RAY_LOAD_FAILURE_ORACLE.md` adds
lazy dispatch/intersection library/function-load failures and same-object retry.
`D3D12_STATE_OBJECT_LOAD_FAILURE_ORACLE.md` adds six-stage creation-time export
library/function-load failure, AH/CH hints, null publication and factory retry.
`D3D12_STATE_OBJECT_ADDITION_LOAD_ORACLE.md` adds addition export load failures,
same-parent factory retry and inherited identifier/stack immutability.
`D3D12_STATE_OBJECT_MULTI_EXPORT_ORACLE.md` adds creation-time second-export
failure after first-export success, null publication and layer-specific cache retry.
`D3D12_STATE_OBJECT_ADDITION_MULTI_ORACLE.md` adds same-parent two-export
partial progress, failure publication and cache retry. Larger/order-varied export
sets are outside the bounded coverage, not an unbounded promotion prerequisite.

## Self-review

Standards and Spec reviews were performed independently against `3b47b7f`.
Fixed findings: isolated feature-probe FAIL lost in aggregation; extended
stage matrix not invoked; ambient runtime/variant not bound to evidence.
Follow-up fixes preserve raw execution failures when provenance is missing
and prevent post-run hashes from describing different binaries.
The no-fallback invocation oracle gap is disclosed above and is not marked PASS.
Existing baseline helper parameter clumps are retained to keep regression
reuse narrow; they do not change the public runtime ABI.

### Standards

Final review: no remaining concrete bugs or hard documented violations.
Judgment calls retained from narrow baseline reuse: graphics helper parameter
clumps and duplicated program-version decoding. Neither alters behavior.

### Spec

One remaining partial requirement: instrumented malformed/ambiguous input
rejection before either compiler. The coverage audit separates this from GPU
semantic requirements and optional larger-export tests.
The follow-up oracles close bounded ordinary/emulated,
native mesh, compute, ordinary VS/PS, HS/DS and GS pipeline-library cases,
plus the six ray-stage shader-library converter entry paths and state-object
candidate probing/failed publication, same-parent additions, lazy synthesis and
creation-time export loads, Metal lazy library/function-load and
PSO/table/function-handle failure/retry.
The gate keeps the entire isolation requirement PARTIAL.

Review summary: Standards 0 hard findings (2 heuristic smells); Spec 1 partial
validation requirement, malformed/ambiguous precompiler rejection traces.

No Feature Level, Shader Model, WaveOps, Atomic64, ROV, tiled-resource,
conservative-raster or typed-UAV capability is promoted.
Next capability work: Min/Max Reduction Filtering, with DXBC and DXIL GPU
readback matrices before any declaration change.

## Validation result

Normal and independently configured no-private release builds succeeded.
Ten host gate unit cases passed, also through Meson in both build directories.
Both Wine configurations passed feature-support, shader-validation,
shader-container and extended shader-stage-matrix probes with shader caching
disabled. Compile flags and Unix runtime hashes were verified independently.
Both aggregate gates reported FAIL as expected:
TiledResourcesTier=0, TypedUAVLoadAdditionalFormats=0,
ROVsSupported=0 and ConservativeRasterizationTier=0.
ResourceBindingTier=2 and logic operations have matching API declarations,
not complete mandatory GPU matrix evidence.

Normal-runtime deployment was restored after no-private testing using the
user's sync script. No game benchmark or Townfall launch acceptance is claimed
by this gate task. The existing DirectX submodule changes remain untouched.

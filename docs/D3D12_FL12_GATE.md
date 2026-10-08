# FL12 evidence gate and backend isolation

## Current Tiled numerical gate (2026-10-08)

The standalone gate now shares run_tiled_gpu_matrix and its 20 current cases
with the FL12 gate, requiring runtime provenance rather than ambient DLLs.
Subset exclusions are UNVERIFIED and cannot qualify Tier 2. AIR raw/structured
feedback is executed by stage/path instead of declared unimplemented; MSC
status, packed-tail, full coverage and native Windows gaps remain separate.
Fractional clamp is actually executed: both builds and no-private API validation
return 19 PASS / clamp FAIL. Additional DXC deployment does not repair it.
See D3D12_TILED_QUALIFICATION_2026-10-08.md. This is a real unresolved numerical
defect, not a historical fixed status or successful rejection contract.

## Independent MinMax numeric evidence (2026-10-08)

The minmax_gpu_matrix registers 52 explicit compute readback cases independently
of minmax_sampler_contract rejection controls. --minmax-compiler-dir deploys DXC
only for MSC numerical cases; all default children force AIR gates off, while
DXBC numerical cases record their explicit opt-ins. Missing, failed or runtime-
mismatched cases remain nonpassing. Numeric success is PARTIAL, never complete
MinMax support: formats, anisotropy, implicit/bias/derivatives, stages, arrays,
indirect/lifetime, sparse and Windows coverage remain outstanding. See
D3D12_MINMAX_QUALIFICATION_2026-10-08.md. No capability or FL promotion.

## Typed compiler deployment qualification (2026-10-08)

The --typed-compiler-dir option stages the compiler/validator pair in dxmt-dxc
only for the full DXIL numeric typed matrix, invoking existing automatic
module-relative production selection. Every qualification child clears inherited
typed-origin/MinMax directory overrides. Actual target PE provenance includes
both nested compiler paths/hashes; absent deployment still executes the legacy
path and may fail. Native-view rejection/control fixtures are unchanged.
See D3D12_TYPED_QUALIFICATION_2026-10-08.md for scope and fresh evidence.
Neither this option nor numeric matrix success promotes the additional-format
API bit, closes arbitrary view/lifetime semantics, or makes the full FL12 gate PASS.

## Actual Unix runtime identity checkpoint (2026-10-08)

Baseline 1c2d3de1, branch feat/d3d12-1. The preparation hypothesis is recorded in
D3D12_CLOSURE_INFRASTRUCTURE_CONSOLIDATION.md: correlate native image identity
with the selected PE process, not arbitrary dyld/helper output or installed files.

Winemetal PE process attach checks the exact-value, default-off
DXMT_TRACE_RUNTIME_IDENTITY=1 diagnostic. Appended unixcall 203 receives one
uint32 Windows PID; native dladdr of that function reports its own image path
plus getpid. Existing call IDs and shared shader/resource structs remain
unchanged. The native and wow64 tables append the same pointer-free scalar call;
wow64 runtime remains unqualified. The diagnostic requires matching PE/Unix
bridge versions; old installations are not diagnostic-compatible. Ordinary
startup never calls the new index when the switch is unset/non-enabling.

run_fixture enables the diagnostic only in controlled qualification processes.
verify_loaded_unix correlates Windows PID with verified PE target, requires one
unambiguous positive Unix PID/absolute path identity, and compares the observed
image-file hash with this build's Unix binary. Helper identities, missing/ambiguous
records and invalid identity remain UNVERIFIED; mismatched image bytes fail.
For pure PE fixtures that neither import nor observe Winemetal, results explicitly
record applicable=false / Unix runtime not exercised. Such a result cannot satisfy
the feature-probe Unix gate. Native Windows observation remains a separate gap.

Validation: both reconfigured full builds pass; 66 Python tests and all 16 host
tests per build pass. Tests cover PID mismatch, missing/ambiguous/invalid identities,
wrong image bytes, absent verified PE target, pure-PE applicability, and prohibition
on using a not-exercised result as feature-process proof. Normal/no-private actual
capability and feature-support probes pass matched PE and Unix path/hash checks.
The three provenance requirements are PASS for each fresh feature-support probe,
while FL12_0_GATE remains FAIL for unsupported capabilities/missing matrices.
This is not a full gate run or capability promotion.

Actual diagnostic images:

- safety-runtime/lib/wine/x86_64-unix/winemetal.so SHA256
  13c4a9ea90b9f8bb26c2d42f316a162690c5b2c8bcb53619933181ce5bc2f9be
- safety-runtime-no-private/lib/wine/x86_64-unix/winemetal.so SHA256
  88ef38043819075f18892472488ea21d029bc84aa2ba125c20ec417ea811326a

Logs under cache dxmt-reconciliation.ZLDvwE: closure-unix-identity-*.log,
closure-unix-feature-*.log, closure-unix-pe-only-control.log. Normal direct startup
controls with the diagnostic unset, 0 and 11 pass capability checks and produce
no identity record (closure-unix-silent-*.log). A real shader-validation pure PE
control passes with applicable=false, not an invented loaded Unix image.
Only cache runtime/overlay PE DLLs and Unix images were refreshed. No game/prefix
deployment, process restart or push. No production capability changes.

Main-agent standards/spec self-review informed by code-review found no outstanding
actionable issue in this identity slice; independent sub-agent review was not
performed. The evidence is diagnostic self-report plus on-disk image hash, not an
adversarial in-memory attestation. File replacement races, all runtime variants,
native Windows and wow64 are not qualified. Tracing/hashing must stay outside
performance measurements. Next work: timestamp oracle, root-feedback profiling,
production counter gating and complete Typed/MinMax/Tiled/format/raster matrices.

## Actual PE module provenance checkpoint (2026-10-08)

Baseline e1205f41, branch feat/d3d12-1. Hypothesis: matching copied/installed files
does not establish which DLLs the target actually loaded. Evidence: historical
cache PE mismatches and Wine builtin-marked DLL resolution; system32 equality
previously blocked cache-only qualification even when those files were unused.
Expected effect: target-process PE path/hash evidence without prefix deployment.
Risk: helper traces, missing imports or unverified Unix images could create a
false provenance PASS. Verification keeps those cases distinct/fail-closed.

Wine runtime executions now use fixed -all,+pid,+loaddll tracing, recorded in
controlled_environment. Actual PE import metadata determines required DXMT DLLs;
an imported d3d12.dll requires the complete D3D12/DXGI/Winemetal PE chain.
Any observed known DXMT module is checked even if not statically imported.
Target PID comes from the exact staged executable path. Missing/ambiguous target
records and missing required modules are UNVERIFIED; other directories or staged
DLL mutation fail. Helper-process records cannot stand in for target modules.
Temporary staging uses canonical paths to avoid /var vs /private/var ambiguity.
Records retain PID, observed paths and post-run hashes matched to staged hashes.
This is trusted test/loader evidence, not an adversarial in-memory image attestation.

verify_build still checks compile variant, installed Wine PE files and installed
winemetal.so against this build, but no longer requires unused prefix system32
files to match. Each executed Wine probe enforces loaded PE validation before
accepting its markers. The feature-query gate additionally requires loaded PE
evidence, including on native Windows where that evidence is currently missing.
Installed Unix hash equality remains only binary preflight: loaded_unix_image is
UNVERIFIED and a distinct mandatory gate row prevents promotion without actual
Unix-image evidence. This deliberately does not claim complete provenance.

Validation: 65 Python tests pass, including PE32/PE32+ import layouts, malformed
metadata, exact PID attribution, helper/ambiguous/missing traces, wrong paths,
changed bytes, stale unused prefix files and mandatory PE/Unix evidence rows.
PE32 parsing coverage is not 32-bit runtime qualification. Actual run_fixture and
verify_build calls pass normal/no-private capability controls, with parent
experimental gates set to 1 and WINEDEBUG=+timestamp. Child tracing remains
deterministic, experimental gates remain 0, and the target's three PE paths/hash
records match; controls remain FL11_1/SM6.0 and FL11_0/SM6.0 respectively.
Logs: dxmt-reconciliation.ZLDvwE/closure-loaded-pe-final-{normal,no-private}.log.
Both configurations reconfigure and all 16 host tests per build pass. No production
C++/shader/bridge ABI changes, prefix deployment, game restart or push.

Main-agent standards/spec self-review informed by code-review found no outstanding
actionable issue for this PE slice; independent sub-agent review was not performed.
Native Windows module observation, delayed imports not observed during a fixture,
actual Unix image identity and full qualification remain gaps. Loader tracing is
diagnostic overhead, not a root-feedback performance benchmark. Timing runs must
separate provenance qualification from measurement while preserving binary identity.
Next: actual Unix runtime evidence, timestamp oracle and root-feedback profiling,
then complete Typed/MinMax/Tiled/format/raster qualification. No capability promotion.

## Experimental environment isolation checkpoint (2026-10-07)

Baseline 23c2fecb, branch feat/d3d12-1. Hypothesis: qualification must not inherit
temporary capability advertisement from the caller or Wine environment defaults.
Evidence: run_fixture previously copied ambient os.environ without overriding
DXMT_EXPERIMENTAL_SM6_6/FL12_0. Expected effect: explicit default-capability
controls independent of the interactive game-launch environment. Risk: changing
the parent environment would disrupt legitimate experimental startup, so only
the child subprocess environment is overridden.

Every fixture execution now explicitly receives DXMT_EXPERIMENTAL_SM6_6=0,
DXMT_EXPERIMENTAL_FL12_0=0 and the existing DXMT_SHADER_CACHE=0. Completed process
results include controlled_environment; the schema-2 report separately records
qualification_environment_policy. The policy is declared configuration, not
proof that unexecuted/missing probes observed it. Production gate implementation
and direct opt-in game startup remain unchanged. Experimental opt-in observations
must be run/reported separately and do not contribute qualification evidence.

Validation: 60 Python tests pass, including mock spawn checks for parent values
1/true/0 and a real child process independently reading the two zero overrides.
Parent values and unrelated WINEPREFIX are preserved. Actual run_fixture calls
with parent SM6.6/FL12_0 set to 1 pass dx12_experimental_caps.exe on both cache
Wine runtimes: normal returns FL11_1/SM6.0 and no-private FL11_0/SM6.0, checking
device-create/query agreement, FL12_1 rejection and lower SM5.1 requests.
Logs: dxmt-reconciliation.ZLDvwE/closure-cap-isolation-{normal,no-private}.log.
Those are controls on the tested AppleFamily 1009/MSC 4.0.1/macOS 27.0.1 host,
not assumptions about other hardware, missing converter or native Windows.
Both build configurations reconfigure and all 16 host tests per build pass;
no C++ production change or new GPU semantic qualification is claimed.

Main-agent standards/spec self-review informed by code-review found no outstanding
actionable issue in this isolation slice; independent sub-agent review was not
performed. Actual loaded-module enforcement remains pending: a fresh Wine
-all,+pid,+loaddll probe establishes process/thread-qualified PE trace syntax,
but neither that trace nor controlled_environment proves the loaded Unix image.
verify_build still requires prefix DLL equality and must be reconciled with
actual-module provenance without prefix deployment. Timestamp oracle,
root-feedback profiling and complete qualification remain open. No push.

## Closure classification update (2026-10-07)

Starting HEAD 050af612, branch feat/d3d12-1. Hypothesis: optional modern-feature
regressions must remain observable without becoming mandatory FL12 prerequisites.
Evidence: mesh and five DXR failure-oracle groups previously contributed to both
FL12_0 requirements and backend isolation. Expected effect: accurate requirement
classification, not a less demanding mandatory contract. Risk: optional failures
could disappear if merely removed; therefore they remain in an independently
aggregated optional_regressions section and the CLI output summary.

Report schema is now version 2. Six Mesh/DXR groups move to optional_regressions;
all probes, failure-injection modes, hashes and raw results remain available.
Mandatory compute/ordinary graphics/GS/HS/DS isolation and pipeline-library
regressions remain required. Missing complete GPU categories and API-only claims
still cannot PASS. The process exit status remains tied to the FL gates; consumers
needing optional regression enforcement must also inspect optional_regressions.
This explicitly separates FL acceptance from general project regression health.

The raw/structured watchlist now distinguishes the missing DXIL/MSC sideband from
implemented AIR transport with focused GPU evidence. AIR complete current matrix
status stays UNVERIFIED; dual-backend closure stays BLOCKED_BY_ARCHITECTURE.
Packed-tail, clamp and native-oracle evidence are not promoted.

Validation: 58 Python unit tests, including missing/all-status transitions for
every optional group, unchanged complete FL requirement lists under optional
changes, visible optional failure aggregation, and mandatory-failure preservation.
Both build configurations reconfigured successfully and all 16 host tests per
build pass (no C++ production changes). Main-agent standards/spec self-review
informed by code-review found no outstanding actionable finding for this slice;
independent sub-agent review and fresh runtime matrices were not performed.
No capability changes, deployment, restart or push. Resource audit refresh,
experimental isolation, actual-module provenance, timestamp oracle, root-feedback
profiling and complete qualification remain pending consolidation work.

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
as bounded invocation coverage, not full backend-contract acceptance; see
`D3D12_BACKEND_ISOLATION_AUDIT.md`. The compute failure/invocation oracle added
in `D3D12_COMPUTE_CONTAINER_REJECTION_ORACLE.md` now observes five compute
container rejection cases with zero compiler calls and cleared output.
`D3D12_GRAPHICS_CONTAINER_REJECTION_ORACLE.md` adds the corresponding ten
ordinary VS/PS rejection cases, including a malformed PS with a valid VS.
The original compiler failure oracle
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

Compute and ordinary VS/PS malformed/ambiguous input rejection now have bounded
instrumented zero-call evidence. Optional graphics-stage malformed inputs and
arbitrary-input/full backend-contract coverage are not claimed. GPU semantic
requirements and optional larger-export tests remain separate.
The follow-up oracles close bounded ordinary/emulated,
native mesh, compute, ordinary VS/PS, HS/DS and GS pipeline-library cases,
plus the six ray-stage shader-library converter entry paths and state-object
candidate probing/failed publication, same-parent additions, lazy synthesis and
creation-time export loads, Metal lazy library/function-load and
PSO/table/function-handle failure/retry.
The gate keeps the entire isolation requirement PARTIAL.

Review summary: Standards 0 hard findings (2 heuristic smells); Spec 1 partial
validation requirement, full backend-contract acceptance beyond bounded probes.

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

## Timestamp submission oracle registration (2026-10-08)

The mandatory timestamp_resolve_submission_oracle row requires the default
200-submission GPU consumer/CPU-gate/list-reset/canary loop and clock calibration.
Build dx12_timestamp_resolve.exe explicitly (build_by_default:false); a missing
fixture stays UNVERIFIED. Failure/absence propagates without accepting the
one-iteration pressure mode or failure-injection mode as successful query evidence.
Both actual cache-runtime executions pass with target PE/Unix provenance and
experimental capabilities explicitly disabled. There are 68 gate Python tests.
The bounded row is not a full query qualification or FL12 promotion; complete
mandatory resource/raster/backend matrices remain required. Production ordering
and limitations are documented in D3D12_TIMESTAMP_RESOLVE_ORDER.md.

# Task Analysis

## Current Branch

`feat/d3d12-1`, clean at start; HEAD `c650c4c`.

## Baseline

Pinned reconciliation target `bad6756a`, merge-base `85bb2dd2`; adapt safety
slices from `2854e864`, not an entire cherry-pick.

## Local Commits Since origin/feat/d3d12

Remote-tracking baseline is stale; pinned-target divergence after audit is
138 ahead / 17 behind. Prior local typed-origin/tessellation work is preserved.

## Current State

Audit committed; no production fixes yet. Direct native null-error diagnostics
reproduce SIGSEGV in both build modes. Compute/mesh omissions are source-proven.

## Existing Implementation

D3D12 classification rejects mixed executable families; direct SM50 entry does
not. SM50 error/shader objects use raw ownership. Compute residency is opt-in;
generic graphics residency chooses pre-raster stages without MSC mesh.

## Relevant Files

`src/airconv/dxbc_converter.cpp`, `src/d3d12/d3d12_command_list.cpp`, native
AIRCONV regression registration and reconciliation audit.

## Existing Tests

Native malformed-input probe in reconciliation cache; current shader boundary,
compute and mesh fixtures. Their coverage will be checked before reuse.

## D3D12 Contract

Retain and declare resources through execution with correct usage/stage masks.
Preserve descriptor flags and recording-only static/live volatile policy.

## DXBC / AIRCONV Impact

Default compute residency; direct legacy-only executable validation; optional
error outputs and failure ownership. No DXIL fallback.

## DXIL / MSC Impact

Correct generic mesh resource stages only. No shader ABI/compiler changes.

## Shared Runtime Impact

No thunk additions, numbering changes, heap-lock changes or lifetime redesign.

## Missing Pieces

Implement the confirmed safety slices and direct API regression; broader
submission retention and metadata/cache adaptation remain separate tasks.

## Hypothesis / Evidence / Expected Effect

Unconditional nullable error writes explain the reproduced signal; RAII plus
conditional ownership transfer must eliminate it. Default-on compute and mesh
stage inclusion should restore target residency declarations, not promote caps.

## Risks

Missing changed raw-pointer call sites in RAII conversion; unexpected malformed
container behavior; default walk cost; mistaking source fix for GPU acceptance.

## Minimal Implementation Plan

Adapt only converter safety hunks; fix default compute and mesh stage selection;
add direct API regression before fix; build both modes and run focused checks;
self-review and local commit, no push.

## Validation Plan

Run original red native probe; register native boundary regression; full normal
and no-private builds and host tests; inspect available compute/mesh fixtures
for focused GPU readbacks without game/prefix writes or process management.

## Capability Impact

None. FL12_0/12_1 and unqualified capabilities remain disabled.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

`c650c4c`; targeted adaptation of `2854e864` against pinned `bad6756a` audit.

## Local Commit

Reported after committing; no self-referential hash embedded here.

## Changed Files

Converter, command-list residency selection, native AIRCONV test/registration,
and this task record. No other reconciliation stack imported.

## Implementation

Errors use scoped ownership and transfer only to non-null caller outputs in
initialization and all five compilation entry points. Initialization retains
shader ownership until success and rejects DXIL, duplicate legacy executables
and mixed SHDR/SHEX before token parsing. Compute residency defaults on with
explicit environment `0` override. Generic MSC mesh declarations include
object/mesh/fragment. Descriptor flags and heap lock scope remain unchanged.

## DXBC / AIRCONV Impact

Direct API failure safety and strict legacy boundary; ordinary compute walk
default restored. DXBC→AIRCONV remains permanent, no fallback introduced.

## DXIL / MSC Impact

Mesh generic resource-use stage mask corrected; no compiler/ABI/thunk changes.

## Shared Runtime Impact

No new slots, shader-footprint narrowing, cache, statistics or submission
reference lifetime redesign. Static descriptors remain recording-only; volatile
and direct-indexed descriptors retain existing submission-live semantics.

## Tests Added

`airconv API boundary`: malformed/null input, null shader output, optional
error outputs, five compilation null-output paths, duplicate/mixed/DXIL chunks.
Test handles are scoped even on unexpected assertion outcomes.

## Tests Run

Both Meson configurations reconfigured before building. New native regression
ran red before the production fix: killed by signal 11. Full normal and
no-private builds completed successfully; final full incremental builds and
host suites completed after test-owner cleanup: 6/6 in each mode.

Original dlopen probe passed against each fresh native library: control result
1/error present, nullable-error result 1/shader absent, process exit 0. An initial
post-fix probe still loaded the old same-named library through DYLD_LIBRARY_PATH
and signaled 11; this was not a fresh-binary result. Fresh library copies placed
first in the path removed that ambiguity. No game/prefix binaries changed.

Cache-only normal/no-private Wine overlays contain current PE and native DLLs.
`dx12_airconv_firstbit.exe` ran with `DXMT_AIRCONV_COMPUTE_RESIDENCY` unset; registry
files contained no override. Both runs exited 0 with exact ten-value GPU readback
passing. This fixture uses an ordinary structured UAV table without samplers.
It is a positive default-path regression, not an exhaustive residency/lifetime
oracle or a red-capable test of the old opt-in behavior.

## Runtime Results

Evidence directory: `/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE`.
Configuration/full-build logs: `safety-config-*`, `safety-full-*`;
final build/host logs: `safety-final-build-*`, `safety-final-host-*`;
GPU output: `safety-gpu-compute-normal.log`, `safety-gpu-compute-no-private.log`.
Process statuses captured separately by command runner. No Metal validation,
game benchmark or mesh resource GPU acceptance claimed in this task.

## Standards Review

Independent readonly review: no hard violation; two coverage/ownership
recommendations. Unexpected test outputs now receive scoped cleanup. Positive
and late-conversion-failure ownership coverage remains incomplete. Main
self-review verified raw shader helper calls use `.get()` and only successful
initialization releases ownership; error stream dies before scoped error owner.

## Spec Review

Independent readonly review found no wrong implementation or scope expansion;
source requirements implemented. Test probes do not establish all late failure
or leak paths. Default compute positive readback is now verified separately;
mesh declaration fix remains source/build verified, resource GPU gate open.

## Known Limitations

No leak-rate measurement, valid-shader conversion-error nullable output test,
late initialization failure test, mesh root/table live-slot GPU gate, repeated
submission retention fix, same-address VA remap or same-build batching A/B.
Existing constant-output mesh fixture has no root/table resource consumer and
would not qualify the corrected generic residency mask; it is not counted.

## Capability Status

PARTIAL overall; direct boundary regression locally validated in both modes.
No broad capability claim.

## Feature Level Impact

FL11_1 unchanged. FL12_0 and FL12_1 not promoted.

## Git Status

Only the five task files are intended for the local commit; final worktree
status and commit ID reported after commit.

## Push Status

NOT PUSHED. No branch merge/rebase/cherry-pick or game/Steam process management.

## Next Recommended Task

Add a resource-consuming MSC mesh root/table regression and direct conversion
ownership failure coverage, then reconcile submission-scoped volatile retention
before metadata/cache/footprint optimization. The full goal remains active.

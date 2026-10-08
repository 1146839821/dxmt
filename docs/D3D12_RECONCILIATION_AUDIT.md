# Task Analysis

## Current Branch

`feat/d3d12-1`, clean at task start, HEAD `6c44dc2`.

## Baseline

Audit target `bad6756a5d55be975a123253b36ad3f495e19c51`; verified merge-base
`85bb2dd2a2a74fa4ae0138b3b9a6165208a4e27b`. The target object was absent locally
and fetched by exact SHA from configured origin without changing branch refs.

## Local Commits Since origin/feat/d3d12

Use the requested pinned target, not the stale remote-tracking ref: verified
137 ahead / 17 behind before this audit's documentation commit. The user's
121-ahead snapshot predates subsequent local work. All 17 patch-ids differ;
that is not evidence that their semantics are all missing.

## Current State

No merge, rebase, cherry-pick, production edit or push is authorized by this
audit. Current worktree and pinned Git objects are authoritative.

## Existing Implementation

Current recording/submission residency uses encoder-owned static/live snapshots,
unique heap retention and locked descriptor resolution followed by encoder
fan-out outside the lock. Target uses cached metadata, generations, whole-list
batching and later shader-declaration footprints.

## Relevant Files

AIRCONV public API/converter; Wine thunk headers/dispatch tables; D3D12 shader
classification, root/descriptor metadata, command-list/queue residency; logger;
target backend/residency tests and current test replacements.

## Existing Tests

Current shader-container and shader-validation sources are byte-identical to
the pinned target. Target-specific cache/footprint/count fixtures are not all
present; existing typed/MinMax GPU passes do not substitute for them.

## D3D12 Contract

Preserve descriptor/data flags, resource retention through GPU completion and
unioned usage/stage masks. This branch and the user's explicit requirements
also require static recording-only observation and volatile live submission
observation. A reread of an unchanged static descriptor is not itself a D3D12
violation; losing captured allocation/view identity or flag semantics is the
concrete integration risk. Caches must invalidate on actual changing identity.

## DXBC / AIRCONV Impact

Inspect direct SM50 API guards and ownership separately from D3D12 classification.
Declared ranges are a missing potential optimization, not proof of shader access.

## DXIL / MSC Impact

Preserve all later typed-origin/MinMax/compiler-root/emulation work. No backend
fallback or feature promotion.

## Shared Runtime Impact

Audit thunk-number collisions, lock scope, lifetime scope and cache assumptions.

## Missing Pieces

Determine per-commit covered/partial/missing/superseded status, specific unsafe
imports and a dependency-ordered reconciliation plan with qualification gates.

## Hypothesis

Commit ancestry understates semantic overlap but also hides safety/performance
patches absent from this branch; wholesale integration is unsafe.

## Evidence

Exact-object Git history and endpoint/commit patches, current call paths and
small cache-only diagnostic probes; no stale runtime report as current evidence.

## Expected Effect

Prioritize actual correctness gaps before performance imports and avoid merging
old residency assumptions over newer descriptor/typed/MinMax contracts.

## Risks

Mistaking equivalent names or unrelated tests for coverage; treating a clean
text merge as an ABI/semantic merge; promoting source audit into GPU acceptance.

## Minimal Implementation Plan

Read all 17 commit intents/changed interfaces and relevant final behavior;
compare current implementations/tests; independent Standards and Spec review;
record decisions. Do not implement reconciliation during this audit.

## Validation Plan

Verify pinned refs/counts, byte-identical test sources, concrete call/slot
differences and red-capable diagnostic probes. Documentation-only repository
change does not require production rebuild or new game/GPU acceptance.

## Capability Impact

None; the full development goal remains active.

# Reconciliation Audit — 2026-10-06

## Outcome

Do not merge or cherry-pick the 17 commits as a batch. Ancestry is divergent,
but semantic coverage is mixed. Prioritize omitted safety slices from
`2854e864` before importing metadata caches or shader footprints. All decisions
below compare pinned target `bad6756a` with current `6c44dc2`; they are not an
audit of a moving remote branch.

## Per-commit ledger

| Target-only commit | Intent | Current disposition and reconciliation decision |
| --- | --- | --- |
| `2854e864` | Isolate shader backends / harden residency | Partial. D3D12 classification overlaps; direct AIRCONV guards/error ownership, default compute residency, mesh resource stage masks and submission-scoped volatile retention do not. Port focused slices; preserve current DXBC tessellation and null-PS support. |
| `11b89c31` | Remaining stage rejection tests | Partial. Compute-as-VS covered in identical container fixture; non-library raytracing rejection fixture and mesh cases differ. Current production non-library rejection exists; adapt missing tests. |
| `b97282c5` | Rename shared resource-use table | Naming superseded. Current `MSCResourceUseTable` is shared via backend selection. Rename alone adds no correctness coverage. |
| `7c61be08` | Fail closed on shader kinds/stages | Covered semantics. Centralized classification/validation exists; container and validation test files are byte-identical to target. No fresh execution claimed here. |
| `5ccbf016` | Retain unique deferred descriptor heaps | Covered intent by different ownership. Current encoder retains unique strong heap references through allocator reset. Do not add duplicate list ownership blindly. |
| `fee20420` | Avoid disabled log formatting | Missing; cache-only probe confirms DEBUG/TRACE arguments still evaluated. Adapt independently, reviewing argument side effects. |
| `0eb023ca` | Cache AIRCONV residency metadata | Partial: root descriptor tables already cached, immutable resource metadata/generation/VA scan reuse absent. Rebuild around current flags and identity semantics, not wholesale import. |
| `aaf7abf3` | Cached metadata validation | Partial: ownership tests exist, exact metadata/generation assertions absent. Adapt after metadata interface settles. |
| `f90b7e52` | AIRCONV residency scan reuse tests | Missing specific unchanged/changed VA, table/heap generation, encoder and direct-index scan checks. Existing SM5 rendering is not a reuse test. |
| `0ef68fc8` | Batch volatile resolution | Partial/different algorithm: current unique live-slot snapshots retain under lock, encoder fan-out outside lock, no `std::sort`; grouping is per encoder rather than whole submission. Preserve stage/usage unions and newer typed/reduction paths. No measured speedup claim. |
| `6b7fc59a` | Aggregate residency statistics | Missing observability. Target output is gated but hot counter increments are not. Any adoption must gate production counting, not just emission. |
| `e147c710` | Batched submission tests | Partial: current generic/typed residency tests differ; exact AIRCONV compute and graphics batching assertions absent. Adapt contracts rather than historical counter values. |
| `e43763ad` | Bound statistics log lines | Missing dependent observability; adopt with gated statistics, not as a standalone correctness repair. |
| `313c0ffd` | AIRCONV declared resource range API | Missing. Target thunk slot 195 collides with current alignment API. Append a verified unused slot and update paired ABI atomically; never import original numbering. |
| `55b02a4c` | Shader-guided descriptor footprint | Missing optimization. Current full-range scan is conservative. Target VS/GS/PS-only footprint would omit current HS/DS; include all stages or fall back conservatively. |
| `ff2e878f` | Shader-guided range tests | Missing dedicated API/footprint/GPU checks. SM5.1 array/space compilation skips are not acceptance. Depends on safely adapted range API and metadata. |
| `bad6756a` | Footprint fallback coverage | Missing capacity/null/sentinel and direct-index/ambiguous fallback cases. Structural fallback assertions and GPU fallback readback must be distinguished. |

All short hashes uniquely identify commits in the verified 17-commit target
range. `git cherry` reports all 17 as `+`; this describes patch identity only.

## Prioritized findings

### P1 — direct AIRCONV optional-error failure is unsafe

`src/airconv/dxbc_converter.cpp` (`SM50Initialize`, around line 1026) initializes
`ppError` conditionally but dereferences it unconditionally on several failures.
The target's RAII/null-output handling and direct mixed/duplicate executable
guards are absent. D3D12 classification does not protect all direct/D3D11 calls.
Related compilation failure paths and raw error/shader ownership require a
focused audit; leaks are source observations, not measured leak rates.

Cache-only native diagnostics against both current normal and no-private
`winemetal.so` confirm malformed input with a non-null error output returns 1,
while the same input with null error output signals SIGSEGV (11). Child isolation
kept the diagnostic runner alive. This is a reproduced API defect, not a game
startup or shader-correctness result.

### P1 — ordinary AIRCONV compute residency remains opt-in

`src/d3d12/d3d12_command_list.cpp` `Reset` (around line 966) enables
`airconv_compute_residency_` only when `DXMT_AIRCONV_COMPUTE_RESIDENCY` is set and
not zero. `PreDispatch` (around line 3829) therefore skips the general AIRCONV
walk for ordinary buffer-only compute unless a reduction-sampler condition
also enables it. Target `2854e864` defaults this path on. Global allocation
residency or captured root values do not establish equivalent declaration and
retention coverage. Confirmed source omission; no new failing GPU case claimed.
Explicit `DXMT_AIRCONV_COMPUTE_RESIDENCY=1` is a mitigation; qualifying sampler
paths provide conditional coverage only.

### P1 — MSC mesh generic resource-use stage mask is incomplete

`EncodeMSCResourceUses` (around line 3400) selects object/mesh stages for tessellation
and geometry emulation, but omits `msc_mesh`, using vertex/fragment instead.
Generic root/table declarations, including deferred live uses, inherit this
mask. Correct masks on other specialized binding paths do not repair it.
Target `2854e864` includes mesh. Source-confirmed wrong declaration; no observed
GPU corruption attributed to it in this audit.

### P2 — volatile execution retention accumulates until allocator reset

Current `ResolvePendingDescriptorUses` (around lines 3522–3563) appends resolved
native references to persistent `EncoderData::resource_refs` on each execution.
Repeated execution can accumulate references and retain replaced volatile
backings until reset. Target uses submission-owned references. This is a
source lifetime/growth concern, not a premature-free finding or a measured
memory profile. Separate recording references from execution references and
release the latter only after their own GPU completion.

### P2 — disabled logging still evaluates formatting arguments

The target logging fixture, compiled against current `log.hpp`, exits 1 with
`disabled DEBUG/TRACE evaluated formatting arguments`. This establishes the
missing macro gate, not its contribution to total CPU submission time.

## Imports that would regress the current branch

1. **ABI collision (`313c0ffd`):** target resource-range slot 195 already means
   `unix_mtldevice_minimumtexturebufferalignment` here; typed-origin and reduction
   slots occupy 196/197. Verify and append 198 or the next unused index across
   public API, PE thunk, native and WOW64 dispatch, with paired deployment and
   old-runtime availability handling.
2. **Descriptor flags (`0eb023ca`):** target cached walkers capture ordinary ranges as live
   pending uses without preserving the static/volatile distinction. Current
   static ranges are recording-only; only volatile/direct-indexed ranges enter
   submission rereads. Preserve RS1.1 flags and existing RS1.0 deserializer
   volatilization. Do not restore `0ef68fc8` lock-held encoder fan-out.
3. **Tessellation footprints (`55b02a4c`):** target narrowing handles VS/GS/PS, not current
   AIRCONV HS/DS. Include all present stages and their usage/stage unions or
   use conservative fallback. Current full-range enumeration over-retains;
   target's incomplete exact footprint could under-retain.
4. **Root VA cache identity:** target same-VA skip assumes unchanged backing.
   Same-address remapping remains unqualified. Require allocation/registry
   epoch invalidation or avoid this cache. This audit did not reproduce remap.
5. **Counters and batching:** keep counters opt-in; retain objects under the heap
   lock and fan out outside. Current hash grouping versus target sorting needs
   exact same-build A/B with comparable workloads, especially low reuse; no
   performance conclusion follows from code shape.

Current root-signature setters dirty both signature and arguments, so the
target's additional signature-dirty predicate is **not** an independent current
defect. Current resource-use emission merges usage/stage masks; preserve this
   when changing deduplication. Base pipeline backend still defaults to AIRCONV
rather than target None, but no externally usable failed PSO was demonstrated.

## Reconciliation order and acceptance gates

Production counter gating follows the user's explicit requirement; its cost
still needs measurement and is not a D3D12 correctness rule.

1. Adapt omitted `2854e864` correctness slices: direct AIRCONV boundary/error
   ownership, default compute residency and mesh masks. Add negative API tests,
   default-environment compute readback and mesh root/table live-slot checks.
2. Separate submission-owned live references from recording ownership. Validate
   repeat execution, descriptor replacement, concurrent in-flight submissions
   and allocator reset; profile retained references/memory, not HUD alone.
3. Adapt disabled-log gating and verify both levels plus argument side effects.
4. Introduce shared immutable metadata preserving flags and safe invalidation;
   add adapted generation/ownership/scan tests before VA reuse optimization.
5. Add the declared-range API with non-colliding paired ABI, capacity/null tests,
   all-stage footprints and automatic conservative fallback GPU readbacks.
6. Only then compare batching/sort variants on identical builds and introduce
   gated/bounded observability. Do not use unrelated rendering passes or skipped
   shader compilation as proof of these contracts.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

Current `6c44dc2`; pinned other-branch target `bad6756a`; merge-base `85bb2dd2`.
Verified divergence before the audit commit: 137 ahead / 17 behind.

## Local Commit

Documentation-only audit; its commit ID is reported after committing, not
self-embedded in this file.

## Changed Files

Only `docs/D3D12_RECONCILIATION_AUDIT.md`.

## Implementation

No production changes, merge, rebase or cherry-pick. Recorded the 17 decisions,
confirmed omissions, hazardous imports and dependency-ordered adaptation plan.

## DXBC / AIRCONV Impact

Identified direct API failure and default compute omissions; no backend changes.

## DXIL / MSC Impact

Identified generic mesh resource-mask omission; preserve newer origin/reduction
paths and the permanent DXIL→MSC routing.

## Shared Runtime Impact

Identified ABI collision, execution-retention scope and cache identity risks;
no deployed binaries changed.

## Tests Added

No repository tests. Temporary native null-error and PE logging diagnostics
are stored in `/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE`.

## Tests Run

Git merge-base/count/cherry and endpoint patch inspection; identical-source
comparison for `dx12_shader_container.cpp` and `dx12_shader_validation.cpp`;
normal/no-private native null-error probes; PE disabled-logging probe;
documentation whitespace check before commit.

## Runtime Results

`airconv-null-normal2.log` and `airconv-null-no-private.log`: control returns 1
with error object; null-error child signal 11; runner exits 1. Initial normal
loader failure is not test evidence. `logging-build3.log`: compilation passed;
`logging-current.log`: runner exits 1 with disabled-argument evaluation failure.
Exit statuses were captured by the process runner separately from log text.
Logs reside in the cache directory above. No fresh full build, GPU acceptance,
game launch or performance benchmark is claimed for this documentation audit.

## Standards Review

Independent readonly review identified the thunk collision, incomplete
all-stage footprint and unsafe lock/lifetime import risks. Main review keeps
current ownership, flags, stage unions and paired ABI constraints explicit.

## Spec Review

Independent readonly review confirmed default compute and mesh-mask omissions,
static-range reread regression risk and null-error diagnostic evidence. Root
signature dirty-state concern was excluded because current setters also dirty
arguments. Retention accumulation is not mislabeled premature release.

## Known Limitations

No general correctness certification, same-address VA remap reproduction,
same-build sort A/B or aggregate-counter profiling. Source/test identity is
not fresh execution. Each adaptation still needs focused tests and GPU gates.

## Capability Status

Unchanged; TypedUAVLoadAdditionalFormats and other unqualified capabilities
remain disabled. Full development goal remains open.

## Feature Level Impact

No FL12_0 or FL12_1 promotion.

## Git Status

Clean at audit start; this document is the sole planned staging/commit target.
The final response records the resulting commit and verified worktree status.

## Push Status

No push. Exact target object fetch did not update local or remote-tracking
branch refs. No game/prefix DLL writes or process-management actions.

## Next Recommended Task

Focused adaptation of omitted `2854e864` safety slices, starting with direct
AIRCONV null-error/boundary handling, default compute residency and MSC mesh
generic stage masks. Do not begin by merging the old cache/footprint stack.

# Task Analysis

## Current Branch

`feat/d3d12-1`; HEAD `460cea8`; clean at task start.

## Baseline

Previous reconciliation audit `c650c4c`, pinned target `bad6756a`, merge-base
`85bb2dd2`. This task adapts execution retention, not an entire target commit.

## Local Commits Since origin/feat/d3d12

Pinned target comparison is 139 ahead / 17 behind before this task; tracking
baseline remains stale. Preserve all local shader/runtime work.

## Current State

Generic live descriptor resolution adds native resource references to encoder
recording state on each execution; reset, not submission completion, frees them.

## Existing Implementation

Queue Submission already owns typed-origin/MinMax bindings, sampler references
and indirect root buffers until completion. Heap slots resolve/retain under
lock; fan-out occurs outside. Static descriptors never enter the pending list.

## Relevant Files

Command-list interface/implementation, command queue Submission and resolver
call sites, typed-buffer residency fixture and this task record.

## Existing Tests

Typed-buffer resolver fixture checks current aligned/unaligned, backend,
SRV/UAV and static/live behavior. It does not assert execution ownership.

## D3D12 Contract

Each execution owns its live resources through its GPU terminal state. Preserve
recording references, pending heap owners, usage/stage unions and static flags.

## DXBC / AIRCONV Impact

Generic live references change owner only; shader path unchanged.

## DXIL / MSC Impact

Same shared host lifetime correction; typed-origin/reduction bindings unchanged.

## Shared Runtime Impact

Explicit execution-reference output on resolver; queue owns it in Submission.
No mutable global/encoder execution vector, no early clear, no new thunk slots.

## Missing Pieces

Stop persistent encoder accumulation; test repeated resolver calls and separate
overlapping execution owners; run current GPU compute readback regressions.

## Hypothesis / Evidence / Expected Effect

Encoder append is visible in ResolvePendingDescriptorUses. An unchanged encoder
reference count after resolution is a red-capable contract. Moving the append
to per-Submission ownership should bound recording state without early release.

## Risks

Breaking internal resolver callers; releasing before GPU completion; clearing
earlier in-flight execution; mistaking resolver-only assertions for GPU lifetime
certification; allocation exceptions must still fail closed.

## Minimal Implementation Plan

Add/run accumulation assertion against old code; adapt explicit resolver owner
and both queue callers; extend separate-owner tests; build both modes, regress,
self-review and locally commit without push.

## Validation Plan

Reconfigure both modes before builds. Run resolver fixture red then green in
cache-only Wine overlays; full builds/host suites and default compute readback.
No game/prefix DLL writes or process management. Completion path source review
must confirm Submission destruction follows waitUntilCompleted, including abort.

## Capability Impact

None; full goal remains active.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

`460cea8`; pinned divergence 139/17 before this task's commit.

## Local Commit

Reported after commit; no hash self-embedded in this document.

## Changed Files

`src/d3d12/d3d12_device.hpp`, `d3d12_command_list.cpp`,
`d3d12_command_queue.cpp`; `tests/dx12/dx12_typed_buffer_residency.cpp`,
`dx12_texture_sampler.cpp`, `dx12_airconv_firstbit.cpp`, `meson.build`;
this task record.

## Implementation

Resolver now requires an explicit execution-reference vector. Both queue
callers pass `Submission::descriptor_resource_refs`; only live-resolution
appends move to this owner. Recording resources, pending descriptors and heap
owners remain intact. Local handle deduplication still retains unique emitted
resources per resolver invocation; every use emits its usage/stage callback.
No descriptor flag, snapshot/heap-lock or shader-binding path changed.

Commit moves the owner into the submission ring. CompletionThread moves the
Submission locally, waits for GPU completion, handles error state, then destroys
its owners at iteration end. Translation failure/allocation exception/commit
rejection before commit destroys local owners after the abort guard unregisters
allocators; no GPU work from that command buffer was submitted. No shared owner
is cleared while an earlier execution is in flight.

## DXBC / AIRCONV Impact

Same shader and resource-use semantics; repeated live executions no longer grow
encoder references. Default compute remains enabled from the prior repair.

## DXIL / MSC Impact

Shared generic lifetime repair; existing origin/MinMax submission bindings and
MSC view validation remain untouched.

## Shared Runtime Impact

Internal C++ resolver callers updated atomically. No PE/native thunk or ABI slot
change. Fresh matching DLLs/tests deployed only into cache Wine overlays.

## Tests Added

Resolver fixture now asserts no recording append, independent old/new execution
owners across actual buffer replacement, 32 repeated resolutions per backend,
one recording sentinel/pending state preserved, duplicate resource retain with
object/mesh stage fan-out, and one owner surviving another owner's clear.
Existing aligned/unaligned SRV/UAV, mixed backend order and empty-static-list
checks are preserved. No native destruction oracle is inferred from vectors.

`dx12_airconv_firstbit --repeat` replays the same closed list 32 times through
the real queue, checks recording reference count after each translation and
fence-completed exact ten-value GPU output. The recorded state cycle now restores
`COPY_SOURCE` to `UNORDERED_ACCESS`, making repeated replay valid. Default mode
still runs once. Sampler-negative resolver caller adapted to explicit owner.

## Tests Run

Both configurations reconfigured before builds. Old resolver red command:
cache `safety-runtime/bin/wine safety-normal/dx12_typed_buffer_residency.exe`
with prefix/overrides as existing cache runs. It exited 1 with
`live resolution appended execution references to recording encoder`.

Full normal/no-private builds and final incremental full builds completed 0;
final host tests each passed 6/6. Final resolver runs each exited 0; final
32-execution GPU runs each exited 0 with the expected readback. Negative MSC
unaligned-view rejection lines in resolver logs are expected fixture cases.

An intermediate resolver test exited 1 because it assumed changing FirstElement
must change AIR native view identity. AIR can reuse a whole-buffer view; the
fixture was corrected to use a second buffer, not by relaxing ownership checks.
No production change was needed for this intermediate test-model failure.

## Runtime Results

Evidence cache `/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE`:
`lifetime-red-build.log`, `lifetime-red-run.log`, `lifetime-config*-*`,
`lifetime-full-*`, `lifetime-final-full-*`, `lifetime-final-host-*`,
`lifetime-final-resolver-*`, `lifetime-final-gpu-*`.
Process exit statuses were separately captured by the command runner.

Final build/cache D3D12 DLL SHA1s match:
normal `b3ac1b5c697558bc23b4554b8ab65e88f2a2c591`;
no-private `3876c5c56a61aa4901b2835489b0db8b6b1dae93`.
GPU repeat runs leave AIR compute override unset. No game benchmark, Metal
validation run or performance/memory delta claimed.

## Standards Review

Independent readonly final review: 0 hard violations, 0 actionable heuristic
smells. Uses existing Submission and WMT::Reference ownership, explicit names
and scopes. Main self-review verified every internal resolver caller is adapted;
retains occur while snapshots are still alive and before resource callbacks.

## Spec Review

Independent readonly review: 0 implementation defects. Recording/static/live
contracts preserved; abort and completion ownership paths consistent. Updated
fixture avoids AIR offset/view identity assumption and keeps the last execution
owner alive when clearing the first. Main execution provides the build/runtime
results above; reviewers performed no tests.

## Known Limitations

Resource replacement's actual GPU in-flight survival and post-completion native
destruction are not directly observed. Resolver independent-owner assertions,
queue repeat/readback and completion-path source review are distinct evidence.
No new GPU-error/allocation-failure injection, cross-queue concurrent replacement
or retained-memory profile. Deduplication remains per resolver rather than
whole submission; no same-build batching/sort performance claim.
Static recording references intentionally still survive until allocator reset.

## Capability Status

PARTIAL overall; scoped accumulation regression passes in both build modes.
Full residency/lifetime matrix and full development goal remain open.

## Feature Level Impact

FL11_1 unchanged; FL12_0/FL12_1 and unqualified feature bits not promoted.

## Git Status

Only the eight task files are intended for commit; final status reported after
local commit. No unrelated working-tree changes observed.

## Push Status

NOT PUSHED. No merge/rebase/cherry-pick, game/prefix DLL changes or process
management actions.

## Next Recommended Task

Adapt the independently missing disabled DEBUG/TRACE formatting gate before
metadata/cache/footprint optimization. Keep actual GPU-retirement/replacement
and resource-consuming mesh acceptance as open tests, not hidden completion.

# Task Analysis

## Current Branch / Baseline
feat/d3d12-1 / origin/feat/d3d12; starting point 019d874, clean worktree.
## Local Commits / Current State
AIR descriptor filter/LOD state is connected; dynamic reduction still rejects.
## Existing Implementation / Relevant Files
Sampler heap writes/copies are unsynchronized and own only each current slot.
Resource descriptor observation already distinguishes static and volatile ranges.
## Existing Tests / D3D12 Contract
Sampler GPU fixture covers actual AIR/MSC readback and exact descriptor copies.
Static descriptors require recording-time observation; volatile ranges require
submission-time live observation. Sampler native objects must outlive execution.
## DXBC / AIRCONV Impact
No shader lowering change. Dynamic sampler lifecycle prerequisite only.
## DXIL / MSC Impact
Same host retention contract; no shared shader ABI or backend fallback.
## Shared Runtime Impact
Atomic heap snapshots retain sampler objects before releasing the heap mutex.
Static sampler table uses retain at recording; volatile uses retain per submission.
Copies lock both heaps without holding locks during encoder fan-out.
## Missing Pieces / Risks
Runtime reduction branching and unsupported-consumer validation remain open.
Risk: deadlocking self-copy, retaining live generations indefinitely, or adding
submission rereads to static ranges. Avoid mutating closed encoder ownership.
## Minimal Implementation Plan / Validation Plan
Implement sampler snapshot/locking and connect real Draw/Dispatch recording and
queue submission retention. Run narrow resolver/lifetime and GPU regression.
## Capability Impact
No sampler acceptance or FL/SM promotion.

Hypothesis: allocator-owned static refs and submission-owned live refs prevent
sampler release after descriptor mutation without growing closed-list state.
Evidence: current native handles reference the objects owned by heap slots.
Expected effect: correct object lifetime across recording/submission/completion.
Risk: resolving unused slots conservatively and additional observation cost.
Validation: focused static/live resolver contracts and real sampler readback.

# Task Result
Implemented sampler heap atomic snapshots, synchronized writes/copies and
self-copy locking. Static descriptor ranges retain native samplers during
recording with no pending entry or submission reread. Root static samplers are
also retained by the recording encoder. Volatile ranges retain the heap and
resolve unique live slots into submission-owned references until GPU completion;
closed encoders never accumulate submission generations. Snapshot allocation,
consumer validation and retention fan-out occur outside the heap lock.

Normal and no-private builds succeeded. Eight focused observation executions
(DXBC/DXIL x static/volatile x both builds) passed with GPU output 255 and correct
variant winemetal.so loader paths. Fourteen further executions passed: static AIR
minimum/maximum/mixed-clamp readbacks (16/240/16), dynamic reduction rejection and
exact descriptor-copy checks, ordinary AIR/MSC root static sampler readbacks,
and typed-buffer submission resolver contracts, each in both builds.
Meson tests passed 3/3 per build; git diff --check passed.

Fixture review caught and corrected the wrong range index for the static flag,
and inspection of an unlinked current encoder before the pass boundary.

## Standards self-review
No blocking documented-standard issue found. Existing Rc and COM ownership are
used; no manual native release or encoder callback under the heap mutex.
Linear static-reference deduplication is a possible performance concern, not a
measured regression; this change makes no performance claim. Independent review
agents could not run because of usage limits; findings here are main-agent review.

## Spec self-review / Limitations
Recording vs submission timing, RS1.0 deserializer compatibility and per-submission
live ownership are preserved. Static ranges remain absent from pending lists.
Direct-indexed heaps and root-updating indirect paths conservatively observe the
whole sampler heap; their complete GPU matrices and concurrent mutation stress
remain unverified. This is lifecycle groundwork, not dynamic Min/Max admission:
dynamic reduction, remaining sample operations, feedback and DXIL reduction stay
open. No capability promotion, full FL gate, game acceptance, benchmark or Metal
validation claim. Installed game DLLs and Wine prefix were not overwritten.

# Task Analysis

Clean baseline `216549a`, branch `feat/d3d12-1`. Preserve 359 invocation modes
and 50 host tests. Creation path only; addition multi-export remains separate.

Reuse real source-linked factory with two ordered aliases. First export uses a
different stage (Callable, or RayGen if target Callable); second target covers
six stages plus AH/CH hints. First must compile/load before second fault.
Forty modes: control/library/function/unsupported/second-pass per target.
Observe both loads, fail second library/function ordinal, or selected target
compiler pass. E_NOTIMPL/null factory output, no partial publication. Retry same
description creates a fresh object; first conversion cached, second cached only
for load failures. Both aliases/group identifiers checked. Later compiler-fault-
armed creation requires cached conversion but fresh real loads for both exports.

Hypothesis/evidence: partial-progress coverage gap, not a confirmed production
defect. Risks: cached progress differs by failure layer; first stage must not be
affected by target compiler fault, hints only apply to second export. Preserve
old single-export paths. No production change unless deterministic contract
failure reproduced. No new GPU/capability/FL/native/game/FPS claim.

Independent normal/no-private reconfigure/full builds, host/Meson tests, Wine
gates with provenance and Metal API/shader validation; restore normal. Independent
Standards/Spec review against baseline, Task Result, local commit without push.

# Task Result

## Implementation and evidence boundary

40 multi-export creation modes added (state-object oracle 108 total), all old
359 modes retained. Two aliases in the same library, first Callable or RayGen,
second requested target; AH/CH hint applies only to second. Library/function
fault ordinal 2 proves a real first load preceded failure. Compiler faults select
entry point plus stage so first candidate probing is not contaminated. For scoped
multi-export materialization failure, fault tests the materialization pointer,
not the old per-stage call count that includes first-export wrong-stage queries.
Unscoped single-export second-pass behavior remains unchanged.

Failed factory output checked from sentinel to null, HRESULT E_NOTIMPL; first
record progress is never publicly published. Fresh-object retry repeats real
loads for both exports. First conversion is cached, second cached only for load
faults; compiler failures require second conversion query/materialization again.
Later compiler-fault-armed creation skips both successful conversions but still
loads both real libraries/functions. Both aliases and optional group identifier
checked; original source names are not public aliases. No successful handles
fabricated; no production source/ABI/backend routing change or bug reproduced.

## Validation and review

Independent reconfigure/full builds normal/no-private PASS. Initial test-only
const export-array declaration did not match bundled header's mutable pExports;
corrected declaration (no cast), rebuilt successfully before runtime checks.
Host 51/51 PASS, Meson 3/3 each PASS. Seven targeted normal modes PASS in
`build/state-multi-targeted.log`: all five RayGen operations, hinted AH second-pass,
Callable function failure (first export RayGen instead). Exact progress/retry
traces checked at actual source-linked factory. Full runtime/provenance and
restoration receipts recorded below; no GPU semantic claim.
MSC compile/integration skills guided real stages/load controls and fault scope;
Metal-validation skill guided environment before device creation. Independent
Standards/Spec review uses baseline `216549a` and this untracked specification.
Standards zero hard findings, one optional trace-generation duplication heuristic
deferred to keep first/second failure expectations explicit. Spec zero implementation
findings; pending runtime evidence closed by final receipts before commit.
Normal receipt `build/fl12-state-multi-normal.json`: 399/399 invocation modes PASS,
old 359 preserved and exactly specified 40 added; build/runtime/executable
provenance checked, all cases API/GPU-validation enabled, no selected assertion/
shader/GPU-validation errors. Both FL12 gates FAIL.
No-private receipt `build-no-private/fl12-state-multi-no-private.json`: 399/399 PASS,
provenance PASS, API/GPU validation enabled for every case, no selected validation
errors. Both variant case sets retain all old 359 modes and add exactly planned
40. Full runners exit 1 with existing typed-UAV/missing-requirement FAIL gates,
not invocation failures. Normal DLLs restored through `sync_dlls.sh`, fresh normal
provenance PASS. Final diff check PASS; actual local commit/clean tree in handoff.

## Limits and next task

Two ordered exports only; larger export sets/orderings, addition parent progress,
native error equivalence, destruction counts/resource growth, concurrency,
GPU hit-group/dispatch semantics, games and FPS remain unverified. No same-object
initialization retry or retained Metal-object identity claim. Capabilities and
FL11_1 unchanged; FL12 gates FAIL due to existing typed-UAV/missing requirements.
Overall isolation PARTIAL. Next: addition multi-export failure/publication/cache
progress with immutable parent. Local commit only, NOT PUSHED.

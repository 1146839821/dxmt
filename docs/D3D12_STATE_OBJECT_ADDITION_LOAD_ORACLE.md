# Task Analysis

Clean task baseline `02d31d4`, branch `feat/d3d12-1`. Preserve 335 invocation
modes and 49 host tests. Extend only addition export load evidence.

Reuse the real source-linked addition factory and existing parent identifier,
stack-size and alias/hit-group assertions. Add 24 modes: six stages plus AH/CH
hints, each control/library/function failure. Parent seed is created with load
observation disabled; arm only afterward. Failed child must be null (sentinel
cleared), E_NOTIMPL, parent identifier/stack unchanged, no child exports on parent.
Retry uses the same parent and description, creates a fresh child, reuses converter
cache but loads real Metal objects. Exact compiler/load traces; no AIR fallback.

Hypothesis/evidence: bounded coverage gap, no confirmed production bug. Expected
effect: permanent parent-immutability and failure/publication/retry evidence.
Risk: parent cache/seed could accidentally trigger injected fault; arm only after
seed validation and reset load ordinals per child call. No production changes,
ABI/capability promotion or GPU/game/FPS claim. Multi-export progress remains open.

Validate independent normal/no-private reconfigure/full builds, host/Meson tests,
full Wine gates/provenance/API+shader validation; restore normal. Independent
Standards/Spec review, Task Result and diff check, local commit without push.

# Task Result

## Scope and implementation

24 addition-load modes added to mandatory addition oracle (70 total). Existing
335 invocation modes retained. Only test prefix normalization changed in the
C++ harness; existing real import wrappers, exact traces, output-sentinel guard,
same-parent retry and inherited identifier/stack checks reused. Parent creation
occurs before observation is armed, so child faults cannot affect seed loading.
Every mode has one child export; counts reset for retry/cache creation.

Each child load failure ultimately returns E_NOTIMPL with null factory output,
and original parent identifier bytes/stack remain unchanged with no child aliases
or hit group published on parent. Retry creates a fresh child from the same parent;
conversion cache reused but real library/function loads required. Later compiler-
fault-armed creation also requires real loads. Parent identity/stack inheritance
checked on successful children. No same-child initialization retry or cached
Metal-object identity claim; no fabricated successful Metal handles.

## Validation and self-review

Independent normal/no-private reconfigure/full builds PASS. Host 50/50 PASS,
Meson 3/3 each PASS. Targeted normal receipt `build/add-load-targeted.log`:
RayGen control, RayGen library failure, AH-hint function failure PASS including
parent seed, child failure/null publication, retry/cache and parent immutability.
Full dual runtime/provenance/validation receipts and normal restoration recorded
below. Permanent test instrumentation only; production unchanged.
MSC compile/integration skills guided real stages and loading; Metal-validation
skill guided environment before device creation; code-review skill guided
independent Standards/Spec review against task baseline.
Standards: zero hard findings, one optional matrix-duplication heuristic deferred
to preserve bounded caller-specific registration. Spec: zero implementation or
scope findings. Final runtime receipts, not static review, close validation.
Normal full receipt `build/fl12-add-load-normal.json`: 359/359 invocation modes
PASS, runtime/build/executable provenance checked, all 359 API/GPU-validation
enabled, no selected assertion/shader/GPU-validation errors. FL12 gates FAIL.
No-private receipt `build-no-private/fl12-add-load-no-private.json`: 359/359 PASS,
provenance PASS, all cases API/GPU-validation enabled, no selected validation
errors. Both case-set comparisons preserve all old 335 modes and add exactly
the specified 24. Full runners exit 1 with existing FAIL capability gates, not
invocation failures. Restored normal DLLs using `sync_dlls.sh`; fresh normal
provenance PASS. Final diff check PASS; actual local commit/clean tree in handoff.

## Limits and next task

No native error equivalence, destruction-count/resource-growth, GPU ray dispatch,
hit-group traversal, concurrent caller, game or FPS validation. Parent invariants
are exposed identifiers/stack and absence of added exports, not a proof of every
internal field. No production defect reproduced. Capabilities/FL11_1 unchanged,
FL12 gates FAIL due to existing typed-UAV and missing requirements. Overall
isolation PARTIAL; multi-export partial progress remains unverified.
Next bounded task: multi-export failure after an earlier export succeeds, including
failure/publication/retry and cache-progress evidence. Local commit, NOT PUSHED.

# Task Analysis

Baseline `5fdfb46`, branch `feat/d3d12-1`, clean working tree. Preserve all
303 existing invocation modes; capabilities and FL12 gates unchanged.

Lazy dispatch loads synthesized dispatch and optional triangle-intersection
libraries/functions before creating its retained PSO. These four boundaries
lack permanent failure/retry traces. This is a coverage task, not a confirmed
production bug: add test-only import wrapping, real forwarding controls, empty
failure output, complete same-object retry and identical no-call retained hits.
Observe library loads by ordinal, function loads by exact name. Arm only after
state-object creation so export compilation remains independently checked.

Expected effect: eight additional modes (two controls, dispatch library/function
failure on each path, intersection library/function failure). No backend fallback,
ABI changes or fabricated successful Metal objects. Risk: wrapper ABI and event
selection must not perturb the old modes. Validation: normal/no-private builds,
host/Meson tests, exact Wine traces and provenance with Metal API/shader validation,
restore normal; Standards/Spec self-review, local commit without push.

If a production failure is found, reproduce a deterministic red before fixing.
Otherwise preserve production code. Broader creation-time export loads,
multi-export progress, concurrency, games and full GPU semantics remain separate.

# Task Result

## Implementation and bounded evidence

Added eight modes to the existing mandatory Metal oracle (19 total); all 303
previous invocation modes retained. Test-linked imports wrap the exact WMT
library/function ABI and forward every non-injected call. Observation is armed
only after real state-object creation and its compiler seed trace succeeds.
Library ordinal 1 is dispatcher, ordinal 2 is triangle intersection; function
loads use exact current production names. No successful objects fabricated.

Each failure returns E_FAIL with empty Metal handles and cleared preseeded root;
trace stops at the selected load boundary, without PSO/table work or compiler/AIR
fallback. Same-object fault-disabled retry repeats complete synthesis/load,
PSO/table/handle/binding work. Fault-armed retained hit preserves all three
handles/root and produces zero new events. Previously populated Metal output
handles are not independently tested. No production defect reproduced: current
implementation satisfies these bounded contracts; production source unchanged.

Normal full receipt `build/fl12-ray-load-normal.json`: 311/311 invocation cases
PASS, including all eight new modes; runtime/build/executable provenance checked.
API and GPU validation enabled in every case, no selected assertion/shader/GPU
validation error. Intersection-function failure ends at its real function-load
boundary with E_FAIL; retry adds all four synthesis calls, both libraries/functions,
PSO/tables, three function handles and all bindings. No-call retained retrieval PASS.
48/48 host tests PASS, independent reconfigure/full builds PASS, Meson 3/3 each.
No-private receipt `build-no-private/fl12-ray-load-no-private.json`: 311/311 PASS,
provenance PASS, all cases API/GPU-validation enabled, no selected validation
errors. Both full runners exit 1 solely with existing FAIL capability gates.
Normal DLLs restored through `sync_dlls.sh`, fresh normal provenance PASS.
Final `git diff --check` PASS; actual local commit reported in final handoff.

## Independent self-review

Against baseline `5fdfb46`, working diff and this untracked specification.
Standards: zero hard findings. Optional mode-string descriptor abstraction is
deferred to avoid expanding this coverage-only task. Adopted full 19-mode set
assertion and preserved legacy Metal failure aggregation; independent explicit
test expectations are intentionally not imported from runner configuration.
Spec: zero implementation findings; completion receipts supplied before commit.
Diagnosis skill constrains a fix to a reproduced bug; none found, so no production
edit. MSC integration skill guided real load controls, exact names and completed
state checks; Metal-validation skill guided environment before device creation.

## Capability and next task

Capabilities/FL11_1 unchanged. FL12_0/FL12_1 remain FAIL due to existing typed-UAV
and missing semantic requirements; runner exit 1 is expected capability refusal.
Isolation remains PARTIAL: creation-time export loads and multi-export progress
unverified. No new GPU dispatch/trace, game, FPS, native Windows, concurrency or
resource-growth acceptance claim. Next bounded item: creation-time export
library/function-load failure and publication/retry coverage. NOT PUSHED.

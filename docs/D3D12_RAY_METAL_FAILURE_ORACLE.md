# Task Analysis

## Branch / baseline / local commits / current state

`feat/d3d12-1`, clean task baseline `2f81d9e`; integration
`origin/feat/d3d12`, merge-base `85bb2dd`. Preserve prior 292 invocation modes
and 45 host tests. Overall isolation PARTIAL; both FL12 gates FAIL.

## Existing implementation / relevant files / tests / contract

Lazy ray-dispatch initialization assigns dispatcher_pso_ before creating/filling
function tables; its early return checks only dispatcher_pso_. New test-local
WMT import wrapping must reach that real source-linked caller, return null on
allocation/handle faults and forward all controls. Reuse synthesis harness,
six-stage fixture and exact traces. Current errors E_FAIL are DXMT-local evidence;
no native Windows or GPU semantic claim. No production debug hooks.

## Hypothesis / evidence / expected effect / risk

Preliminary observation only: PSO publication precedes table completion. Build
a red-capable same-object failure/retry test before diagnosing/fixing. Eleven
modes: dispatch/intersection real controls plus PSO, VFT, IFT and first visible
function-handle failures, and intersection-function-handle failure. Exact WMT
events and real synthesis required; output stays empty on failure, retry must
rebuild a complete state, retained hit must have identical handles/zero calls.
Risks: intermediate references must be released after failure; successful path
must retain all completed objects. Concurrency is explicitly out of scope.

## Backend / shared runtime / missing pieces / minimal plan

No DXBC/AIRCONV or DXIL/MSC parsing/ABI change; no shader fallback. Record red
evidence and ranked falsifiable hypotheses; only then apply the smallest state
publication fix if justified. Keep PSO/tables local until all function handles
and bindings succeed. Mandatory gate/provenance/host regression. Metal library/
function-load failures, multi-export progress, GPU output and concurrent callers
remain separate; overall isolation PARTIAL and capabilities unchanged.

## Validation / capability impact

Independent normal/no-private reconfigure/full builds; targeted red/green,
host/Meson tests, full Wine gates with API/Shader Validation before device creation,
runtime/executable provenance, restore normal. Parallel Standards/Spec review
against `2f81d9e`; Task Result, diff check, local commit, no push. FL11_1 unchanged;
FL12 gates remain FAIL.

# Task Result

## Branch / baseline / local commit / changed files

`feat/d3d12-1`, integration `origin/feat/d3d12`, task baseline `2f81d9e`.
Actual local commit and clean tree reported in final handoff. Production
`d3d12_raytracing_pipeline.cpp`, backend-failure executable/import wrapping,
FL gate, host tests, guide and this document only.

## Red evidence / diagnosis / implementation

Two controls PASS; `metal-dispatch-vft` FAIL twice on baseline production source.
First call: E_FAIL, dispatch synthesis plus PSO/VFT/IFT events, output empty.
Fault-disabled retry: S_OK, zero new events, missing VFT, final oracle FAIL.
Minimal agent-runnable seam is the actual source-linked GetDispatchState caller.
Ignored receipts: `build/ray-metal-red.log`, `build/ray-metal-red-repeat.log`.
Ranked predictions recorded before fix: premature retained PSO explains no-call
success; poisoned synthesis would produce new synthesis failures; uncleared
injection would produce new WMT calls/errors. Last two contradict the trace.

Keep PSO, VFT and IFT in local WMT references through creation, every function
handle and every table binding. Publish completed tables then the PSO success
marker only after all checks succeed. Early failures release local references
and leave no retained success marker. No concurrent-initialization claim.
Test-import wrappers return null for selected allocations/handles and otherwise
forward real calls, including setters. No successful handles fabricated and no
production fault hooks. State/object/root lifetimes retain existing semantics.

## Backend / shared runtime / tests added / validation

No shader parsing/ABI/backend routing changes or capability promotion. 11 modes
added, prior 292 retained, mandatory gate/provenance and host checks. Positive
controls require actual PSO/tables/root. Failed outputs empty; full same-object
retry must include synthesis, PSO/tables, each handle and each binding. Fault-
armed retained retrieval requires identical handles and zero events. No compiler
or AIR fallback during initialization/retry. Metal wrappers disabled for old modes.
Normal green targeted 11/11 PASS: VFT retry now includes two real synthesis calls,
PSO/VFT/IFT, handle and setFunction. Independent normal/no-private full builds
PASS; 47/47 host tests PASS; Meson 3/3 PASS each. Full normal/no-private gates:
303/303 invocation cases PASS each, build/runtime provenance PASS each, API and
GPU validation enabled for all 303 cases each; no selected validation errors.
Receipts: `build/fl12-ray-metal-normal.json` and
`build-no-private/fl12-ray-metal-no-private.json`. Both runners exit 1 because
the existing typed-UAV/missing-requirement FL gates remain FAIL. Normal DLLs
restored with `sync_dlls.sh`; fresh normal provenance PASS before commit.
Permanent test instrumentation only; no debug
logs or throwaway source introduced.
Additional public `dx12_raytracing_dispatch.exe ray_dispatch_sm6.lib.cso`
regression PASS in both variants with actual GPU readback `0xd312d312`. This is a narrow
raygen/dispatch result, not hit-group or full DXR acceptance. Initial attempt was
UNVERIFIED because the optional fixture was not yet built; explicitly building
the fixture then rerunning passed, without weakening required markers.

## Self-review

Independent parallel review against `2f81d9e`, including untracked spec.
Standards: zero hard violations, one nonblocking repeated fault/terminal-event
mapping heuristic; retain explicit adjacent-test expectations. Spec: zero
implementation findings; one pending evidence item during concurrent review
(red/green and dual runtime results), resolved by Task Result before commit.
Diagnosis skill required deterministic red evidence before production fix;
MSC-compilation/integration skills guided real controls/forwarding and completed
state checks; Metal-validation skill guided environment before device creation.

## Limitations / capability / Feature Level / next task

No library/function-load failure injection, multi-export partial-progress failure,
concurrency stress, native Windows or game/FPS acceptance. Failure output starts
with empty WMT handles and a preseeded root pointer; previously populated WMT
output clearing is not independently demonstrated. Backend isolation PARTIAL.
FL11_1/capabilities unchanged; FL12 gates FAIL due to existing typed-UAV DXIL
matrix and other missing requirements. NOT PUSHED.
Next: bounded library/function-load failure routing or multi-export partial-
progress isolation; keep these separate from broader GPU raytracing validation.

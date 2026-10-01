# Task Analysis

## Branch / baseline / current state / existing implementation

`feat/d3d12-1`, clean task baseline `673c1fd`, integration `origin/feat/d3d12`,
merge-base `85bb2dd`. Preserve prior 280 invocation modes and 43 host tests.
Source-linked state-object implementation lazily synthesizes dispatch query and
materialization, then triangle intersection query/materialization if hit groups
exist, creates real Metal PSO/function tables and retains dispatch state.
`GetD3D12RaytracingDispatchState` clears caller output first. Unsupported-feature
and unavailable synthesis return E_NOTIMPL; other errors return E_FAIL.

## Contract / backend / shared runtime / relevant files / tests

Current DXMT error routing only, not native Windows normative HRESULT evidence.
Actual source-linked GetDispatchState path must use MSC synthesis and never
AIRCONV or shader recompilation after object creation. Retain real root/function
conversion setup and check its exact ordered compiler traces before synthesis.
Existing public state-object tests do not force lazy synthesis failures.
Reuse backend-failure executable, source linkage, six-stage fixture and gate.
No production compiler, runtime, ABI, binding, residency or capability changes.

## Hypothesis / evidence / expected effect / risk / minimal plan

Hypothesis: failed lazy synthesis publishes no dispatch-state handles and permits
same-object retry without poisoning the retained PSO fast path. Evidence: output
clear, two-pass error branches and dispatcher_pso_ early return. Add test-import
wrappers for both synthesis APIs. Twelve modes: two paths times control,
unsupported, unavailable, memory, invalid and materialization failure.
Real controls must return actual PSO, visible/intersection tables and root pointer;
failures must return empty state. Retry requires exact complete real synthesis
sequence; fault-armed repeat requires zero calls and identical state handles.
Intersection retry re-synthesizes dispatch before intersection because no PSO
was retained. Synthesis injection never fabricates successful outputs.
Risk: real PSO/table control can expose an independent runtime limitation; do
not mask that or relax control acceptance. Metal pipeline/table failure routing
and concurrency remain separate, as does command encoding/GPU output.

## Validation / missing pieces / capability impact

Independent normal/no-private reconfigure/full builds, host/Meson tests, targeted
and full gates with API/Shader Validation before device creation, runtime and
executable provenance; restore normal. Parallel Standards/Spec review against
`673c1fd`. Task Result, diff check, local commit, no push. Keep isolation PARTIAL:
Metal library/PSO/table error injection, multi-export progress, GPU tracing,
native Windows and gameplay remain unverified. FL11_1/capabilities unchanged;
FL12 gates remain FAIL.

# Task Result

## Branch / baseline / local commit / changed files

`feat/d3d12-1`, integration `origin/feat/d3d12`, task baseline `673c1fd`.
Actual local commit and clean tree reported in final handoff. Test executable,
Meson import wrapping, FL gate, host tests, guide and this document only.

## Implementation / backend / shared runtime impact

Wrap test-import pointers for both MSC synthesis APIs, forwarding all nonfaulted
calls to real implementations. Observe query/materialization explicitly; faults
return only errors, never fabricated metallibs or PSOs. Production lazy-state
factory/converter remains unchanged. Object creation trace must have seven
RayGen events; intersection setup adds hinted ClosestHit query/materialization.
No synthesis occurs during creation and no shader compiler/AIR events are
allowed in the lazy initialization/retry/retained-hit phases.
Controls require actual Metal compute PSO, visible/intersection function tables
and the expected global-root pointer. Failure must clear all handles and the
preseeded root output. Unsupported/unavailable map to E_NOTIMPL; invalid/memory
to E_FAIL. These are current DXMT caller results, not native/spec conclusions.
Same-object retry requires the complete real two/four-call synthesis sequence;
intersection failure redoes dispatch synthesis before intersection. Fault-armed
retained-state retrieval requires zero calls and identical PSO/table handles.
No production parsing, ABI, root binding, cache, residency or capability changes.

## Tests added / tests run / runtime results

12 mandatory modes added; previous 280 retained. Host coverage requires missing/
failed evidence and all 12 modes, with existing executable/runtime provenance.
Independent normal/no-private reconfigure/full builds PASS. During implementation
the test's root-helper HRESULT check was corrected to FAILED before runtime
execution; no production fix or lowered expectations. 45/45 host tests PASS;
Meson 3/3 PASS per configuration. Normal targeted 12/12 PASS with API/Shader
Validation enabled before device creation and real Metal PSO/table controls.
Full Wine gate: 292/292 invocation modes PASS per configuration (prior 280 plus
new 12); executable/runtime/build provenance PASS. API and Shader Validation
enabled before device creation in every invocation; no validation errors observed
in these cases. Full runners exit 1 because the existing typed-UAV DXIL matrix
FAIL and other missing requirements keep both FL12 gates FAIL, not because of
synthesis oracles. Installed normal runtime restored and provenance reverified
PASS before commit. Ignored receipts: `build/fl12-synthesis-normal.json`,
`build-no-private/fl12-synthesis-no-private.json`, respective `synthesis-gate.log`,
sync/restore logs, normal targeted receipt and Meson test logs.

## Self-review

Independent Standards/Spec review against `673c1fd`, including untracked spec.
Standards: zero hard violations and zero actionable heuristics. Spec: zero
scoped implementation findings; runtime acceptance supplied by the main task.
MSC-compilation skill guided real conversion/synthesis forwarding, integration
skill guided actual PSO/function-table/root controls, Metal-validation skill
guided environment before device creation. No unresolved scoped findings.

## Known limitations / capability / Feature Level impact

No dispatch encoding/GPU readback, native Windows, race stress, corrupted/empty
synthesis-output injection, fused hit groups or gameplay/FPS acceptance.
Failure-state WMT handles start empty; clearing previously populated WMT outputs
is not independently demonstrated (only the root output is preseeded). Metal
library/function/PSO/table allocation failure routing and multi-export partial
progress remain unverified; overall isolation stays PARTIAL rather than moving
to PASS. Existing typed-UAV DXIL failure and other missing requirements keep
both FL12 gates FAIL. FL11_1 and all advertised capabilities unchanged.

## Git / push / next task

Final diff check PASS; clean post-commit tree reported in handoff. NOT PUSHED.
Next: bounded Metal PSO/table failure and retry/publication checks in lazy
ray-dispatch state, keeping it separate from actual GPU semantic acceptance.

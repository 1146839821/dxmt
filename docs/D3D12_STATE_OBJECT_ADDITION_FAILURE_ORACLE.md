# Task Analysis

## Branch / baseline / local commits / current state

`feat/d3d12-1`, clean task baseline `89695cb`; integration
`origin/feat/d3d12`, merge-base `85bb2dd`. Existing gate and invocation work
retained: 234 modes, 41 host tests. Isolation PARTIAL, FL12 gates FAIL.

## Existing implementation / relevant files / tests / contract

`AddD3D12RaytracingStateObject` clears output, validates same-device/implementation,
then initializes a new object from the parent. Initialize requires additions
permission and copies parent records/config before probing new exports.
Existing public state-object test covers successful additions; intercepted
creation oracle does not cover additions. Reuse source-linked production factory,
converter/import wrapper, stage/hint fixtures and exact trace checks.
Relevant files: backend-failure executable, FL gate, host tests and gate guide.
Current error-policy characterization is DXMT_LOCAL_PASS, not native/spec proof.

## Hypothesis / evidence / expected effect / risks

Hypothesis: a failed child neither publishes an object nor mutates parent records
or stack state, enters AIRCONV, or poisons successful retry/cache reuse.
Evidence: separate child allocation, record/config copying, shared candidate loop.
Expected effect: 46 mandatory modes, parent control plus six unhinted/two hinted
stages times five operations; six rejection modes including duplicate public name
and disabled parent additions. Check exact parent and addition traces separately,
inherited identifier bytes, parent stack size and absent added exports in parent.
Risk: parent conversion must use another export so it cannot mask the injected
child stage through cache. Reset per-stage injection counters after seed, never
erase parent evidence; failed unhinted stages must still re-probe on cache reuse.

## Backend / shared runtime / missing pieces / capability impact

DXBC/ordinary rejection calls neither compiler; ray libraries remain MSC-only.
No production implementation, ABI, cache, residency or capability change.
Reuse existing creation test with narrowly scoped addition setup/assertions.
Dispatcher/intersection synthesis failures, lazy dispatch creation, GPU tracing,
multi-export partial-progress failure, native Windows and gameplay remain separate.
Overall isolation PARTIAL, FL11_1 unchanged, FL12 gates FAIL.

## Minimal implementation / validation plan

Extend state-object oracle with real parent/control/add factory path and immutable
parent checks, six rejection modes; mandatory gate/provenance and host coverage.
Independent normal/no-private reconfigure/full builds, host/Meson tests, full
Wine gates with API/Shader Validation before device creation, public state-object
regression and provenance; restore normal. Parallel Standards/Spec review against
`89695cb`; Task Result, diff check, local commit, no push.

# Task Result

## Branch / baseline / local commit / changed files

`feat/d3d12-1`, integration `origin/feat/d3d12`, task baseline `89695cb`.
Actual local commit and clean tree reported in final handoff. Changes confined to
the backend-failure executable, FL gate, host tests, gate guide and this document.

## Implementation / backend / shared runtime impact

Extend the existing state-object harness with normalized addition modes; ordinary
creation modes retain their original inputs and trace expectations. Source-linked
production Add factory and converter remain unchanged. Parent uses a different
real export (Miss, or Callable for Miss additions), additions permission and
explicit root/config. Its seven-event real control trace is checked before
injecting faults. Per-stage counters reset only after that seed; all trace
evidence remains available. No backend fallback or successful-output fabrication.
Every addition verifies parent identifier bytes, stack size and absence of new
exports/hit groups; successful children inherit identifier bytes and stack size.
Failed output sentinel is checked before COM adoption. Same-parent retry needs
real conversion; fault-armed cached additions skip only the accepted stage and
still probe five wrong stages when unhinted. AH/CH hints remain single-stage.
Legacy/ordinary/qualifier rejection invokes no compiler after the parent seed.
Missing-export rejection performs six queries; duplicate/disabled-additions
returns E_INVALIDARG before compiler calls. Selected compiler faults return
E_NOTIMPL at the caller, not the converter HRESULT; characterize DXMT behavior
only. No production code, ABI, residency or capability changes.

## Tests added / tests run / runtime results

46 mandatory modes added, existing 234 retained. Host checks missing/failed
evidence, all modes including new rejections, executable/runtime provenance.
Independent normal/no-private reconfigure/full builds PASS without source repair.
43/43 host tests PASS; Meson 3/3 PASS per configuration. Initial normal targeted
run 46/46 PASS; normal public state-object/addition regression PASS.
Full Wine gate: 280/280 invocation modes PASS per configuration, including
all previous 234 and new 46; executable/runtime/build provenance PASS. Metal
API and Shader Validation enabled before device creation for every invocation;
no validation errors observed in them. Public state-object/addition regression
PASS in both configurations. Full runners exit 1 because existing typed-UAV
DXIL matrix FAIL and other missing FL requirements keep both FL gates FAIL,
not because of the invocation oracles. Normal deployment restored and provenance
reverified PASS before commit. Ignored receipts: `build/fl12-state-add-normal.json`,
`build-no-private/fl12-state-add-no-private.json`, respective `state-add-gate.log`,
`state-add-public.log`, sync/restore and Meson test logs.

## Self-review

Independent parallel Standards/Spec review against `89695cb`, including this
untracked document. Standards: zero hard violations, one nonblocking repeated
creation/addition mode-matrix heuristic. Retain explicit caller-specific matrices
without unrelated test restructuring. Spec: zero scoped implementation findings;
runtime verification performed by the main task, not inferred by reviewers.
MSC-compilation skill guided real parent/control conversions; Metal-validation
skill guided environment before device creation. No unresolved scoped findings.

## Known limitations / capability status / Feature Level impact

Parent/addition creation evidence only; multi-export partial-progress failure,
cross-device/foreign implementation rejection injection, changed inherited
configuration rejection, dispatch/intersection synthesis, lazy dispatch-state
creation, GPU DispatchRays/readback, native Windows and gameplay/FPS unverified.
Backend isolation remains PARTIAL. FL11_1 unchanged; FL12_0/FL12_1 gates remain
FAIL due to existing typed-UAV DXIL matrix failure and other missing requirements.

## Git / push / next task

Final diff check PASS; clean post-commit tree reported in handoff. NOT PUSHED.
Next: bounded dispatch/intersection-synthesis failure-isolation evidence, keeping
compiler, synthesis, Metal pipeline and actual GPU execution evidence distinct.

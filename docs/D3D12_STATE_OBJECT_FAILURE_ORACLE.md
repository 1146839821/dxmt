# Task Analysis

## Current Branch / Baseline / Local Commits Since origin/feat/d3d12

`feat/d3d12-1`, clean task baseline `2e65ac4`; integration baseline
`origin/feat/d3d12`, merge-base `85bb2dd`. Existing gate, format/typed-buffer
work and ten compiler invocation families retained (190 modes, 39 host tests).

## Current State / Existing Implementation / Relevant Files / Existing Tests

`d3d12_raytracing_pipeline.cpp` exposes the actual state-object factory.
Initialize probes six candidate stages per unhinted export; AH/CH hit-group
imports restrict that export to one stage. Converter failures continue probing;
no valid candidate returns E_NOTIMPL. Factory clears output before initialization.
Existing public state-object test checks creation/properties/additions without
import interception; converter oracle observes only the callee, not this caller.
Reuse `dx12_backend_failure.cpp`, gate, host tests, six-stage and qualifier
fixtures. Source-link the production state-object implementation in Meson.

## D3D12 Contract / DXBC / AIRCONV Impact / DXIL / MSC Impact

Measure DXMT_LOCAL_PASS caller behavior, not NATIVE_OBSERVED or a normative
HRESULT decision. DXIL ray libraries remain MSC-only; legacy/ordinary rejection
must call neither backend. Six real single-export controls plus AH/CH hinted
controls; selected-stage invalid/unsupported/memory/materialization failures
must leave no object, continue the actual candidate sequence, then permit a
same-process fresh factory retry and fault-armed converter-cache reuse.
Check exact ordered export/stage/pass traces and returned shader identifiers.

## Shared Runtime Impact / Missing Pieces / Risks

No production change or capability promotion. Dispatcher/intersection synthesis,
DispatchRays, GPU output, parent/addition failure routing, native Windows and
gameplay/FPS remain separate. Overall isolation stays PARTIAL for those paths.
Wrong-stage candidates may fail at compiler extraction; controls must use actual
MSC output rather than mock success. Successful conversion cache suppresses
only the accepted stage on a repeat; failed candidates must still be observed.

## Hypothesis / Evidence / Expected Effect / Minimal Implementation Plan

Hypothesis: state-object candidate probing never falls back to AIRCONV and
failed initialization publishes no object or poisoned conversion-cache entry.
Evidence: factory InitReturnPtr and candidate-loop continue/no-found branches.
Expected effect: bounded caller evidence complements the converter oracle.
Add test-only stage-filtered faults, 40 positive/failure modes (six unhinted and
two hinted stages times five operations), and four precompiler/missing-export
rejections. Add mandatory gate row/provenance/host checks; keep broader gap.

## Validation Plan / Capability Impact

Reconfigure/full-build independent normal/no-private; fixtures, host/Meson tests,
full Wine gates with Metal API/Shader Validation before device creation and
runtime/executable provenance; public state-object regression; restore normal.
Independent Standards/Spec review against `2e65ac4`; final diff check, Task Result,
local commit, no push. FL11_1 unchanged; FL12 gates remain FAIL.

# Task Result

## Branch / Baseline / Local Commit

`feat/d3d12-1`, integration `origin/feat/d3d12`, task baseline `2e65ac4`.
Final handoff reports the actual local commit hash; no history rewrite or push.

## Changed Files / Implementation

Test-source link `d3d12_raytracing_pipeline.cpp` in `tests/dx12/meson.build`;
extend `dx12_backend_failure.cpp`, `dx12_fl12_gate.py`, host tests and gate guide.
Production state-object factory and converter are linked unchanged; MSC/AIRCONV
import wrapping observes actual calls. No fake successful candidate or DLL hook.
Single-export library descriptors rename each source to PublicExport; AH/CH
hinted cases add one triangle hit group. Successful objects expose that public
identifier (not the source name) and the hinted hit-group identifier.

## DXBC / AIRCONV Impact / DXIL / MSC Impact / Shared Runtime Impact

Legacy/ordinary shaders rejected before any compiler. Every ray candidate uses
MSC only. No production parser/compiler, ABI, cache, residency, runtime or
capability changes. Factory HRESULT is checked separately from converter faults:
invalid/unsupported/memory/materialization faults yield no selected candidate,
E_NOTIMPL and a cleared state-object pointer, not direct converter HRESULTs.
This characterizes current DXMT behavior, not native Windows/spec correctness
of that error policy. Failed outputs are never adopted as COM objects.

## Tests Added / Tests Run / Runtime Results

44 invocation modes added: six unhinted and two hinted stages times real control
and four failures; legacy/ordinary/qualifier/missing-export rejection. Exact trace
checks cover every candidate in order. Correct-stage control has two passes;
wrong-stage/missing-export candidates have query only. Qualifiers call neither
compiler. Selected first-pass failures still probe remaining candidates; selected
materialization failures perform both calls. Retry executes the full real
conversion again; cache reuse skips only the successful stage, still reprobes
failed unhinted stages (five queries), while hinted reuse has zero compiler calls.
AIRCONV events are forbidden by each complete ordered trace.

Independent normal/no-private reconfigure/full builds PASS. Initial compile
found SDK pExports requires non-const storage; corrected the test descriptor,
then full builds passed without a production repair or relaxed expectations.
41/41 host tests PASS; Meson 3/3 PASS per configuration. Initial normal targeted
run 44/44 PASS and existing public state-object/addition regression PASS.
Full Wine gate: 234/234 invocation modes PASS per configuration (existing 190
plus new 44); runtime/executable/build provenance PASS. Metal API and Shader
Validation enabled before device creation in every case; no validation errors
observed in these invocations. Public state-object/addition regression PASS in
both configurations. Full runners exit 1 because existing typed-UAV DXIL matrix
FAIL and other missing requirements keep both FL12 gates FAIL, not because of
the new oracle. Normal installed runtime restored and provenance reverified PASS.
Ignored receipts: `build/fl12-state-object-normal.json`,
`build-no-private/fl12-state-object-no-private.json`, respective
`state-object-gate.log`, `state-object-public.log`, sync/restore and Meson logs.

## Self-review

Independent parallel review against `2e65ac4`, including the untracked spec.
Standards: zero hard violations, one nonblocking duplicate ray-export/mode
setup heuristic. Retain narrow caller-specific assertions without unrelated
test restructuring. Spec: zero scoped implementation findings; runtime
verification completed by the main task rather than inferred by the reviewers.
An additional helper-reuse suggestion is nonblocking. Sentinel is checked
before COM adoption; successful factory/properties references use ownership
transfer. MSC-compilation skill guided real conversion controls and
Metal-validation skill guided environment before device creation.

## Known Limitations / Capability Status / Feature Level Impact

Bounded state-object creation evidence only. Parent/addition failure injection,
dispatcher/intersection synthesis failure routing, lazy dispatch-state creation,
GPU DispatchRays/readback, fused hit groups, native Windows and gameplay/FPS
not validated here. Overall backend isolation remains PARTIAL; no capability
or shader-model promotion. FL11_1 unchanged; FL12_0/FL12_1 gates remain FAIL.

## Git Status / Push Status / Next Recommended Task

Final diff check PASS; clean post-commit tree recorded in handoff. NOT PUSHED.
Next: close bounded state-object addition compiler-failure routing before
claiming entire shader-library caller coverage; synthesis paths stay separate.

# Backend isolation coverage audit

## Task Analysis

### Current Branch / Baseline / Local Commits

`feat/d3d12-1`; read-only reference `origin/feat/d3d12` at `e147c710`.
Task baseline `2ba21a4`; 30 local commits since the reference.

### Current State / Existing Implementation / Relevant Files / Existing Tests

`dx12_fl12_gate.py` aggregates container/stage contracts and fourteen invocation
oracles. `dx12_shader_container.cpp` tests malformed/ambiguous containers and
mixed families. `dx12_backend_failure.cpp` instruments production-linked
compiler entry points. Previous normal/no-private receipts contain 439 passing
invocation modes each; they are previous runtime evidence, not fresh runs.

### D3D12 Contract / Backend and Shared Runtime Impact

Master prompt sections 5/6/14 require DXBC -> AIRCONV, DXIL -> MSC, no fallback,
and rejection of mixed families, ambiguous/invalid containers and wrong stages.
GPU output correctness is a separate acceptance layer. Neither compiler, shader
ABI nor shared GPU runtime is changed by this task.

### Missing Pieces / Risks

Hypothesis: a missing classification probe can be silently omitted from the
isolation aggregate. Evidence: the comprehension filters `if name in probes`.
Expected effect: missing mandatory evidence becomes UNVERIFIED, not ignored.
Risk: synthetic host fixtures omitted the extended stage probe; make them
explicit rather than weakening production aggregation.

### Minimal Implementation Plan / Validation Plan / Capability Impact

Require all three contract probes; test missing and non-PASS statuses independently
with all invocation probes passing. Audit the existing coverage without expanding
DXR matrices speculatively. Reconfigure/build/test both variants; reaggregate
previous receipts with the new ledger, explicitly without a fresh GPU run.
Independent Standards/Spec review before local commit; no push or promotion.

## Coverage boundary

| Requirement | Evidence | Remaining acceptance |
| --- | --- | --- |
| VS/PS/CS routing and compiler failure without fallback | compute/graphics invocation oracles; exact AIRCONV/MSC counts/traces | bounded inputs only |
| HS/DS/GS and mixed/wrong-stage rejection | tessellation/geometry oracles and container/stage fixtures | not GPU tessellation/geometry correctness |
| Ambiguous and malformed containers rejected | shader validation/container fixtures via public PSO API | add instrumented zero-compiler-call rejection for malformed/ambiguous inputs |
| Cache/retry and extended MS/AS/ray paths | library/state-object/synthesis/Metal oracles, including two-export progress | bounded extended-path regression, not a new FL12 prerequisite |
| Complete evidence aggregation | this change requires validation/container/stage probes | missing evidence must remain UNVERIFIED |
| Shader ABI, reflection, binding and output correctness | separate semantic fixtures required by FL ledger | cannot be inferred from factory success or invocation traces |

Finite fixtures cannot prove arbitrary future edits impossible. Larger export
counts/order permutations are useful targeted tests if a source hypothesis demands
them, not an endlessly growing prerequisite invented from prompt section 14.
Full isolation stays PARTIAL pending the concrete precompiler rejection trace gap;
GPU semantic requirements stay separate. Next bounded task: malformed/ambiguous
compute inputs with exact zero AIRCONV/MSC invocation evidence. Then return to
the mandatory semantic priorities (min/max evidence and typed-UAV failures).

## Task Result

### Branch / Baseline / Local Commit / Changed Files

Branch `feat/d3d12-1`; reference `origin/feat/d3d12`; task baseline `2ba21a4`.
Local commit: this audit's commit (resolve via `git log -- this-file`). Changed
files: this document, `D3D12_FL12_GATE.md`, `dx12_fl12_gate.py`, and its host tests.

### Implementation / Backend and Shared Runtime Impact / Tests Added

Missing validation/container/stage evidence now contributes UNVERIFIED rather
than disappearing from aggregation. All-three-missing input no longer throws
an empty-aggregate exception. New host coverage exercises each mandatory probe
absent and at every recognized status with all invocation probes passing; it
also exercises all contracts absent and checks that an invocation FAIL wins.
DXBC/AIRCONV, DXIL/MSC, shared runtime and deployed DLLs are unchanged.

### Tests Run / Runtime Results

2026-10-02: Python host tests 53/53 PASS; normal and no-private configurations
reconfigured and built successfully, each passing 3/3 Meson tests. Diff check
passed. Previous `build/fl12-add-multi-normal.json` and
`build-no-private/fl12-add-multi-no-private.json` receipts were read and
reaggregated using the changed ledger: each retains 439 PASS invocation cases,
isolation PARTIAL and both FL12 gates FAIL. No fresh Wine/GPU run, DLL deployment
or new GPU acceptance is claimed. Existing normal deployment was left unchanged.

### Self-review

Code-review skill separated independent Standards and Spec reviews against
2ba21a4. Standards identified a stale review-summary description and the then
missing Task Result; both documentation findings are now corrected. One
nonblocking duplicated-test-setup suggestion is retained to keep expected
probe names independent. Spec found no actionable implementation defect; the
explicit malformed/ambiguous rejection trace gap remains a partial requirement.

### Known Limitations / Capability Status / Feature Level Impact

Backend isolation: PARTIAL. No arbitrary-input, concurrent execution or GPU ABI
correctness proof follows from bounded tests. No capability changes; FL11_1
advertisement is unchanged; FL12_0 and FL12_1 remain FAIL.

### Git Status / Push Status / Next Recommended Task

Only the four task files are included in the local commit; no unrelated edits.
NOT PUSHED. Next: instrument malformed/ambiguous compute input rejection with
zero AIRCONV/MSC calls before returning to the mandatory semantic backlog.

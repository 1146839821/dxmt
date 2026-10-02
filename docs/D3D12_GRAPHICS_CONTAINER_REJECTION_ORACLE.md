# Graphics container rejection invocation oracle

## Task Analysis

Branch `feat/d3d12-1`; reference `origin/feat/d3d12`; task baseline `296f360`.
Hypothesis: a malformed PS must be rejected before even a valid VS is compiled.
Evidence: graphics initialization classifies all stages before backend work;
existing public container rejections do not instrument calls.
Expected effect: ten required modes (VS/PS x five input classes) with exact
HRESULT, cleared sentinel output, empty trace and zero AIRCONV/MSC calls.
Existing implementation/tests: reuse compute container constructor and ordinary
graphics factory/trace harness, plus mandatory-mode host aggregation tests.
Risk: wrong-stage or malformed fixtures could mask ambiguity; duplicate/hybrid
must classify Ambiguous before invoking the factory. The legacy CS chunk in the
hybrid is intentional: ambiguity precedes stage decoding, not a valid PSO input.
Minimal plan: test-only extension, no production compiler/ABI/shared runtime
changes. Validate both configured builds/tests and fresh compute+graphics
invocation receipts under Metal validation, then restore normal DLLs/provenance.
Self-review and local commit only. No shader semantic or capability promotion.
Other optional graphics stages and arbitrary input completeness are not claimed;
after this bounded routing task, return to mandatory semantic validation.

## Task Result

Branch/reference unchanged; local commit: this document's commit. Changed files:
test-linked harness, gate registration/status explanation, host tests, guide,
audit follow-up and this document. Neither production backend nor shared runtime
implementation changed. The existing fixture constructor was renamed for reuse.

Ten modes added: each VS/PS slot tests truncated, invalid-offset, no-executable,
duplicate DXIL and hybrid input while the partner slot contains a normal fixture.
Duplicate/hybrid explicitly require Ambiguous classification. The graphics
factory must return exact E_FAIL for parser errors or E_INVALIDARG for the other
three classes, clear sentinel output and make zero compiler calls with no trace.
Each mode's failure independently fails the host oracle aggregate.

2026-10-02: host tests 55/55 PASS; both configurations reconfigured and built,
Meson tests 3/3 each PASS. Fresh normal/no-private receipts each passed graphics
30/30 and compute 16/16 (46 total). Receipts:
`build/graphics-container-normal.json` and
`build-no-private/graphics-container-no-private.json` (ignored build outputs).
Every invocation has Metal API/GPU validation markers. All 15 combined container
rejection cases have exact zero-call/empty-trace records. No failed assertion or
Shader/GPU Validation Error markers found. Before/after provenance PASS for both
variants. Normal DLLs restored and fresh normal provenance PASS.
No whole-ledger invocation rerun or GPU drawing acceptance is claimed.

### Standards

Independent review against 296f360 found no hard violations or concrete bugs.
Two nonblocking heuristics remain: duplicated rejection HRESULT/precondition
logic and numeric mode-prefix offsets. Narrow baseline reuse is retained;
both compute and graphics were freshly rerun to check shared fixture behavior.

### Spec

Independent review found no implementation conflict with master prompt sections
5/6/14. Its pending-receipt finding is now resolved by the two fresh receipts
and restoration above, verified by the main agent. No scope expansion to optional
stages or arbitrary input proof. Code-review skill separated review axes; Metal
validation skill guided environment enablement and stderr error inspection.

### Known Limitations / Capability / Git / Next

Full backend isolation remains PARTIAL beyond bounded factory routing probes.
Reflection, ABI, binding and GPU output need separate semantic acceptance.
No capability or feature-level promotion. Only task files included; NOT PUSHED.
Next: refresh mandatory semantic evidence, beginning with existing min/max
readback receipts and the known typed-UAV DXIL failure; do not extend this
bounded routing matrix indefinitely without a source-driven hypothesis.

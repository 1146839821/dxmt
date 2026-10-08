# MSC graphics indirect root updates

## Task Analysis

- Hypothesis: ordinary MSC graphics can reuse compute's reflected per-command
  TLAB templates and root patching if non-inherited vertex inputs are restored
  using the ordinary vertex-fetch ABI, not the emulation vertex table.
- Evidence: the current graphics resolver binds AIR qwords only; MSC graphics
  updates are rejected. Compute already validates reflected kind/offset/size,
  copies immutable templates, and patches constant bytes or 64-bit root VAs.
  Graphics root VA allocations now have completion-owned submission snapshots.
- Expected effect: ordinary VS/PS DRAW/DRAW_INDEXED signatures can update root
  constants/CBV/SRV/UAV, with both-stage TLAB/heaps and unchanged vertex inputs.
- Risk: host/MSL layout drift, lost inherited bindings, reflection alignment,
  static sampler lifetime, and per-command buffer visibility. VB/IB updates and
  emulated pipelines remain rejected because they need additional ABI support.
- Validation: reconfigure/full build/host suites in both variants; actual MSC
  graphics numeric readback and AIR/compute regressions; two-axis self-review.
  No full matrix, game, tessellation, performance or FL12_0 promotion claim.

UAV validation follow-up before claiming acceptance: both direct and indirect
existing vertex-UAV fixtures reproduce an end timestamp of zero. Hypotheses are
query/pass ordering, racing vertex UAV writes, or invalid root address/residency.
Retain the failed logs and add readback diagnostics to distinguish them; do not
remove the timestamp assertion or count these failures as passing executions.

Timestamp ordering hypothesis after minimization: the single-writer UAV variant
still returns correct `0xff00ff00` data but zero end timestamp; no-private root
constants also reproduce the timestamp failure. This rules out a UAV data race
or the new graphics root address as the sole cause. The timestamp encoder samples
at encoder start, before its in-encoder fence wait. An EndQuery timestamp must
observe preceding GPU work; sample at encoder end, after the waited dummy fill.
Expected effect: ordered query samples without weakening the readback/assertion.
Risk: counter sampling/driver behavior may remain flaky; test repeated direct
and indirect queries in both builds and retain all failed evidence.

Experiment result: moving sampling to encoder end did not remove the zero
timestamp, despite correct UAV data. The experimental queue change is removed
from this binding slice rather than labeled a query fix. A validation-enabled
run also reproduces correct UAV data with zero end timestamp; no API/shader error
is reported in that run (this is not proof that all validation was effective).
The existing timestamp assertion stays intact and this issue remains open.

## Task Result

Ordinary MSC VS/PS ExecuteIndirect now admits root constants/CBV/SRV/UAV for
DRAW and DRAW_INDEXED. Render and compute reuse the reflected kind/offset/size
validation and TLAB template allocator. The GPU resolver copies a separate
per-command TLAB, patches inline constants or 64-bit addresses, and binds it
and the resource/sampler heaps to vertex and fragment stages. Unmodified vertex
inputs are restored through ordinary vertex-fetch slots, not the emulation
table. Null vertex bindings survive; slots exceeding Metal's binding range fail
recording. The existing root-VA submission snapshot and buffer barrier are reused.

VB/IB-updating signatures and MSC GS/tessellation/mesh remain rejected. AIR
qwords remain separate, and typed compute keeps per-submission materialization
without modifying allocator command nodes. API capability declarations do not
change. This is implementation progress, not complete graphics qualification.

Evidence: `/Users/zhangbo/.cache/dxmt-msc-graphics-indirect.tO5aOS`.

- Both reconfigured full builds and final full builds completed successfully;
  final host suites pass 5/5 per variant. Optional fragment fixtures are registered
  in Meson; Wine DXC produced the VS/PS SM6.0 fixtures used here.
- Primary new graphics batch: 14 executions (seven modes in each variant).
  Constants, CBV, SRV, partial constants, fragment constants and indexed CBV
  pass in both variants. Normal UAV passes; no-private UAV returns the expected
  `0xff00ff00` but fails the unchanged timestamp assertion: **13 PASS, 1 FAIL**.
  Recording roots start red or zero, not the indirect green/address values;
  the partial-constant case preserves alpha from the copied template.
- Two additional texture-root-CBV executions preserve non-updated SRV/sampler
  tables and return the expected green pixel. Both fail the same zero timestamp
  assertion. They are not counted as fully passing tests.
- Twelve regression GPU executions pass: AIR/MSC GPU-produced compute roots in
  both variants (four), typed multi-command compute in both variants (two), and
  three AIR graphics cases per variant (six). Primary plus heap/regression work
  therefore totals **28 executions: 25 complete PASS, 3 timestamp FAIL**.
- Initial direct/indirect and single-writer query failures are retained. Three
  old-DLL direct controls and five alternating old/new direct pairs all pass;
  these do not establish the zero timestamp's cause or rule out a regression.
  The end-sampling experiment failed and its production change was removed.
  Validation was requested on one launch; no API/shader error is reported, but
  correct data plus zero timestamp remains. No query fix or validation-wide
  certification is claimed.

Standards self-review: checked mirrored 152-byte host/MSL packet and initialization,
shared reflected bounds, allocation failures, vertex-slot limits/null behavior,
immutable templates, root state reset, and unchanged private compute attachment.
Spec self-review: MSC guidance determined reflected TLAB/heap and ordinary vertex
binding ABI; GPU fixtures distinguish indirect updates from recorded values.
One unresolved qualification finding remains: zero timestamps prevent full GPU
probe acceptance for no-private UAV and both heap cases. It is not hidden by
changing assertions or presenting correct pixel data as a complete test pass.
Main-agent two-axis review only; independent subagent review is unavailable.

Remaining work includes the query issue, multi-command/repeat/concurrent graphics,
static sampler lifetime and descriptor update matrices, typed graphics, VB/IB
updates and emulation ABIs. No prefix/game DLL edits or process kills. No fresh
game/tessellation benchmark, performance claim, full matrix or FL12_0 promotion.

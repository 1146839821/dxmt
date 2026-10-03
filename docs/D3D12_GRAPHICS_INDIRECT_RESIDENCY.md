# Graphics indirect root address retention

## Task Analysis

- Hypothesis: ordinary AIR graphics indirect root descriptors have the same
  submission-time allocation ownership requirement as compute root descriptors.
- Evidence: compute marks `indirect_root_va` and snapshots registered allocations;
  ordinary graphics builds GPU-selected root qwords but does neither. The render
  resolver also writes root buffers without an explicit buffer visibility barrier.
- Expected effect: share submission snapshot ownership across compute/render,
  declare vertex/fragment access outside the registry lock, and order resolver
  writes before indirect shader reads.
- Risk: conservative all-buffer read/write retention cost; invalid addresses and
  native same-address remap remain unqualified. MSC graphics admission is unchanged.
- Validation: both reconfigured full builds and host suites; AIR DRAW and
  DRAW_INDEXED root-CBV numeric readbacks with allocation after Close and queue
  gated application release after translation; ordinary indirect and compute
  regressions. Main-agent two-axis self-review, no FL promotion or game claim.

This is a production graphics residency slice, not completion of MSC graphics
TLAB integration, typed graphics, or the FL12_0 acceptance matrices.

## Task Result

Ordinary graphics root-CBV/SRV/UAV updating signatures now mark their render
encoder for submission-time buffer snapshots. Render and compute share the
snapshot/retention helper; render declares vertex/fragment read/write usage
after the device registry lock is released. Submission references keep native
allocations alive until completion. Constant-only/non-updating encoders do not
scan the registry. A render buffer barrier orders resolver-written tables before
the indirect vertex/fragment reads. MSC graphics and GS/tessellation root-update
admission remain unchanged and rejected.

Evidence: `/Users/zhangbo/.cache/dxmt-graphics-indirect.NDglaJ`.

- Both reconfigured full builds and reviewed full builds completed successfully;
  host suites passed 5/5 in each variant.
- Four new AIR GPU executions passed DRAW and DRAW_INDEXED root-CBV cases across
  normal/no-private. The root allocation is created and argument address populated
  after Close. Execution is queue-fence gated; the application reference is
  released after translation and before the gate opens. Pixel readback is
  `0xff00ff00`. This verifies internal allocation retention in the fixture, not
  a relaxation of the application's D3D12 resource lifetime obligations.
- Six existing AIR graphics regressions passed (indirect, indexed indirect and
  direct root-CBV in both variants). Those existing fixtures include GS, while
  the new root-updating fixtures deliberately use ordinary VS/PS.
- Four AIR/MSC compute GPU-produced root-VA regressions passed across both builds,
  with numeric outputs 107,209: **14 final focused GPU executions total**.
- The initial new-fixture run mistakenly inherited the helper's default GS.
  Both root-updating cases correctly failed Close under the existing GS boundary;
  the log is retained. The fixture was corrected to ordinary VS/PS, not the
  production GS guard. These failed executions are not counted as passing tests.

Standards self-review: extracted duplicated snapshot ownership, preserved
allocation-failure propagation and lock-free native fan-out, checked render
stage masks and barrier direction, and made fixture cleanup release the queue
gate even on failure. Spec self-review: verified post-Close registration,
release-before-GPU-completion and independent pixel oracle without CPU argument
inspection in production. No remaining actionable finding in this bounded scope.
These are main-agent two-axis reviews, not independent subagent review evidence.

No prefix/game DLL updates, process kills, game performance/tessellation test,
Metal-validation run, full matrix or capability promotion. Graphics SRV/UAV,
fragment-root, multi-command/repeated/concurrent execution and native same-address
remap need further focused evidence. Conservative all-buffer retention remains
unprofiled. MSC graphics TLAB integration is the next root-binding implementation
step; the four FL12_0 production workstreams and complete workload remain open.

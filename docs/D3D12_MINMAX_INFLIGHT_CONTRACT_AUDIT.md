# MinMax in-flight contract audit (2026-10-08)

## Hypothesis / evidence / effect / risk / validation

Hypothesis: the deployed-dispatch red is an invalid observation oracle, not
evidence that the asynchronous queue must snapshot volatile descriptors on the
calling thread. Alternatives were a production binding lifetime defect and a
clamp-repair regression. Matched old/new/old-repeat/new-repeat native runs all
failed before this change; the clamp implementation cannot alone explain them.

Evidence: the original fixture held GPU execution behind a fence, submitted a
direct list, edited its referenced sampler descriptors, and submitted the SAME
direct list again before completion. Both actions violate the application
contract. DESCRIPTORS_VOLATILE (including RS1.0) does not permit editing submitted,
unfinished references. A direct list must finish its previous execution before
being resubmitted. ExecuteCommandLists returning does not guarantee that the
asynchronous translation worker has materialized bindings.

Sources:

- [Root Signature 1.1, DESCRIPTORS_VOLATILE](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1#descriptors_volatile)
- [Command queue/list design](https://learn.microsoft.com/en-us/windows/win32/direct3d12/design-philosophy-of-command-queues-and-command-lists)

Expected effect: preserve a legal overlapping submission test without adding a
production wait or changing static recording-time residency / volatile submission
resolution. Risk: accidentally losing overlap, dependency ownership or barrier
correctness while repairing the fixture.

## Repair

The overlap case uses independent direct lists, allocators and sampler heap
versions. Both heaps are initialized before submission and retained unchanged
through completion. The queue gate holds execution until both lists and the
intermediate GPU readback observer are enqueued; the observer preserves the first
result before the second dispatch overwrites it. Live sampler modes still check
MIN=16 followed by MAX=240. Static modes use unchanged descriptors. Static-sampler
root/PSO application references are released after both lists close and before
submission, retaining the closed-command ownership check.

The preceding three sequential replays still update volatile descriptors only
after completion and reexecute the same closed list. Static sampler descriptor
tables are no longer edited between executions. No renderer, submission worker,
capability or residency implementation changes were made.

## Fresh validation

- Both targeted fixture/shader builds passed.
- Normal and no-private builds: two sequential deployed runs each, all PASS.
  Each checks eleven roots x five executions; 220 dispatch executions overall.
- Actual target PE, Unix runtime and deployed compiler paths/hashes pass the
  unified gate's provenance checks in all four runs.
- Both host suites: 16/16 PASS, including the 82-test Python gate suite.
- Main-agent Standards/Spec self-review and git diff --check passed. No independent
  review or fresh Metal validation is claimed in this checkpoint.

Full output and provenance: external recoverable evidence file
`/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE/minmax-legal-inflight.json`.
Prior invalid red comparisons remain archived, not deleted or relabeled as a
production regression. This closes only the focused legal MinMax lifetime
oracle. Full MinMax, sparse status, format/raster and native Windows qualification
remain incomplete; no Tier or FL promotion follows.

# Root VA same-address remap task

Baseline: a7d7203c, clean worktree at inspection. This is the user's outstanding
P2 root-VA item, not a feature-level promotion or complete lifetime matrix.

Hypothesis: root residency must observe the correct VA generation at its defined
recording/submission boundary and retain that generation until GPU completion.
Evidence: EncodeRootResourceUses currently calls LookupBufferByVA while recording;
the registry lookup returns a raw allocation pointer after releasing its lock.
It then dereferences the pointer to retain a native resource. A concurrent owner
unregister can therefore race the ownership acquisition. Indirect root-updating
commands already use SnapshotRegisteredBuffers at submission, acquiring strong
allocation references under the registry lock and fanning out outside it.

Existing dx12_va_snapshot proves same-allocation owner replacement, stale-owner
unregister protection and retention after unregister. It does not substitute a
different allocation at the same VA or execute a root-descriptor GPU consumer.
Do not credit it as same-address root remap acceptance.

Next implementation: add a retained single-VA lookup boundary for CPU-known root
addresses, acquiring ownership under the registry lock. Then determine and honor
root descriptor data/static/volatile semantics for submission observations,
without rereading static descriptor ranges or expanding heap lock scope.
Expected effect: remove the ownership-acquisition race and provide a coherent
generation token for the subsequent remap path. Risk: static timing changes,
allocation lifetime inflation and conflating sparse physical mapping changes
with a different virtual allocation. The retained lookup alone is not remap closure.

Validation required: retained lookup/unregister control, real direct and indirect
CBV/SRV/UAV consumer evidence, legal same-address generation replacement, repeated
closed-list submissions and in-flight old-generation lifetime. First establish
whether an exact replacement can be constructed through supported public resource
APIs; an internal owner-only swap is not equivalent GPU evidence.

## Retained lookup implementation result

SnapshotBufferByVA now returns Rc<BufferAllocation> acquired under the VA registry
lock. Both lookup methods share the locked range/offset validation. CPU-known root
residency encoding uses the retained method and performs native retention/encoding
after the lock is released. This fixes the observed acquisition race without
altering root staging, static descriptor timing or submission remap policy.

Normal/no-private full builds and explicit dx12_va_snapshot targets pass. Both
cache-only Wine runtimes pass offset 17, null-output, missing-address, owner/stale
unregister and retained-allocation access after public resource release controls.
Logs: `dxmt-reconciliation.ZLDvwE/root-va-*`. No game/prefix changes or push.
Main-agent self-review: no nested registry lock, no encoding under the lock,
shared range validation, and strong ownership returned before lock destruction.
This test is a structural/lifetime control, not a GPU remap oracle or concurrent
stress proof. Other raw-lookup consumers and real same-address submission behavior
remain open; the overall root-VA task is not complete.

## Exact-address public alias construction

Two upload placed resources created at heap offset 0 produce the same GPU VA and
different BufferAllocation objects on both tested configurations. The extended
fixture verifies new-owner lookup after releasing the old public resource,
retained old-allocation access and protection against stale unregister. It now
requires both exact address equality and distinct allocation identity, rather
than treating a nonmatching-address run as a passing remap case.
Evidence: `root-va-alias-{normal,no-private}.log` in the same cache. Both explicit
targets compile and both Wine processes pass; this is registry/lifetime evidence,
not a submitted GPU root-consumer result. The public construction removes a
previous uncertainty and supplies the next direct/indirect remap fixture input.

## GPU root-consumer checkpoint

The indirect root fixture now has a `--remap` mode: six distinct public placed
CBV/SRV/UAV targets are replaced at identical heap offsets/VA after list closure,
then only the new CBV/SRV generations are initialized. GPU-produced root arguments
remain unchanged and expected output changes from 107/209 to 317/619. Both builds
pass the initial AIRCONV consumer runs, including release after gated submission.
Logs: `root-remap-air-{normal,np}.log`. Self-review added explicit alias activation
before dispatch and reactivation before recorded output-copy aliases, plus a named
placed-allocation argument instead of temporarily mutating the remap mode flag.
These latest refinements require rebuild/retest. Direct/MSC/replay and old-generation
overlap acceptance remain open; this is not complete root-VA task closure.

Latest alias refinements compile and pass on both builds. AIRCONV and MSC
consumer runs both return 317/619 in remap mode; unchanged no-remap controls
return 107/209 in all four backend/build combinations. Logs:
`root-remap-final-{air,msc}-{normal,no-private}.log` and `root-remap-control-*`.
Main self-review checks heap/target index pairing (two UAVs followed by alternating
CBV/SRV), exact VA identity before owner release, new-only input initialization,
activation/reactivation barriers, and independent numeric expectations.
Both targets keep the same shared heap storage; this verifies legal placed alias
generation replacement, not a reserved-resource physical tile remap. No production
defect was exposed in the existing indirect submission snapshot path. Direct-root,
repeat-list and overlapping old-generation cases remain separate unfinished work.

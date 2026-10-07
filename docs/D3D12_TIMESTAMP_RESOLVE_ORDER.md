# Timestamp resolve ordering investigation

## Completion-boundary discrimination (2026-10-08)

Baseline 1821386a, branch feat/d3d12-1. The diagnosing-bugs skill guided a fresh
red-capable native loop before hypotheses: original untracked probe produced
3/200 zero second GPU values, with valid CPU-native pairs and ordinary-copy
sentinels. This reproduces without Wine/MSC/D3D12, not proof of a driver defect.
Existing failed event/split/storage/repeat experiments were reviewed, not repeated
as if new. No production synchronization change is introduced in this checkpoint.

Ranked predictions: (1) an observed sampling-command-buffer completion boundary
may make a later GPU resolve reliable; (2) CPU resolve may change subsequent GPU
visibility; (3) neither boundary changes the native resolve symptom. Added modes:

- post-complete waits for the original buffer, then submits a second GPU resolve
  into independent storage; no CPU counter resolve occurs before that second
  resolve. CPU-native diagnostic happens only after the second GPU completes.
- post-cpu has the same layout/boundary but performs CPU-native diagnostic before
  the second GPU resolve, isolating that intervention.
- Both keep the original GPU result and its failure in exit status. Second results
  never replace it. Real GPU timestamps are compared exactly with the same native
  samples, not merely checked for monotonicity.

Strict native compilation passes. Three exact-oracle processes per mode:

| Mode | Iterations | First GPU resolve failures | Post-boundary failures/mismatches |
| --- | ---: | ---: | ---: |
| post-complete | 600 | 2 + 5 + 7 = 14 | 0 |
| post-cpu | 600 | 10 + 7 + 12 = 29 | 0 |

All six processes return failure because the original result remains invalid.
These bounded observations support a completion-boundary candidate and show CPU
counter resolution is not necessary for the observed second-result success.
They do not prove a production remedy, every ordering contract, or zero failure
probability. Allocations/extra commands alter scheduling; observed host completion
is stronger than the previously unsuccessful GPU-only event/split interventions.

Apple's [counter conversion documentation](https://developer.apple.com/documentation/metal/converting-a-gpus-counter-data-into-a-readable-format)
describes CPU resolution after GPU completion, GPU blit resolution, and invalid
zero/error values. The oracle now explicitly rejects MTLCounterErrorValue in
addition to zero/nonincreasing values. --oracle-self-test validates normal,
zero, reversed/equal and sentinel pairs without needing a GPU. This strengthens
the oracle; it does not change production timestamp frequency or substitute CPU
time/data for GPU query results.

Final strict compilation and oracle self-test pass. With Metal API validation
explicitly enabled, final post-complete run retains 2/200 original failures and
0 post failures/mismatches, without API misuse diagnostics. using-metal-validation
guided configuration and enabled-message checking; shader validation is not claimed.
Final default still returns failure with the unchanged zero-end symptom.
Evidence: cache dxmt-reconciliation.ZLDvwE/closure-timestamp-baseline.log,
closure-timestamp-exact-{post-complete,post-cpu}-{1,2,3}.log,
closure-timestamp-final-post-complete-api.log and closure-timestamp-final-default.log.
The standalone Objective-C diagnostic is not in the Meson FL acceptance graph;
no DXMT/Wine build/deployment, game restart or full GPU qualification occurred.

Main-agent standards/spec self-review informed by code-review found no outstanding
actionable issue in the diagnostic/oracle changes; independent sub-agent review
was not performed. The probe is retained in its explicitly diagnostic location,
not as a successful regression workaround. No temporary production instrumentation
was added. Next: test native completion-handler continuation, without invoking CPU
counter resolve or blocking the submitting caller; then evaluate a production
queue-safe continuation that preserves downstream GPU consumers, external waits,
error publication and completion-owned lifetimes. Do not add a synchronous GPU
wait to ExecuteCommandLists, fake timestamps or a retry-as-PASS rule. Timestamp
closure remains incomplete; root-feedback profiling must distinguish this oracle
gap from CPU submission costs. No capability promotion or push.

## Task Analysis

- Hypothesis: zero end timestamps may arise from early CPU fence/event completion,
  missing native samples, or GPU counter resolution before sampling becomes visible.
- Evidence: three fresh no-private textured indirect probes give FAIL/PASS/FAIL,
  with correct green pixels and end timestamp zero in failed runs. Moving native
  sampling to encoder end did not fix it and that experiment was removed.
- Expected effect: opt-in fence wait/result and CPU native-counter diagnostics
  distinguish sampling from GPU resolve, without weakening public readback checks.
- Risk: instrumentation changes scheduling; repeat and compare diagnostically
  enabled/disabled runs. Keep diagnostic native dependencies isolated to tests.
- Validation: build both variants, reproduce the same actual probe, inspect raw
  counters versus resolved output after a checked queue fence. No FL promotion.

Fresh diagnostic evidence: all five runs observe fence value 1 and increasing
nonzero native CPU-resolved counters; four GPU-resolved second timestamps are
zero. This rejects early event return and missing samples as the sole causes.
Test an explicit Metal GPU event signal/wait after each timestamp encoder to
order sampling before subsequent GPU counter resolution. This is a GPU dependency,
not CPU readback substitution. Event creation/value exhaustion must fail closed;
ordinary encoders without timestamps must not incur this extra dependency.

GPU event experiment: both full builds reached their final link steps. Five
fresh no-private runs returned FAIL/PASS/PASS/FAIL/FAIL. Every native counter
pair was valid and every completed fence was 1; failed GPU resolves still had
end timestamp zero and correct green pixels. The event experiment did not fix
the issue and was removed. Evidence: task-owned cache directory
`/Users/zhangbo/.cache/dxmt-query-order.5beUyW/event-{1..5}.log`.

## Next diagnostic experiment

- Hypothesis: multi-sample native GPU resolution observes incomplete results;
  alternatively adjacent destination writes overlap or CPU visibility is stale.
- Evidence: valid raw sample pairs coexist with zero GPU-resolved sample 1.
  Native range and destination offset forwarding match the recorded values.
- Expected effect: two single-sample resolves distinguish bulk resolution from
  the other explanations without changing sampling or completion ordering.
- Risk: additional commands alter scheduling; this is diagnostic, not a fix.
- Validation: opt-in split mode, repeat original probe, retain all assertions.

Single-sample experiment failed three of five runs (FAIL/PASS/FAIL/PASS/FAIL).
Next hypothesis: coalesced blit copies and native counter resolution writing
adjacent regions of the same buffer are not sufficiently ordered. Isolate each
timestamp resolve in its own fenced blit encoder, leaving native sampling and
the original bulk readback unchanged. Risk: encoder overhead, and a scheduling
change could hide rather than explain the issue. Validate against both original
and split probes before retaining any production change.

## Result and remaining gap

- Isolated timestamp encoder, original bulk resolve: 25/25 complete passes;
  first five enabled native diagnostics, remaining twenty disabled them.
- Isolated timestamp encoder, single-sample resolves: 9/10 passes, one zero
  end timestamp. The loop's initial `bulk` label was incorrect: environment
  variable presence enables split mode even when its value is `0`. These ten
  runs are all single-sample runs, not bulk evidence. Correct bulk runs explicitly
  unset that variable (`isolate-original-{1..25}.log`).
- Evidence narrows the issue to native resolution/destination ordering but
  does not prove the cause or a complete fix. Independent-encoder experiment
  was removed; no production synchronization change remains.
- Native diagnostics, split-mode hook and internal test dependencies were
  removed. The existing public readback assertions remain unchanged. Keep the
  checked event wait so failed waits cannot silently fall through into readback.
- Next discriminating probe: separate timestamp and occlusion destination cache
  lines/resources with the original encoder arrangement; then test resolving to
  an isolated temporary native destination before ordered copying, if evidence
  supports destination-write interference. Do not replace GPU results on CPU.

This checkpoint is investigation and test hardening, not timestamp qualification,
FL12_0 closure, a game test, tessellation acceptance or a performance benchmark.

Cleanup validation: both variants reconfigured and completed full builds with
exit 0. Three fresh original no-private runs on cleaned source all failed the
unchanged zero-end-timestamp assertion with green pixels, confirming the red
signal remains (`clean-{1..3}.log`). No event wait failure occurred. Main-agent
Standards/Spec self-review found no remaining experimental production edits or
weakened assertions; independent review was unavailable. `git diff --check`
passed. Wait checking is test hardening, not a resolution of the timestamp bug.

## Destination separation experiment

- Hypothesis: adjacent occlusion copy and timestamp resolve writes interfere.
- Evidence: isolated-encoder bulk queries passed 25 times; original coalesced
  queries failed three fresh runs, with native samples previously proven valid.
- Expected effect: relocating occlusion output from byte 16 to byte 256 should
  remove adjacency without changing sampling or blit encoder boundaries.
- Risk: larger allocation changes scheduling; success alone is not a fix.
- Validation: original textured indirect probe five times with all assertions;
  restore original layout afterwards. Separate resource is a subsequent probe.

Byte-256 separation returned PASS/FAIL/FAIL/FAIL/FAIL. Thus destination adjacency
alone does not explain the symptom. Next isolate the occlusion result into a
distinct resource, retaining the same encoder arrangement and timestamp range.

Distinct-resource probe returned FAIL five times, always zero end timestamp and
green pixels (`resource-{1..5}.log`). Metal API validation explicitly reported
enabled, then reproduced the same failure without an API misuse diagnostic
(`resource-validation.log`). This does not prove shader or synchronization
correctness. Both destination experiments were removed from source. Together
these results reject adjacent destination writes as a sufficient explanation;
next investigation must target counter sampling-to-resolution visibility rather
than padding or separating application readback buffers.

## Submission-boundary experiment

- Hypothesis: counter visibility requires a stronger boundary than commands in
  the sampling command buffer. Alternative: resolution algorithm or sampling
  placement itself is defective.
- Evidence: destination separation does not help, isolated bulk encoders do.
- Expected effect: move query resolution into a second submitted command list,
  with a distinct allocator, no CPU wait and unchanged GPU readback assertions.
- Risk: extra submission changes scheduling; not yet a production solution.
- Validation: repeat original fixture with boundary opt-in; default unchanged.

Boundary probe passed 25/25 fresh no-private runs with no CPU wait. Test the
corresponding production boundary: retain all constituent command buffers in one
submission, split before a counter-resolving blit when this buffer has sampled
timestamps, commit only after translation succeeds, and retain allocations until
all constituent GPU buffers complete. No timestamp-free split or CPU result
substitution. Risks include extra command-buffer cost, cross-buffer ordering and
partial error reporting; completion must inspect every constituent buffer.

Production split alone returned 9/10 passes; run 6 retained the same zero-end
failure. It is insufficient. Next test explicit GPU event ordering between the
constituent buffers; fail closed on event creation/value exhaustion. No CPU wait.

Explicit event plus production split also returned 9/10 passes, with run 8
failing the same timestamp assertion. Both production candidates and the
submission-boundary test hook were removed. The 25 passing diagnostic submits
are not complete qualification or proof that splitting alone fixes the bug.
Evidence: `production-boundary-{1..10}.log`, `production-event-{1..10}.log`.
Next distinguish sample attachment visibility from resolve behavior with a
minimal native counter-only reproduction, before adding more queue complexity.

## Native minimization

- Hypothesis: the same attachment/fence/GPU-resolve pattern fails without
  Wine/MSC; alternatives are untracked resource synchronization or bridge/load.
- Evidence: multiple GPU ordering experiments only reduce failure frequency.
- Expected effect: native Objective-C Metal loop separates runtime layers.
- Risk: minimal workload may not reproduce; a pass cannot exonerate the bridge.
- Validation: fresh sample buffer, two sampled blit passes, fenced dummy fill,
  same-buffer GPU resolve, compare CPU-native pair after completion, 200 runs.
  Harness lives in task cache, not a capability/acceptance test.

Native original harness reproduced zero second GPU timestamps with valid CPU
samples: untracked 2/200 failures, tracked 2/200; API validation enabled then
3/200 failures without misuse diagnostics. A repository standalone probe adds
native-pair and dummy-copy oracles; five subsequent untracked processes yielded
FAIL/FAIL/PASS/FAIL/PASS (200 iterations each). This rules out Wine/MSC/indirect
as necessary conditions, not a definitive driver-fault attribution.

Next hypothesis: shared sample-buffer storage affects GPU resolve visibility.
Compare private storage, using unchanged GPU monotonicity/copy assertions;
CPU native resolution is unavailable in that mode and must not be substituted.

Private sample storage reproduced four failures in 200 iterations. Do not change
production storage mode on this evidence. Native counter repro is retained as
`tests/dx12/metal_timestamp_resolve_probe.m`, intentionally standalone and not
registered as FL acceptance. Build/run from repository root:

```sh
xcrun clang -fobjc-arc -Wall -Wextra -Werror -framework Foundation -framework Metal \
  tests/dx12/metal_timestamp_resolve_probe.m -o /tmp/dxmt-metal-counter-probe
/tmp/dxmt-metal-counter-probe
/tmp/dxmt-metal-counter-probe tracked
/tmp/dxmt-metal-counter-probe private
MTL_DEBUG_LAYER=1 /tmp/dxmt-metal-counter-probe
```

Exit 0 means this 200-iteration process passed, not a bug fix; exit 1 means the
GPU timestamp oracle failed, exit 2 means setup/native/copy oracle failure.
Fresh processes have both passing and failing results; repeated launches are
needed. Evidence directory: `/Users/zhangbo/.cache/dxmt-native-counter.TNlJCN`.
No Wine/game runtime mutation, new production workaround or capability promotion.

Final standalone build passed with `-Wall -Wextra -Werror`. Three fresh final
untracked runs reproduced 5/200, 5/200 and 1/200 failures with valid native pairs
and dummy-copy oracles (`final-{1..3}.log`). Main-agent Standards/Spec review
found no production changes or weakened assertions; independent review was
unavailable. Repository diff whitespace check passed. Native diagnostic does
not join the cross-build graph and is not a passing D3D12 regression claim.

## Sampling-point discrimination

- Hypothesis: command-boundary sampling with a barrier avoids pass-attachment
  visibility failure; alternatives are pass-attachment placement or resolve.
- Evidence: standalone counter-only probes fail in all tested storage modes.
- Expected effect: query actual device stage/blit/dispatch/draw sampling support
  before attempting any unsupported sampling API.
- Risk: capability presence is not semantic correctness.
- Validation: compile native probe and inspect live capabilities; then test only
  supported sample points, preserving timestamp and ordinary-copy oracles.

Live Apple M4 capabilities: stage=1, blit=0, dispatch=0, draw=0. Direct
command-boundary sampling is unavailable. Next compare sampled compute passes
against sampled blit passes, keeping the intervening blit workload, fence chain,
GPU resolve and readback oracles. Compile a one-thread native zero-store kernel;
this is a diagnostic pass-type comparison, not a shader backend substitution.

Compute sampling also reproduced 7/200 failures, with valid CPU raw timestamps
and the ordinary-copy oracle intact (`compute.log`). Thus the problem is not
specific to blit sampling attachments. Native diagnostic now prints the live
sampling capability set and admits a `compute` comparison mode. No production
encoder substitution is justified by these results.

Final native compilation passed strict warnings; a fresh compute run retained
the red-capable oracle (`compute-final.log`). Main-agent Standards/Spec review
only, independent reviewer unavailable. No API declaration or GPU acceptance
status changed; diff whitespace check passed. This is diagnostic evidence, not
closure of the timestamp or feature-level requirement.

## Repeated GPU resolve discrimination (baseline aecf5f91)

Hypothesis: a later fenced GPU resolve of identical samples may recover a zero
end value. Evidence: fresh current native counter-only run failed 17/200 with
valid CPU-native samples. Expected effect: compare first and second resolves
without CPU substitution. Risk: additional encoder and destination storage
change scheduling; no causal inference from failure-rate differences alone.
Validation: preserve first resolve, copy/native oracles, add a second fenced
blit resolve into nonoverlapping bytes of the same destination, and fail if
either GPU pair is invalid. The opt-in `repeat` mode leaves default allocation
and encoder arrangement unchanged. This is not a production retry mechanism.

Strict native compilation passed. First repeat process had 16/200 first-resolve
failures and 16/200 second-resolve failures, at the same iterations. Final source
repeat run had 17/200 first failures and 16/200 second failures: repeat resolution
does not reliably restore valid results. CPU-native pair and dummy-copy oracles
remained valid. Final default mode also returned the existing failure exit 1;
this retained red signal is not a regression pass. Logs `timestamp-native-*`
are in `/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE/`.

Run the existing strict build command, then invoke the probe with `repeat`.
No DXMT/Wine build is needed for this standalone Objective-C diagnostic.
Self-review: first result is never overwritten; second range starts at byte24
and occupies16 bytes in a40-byte repeat-only allocation; ordinary-copy bytes
remain16..23. Both GPU oracles influence exit status. No production fallback,
weakened assertion, prefix deployment or feature-level claim was introduced.
Next investigate the representation consumed by GPU resolve versus native CPU
resolve, rather than retaining unproven extra encoder/event workarounds.

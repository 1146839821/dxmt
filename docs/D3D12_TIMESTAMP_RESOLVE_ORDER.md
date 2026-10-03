# Timestamp resolve ordering investigation

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

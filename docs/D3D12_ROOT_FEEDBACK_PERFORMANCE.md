# Root feedback performance

## Preparation (2026-10-08)

Baseline 74bbc767. Hypothesis: feedback adds registry snapshot/table construction
and native residency fan-out on the commit worker, plus a GPU VA-table lookup;
cost should grow with registered buffer count/target rank. Evidence: current
RetainIndirectRootBuffers and SnapshotRegisteredBuffers source, not FPS inference.
Expected effect: a matched workload/control exposes scaling before optimization.
Risk: shader compilation, caller-only timings, invalid timestamps, CPU time
granularity and unrelated GPU load can obscure attribution. Validation: assert
actual PSO feedback selection, observe registry count/rank, validate every GPU
output and timestamp pair, exclude creation/compilation/warmup, alternate cases
using identical runtime/executable hashes. No residency/lookup algorithm changes.

GetProcessTimes includes CPU execution across process threads, including the
worker; it is not isolated worker attribution. Its kernel/user counters use
100-nanosecond units ([Microsoft documentation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)).
Report whole-process CPU, caller enqueue wall time, completion wall time and GPU
query time separately. API/shader validation must be off for timing; correctness
validation is a separate run. This benchmark is not a game FPS or full FL matrix.
The timed CPU/wall window drains/destroys the queue at the end, including final
worker retirement and an amortized teardown cost. GPU samples must advance
between iterations, not just form individually increasing but stale pairs.

## Frozen-build baseline

Apple M4, macOS 27.0.1 (26A434), normal cache Wine runtime. One compute dispatch
runs 4,096 lanes reading a 1,024-word ordinary upload buffer through a root SRV.
The control performs the same read/write; feedback consumes Load status through
CheckAccessFullyMapped. Both compile cs_5_0 with SKIP_OPTIMIZATION. The fixture
asserts actual AIR PSO feedback selection, records the actual ordered-registry
count/input rank, validates all output words on every iteration, and rejects
invalid or stale timestamp pairs. Padding buffers remain alive throughout.

After eight warmups, each process measures 512 completed submissions. Three
paired rounds reverse control/feedback order in the middle round. The EXE,
three PE DLLs and Unix winemetal.so hashes stay identical throughout. API/shader
validation, HUD, shader cache and experimental capability flags are explicitly
zero. This is an A/B workload comparison within one build, not an optimization
before/after comparison and not the separate std::sort experiment.

The table shows medians of three process-level summaries, in microseconds per
submission (GPU interval brackets dispatch work, not the full submission):

| Registered buffers / input rank | Mode | Process CPU | Caller enqueue wall | Completion wall | GPU median |
| --- | --- | ---: | ---: | ---: | ---: |
| 4 / 0 | control | 253.906 | 9.596 | 521.592 | 8.709 |
| 4 / 0 | feedback | 273.438 | 9.735 | 526.492 | 8.875 |
| 260 / 256 | control | 253.906 | 9.743 | 517.021 | 8.708 |
| 260 / 256 | feedback | 468.750 | 10.400 | 771.520 | 64.083 |
| 1,028 / 1,024 | control | 273.438 | 9.938 | 518.887 | 8.750 |
| 1,028 / 1,024 | feedback | 917.969 | 11.055 | 1,443.649 | 289.833 |

Observed process CPU totals advance in 10-ms steps, about 19.531 us/submission
at 512 iterations. The small-registry CPU difference is one such step and cannot
establish a precise overhead. At 1,028 entries, feedback GPU medians range
289.291--486.583 us and process CPU ranges 917.969--1,113.281 us. Preserve this
variation: the desktop/GPU was not quiesced and thermal/background load was not
controlled. Do not derive a game FPS estimate or CPU sub-phase breakdown.

Two additional exploratory pairs separate registry size, rank and batching:

- With 1,028 entries but input rank 0, control/feedback GPU medians are
  8.750/68.583 us and process CPU is 273.438/898.438 us. Early lookup does not
  eliminate all GPU overhead; do not attribute the entire difference to scanning.
- With 260 entries, rank 256 and 32 dispatches in one encoder, control/feedback
  GPU intervals are 28.541/1,612.375 us; process CPU is 273.438/488.281 us.
  CPU setup is amortized across dispatches while substantial GPU work remains.

All 22 timing cases pass (11,264 measured submissions plus warmups). Separate
worker CPU accounting self-tests pass in both variants: an idle waiting caller
observes 1,000,000 process-CPU 100-ns units for a 100-ms busy worker. This verifies
inclusion, not exact attribution of the real commit worker. The no-private
feedback correctness run with API validation enabled passes and is excluded from
performance comparison. A normal provenance run passes target PE/Unix hashes
with the same EXE; existing sparse root raw/structured boundary/alternating-remap
GPU regression also passes. Both fixture builds and 16 host tests per build pass.

Evidence under /Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE:
root-feedback-perf-matrix.json, root-feedback-perf-rank-batch.json,
root-feedback-perf-*.log and root-feedback-perf-provenance.json. The baseline
artifacts are preserved separately for subsequent optimization comparisons.
No production code, residency counters, game/prefix DLLs or game processes changed.

Main-agent standards/spec review checked timing-window/drain semantics, actual
consumer selection, timestamp freshness, matched hashes and uncertainty labels.
No independent review, no-private performance comparison, sparse performance
matrix or graphics-stage performance qualification is claimed. Diagnosis guided
measurement before optimization; Metal validation guided the separate correctness
run. No LLVM/AIR transformation was changed during this task.

The next bottleneck to investigate is full-registry snapshot/table/fan-out for
fixed root VAs, while retaining the conservative GPU-selected/indirect path and
same-address remap semantics. GPU lookup is linear in current source, but alias
and interval precedence must be preserved before replacing it. Full mandatory
Typed/MinMax/Tiled/format/raster qualification and the independent production
std::sort A/B/counter audit remain separate requirements.

Reproduce a case after explicitly building dx12_root_feedback_perf.exe:

```sh
wine dx12_root_feedback_perf.exe 1024 1 512 control last
wine dx12_root_feedback_perf.exe 1024 1 512 feedback last
wine dx12_root_feedback_perf.exe --cpu-accounting-self-test
```

Use the controlled environment and matched cache runtime above, alternate
order/repeat, and retain raw logs/hashes; the illustrative commands alone do not
establish provenance or environmental control.

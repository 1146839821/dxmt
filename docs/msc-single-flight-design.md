# MSC process-local cache: concurrent misses

## Baseline and evidence

The MSC conversion cache used a shared lock for lookup and an exclusive lock
for insertion, but released the lock for both conversion passes. Concurrent
misses for the same key could therefore compile the same shader repeatedly.
In an isolated Wine x86_64 test with eight simultaneous identical compute PSOs
and a test-only 150 ms compiler delay, the pre-change path made 16 real MSC
compile calls. This is an overlap control, not an uninstrumented benchmark.

Apple describes MSC's separate compiler objects and conversion interface in
its [shader converter documentation](https://developer.apple.com/metal/shader-converter/).
The change does not share an `IRCompiler` object between threads.

## Design card

- **Goal:** Deduplicate simultaneous cold conversions for the same existing
  process-local cache key, retaining normal cache and error behavior.
- **Scope:** Hold a per-key gate through lookup recheck, conversion and cache
  insertion. Different keys have independent gates; warm hits skip the gate.
- **Non-scope:** Persistent-cache identity, eviction, sharing failures, GPU
  resource lifetime, capability promotion, or a global compile lock.
- **Boundary:** A weak-reference gate table owns no compiler or shader output.
  RAII releases the gate on success, error or C++ exception; a failed attempt
  does not create a cached failure and the next waiter may retry. The last
  holder removes its table entry.
- **Tests:** Check same-key exclusion, different-key progress and failure
  release; measure actual Wine bridge calls for eight simultaneous PSOs; force
  one compiler failure; run an existing GPU readback and negative control.
- **Risks:** Hash collisions in the pre-existing cache key remain an existing
  correctness assumption. A recursive same-thread request for the identical key
  would deadlock; this path must not call back into DXMT conversion. Threads
  waiting for a failed conversion retry serially rather than sharing failure.
- **Rollback:** Revert the gate and second lookup together; retain the existing
  cache, its key and the fail-closed persistent guard.
- **Done:** Focused tests, isolated runtime evidence and both full builds pass.

## Validation

The native tests passed on arm64 and x86_64 for same-key serialization,
different-key parallelism and failure release. On the same instrumented Wine
test before the single-call fast path, eight successful PSOs required two MSC
calls (size and fill passes), rather than 16. A forced first compile failure produced one failed
PSO, then seven successful PSOs and two successful MSC calls. Stage/build
hashes and loaded WineMetal export bytes and Unix path were checked. The
artifacts are under `dxmt-test/n94-single-flight/{before,after-final,fail-first}`.
This does not measure production frame times or establish persistent identity.
An isolated Wine fragment GPU readback and deliberately wrong expected-output
control also passed with Metal API and shader validation; the logs are under
`dxmt-test/n94-single-flight/gpu`.

The real Wine test also launches eight PSO creations split between two
different DXIL compute shaders. With the same test-only delay, it observes
four compiler calls and a maximum of two overlapping calls. As a negative
control, a test-only global mutex reduces the maximum to one while preserving
four calls. The normal same-key test still observes two calls. The stage/build
hashes, PE export bytes and loaded Unix path are checked; logs are under
`dxmt-test/n94-single-flight/{two-shaders-verified,two-shaders-serial,same-key-recheck}`.
These observations validate this particular workload, not an application-wide
performance claim.

After the single-call fast path, the same instrumented workload used one MSC
call for eight matching PSOs and two overlapping calls for two distinct
shaders. The forced-first-failure control continued to recover. Evidence is
under `dxmt-test/n94-single-flight/{one-pass,one-pass-failure,one-pass-parallel}`.

## PSO timing and repeated-creation control

The isolated concurrent probe records each of eight complete
`CreateComputePipelineState` calls and then eight sequential creations using
the first shader in the same process. The runner saves all samples and
median/P95/min/max in `summary.json` and checks that no traced MSC conversion
occurs after the concurrent phase. A same-shader run produced one real MSC
call for eight concurrent creations and no additional call for the eight
repeated creations. A forced first failure produced one failed initial PSO,
then successful recovery and repeated creations. Two distinct shaders
produced two real MSC calls, with two overlapping calls. A forced-serialization
control reduced the maximum overlap to one. Stage/build hashes, export bytes
and actual bridge/runtime paths were checked. Results are in
`dxmt-test/n95-capability-gate/result-concurrent-timing{,-failure,-two,-serial}-20261103`.

The wrapper intentionally delays compilation to make overlap observable. In
the same-shader run concurrent PSO median/P95 were 188.064/192.158 ms and
repeated PSO median/P95 were 0.3985/0.570 ms; P95 of eight samples is the
maximum. These are complete PSO creation times under an artificial delay, not
isolated cache lookup or single-flight wait times, GPU timings, production
latencies or an application speedup. One process per condition cannot establish
a performance distribution. Memory growth and real-world hit rates remain
unmeasured. Run `tests/dx12/run_msc_concurrent.py --help` for the required
isolated stage, prefix, build and converter arguments.

The runner's `--no-delay` mode omits the injected sleep for the identical
shader case while retaining the conversion trace wrapper and its logging.
Five separate Wine processes each created eight concurrent and eight repeated
PSOs. Each process recorded exactly one real MSC conversion and no further
conversion in the repeated phase. On the M1 Max/macOS 27.2 test host, the five
per-process concurrent medians were 9.804, 9.3755, 11.484, 12.2875 and
10.529 ms; the corresponding repeat medians were 0.348, 0.3595, 0.351, 0.367
and 0.339 ms. Across the 40 samples per phase, pooled median/P95/min/max were
11.2185/15.729/9.078/17.252 ms for concurrent PSOs and
0.3575/0.494/0.328/0.514 ms for repeated PSOs. Raw samples and identities are
in `result-concurrent-no-delay-20261103-{1..5}` under the n95 test directory.

These are complete PSO calls, with tracing and shared OS/driver caches still
active; concurrent samples within each process are dependent. A process-first
call is not necessarily a driver-cold compile. The result does not isolate
cache lookup, actual single-flight waiting, memory use, GPU output or game
performance. This is a baseline for one fixture and one device, not evidence
to change the cache policy.

## Process-memory observation design card

- **Goal:** Observe process memory before PSO creation, after concurrent creation and after sequential repeats.
- **Baseline:** `a76d3d15`, `feat/d3d12`; existing PSO timing and MSC call tracing, without cache allocation accounting.
- **Evidence:** Microsoft's `PROCESS_MEMORY_COUNTERS` describes process counters; Wine availability and semantics require verification.
- **Scope / non-scope:** Add three raw snapshots and JSON output; exclude cache allocation attribution, GPU memory, peak usage, leak detection and cache-policy changes.
- **Architecture:** Standalone test query outside PSO timers; no production converter, cache, WineMetal ABI or capability change.
- **Proposed changes:** Dynamically query PSAPI; failed query or zero working set becomes `NOT_AVAILABLE` / JSON null; reject missing, duplicate or malformed records.
- **Test plan:** Focused build, paired isolated runtime, separate no-delay processes, forced-failure recovery, raw-log/JSON comparison and parser controls.
- **Risks:** Allocator, thread, Wine, driver and compiler activity all affect process totals; PSOs are released at phase boundaries but device caches remain live.
- **Rollback:** Revert only the probe query and runner memory reporting.
- **Definition of done:** Real observations or explicit unavailability, parser verification, focused build/review and recorded limitations.

## Process-memory validation — 2026-11-04

`LOCAL_PASS`: On M1 Max, macOS 27.2 (26B123), Metal Toolchain 27.2 and
Wine 11.0, five independent processes each created eight concurrent PSOs and
eight sequential repeats with `--no-delay`. Every process recorded exactly
one successful real MSC conversion, no conversion in the repeated phase and
three nonzero working-set observations. The forced-first-failure control
reported one initial PSO failure, recovery, and one successful MSC conversion.
The dynamically loaded converter SHA-256 was
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.
The runner checked stage/build bytes, loaded PE export bytes, native D3D12/DXGI,
builtin WineMetal and the actual Unix bridge/converter paths.

| Whole-process working set | Median bytes | P95 bytes | Min–max bytes |
| --- | ---: | ---: | ---: |
| Before PSO creation | 50,868,224 | 51,040,256 | 50,712,576–51,040,256 |
| After concurrent phase | 58,466,304 | 58,585,088 | 58,261,504–58,585,088 |
| After repeats | 58,482,688 | 58,605,568 | 58,281,984–58,605,568 |
| Paired concurrent − before | 7,548,928 | 7,655,424 | 7,544,832–7,655,424 |
| Paired repeat − concurrent | 20,480 | 32,768 | −40,960–32,768 |

These are five snapshots per phase, with nearest-rank P95 equal to the maximum.
Differences are paired within each process, not differences of the pooled
medians. Across the 40 complete calls per phase, concurrent PSO median/P95 were
13.302/44.846 ms (range 8.907–44.872 ms), and repeat median/P95 were
0.3535/0.496 ms (range 0.332–0.531 ms). The first process accounts for the
large concurrent tail. Tracing and shared OS/driver caches remain active;
these are neither isolated cache lookup/wait times nor driver-cold timings.

The JSON retains `pagefile_usage_bytes` exactly as reported. Microsoft's
[PROCESS_MEMORY_COUNTERS reference](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters)
defines `PagefileUsage` as process commit charge, not bytes currently swapped.
The upstream Wine 11.0 macOS implementation instead assigns Mach
`virtual_size` to `PagefileUsage` and `resident_size` to `WorkingSetSize`
([ntdll/unix/process.c, lines 998–1004](https://github.com/wine-mirror/wine/blob/wine-11.0/dlls/ntdll/unix/process.c#L998-L1004)).
The observed 44.3–44.6 GB values are therefore retained as a Wine-dependent
counter, not interpreted as physical, private, swapped or MSC-owned memory.
`wine --version` identifies the tested executable as 11.0; this source
comparison does not establish the executable's exact source revision.

`LOCAL_PASS`: The final parser reproduced all six raw-run observations,
preserved values above 32 bits, accepted CRLF and explicit unavailability,
and rejected seven controls: missing, duplicate, zero, negative, truncated,
trailing-garbage and malformed-duplicate observations. The final source was
rebuilt and rerun separately. The PSAPI-unavailable path was tested at the
parser level; the host runtime itself returned available counters.

Artifacts are under `dxmt-test/n95-capability-gate/` outside the repository:
`result-concurrent-memory-20261104-{1..5}`, `result-concurrent-memory-failure-20261104`
and `result-concurrent-memory-final-20261104`. Each contains raw logs and
`summary.json`; the first directory also contains `series-analysis.json` with
paired deltas, timing summaries and parser-control results. To repeat with a
new output directory:

```sh
test_root=/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate
python3 tests/dx12/run_msc_concurrent.py \
  --stage "$test_root/stage-msc-memory-20261104" \
  --prefix "$test_root/prefix-msc-space-fresh-20261104" \
  --build "$test_root/dxmt-current-patches-20261103" \
  --converter /usr/local/lib/libmetalirconverter.dylib \
  --output "$test_root/result-concurrent-memory-next" \
  --expected-calls 1 --no-delay
```

**Gate outcome:** Baseline/design, implementation and focused validation pass
for this observation slice. No Feature Level, Shader Model, Tier, production
shader behavior or cache policy changes. GPU dispatch/readback and Windows-native
or Mullion comparisons are `NOT_RUN` in this slice. Whole-process growth is
not evidence of an MSC leak or a cache-size measurement; the tiny one-shader
workload does not test unbounded cache growth. The next measurement should
attribute retained bytes and entries to the existing process-local conversion
cache under multiple distinct shaders, without changing its ownership/policy.

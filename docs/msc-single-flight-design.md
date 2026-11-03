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

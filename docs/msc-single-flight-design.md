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
test, eight successful PSOs required two MSC calls (the existing size and fill
passes), rather than 16. A forced first compile failure produced one failed
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

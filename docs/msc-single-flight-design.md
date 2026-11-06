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

## Cache-retained storage design card

- **Goal:** Measure entries and retained conversion data under distinct-shader growth and warm reuse.
- **Baseline:** `88dbe00b`, `feat/d3d12`; one process-local unordered map owns converted shader copies, with no capacity policy.
- **Evidence:** `MSCConversionCache` and `ConvertD3D12ShaderInternal` in `src/d3d12/d3d12_shader_converter.cpp`; whole-process counters cannot attribute these allocations.
- **Scope:** Default-off `DXMT_MSC_CACHE_TRACE=1` snapshots, existing concurrent probe/runner extended with a bounded distinct-shader corpus.
- **Non-scope:** Eviction, persistent caching, heap allocator overhead, GPU memory, cache-policy changes or new capability claims.
- **Boundary / selection:** Derive statistics from the existing owner under its lock, rather than maintain duplicate counters or expose a new DLL/COM API.
- **Changes:** Record entry count, vector sizes/capacities, entry-point size/capacity, inline mapped-value bytes and bucket count on misses, hits and insertions.
- **Cost / risk:** Disabled mode only checks a process-static flag per event; enabled mode scans O(entries) and logs under the existing lock, so traced timings are diagnostic, not performance evidence.
- **Test plan:** Trace disabled control, same-key concurrency, distinct shader growth/replay, failed-conversion recovery, parser negatives and adjacent GPU output regression with validation.
- **Rollback:** Remove tracing and the corpus mode together; retain the cache owner/key/lifetime unchanged.
- **Done:** Correctly attributed data/storage counts, real compile-count controls, builds, explicit measurement limits and a focused commit.

## Cache-retained storage measurement — 2026-11-04

The default-off `DXMT_MSC_CACHE_TRACE=1` diagnostic emits `MSC_CACHE` events
at info log level. Each snapshot is computed directly from the process-local
cache under its existing lock. No parallel accounting state, public export,
cache key, insertion policy or lifetime changed. Disabled mode performs a
process-static flag check; enabled mode scans O(entries) and logs while holding
the lock. It deliberately perturbs timings and must not be used as a speedup
benchmark. The runner's JSON records whether tracing was enabled.

The storage fields have distinct meanings:

- `metallib_bytes` / `stage_in_bytes`: sum of vector lengths in cached copies.
- `metallib_capacity_bytes` / `stage_in_capacity_bytes`: sum of vector capacities,
  excluding allocator bookkeeping or page granularity.
- `entry_point_chars` / `entry_point_capacity_chars`: string lengths/capacities,
  excluding the terminating null. Capacity may reside inside the mapped object
  via small-string optimization; it is not an additional heap allocation.
- `mapped_value_bytes`: entry count multiplied by `sizeof(D3D12ConvertedShader)`;
  includes the vector/string objects, fixed reflection and padding, but not
  the vectors' external storage, map keys, nodes or bucket allocation.
- `buckets`: number of map buckets, not their allocated byte size.

These observations do not include Metal library/PSO allocations, copies returned
to callers, in-flight conversion buffers, single-flight state, driver storage,
GPU memory or allocator overhead. The runner retains whole-process PSAPI
snapshots separately; subtracting them does not recover an exact cache footprint.

The `--corpus-count` mode uses the existing empty compute HLSL fixture, compiled
with repository DXC (`cs_6_0`, `-O3`) and threadgroup X sizes 1 through N. It
requires distinct DXIL hashes, creates every PSO once and then replays all of
them. Counts are limited to 2–64. All generated inputs, DXC/compiler identities,
raw logs and cache snapshots are saved outside the repository. The probe flushes
phase markers before starting later PSOs, so the no-recompile-after-first-pass
check can observe real ordering rather than buffered output.

`LOCAL_PASS`: Tracing disabled emitted no cache records. With tracing enabled,
eight same-key concurrent PSOs plus eight repeats retained one entry and made
one MSC call. The forced-first-failure case left the cache empty at failure,
then recovered to one entry/one successful conversion. Two distinct concurrent
shaders retained two entries and made two overlapping conversions. The first
8-shader corpus recorded eight misses, eight insertions and eight hits, with
no mutation or conversion in the replay. Its final metallib length/capacity
was 32,176 bytes and mapped-value storage was 72,768 bytes.

`LOCAL_PASS`: Nine parser controls rejected a missing insertion, duplicate
insertion, changed state on a hit, negative count, capacity smaller than size,
unknown event, empty record, duplicate field and malformed record. Raw logs
were independently parsed and compared with JSON. Native single-flight and
output-buffer tests both passed. The full `build` and `build-X86` builds
passed; the final probe edit was rebuilt in `build` and the separate candidate.

`LOCAL_PASS`: The existing threadgroup-memory GPU regression ran with cache
tracing and Metal API/GPU validation. Default and coherency-enabled compiler
controls passed with both root-descriptor and descriptor-table bindings;
both deliberately wrong expected-output controls failed as intended. All six
logs contained an actual cache insertion and verified runtime paths. The first
attempt was `NOT_RUN` because the opt-in shader fixture had not been built;
after building `dx12_compute_sm6_threadgroup_coherency_fixture`, the complete
regression passed. This covers those fixtures, not a general new feature claim.

Artifacts under `dxmt-test/n95-capability-gate/`:
`result-cache-storage-{off,same,failure,two,corpus8,corpus32}-20261104`
and `result-cache-storage-gpu-ready-20261104`. Use a new output directory:

```sh
test_root=/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate
python3 tests/dx12/run_msc_concurrent.py \
  --stage "$test_root/stage-msc-cache-storage-20261104" \
  --prefix "$test_root/prefix-msc-space-fresh-20261104" \
  --build "$test_root/dxmt-current-patches-20261103" \
  --converter /usr/local/lib/libmetalirconverter.dylib \
  --output "$test_root/result-cache-storage-next" \
  --expected-calls 32 --corpus-count 32
```

Use `--cache-trace --expected-calls 1` instead of corpus mode for same-key
concurrency; add `--fail-first` for recovery. Use `--two-shaders --cache-trace
--expected-calls 2` for independent-key overlap. With `--no-delay` and no trace
flag, the existing timing mode remains available.

The 32-shader corpus also passed: exactly 32 real MSC conversions, 32 insertions
and 32 replay hits. Every input hash differed; replay produced no conversion
and no change to any storage field. The final snapshots were:

| Distinct compute shaders | Metallib bytes / capacity | Mapped-value bytes | String chars / capacity | Buckets |
| ---: | ---: | ---: | ---: | ---: |
| 8 | 32,176 / 32,176 | 72,768 | 56 / 120 | 13 |
| 32 | 128,992 / 128,992 | 291,072 | 224 / 480 | 59 |

After the final probe cleanup, a separate two-input run passed with the latest
built executable hash, two insertions and stable replay. Its logs and JSON are
in `result-cache-storage-final-corpus2-20261104`.

Both cases retained zero stage-in bytes/capacity. On this GCC x86_64 build,
each mapped value occupies 9,096 bytes, including fixed reflection storage.
The cached metallib vector capacities equal their actual data sizes; these
copies do not retain the converter's 64 KiB initial work-buffer capacity for
each small shader. This is an observed property of the tested copy path/STL,
not a portable capacity guarantee. `storage-analysis.json` in the 32-input
result directory records the independently checked corpus hashes, raw cache
snapshots and phase ordering.

The 8-input process working set was 50,597,888 → 56,623,104 → 56,627,200 bytes;
the 32-input process was 50,384,896 → 57,188,352 → 57,204,736 bytes. These
processes ran with heavy tracing and differing host/cache activity; comparing
their deltas does not measure the incremental per-shader heap cost. No median,
P95 or optimization percentage is claimed for these diagnostic runs. The
corpus uses empty compute shaders and cannot establish representative game
memory, eviction behavior, nonempty stage-in storage or GPU output correctness
of arbitrary cached shaders. The separate GPU regression addresses only its
own resource-using fixtures.

**Gate outcome:** Baseline/design, implementation, focused validation and
scope review passed. There is no public capability or production cache-policy
change. Source inspection shows the existing static map has no eviction path;
the measured growth to 32 entries is consistent with that ownership. It is not
proof of a leak or a capacity recommendation. Next: measure resource-using
shader variants with GPU readback before and after cache replay, using this
same diagnostic, before designing a capacity or representation change.

## Resource-using cache replay design card

- **Goal:** Prove GPU output correctness after conversion-cache hits across distinct DXIL and root-layout keys.
- **Baseline:** `7868ed0f`; storage tracing passed for empty shaders; existing threadgroup tests dispatch resource-using shaders but do not recreate PSOs on cache hits.
- **Evidence:** `RunNumericDifferential` already verifies two inputs, root/table bindings, exact output/guards, closed-list replay, fenced reset and pending-resource release.
- **Scope:** Reuse that helper for multiple compile-time output variants, both root layouts, first conversion and a second PSO-creation pass in one process.
- **Non-scope:** Cache policy, key changes, production shader semantics, graphics/stage-in coverage and performance claims.
- **Boundary:** Existing C++ probe, HLSL, independent Python oracle generator and concurrent-cache runner; no new production code or runner.
- **Plan:** Generate distinct DXIL/reference pairs; trace eight inserts for four variants and two layouts, then eight stable hits; verify GPU bytes throughout.
- **Negative controls:** On the last warm case, deliberately corrupt the reference or select variant zero with the last variant's reference; require the targeted GPU mismatch.
- **Cost / risk:** Validation and cache tracing intentionally perturb timings; fixtures are bounded microtests, not representative workloads.
- **Validation:** Isolated paired WineMetal, real conversion counts, phase-specific traces, byte/guard checks, runtime identity, old threadgroup regression and focused build.
- **Rollback / done:** Revert test-only mode/variant support; done when positive and both negative controls run and evidence is recorded without capability promotion.

## Resource-using cache replay results — 2026-11-04

`LOCAL_PASS` on the M1 Max/macOS 27.2/Wine 11.0 isolated stage, with the real
MSC dylib SHA-256
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.
Four DXIL shaders use the same resource layout and threadgroup barriers, but
apply compile-time XOR values 0–3 so their outputs differ. Independent Python
reference files differ too, while their two input datasets are identical.
Each variant runs with root SRV/UAV descriptors and descriptor tables in the
same process. Each case creates and releases its own device/PSO/resources;
the second phase reconstructs them and exercises the process-local conversion
cache. Every PSO is checked to use the MSC backend and reflected 64×1×1 threads.

The first phase recorded eight misses, eight real conversions and eight
insertions: four shaders multiplied by two serialized root layouts. The second
phase recorded eight cache hits, zero conversions and no storage changes.
All 16 cases passed three byte-exact GPU readbacks each (3,072 output bytes
plus 64 guard bytes per readback), for 48 readbacks. The reused helper also
checks closed-command-list replay, fenced reset, rejection of a pending
allocator reset and release of application references while submission is
blocked behind a fence. Both Metal API and GPU validation were enabled;
no validation errors were reported. Stage/build bytes, loaded PE export bytes
and actual WineMetal Unix and MSC paths were checked.

| Control | Successful GPU cases before termination | Final readback | Actual conversions | Expected process exit |
| --- | ---: | --- | ---: | ---: |
| Positive | 16 | Exact output/guards | 8 | 0 |
| Wrong reference on last warm case | 15 | byte 32: actual 82, expected 83 | 8 | 1 |
| Wrong cached shader on last warm case | 15 | byte 32: actual 81, expected 82 | 8 | 1 |

Both negative cases deliberately fail only the final warm descriptor-table
case. The runner requires all preceding checks, the expected cache hit and the
precise data-mismatch location, rather than accepting any nonzero exit. These
controls demonstrate sensitivity to incorrect output and shader selection;
they do not corrupt the production cache. All three processes retain eight
entries: 38,528 bytes of metallib length/capacity, 72,768 bytes of mapped-value
objects, 32 entry-point characters (120 capacity), 13 buckets and zero stage-in
storage. The accounting limits in the preceding section still apply.

`LOCAL_PASS`: Nine log mutations were rejected: missing readback, incorrect
shader selection, missing warm hit, warm recompilation, missing completion,
validation error, missing validation, missing submission-lifetime result and
an extra malformed phase marker. The final runner reparsed both negative logs
and independently matched every cache snapshot to JSON. A final fresh positive
run included DXIL, compiler, oracle-generator and input/output fixture hashes.

`LOCAL_PASS`: The default oracle input/output bytes are unchanged; the default
HLSL variant also produces exactly the previous DXIL SHA-256
`c57607c73a172d41babae3342a9d2f7cb285b3b6229965f4f866c6d0924faa1b`.
All six original threadgroup flag/binding/readback controls passed. The old
concurrent no-delay path still made one conversion for eight concurrent PSOs
and none for eight repeats. The C++ probe built in both `build` and the separate
candidate configuration; Python parsing and `git diff --check` passed.

One fixture-build attempt failed with Wine protocol mismatch `930/784` because
the Meson DXC command uses `toolchains/wine`, while the runtime stage has a
different Wine binary. Retrying the Meson target with a separate build-only
prefix (`prefix-dxc-build-cache-gpu-20261104`) succeeded. No running test server
was stopped or prefix reset. Use separate prefixes for these Wine builds.

Raw artifacts under `dxmt-test/n95-capability-gate/` are
`result-cache-gpu-final-positive-20261104`,
`result-cache-gpu-negative-{reference,variant}-20261104`,
`result-cache-gpu-default-regression-20261104` and
`result-cache-gpu-legacy-concurrent-20261104`. The earlier positive directory
also contains `parser-controls.txt`. Reproduce with a new output directory:

```sh
test_root=/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate
python3 tests/dx12/run_msc_concurrent.py \
  --stage "$test_root/stage-msc-cache-gpu-20261104" \
  --prefix "$test_root/prefix-msc-space-fresh-20261104" \
  --build "$test_root/dxmt-current-patches-20261103" \
  --converter /usr/local/lib/libmetalirconverter.dylib \
  --output "$test_root/result-cache-gpu-next" \
  --expected-calls 8 --corpus-count 4 --gpu-corpus
```

Add `--gpu-negative reference` or `--gpu-negative variant` in separate new
output directories for the negative controls.

**Gate outcome:** Baseline/design, test-only implementation, validation and
scope review passed for resource-using compute cache replay. Production cache
code, keys, policy, WineMetal ABI and public FL/SM/Tier declarations did not
change. These are controlled compute fixtures, not representative game memory
or all-shader correctness evidence. Graphics/stage-in cache replay, Function
Constant specialization identity and other GPU/OS/MSC versions remain
`NOT_RUN`. Cache tracing and Metal validation preclude latency/speedup claims;
whole-process memory is explicitly unmeasured in GPU corpus mode. The next
recommended slice is Function Constant specialization/PSO cache isolation,
which is a separate requirement from ordinary DXIL conversion-cache reuse.

## Graphics stage-in cache replay design — 2026-11-04

- **Goal:** Verify cached VS/stage-in, PS and GS conversion results through real D3D12 PSO recreation and exact GPU readback.
- **Baseline:** `e304a986`, `feat/d3d12`; the existing `.gitignore` edit remains outside the slice. Compute cache replay and test-owned Function Constant cache isolation have separate evidence. Ordinary VS/PS does not synthesize separate stage-in; MSC GS/TS does (`d3d12_pipeline_graphics.cpp:1091-1123,1185-1196`).
- **Contract / evidence:** Microsoft `D3D12_INPUT_ELEMENT_DESC` defines `InputSlot` and `AlignedByteOffset`; the VBV supplies stride. Apple's installed MSC manual describes separate stage-in synthesis for emulation. `MakeMSCConversionCacheKey` includes the initialized input layout (`d3d12_shader_converter.cpp:338-345`), and cached values own both metallib vectors (`:1849-1855`). These are source observations, not GPU proof. Microsoft reference: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_input_element_desc .
- **Scope / boundary:** Extend the existing geometry differential probe and stage-in runner only. Three input layouts: slot 0/offset 8/stride 24; slot 3/offset 8/stride 32; slot 0/offset 24/stride 48. Keep shader bytes and root signature fixed. Recreate devices/roots/PSOs/resources for every case, first with new keys and then with cache hits.
- **Design:** Preserve the canonical CPU vertex values and oracle; pack the upload bytes separately for each layout. Expect three VS/stage-in entries and shared PS/GS entries, five conversions total, then no warm conversion. Require nonempty stage-in storage and no warm storage growth. Reuse exact readback, closed-list replay, fenced reset, pending-reset rejection and release-before-completion checks.
- **Negative controls:** On the final warm layout, either corrupt the expected output or select layout 0 while retaining layout 2's valid padded VB. The latter intentionally requests an existing wrong stage-in key; it must produce a pixel mismatch without invalid GPU addressing. Neither control corrupts the production cache.
- **Test plan:** Build the executable and existing three SM6 fixtures; run all cases under Metal API/GPU validation in a paired isolated Wine stage, checking runtime identities and exact cache events; rerun the original stage-in baseline/growth tests and mutate recorded parser inputs.
- **Non-scope:** TS, mesh, new shader semantics, native Windows/Mullion execution, production cache policy, persistent caching, performance conclusions or public capability promotion. Mullion uses a different shader backend and is not an MSC cache oracle. Broader input layouts remain unverified.
- **Risks:** Sparse-slot handling could reveal an existing emulation bug. Tracing perturbs performance. Scope a discovered defect before changing runtime code.
- **Rollback / done:** Remove the new opt-in mode and checks independently. Require successful GPU/counter oracles, intended negative failures, adjacent regressions, reviewed diff and a focused commit without pushing. Gate 0 and Gate 1 complete.

## Graphics stage-in cache replay results — 2026-11-04

**Gate 2:** `dx12_graphics_sm6.cpp` now accepts `--gs-cache-replay VS PS GS`
and optional `--wrong-expected` / `--wrong-layout`. It calls the existing GS
differential path six times with independently packed buffers. The original
layout remains the default for existing callers. The optional layout modes
change only the probe's input descriptors, upload packing and VBV. Shader
sources and the production converter, cache key and pipeline code are unchanged.
`run_msc_stage_in_retry.py --mode cache-replay` reuses the trace wrapper and
shared cache-snapshot parser, checks each phase's events and GPU results, and
saves identities/raw logs/snapshots to JSON. This report and the MSC work report
record the design and evidence.

**Gate 3 / LOCAL_PASS:** The probe/fixtures and complete existing candidate
build succeeded. On Apple M1 Max, macOS 27.2, Wine 11.0, the positive process
created six D3D12 graphics PSOs across three layouts and two phases. The first
layout converted VS, PS and GS. The next two converted only VS/stage-in and
reused PS/GS. All three warm cases used three cache hits each, with no extra
conversion. Totals: five conversions/inserts, thirteen hits, five entries.

All eighteen GPU readbacks checked 3,072 bytes exactly, including suppressed
primitives and strip gaps. Each of six cases checked closed-list replay,
fenced reset, pending allocator-reset rejection, and released application
references while submission waited on a gate. Backend/reflection checks
confirmed MSC geometry in every case. Metal API and shader validation were
enabled; loaded PE export bytes, PE/Unix/MSC paths and stage/build contents
were verified.

| Control | Completed cases before mismatch | Intended final warm mismatch | Exit |
| --- | ---: | --- | ---: |
| Correct layouts | 6 | None | 0 |
| Wrong reference | 5 | Pass 0, byte 0: actual 1, expected 0 | 1 |
| Wrong cached layout | 5 | Pass 0, byte 1: actual 31, expected 79 | 1 |

Both negative processes still recorded five conversions and thirteen hits.
Their final case used existing cached data and failed at the expected pixel;
all prior fifteen readbacks passed. This demonstrates the oracle's sensitivity
to an incorrectly selected stage-in result; it is not a corruption of the cache
implementation or proof against every possible cache collision.

The final cache retained 81,120 bytes of stage metallibs and 9,876 bytes of
stage-in metallibs, with equal vector capacities. The five mapped values
occupied 45,480 bytes; entry-point character length/capacity were 35/75 and
bucket count was 13. Warm cases did not change any snapshot field. These
measurements exclude allocator/node overhead, GPU storage and PSO copies;
they are not total memory consumption and do not justify a capacity policy.

The original baseline and injected stage-in growth modes also passed their
GPU/negative controls (three and four conversions respectively). Fourteen
parser mutations rejected missing/wrong cases, selection, completions,
readbacks, lifetime checks, extra conversion, missing status, zero stage-in
storage, unexpected failure, empty reflection and missing-failure/false-success
negative logs. Mutation results are saved in `parser-controls.json`.

Runtime WineMetal Unix SHA-256:
`c9e20da27632144651fd744d01ddb26445800ea4baa1fbd36a0257188f1fbd8c`.
Actual MSC file `/usr/local/lib/libmetalirconverter.dylib` SHA-256:
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.
The wrapper links that file and both paths are checked; this records content
identity, not a runtime product-version query. Results are under the external
`/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/`:

- `result-msc-graphics-cache-20261104/`
- `result-msc-graphics-cache-baseline-20261104/`
- `result-msc-graphics-cache-grow-20261104/`

Reproduce from the repository root with fresh output directories:

```sh
base=/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate
WINEPREFIX="$base/prefix-dxc-build-cache-gpu-20261104" meson compile \
  -C "$base/dxmt-current-patches-20261103" dx12_graphics_sm6 \
  dx12_geometry_differential_3_vs dx12_geometry_differential_3_ps \
  dx12_geometry_differential_3_gs
meson compile -C "$base/dxmt-current-patches-20261103"
python3 tests/dx12/run_msc_stage_in_retry.py \
  --stage "$base/stage-msc-graphics-cache-20261104" \
  --prefix "$base/prefix-msc-space-fresh-20261104" \
  --build "$base/dxmt-current-patches-20261103" \
  --converter /usr/local/lib/libmetalirconverter.dylib \
  --output "$base/NEW-graphics-cache-results" --mode cache-replay
```

Prepare the stage as documented in `DEVELOPMENT.md`, refreshing D3D12/DXGI,
both WineMetal halves and the probe/three fixtures from the build. Keep the
build-only Wine/DXC prefix separate from the compatible runtime prefix.
Use `--mode baseline` or `--mode grow` with another output path for regressions.

**Gate 4:** Scope and whitespace review passed. Only test code/runners and
reports changed; no public capability, cache policy, production API or backend
behavior changed. Ordinary VS/PS vertex-fetch cache replay, TS/mesh, different
devices, arbitrary layouts, persistence and performance remain **NOT_RUN**.
The next recommended slice is ordinary VS/PS cache-hit PSO recreation with
dynamic vertex strides. The wider MSC task remains active. Commit this slice
locally, retaining the pre-existing `.gitignore` change and without pushing.

## Ordinary vertex-fetch cache replay design — 2026-11-04

- **Goal / baseline:** At `ce1f59ae`, verify real D3D12 VS/PS cache-hit PSO recreation together with dynamic vertex-fetch variants. Branch `feat/d3d12`; preserve the existing `.gitignore` edit. Previous GS stage-in evidence does not cover this path.
- **Contract:** Microsoft defines VBV `StrideInBytes` as the byte size of an entry and `InstanceDataStepRate` as the number of instances sharing an element. References: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_vertex_buffer_view and https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_input_element_desc . Apple MSC's ordinary Metal vertex-fetch path uses a Metal vertex descriptor; separate stage-in is unnecessary for this fixture.
- **Source evidence:** `RunVertexStride` already verifies exact outputs for packed/padded/zero strides, slot 3, instance rates, indexed/indirect draws and released PSO references. `GetVertexStridePipeline` keys native variants by the active VB strides (`d3d12_pipeline_graphics.cpp:502-538`); ordinary conversion passes no stage-in layout (`:1118-1123`). This slice tests reuse of that existing architecture without modifying it.
- **Scope / boundary:** Extend the existing graphics probe and graphics/stage-in runner. Recreate devices/roots/PSOs/resources for seven modes, then repeat: packed, vertex, instance, instance-zero, indexed, indirect and lifetime. The lifetime case creates two PSOs before each of two recordings. Verify every created PSO uses ordinary MSC vertex fetch.
- **Expected evidence:** Two conversions/entries shared by all twenty D3D12 PSO creations; thirty-eight hits; zero stage-in bytes; forty-two readbacks of 4,096 exact bytes. Include closed-list replay, fenced reset, pending reset rejection and released pipeline references.
- **Negative controls:** Corrupt the reference or bind an intentionally stale zero stride on the final warm lifetime case. Require all earlier cases to pass, then the exact intended pixel mismatch. This tests oracle sensitivity to wrong vertex fetch, not a mutation of the production cache.
- **Non-scope:** New shader semantics, production diagnostics/policy, native Windows/Mullion execution, arbitrary layouts, cross-device persistence or performance claims. Mullion is not an MSC cache oracle. No capability declaration changes.
- **Test plan / risks:** Use paired isolated Wine and Metal API/GPU validation, exact trace/creation counts, parser mutation controls, plus the existing GS cache regression. Tracing perturbs timings; no performance conclusion is authorized by this test.
- **Rollback / done:** Remove the opt-in mode and checks independently; require build, exact GPU/cache results, intentional negative failures, regression and diff review before a local commit without pushing. Gate 0 and Gate 1 complete.

## Ordinary vertex-fetch cache replay results — 2026-11-04

**Gate 2 / changed files:** `dx12_graphics_sm6.cpp` adds
`--vertex-cache-replay VS PS [--wrong-expected|--wrong-stride]`, reusing
`RunVertexStride` and its independent pixel oracle. Every PSO creation checks
MSC backend, ordinary vertex input enabled and GS/TS emulation disabled.
The existing `run_msc_stage_in_retry.py` gains `--mode vertex-cache`, selects
the two existing vertex-stride fixtures, and verifies per-case conversion,
creation, readback and lifetime markers. No production runtime source,
shader source, converter key or cache policy changed. The two reports record
the design and evidence.

**Gate 3 / LOCAL_PASS:** The probe/fixtures and complete existing candidate
build passed on Apple M1 Max, macOS 27.2, Wine 11.0. Seven modes in two phases
created twenty D3D12 PSOs: one per mode/case except lifetime, which created
four. Two initial VS/PS conversions were followed by thirty-eight cache hits,
including twenty in the second phase. Each case recreated its D3D12 device,
root signature, PSOs and resources on the same physical Metal device. The
cache retained two entries and zero stage-in bytes throughout; only the first
case performed conversions.

Forty-two readbacks each checked every byte of the 4,096-byte target. Tested
strides were 12 (packed) and 16/32/16/0 (dynamic), all on sparse slot 3, with
per-vertex and per-instance fetch, instance step rates 2/0, indexed and indirect
draws. All cases exercised closed-list replay, fenced reset and rejection of
pending allocator reset. The lifetime cases additionally released both public
PSO references before execution/replay and recreated them before the final
recording. These are D3D12 PSO creation counts, not counts of native Metal
stride variants. Native variant-cache hit/miss counters were not measured.

| Control | Completed cases / readbacks before failure | Final warm mismatch | Exit |
| --- | --- | --- | ---: |
| Correct input | 14 / 42 | None | 0 |
| Wrong reference | 13 / 39 | lifetime pass 0, row 0, x 0: actual 1,2,3; expected 0,2,3 | 1 |
| Wrong zero stride | 13 / 39 | lifetime pass 0, row 0, x 1: actual 1,2,3; expected 2,5,8 | 1 |

Each negative process performed eighteen D3D12 PSO creations, two conversions
and thirty-four hits; its final case stopped before the second recording.
Zero stride remains a valid constant-fetch input. Selecting it intentionally
with an unchanged oracle tests sensitivity to stale/wrong fetch, without
invalid GPU access or a production-cache mutation.

The final cache contained 10,728 bytes of metallib data/capacity, 18,192 bytes
of mapped values, 14 entry-point characters (30 capacity), and 13 buckets.
After the initial insertions every snapshot was stable. These fields exclude
allocator/node overhead, native PSOs and GPU memory; no total-memory or
performance conclusion follows from them.

Metal API and shader validation, paired PE/Unix hashes, loaded export bytes
and actual wrapper/MSC paths were checked. Sixteen parser mutations were
rejected, covering missing/wrong cases, control selection, completion, PSO
creation/backend, readbacks, lifetime, extra conversion, absent summary,
nonzero stage-in storage, unexpected failure and missing-failure/false-success
negative logs. Results are in `parser-controls.json`. The previous GS
cache-replay mode also passed again, including both of its negative controls.

Runtime WineMetal Unix SHA-256:
`c9e20da27632144651fd744d01ddb26445800ea4baa1fbd36a0257188f1fbd8c`.
Actual MSC `/usr/local/lib/libmetalirconverter.dylib` SHA-256:
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.
Raw logs, fixture/binary identities and snapshots are under the external
`dxmt-test/n95-capability-gate/` directory in
`result-msc-vertex-cache-20261104/` and
`result-msc-vertex-gs-regression-20261104/`.

Reproduction from the repository root, after preparing the paired stage:

```sh
base=/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate
WINEPREFIX="$base/prefix-dxc-build-cache-gpu-20261104" meson compile \
  -C "$base/dxmt-current-patches-20261103" dx12_graphics_sm6 \
  dx12_vertex_stride_vs dx12_vertex_stride_ps
meson compile -C "$base/dxmt-current-patches-20261103"
python3 tests/dx12/run_msc_stage_in_retry.py \
  --stage "$base/stage-msc-vertex-cache-20261104" \
  --prefix "$base/prefix-msc-space-fresh-20261104" \
  --build "$base/dxmt-current-patches-20261103" \
  --converter /usr/local/lib/libmetalirconverter.dylib \
  --output "$base/NEW-vertex-cache-results" --mode vertex-cache
```

Refresh the stage's probe, two fixtures and paired runtime files from the
build, without changing the user's Wine installation. Retain separate build
and runtime Wine prefixes. Use another fresh output directory and
`--mode cache-replay` with the three GS fixtures to run the adjacent regression.

**Gate 4:** Scope and whitespace checks passed. Public features, shader models,
tiers, production policies and dual backends are unchanged. Other shaders,
layouts, physical GPUs, driver versions, persistence and representative
performance remain **NOT_RUN**. The wider MSC task remains active. The next
recommended slice is an audit/design of reliable loaded MSC image identity,
which is a prerequisite for any opt-in persistent-cache experiment. Commit
these tests and reports locally, preserve `.gitignore`, and do not push.

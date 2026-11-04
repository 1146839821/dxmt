# MSC Compute-to-Fragment execution overlap

The correctness and whole-workload timing baseline is complete. The initial
slice below accepted one ALL capture and rejected the PIXEL captures. The
[subsequent attribution fix and paired results](msc-trace-attribution.md)
resolved the layout-depth mistake and added submission/execution-point checks;
three fresh ALL/PIXEL pairs passed with no observed Compute/Vertex overlap.
The initial capture failures remain retained. No production optimization or
capability change is included.

## Design card — 2026-11-04

**Goal:** Establish a correct D3D12 workload and Metal System Trace evidence
for the execution-overlap question in the MSC performance task, section 10.

**Baseline:** `feat/d3d12`, `f6c6d24168c3121bf1e2d8e76a2db850315e8ba7`,
macOS 27.2, Metal 32023.922, Apple M1 Max. Only the user's `.gitignore` was
modified before this slice; it remains excluded. Use the isolated patched-LLVM
x64 build and a separate Wine stage, following `.github/build_dxmt.sh` options.

**Evidence:** Apple's installed `/opt/metal-shaderconverter/docs/UserManual.md:936`
describes the Compute-write/Fragment-read dependency and recommends Metal
System Trace. Current `src/d3d12/d3d12_command_list.cpp:3197` selects both
Vertex and Fragment for ordinary MSC graphics. `:4620` uses the same broad
stages for indirect resource residency, including the SRV texture at `:4756`.
The exact descriptor footprint at `:4637` applies only to Airconv. This is
`SOURCE_AUDIT`, not proof that narrowing the stages is safe or faster.

`src/d3d12/d3d12_command_queue.cpp:1382` also waits on the shared encoder fence
before the ordinary Vertex stage; the preceding Compute encoder updates that
fence at `:1416`. This explicit dependency applies to both visibility variants.
Narrowing descriptor residency alone would leave it in place. Its correctness
scope must be audited separately before any attempt to allow earlier Vertex
execution; this baseline preserves the fence.

**Scope / boundary:** A test executable, HLSL fixture, runner, build registration
and report. No production queue, barrier, root-signature, residency or descriptor
ABI changes. Compare application SRV table visibility ALL and PIXEL; both
currently receive broad Metal residency declarations. Keep the same compiled
DXIL, dispatch/draw sizes and resource states in both cases.

**Proposed changes:** Compute fills a 128x128 R32_UINT texture. A separate
Vertex calculation generates a flat integer per 4x4 cell; Fragment reads the
texture and XORs that integer into an R32_UINT render target. The CPU validates
all 65,536 output bytes after every submission, with changing seeds. Retain
the UAV barrier, UAV-to-PIXEL_SHADER_RESOURCE transition and all residency
declarations. Use an explicit normal/wrong-oracle control for each visibility.

**Test plan:** Build the probe and cs/vs/ps DXIL. Assert both PSOs use MSC,
verify deployed and actually loaded PE/Unix identities, run both variants
under Metal API/shader validation, and require wrong-output controls to fail.
Run separate unvalidated timing processes in alternating order, discard two
warmups, retain raw samples and report median/P95/range. Timestamp only the
whole Compute-to-Draw interval; there is no intermediate timestamp between
the dependent stages. Collect a separate Metal System Trace for stage overlap;
whole-workload spans cannot establish overlap percentage.

**Risks:** Root visibility changes PSO inputs but current residency may remain
broad. UAV synchronization, explicit encoder fences and hardware scheduling
can dominate any effect. Timestamp sampling adds work at the interval edges;
trace instrumentation also perturbs timings. The synthetic workload cannot
establish game performance, occupancy, bandwidth or general shader correctness.

**Rollback / done:** Remove only the opt-in test targets/files to revert. This
slice is done when correctness, timing and trace availability are reported with
reproducible evidence, including any unavailable stage data. No performance
optimization or capability promotion is accepted without a valid timeline and
broader semantic validation.

## Contract and implementation evidence

| Evidence type | Location | Meaning for this test |
| --- | --- | --- |
| SPEC_REQUIRED | Microsoft `D3D12_SHADER_VISIBILITY`, downloaded `ne-d3d12-d3d12_shader_visibility.md:57,77,93` | ALL exposes the root slot to all stages; PIXEL restricts access to PS. Compute uses ALL. Visibility alone does not establish actual resource use. |
| SPEC_REQUIRED | Microsoft `D3D12_RESOURCE_STATES`, downloaded `ne-d3d12-d3d12_resource_states.md:76,92` | UAV writes and pixel SRV reads use the corresponding states. The test retains an explicit UAV barrier and the transition. |
| SOURCE_AUDIT | Apple `/opt/metal-shaderconverter/docs/UserManual.md:936` | Shared argument-buffer resources can introduce a Compute-to-Vertex dependency; inspect execution with Metal System Trace. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_list.cpp:3197,4620,4637,4756` | Ordinary MSC graphics declares Vertex and Fragment usage; the exact descriptor footprint path is Airconv-only. ALL versus PIXEL does not currently narrow MSC residency. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_queue.cpp:1382,1416` | Compute updates the fence subsequently waited on before Vertex. Residency narrowing alone cannot remove this explicit dependency. |
| LOCAL_PASS | `tests/dx12/dx12_msc_execution_overlap.cpp:68,176,269,347`; `tests/dx12/msc_execution_overlap.hlsl:17,26,41` | Configuration checks, MSC backend assertions, synchronized dispatch/draw, and exact output oracle exercise the actual D3D12 path. |
| NOT_RUN | Windows native / Mullion | No comparative runtime result or transplanted implementation is claimed. This slice adds a DXMT-specific measurement probe. |

The Microsoft files and their official MicrosoftDocs/sdk-api source URLs are
retained in the evidence directory with `spec-sources.json`. No production
contract, descriptor ABI or synchronization policy changes in this slice.

## Environment, artifacts and verification

Evidence root:
`/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-msc-overlap-20261104/`.
All paths below this section are relative to that directory unless specified.

The machine reports Apple M1 Max, macOS 27.2 (26B123), Xcode 27.2 (27B52),
Metal 32023.922 / component 27B47, and staged Wine 11.0. The baseline's fresh
full x64 build is documented in
[the macOS 27.2 identity audit](msc-runtime-cache-identity-design.md#macos-272-revalidation--2026-11-04).
Its Metal AIR/metallib outputs were compiled with the installed toolchain.
For this slice, the three DXIL fixtures and the new executable compiled, and
the full incremental build succeeded (`build-retry.log`, `full-build.log`,
`arithmetic-build.log`, `final-build.log`, `delivery-build.log`). This slice did
not rebuild x86.

The isolated build is `../dxmt-metal272-identity-audit-20261104/`; its LLVM is
`../llvm-current-patches-20261103/install/`. The staged runtime is
`../stage-msc-overlap-20261104/`, paired with
`../prefix-msc-identity-refresh-20261104/`. Build-time DXC uses a separate Wine
prefix. No installation into the user's Wine environment was performed.

`final/summary.json` retains exact argv, environment, all raw samples and
SHA-256 identities for the staged executable, CS/VS/PS DXIL, D3D12, DXGI and
both WineMetal halves. The runner compares stage/build bytes, validates the
WineMetal builtin/app-local pairing, and checks actual loaded PE export bytes
and PE/Unix/MSC paths. Key identities:

| Artifact | SHA-256 |
| --- | --- |
| `/usr/local/lib/libmetalirconverter.dylib` | `cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4` |
| WineMetal Unix library | `c9e20da27632144651fd744d01ddb26445800ea4baa1fbd36a0257188f1fbd8c` |
| Final probe executable | `751d0a467248e601cfe79fdd8a797e3c7b3762aa7ac6fbc0c80f6f9b959619f9` |

The MSC hash identifies the file used; it is not a runtime product-version
query. Identity V1 and persistent-cache policy are unchanged.

**LOCAL_PASS:** ALL and PIXEL each completed four exact 65,536-byte readbacks
under Metal API and GPU validation (two warmups, two measured samples). Both
wrong-oracle controls completed two valid warmups, then exited 1 at sample 2,
pixel (0,0), with exactly one expected bit inverted. There were no reported
Metal validation errors in these final runs. The runner rejected 13 mutations
of the final logs, including missing/duplicate samples, nonfinite or invalid
times, wrong byte counts/configuration and false completion/negative results
(`final-parser-controls.json`).

Five ordinary timing processes per visibility each produced two warmups and
ten measured samples. All 120 timing-process readbacks passed. Validation and
trace instrumentation were disabled for these timing processes. Process order
alternated ALL/PIXEL and PIXEL/ALL; every process used a separate DXMT cache path.
Shader/PSO creation preceded the loop. Driver/system caches and GPU clocks were
not reset or controlled. CPU readback verification and fence completion separate
submissions, so this is not a continuous frame-throughput benchmark.

## Whole-workload timings

Each visibility has 50 measured samples at 1,024 recurrence iterations.
Times are milliseconds; P95 uses the nearest-rank sample. Raw data and
per-process statistics are in `final/summary.json`.

| Measurement | ALL median / P95 / min / max | PIXEL median / P95 / min / max |
| --- | --- | --- |
| GPU timestamp span | 1.292771 / 1.923208 / 0.680792 / 2.023167 | 1.249959 / 1.691958 / 0.711000 / 1.893750 |
| CPU encode | 0.190650 / 0.234600 / 0.150100 / 0.275400 | 0.191500 / 0.253100 / 0.161600 / 0.445600 |
| CPU submit | 0.292400 / 0.347500 / 0.230000 / 0.374700 | 0.295900 / 0.362400 / 0.238000 / 0.421600 |

GPU per-process medians ranged from 1.209937 to 1.393458 ms for ALL and
1.060542 to 1.347042 ms for PIXEL. The direction of the paired difference
changed between processes. These measurements do not establish a speedup.
The GPU timestamp span includes dispatch, draw, dependencies and scheduling;
it does not isolate shader execution. CPU encode covers allocator/list reset
through Close, including readback/query commands. CPU submit covers
ExecuteCommandLists and Signal calls, excluding GPU completion wait.

## Trace collection and completeness

The installed Metal System Trace template can record GPU Compute, Vertex and
Fragment channel intervals, together with command-buffer/encoder identities.
Its default configuration has Shader Timeline disabled and no selected
hardware counter set. Channel intervals establish observed scheduling, not
shader instruction timing, occupancy, register pressure or bandwidth.

An initial collector invocation under Rosetta failed with `ktrace cannot
trace the system under Rosetta translation` (`trace-record.log`). Launching
the collector with `/usr/bin/arch -arm64 xcrun xctrace` succeeded while Wine
remained x64. Capture runs are separate from the ordinary timing runs.

The initial analyzer joined `metal-gpu-intervals` to
`metal-application-encoders-list` by command-buffer and encoder IDs, filters
the exact launched PID, checks the target arguments/exit and readback log,
and ignores Blit encoders even when they execute on the Compute channel.
It requires all expected workloads and Compute/Vertex/Fragment data, with
exactly one Compute and one Render encoder for each workload. It unions
intervals before computing intersections. Its depth-zero restriction was later
found to reject valid layout tracks; the attribution follow-up replaces that
restriction with submission and execution-point cross-checks. Missing events
continue to cause failure.

`final-trace-all-analysis.json` contains 22 complete workloads (including two
warmups), PID 72390. Every observed Compute/Vertex and Compute/Fragment
intersection was zero nanoseconds. Compute-end to Vertex-start gaps ranged
from 23.541 to 885.708 microseconds, median 37.9375 microseconds. These gaps
include scheduling effects and cannot be attributed entirely to a fence wait.

Two PIXEL captures exited successfully and passed all 22 readbacks, but each
export lacked one Vertex channel event. They are retained and rejected as
incomplete (`incomplete-traces.json`), rather than classified as zero overlap.
The missing command buffers were `12786372237` (PID 72967) and `12803822489`
(PID 74329).

A shorter pair with ten measured samples and two warmups per visibility
(`matched-trace-all`, PID 75735; `matched-trace-pixel`, PID 76376) also exited
successfully and passed all readbacks. The exported data contains nested
Active events. The PIXEL export additionally assigns a Fragment event to
Compute encoder `12838877195`, and omits Vertex for command buffer
`12838877263`. These exports are rejected, and neither is a valid paired
overlap measurement. The cause of these capture/attribution anomalies is
**UNVERIFIED**. `trace-acceptance.json` records all five acceptance results;
only the first ALL capture is accepted. Further collection attempts were
stopped rather than relaxing the data contract.

The analyzer rejected thirteen mutations of actual exported data: missing
Compute/Vertex/Fragment, wrong PID, wrong workload arguments, target failure,
wrong schema, missing/duplicate encoder, zero duration, wrong channel and
empty GPU data, plus unsupported nested depth. A deliberately shifted Vertex interval produced the expected
positive intersection, proving the calculation does not always return zero.
These are **parser controls, not GPU measurements**. The script, mutated inputs
and results are preserved in `check_trace_analyzer_final.py` and
`analyzer-controls-final/summary.json`.

## Failed experiments and test refinements

- **LOCAL_FAIL:** The first vertex shader used a dynamically indexed constant
  array for triangle corners. Metal validation reported an invalid device or
  constant load in `vs_main`; the first pixel was zero instead of 2880362137
  (`initial/validation-all.log`). Arithmetic corner generation avoids that
  path. This does not fix or prove the cause of the separately tracked MSC
  dynamic-constant-array failure.
- The first trace included an explicit clear, which DXMT emitted as a separate
  Render encoder. Its tiny Vertex work was unrelated to the checksum draw.
  The final probe omits this unnecessary clear and overwrites/checks every
  pixel. `all-native.trace` and `arithmetic/` preserve the earlier experiment;
  they are excluded from final timing and trace conclusions.
- Incomplete PIXEL exports are retained as capture failures, even though the
  application itself passed. The analyzer's completeness requirements were
  kept unchanged.

## Reproduction

From the repository root, use the isolated build configured with tests and
DXC. The build/compiler options follow `.github/build_dxmt.sh` and
`docs/DEVELOPMENT.md`; do not run that script's installation steps into a
shared Wine environment for this experiment.

```sh
base=/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate
export PATH="/Users/zhangbo/Documents/Vibe-Codeding/dxmt/toolchains/llvm-mingw-20260908-ucrt-macos-universal/bin:$PATH"
WINEPREFIX="$base/prefix-dxc-build-cache-gpu-20261104" meson compile \
  -C "$base/dxmt-metal272-identity-audit-20261104" \
  dx12_msc_execution_overlap dx12_msc_execution_overlap_cs \
  dx12_msc_execution_overlap_vs dx12_msc_execution_overlap_ps
meson compile -C "$base/dxmt-metal272-identity-audit-20261104"
python3 tests/dx12/run_msc_execution_overlap.py \
  --stage "$base/stage-msc-overlap-20261104" \
  --prefix "$base/prefix-msc-identity-refresh-20261104" \
  --build "$base/dxmt-metal272-identity-audit-20261104" \
  --converter /usr/local/lib/libmetalirconverter.dylib \
  --output "$base/NEW-overlap-results" --processes 5 --samples 10 --iterations 1024
```

Before running, refresh the stage with the executable, fixtures, D3D12/DXGI
and the paired WineMetal PE/Unix libraries from this build, including the
builtin WineMetal copy. The runner verifies those bytes; its output directory
must not exist. Keep build-time and runtime Wine installations on their
respective prefixes. See `final/summary.json` for the actual validated argv.

For traces, replay the retained `*-command.json` argv from its recorded `cwd`,
using fresh output/cache/log paths and a parent environment without inherited
WINE/DXMT/MTL/DYLD settings. The command specifies its Wine/loader environment
explicitly. Use native arm64 xctrace, template `Metal System Trace`,
`--time-limit 30s --no-prompt --target-stdout TARGET.log`, and launch the same
probe with `all|pixel 10 1024 normal trace`. Export each schema separately:

```sh
/usr/bin/arch -arm64 xcrun xctrace export --input CAPTURE.trace \
  --toc --output TOC.xml
/usr/bin/arch -arm64 xcrun xctrace export --input CAPTURE.trace \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="metal-gpu-intervals"]' \
  --output GPU.xml
/usr/bin/arch -arm64 xcrun xctrace export --input CAPTURE.trace \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="metal-application-encoders-list"]' \
  --output ENCODERS.xml
/usr/bin/arch -arm64 xcrun xctrace export --input CAPTURE.trace \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="metal-gpu-submission-to-command-buffer-id"]' \
  --output SUBMISSIONS.xml
/usr/bin/arch -arm64 xcrun xctrace export --input CAPTURE.trace \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="metal-gpu-execution-points"]' \
  --output POINTS.xml
python3 tests/dx12/analyze_msc_execution_overlap_trace.py \
  --toc TOC.xml --gpu GPU.xml --encoders ENCODERS.xml --stdout TARGET.log \
  --submissions SUBMISSIONS.xml --points POINTS.xml \
  --visibility all --samples 10 --iterations 1024 --output NEW-analysis.json
```

Use absolute paths for xctrace output arguments. Run the second visibility
separately. Analysis output must not already exist. The earlier 22-workload
captures require `--samples 20`; arguments and readback count must match.

## Initial slice delivery audit and next slice

| Gate | Result | Evidence / limitation |
| --- | --- | --- |
| 0 — Baseline | PASS | `f6c6d241`, branch/worktree inspection, local build/toolchain/runtime identity, source and specification audit. |
| 1 — Design | PASS | The design card above limits this slice to measurement and preserves required barriers/residency. |
| 2 — Implementation | PASS | Probe, fixtures, timing runner, strict trace analyzer and build targets implemented. |
| 3 — Validation | PARTIAL | Full incremental build, exact readbacks, negative controls, timing and ALL trace passed. PIXEL stage comparison is BLOCKED by incomplete/unsupported trace exports. |
| 4 — Delivery | PASS within stated scope | Diff/format/Python syntax checked. Capture failures and the earlier shader failure are retained; no unsupported optimization conclusion is promoted. |

Changed files and responsibilities:

| File | Responsibility |
| --- | --- |
| `tests/dx12/dx12_msc_execution_overlap.cpp` | D3D12 execution, backend/identity checks, timestamps, exact CPU oracle and wrong-output control. |
| `tests/dx12/msc_execution_overlap.hlsl` | Independent Vertex work and dependent Compute/Fragment texture access. |
| `tests/dx12/run_msc_execution_overlap.py` | Paired runtime verification, validated correctness, separate timing processes and JSON statistics. |
| `tests/dx12/analyze_msc_execution_overlap_trace.py` | Strict XML/PID/encoder attribution and interval analysis; rejects unsupported/incomplete inputs. |
| `tests/dx12/meson.build` | Test executable and opt-in CS/VS/PS DXIL targets. |
| `docs/msc-execution-overlap.md` | Design, specification/source evidence, measured results, failures and reproduction. |
| `docs/D3D12_MSC_HLSL_PERFORMANCE_WORK.md` | Updated task-wide execution-overlap status. |

**Capability impact:** No public Feature Level, Shader Model or Tier change;
no runtime-source, ABI, compiler policy, persistent-cache or D3D10/11 change.
Remaining risks are measurement overhead, platform-specific trace schemas,
incomplete capture data and limited synthetic coverage. Occupancy, register
pressure, bandwidth, resident memory, representative games, Windows, Mullion
and additional GPU families are **NOT_RUN** in this slice. The MSC task as a
whole remains incomplete.

**Git state:** Baseline was ahead 47 / behind 0 relative to the locally stored
`origin/feat/d3d12` reference; no remote refresh or push was performed. Commit
only the seven listed files using the repository's `test(d3d12): ...` style.
The pre-existing `.gitignore` edit remains outside this commit.

**Next recommended slice:** Establish a trustworthy paired trace protocol for
this same probe: inspect the nested/channel-attribution anomalies against
Instruments' raw driver events, document supported schema semantics, and
require complete ALL/PIXEL results before proposing a stage-scoped fence
change. Preserve the current queue fence and resource-use declarations until
that evidence and a separate dependency-safety design are available.

That recommended measurement slice is now completed in
[the attribution report](msc-trace-attribution.md). Its next step is a scoped
dependency-safety design; the initial failures above remain historical evidence.

# MSC execution trace attribution

The analyzer's layout-depth rejection is fixed. Three predeclared ALL/PIXEL
capture pairs passed submission/point/encoder cross-checks: 72 workloads and
216 GPU execution-point pairs. No Compute/Vertex overlap was observed in
either visibility. This resolves the paired-measurement blocker, without
changing production synchronization or establishing an optimization benefit.

## Design card — 2026-11-04

**Goal:** Establish a verifiable ALL/PIXEL GPU-channel comparison for the
existing D3D12 MSC execution-overlap probe, correcting analyzer assumptions
against installed Instruments evidence before changing any GPU scheduling.

**Baseline:** `feat/d3d12`, `2e7e7ef6a37dc930790b59169407cb144e0d33f3`, ahead
48 / behind 0 against the locally stored origin reference. Only the user's
`.gitignore` is modified. macOS/Xcode/Metal Toolchain 27.2, Apple M1 Max, Wine
11.0. Reuse the measured probe and isolated stage; production source is unchanged.

**Evidence / Gate 0:** The existing analyzer reproduces the failure on
`matched-trace-pixel` in seconds. XML IDs are unique, and the GPU interval
encoder association agrees with the submission mapping. However, the installed
Instruments modeling rules assign `event-depth` from a layout reservation ID,
not a validity flag. Thus rejecting all nonzero depths is overly restrictive.
Missing Vertex data and unexpected Compute/Fragment attribution also exist in
the lower-level `metal-gpu-execution-points` and submission tables; they cannot
be repaired by removing the depth check or guessing shader stages.

Installed source root:
`/Applications/Xcode.app/Contents/Applications/Instruments.app/Contents/Packages/GPU.instrdst/Contents/Extensions/`.

| Evidence type | Source under that root | Meaning |
| --- | --- | --- |
| SOURCE_AUDIT | `com.apple.metalsystrace-gpu.dtac/modeling-rules/rules-0002.clp:539-576,1298` | Execution point functions 1/2 delimit intervals; layout reservation ID becomes event depth. |
| SOURCE_AUDIT | `com.apple.metalsystrace-gpu.dtac/modeling-rules/rules-0002.clp:788-866` | GPU submission ID joins execution intervals to command-buffer/encoder identity. |
| SOURCE_AUDIT | `com.apple.metalsystrace-gpu-execution-tracking.dtac/modeling-rules/rules-0002.clp:273-310` | AGX emits start/end execution points with channel identity and low 32 bits of trace ID. |
| SOURCE_AUDIT | `com.apple.metalsystrace-gpu-execution-tracking.dtac/modeling-rules.clp:27-29` | AGX Vertex/Fragment/Compute channel IDs are `0x123457/0x123458/0x123459`. |
| LOCAL_FAIL | Prior `final-trace-pixel`, command buffer `12786372237` | The lower-level table also has no Vertex points for its Render encoder. |
| LOCAL_FAIL | Prior `matched-trace-pixel`, encoder `12838877195` | The submission table maps an observed Fragment-channel interval to this Compute encoder. |

**Scope / boundary:** Trace analysis, regression fixtures and evidence only.
Retain the same DXIL, executable, barriers, queue fence, resource residency,
workload and ordinary timing baseline. Microsoft resource-state/root-visibility
contracts are unchanged; their source evidence remains in the previous report.
No Mullion code is copied and no Windows result is claimed.

**Proposed changes / Gate 1:** Accept nonnegative layout depths, independently
join submission identity, reconstruct start/end point pairs per submission and
channel, and require agreement with every selected GPU interval. Reject missing
pairs, ambiguous IDs, coalesced encoders and wrong channel associations. Retain
exact target PID/arguments/exit/readback checks and complete workload count.
Use a fixed number of fresh captures, reporting every rejection alongside any
accepted results; do not repeat until a preferred overlap number appears.

**Tests:** Preserve a minimal real exported depth-one case as a parser
regression, then inject missing/wrong points and mappings. Replay all five prior
captures under the new contract. Collect a fixed three ALL/PIXEL pairs with ten
measured samples plus two warmups, retaining separate schema exports and actual
loaded-runtime identity evidence. Raw channel timing is not shader instruction,
occupancy or bandwidth evidence.

**Risks:** Instruments private schema and AGX channel IDs are version-specific;
raw points and intervals share upstream collection and are not independent
hardware observations. Agreement catches parsing/modeling inconsistency but
cannot prove instrumentation accuracy. Incomplete/conflicting captures remain
unsupported, not inferred as zero overlap. No production performance claim.

**Rollback:** Revert analyzer/tests/report changes only. Preserve captured
evidence and the user's existing work.

**Done:** The depth handling is source-justified and tested; all accepted inputs
pass point/submission/encoder cross-checks; every attempted capture has an
explicit outcome; a paired result or a concrete remaining blocker is documented.

## Diagnosis and decisions

**SOURCE_AUDIT:** `event-depth` is a layout track, populated from the layout
reservation used to display concurrent intervals. Accepting a nonnegative
value preserves the interval's start/end, PID and encoder checks. It does not
mean an incomplete interval can be accepted. The previous analyzer rejected
valid data here; it was deliberately conservative but incorrectly classified
the field. The updated analyzer also rejects negative layout depths.

**LOCAL_OBSERVED:** `matched-trace-pixel` contains these duplicate submission
IDs in the exported mapping, on the same accelerator 2982:

| Submission ID | Target PID / encoder | Other PID / encoder |
| --- | --- | --- |
| `4265743417` | `76376` / `12838877195` (Compute) | `2034` / `175036264` |
| `4265753468` | `76376` / `12838877266` (Render) | `653` / `8151307365` |

The first ID supplies the unexpected Fragment interval attributed to the
Compute encoder. Execution points carry no PID, and this model joins them by
submission ID; selecting a target PID only after that join cannot disambiguate
the source. `submission-collisions.json` retains the full relevant mappings.
The model extracts the low 32 bits of AGX trace IDs, but the underlying reason
those IDs collide is not established by this audit. No driver defect is claimed.

**Design decision:** Require both
`metal-gpu-submission-to-command-buffer-id` and `metal-gpu-execution-points`
exports. For each relevant target Compute/Render encoder, submission identity
must be unique across the entire mapping export. Reject ambiguous identities,
coalesced encoders, unknown channel assignments, unpaired/duplicate endpoints
and disagreement with derived GPU intervals. Pair endpoints by submission,
channel and slot, preserving separate execution intervals. Do not repair
missing Vertex data using the expected command order or CPU timings.

`tests/dx12/analyze_msc_execution_overlap_trace.py:64` implements these checks;
`:119` applies them to the target/readback validation and interval analysis.
Output schema is now 2, with hashes of all six input artifacts and the number
of checked point pairs. CLI callers must supply the two additional exports.
The original captured files and version-1 analysis results remain retained.

## Validation evidence

Evidence root:
`/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-msc-trace-attribution-20261104/`.

- `model-sources.json`: installed Instruments model paths and SHA-256 identities.
- `prior-replay-results.json`: all five prior captures re-evaluated with the new
  contract. The original 22-workload ALL capture still passes (66 point pairs).
  The 12-workload ALL capture with nonzero layout tracks now passes (36 pairs).
  The first PIXEL capture still lacks a Vertex event; the other two PIXEL
  captures fail on ambiguous submission identity. None is silently repaired.
- `capture-manifest.json`: the fixed six-capture sequence, exact argv/cwd,
  record/export exit statuses, and hashes for all five XML exports per capture.
- `capture-results.json`, `paired-results.json`, `pair-*-analysis.json`: all
  outcomes and checked intervals, including the two warmups per process.
- `validate_captures.py`: replay/identity check used for the full fixed batch.

**LOCAL_PASS:** The new `TraceTest.test_layout_depth_has_same_timing` first
failed against the baseline analyzer with `nested GPU channel attribution is
unsupported`. After the fix, all 16 parser regressions pass. They cover
nonzero/negative depth, positive intersection, a gap between execution segments,
foreign-process intervals, cross-process ID collision, missing/wrong/coalesced
mapping, missing or duplicate endpoints, slot mismatch, wrong channels,
duplicate GPU intervals and disagreement between intervals and endpoints.
These small fixtures test parsing and arithmetic; they are not GPU observations.

**LOCAL_PASS:** All six fresh target processes exited 0 and each verified 12
complete 65,536-byte readbacks. The validator checked loaded PE export bytes,
PE/Unix/MSC paths and staged artifact hashes against the previous measured
build. The executable remains
`751d0a467248e601cfe79fdd8a797e3c7b3762aa7ac6fbc0c80f6f9b959619f9`;
MSC remains
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.
This is file identity, not a runtime product-version query. The full artifact
set is recorded in the preceding overlap baseline's `final/summary.json`.

The batch was declared before results were inspected: three rounds, alternating
ALL/PIXEL and PIXEL/ALL order. Every process used 10 measured samples, 2 warmups,
1,024 recurrence iterations and fresh cache/log/trace paths. All six recording
and 30 export commands exited 0. No rejected fresh capture was replaced.

| Round / visibility | PID | Workloads / point pairs | Compute–Vertex intersection | Compute-end → Vertex-start median / min / max (µs) |
| --- | --- | --- | --- | --- |
| 0 / ALL | 86011 | 12 / 36 | 0 ns in every workload | 41.021 / 33.458 / 753.042 |
| 0 / PIXEL | 86711 | 12 / 36 | 0 ns in every workload | 36.8535 / 31.916 / 552.958 |
| 1 / PIXEL | 87373 | 12 / 36 | 0 ns in every workload | 34.667 / 10.166 / 418.458 |
| 1 / ALL | 88095 | 12 / 36 | 0 ns in every workload | 39.4375 / 32.708 / 504.250 |
| 2 / ALL | 88860 | 12 / 36 | 0 ns in every workload | 36.5625 / 15.875 / 522.041 |
| 2 / PIXEL | 89394 | 12 / 36 | 0 ns in every workload | 221.646 / 11.416 / 2266.959 |

Compute–Fragment intersections were also zero. The checked point pairs cover
Compute, Vertex and Fragment; timestamp/readback Blit encoders are excluded.
The table includes warmups and describes captured channel intervals only. Gaps contain scheduling and
instrumentation effects; they are not isolated fence-stall durations or a
visibility speedup metric. Large variation, especially in the final PIXEL
capture, remains visible. The uninstrumented 50-sample-per-visibility timing
baseline was not rerun or replaced.

This observation agrees with the source-level dependency in
`src/d3d12/d3d12_command_queue.cpp:1382,1416`: Compute updates the fence waited
on before Vertex. It does not prove that moving or removing that wait is safe,
or quantify the contribution of fence versus residency versus scheduling.

## Reproduction and supported scope

Run parser regressions from the repository root:

```sh
python3 -B tests/dx12/test_msc_execution_overlap_trace.py -v
```

Reuse the capture protocol in [the baseline report](msc-execution-overlap.md#reproduction).
For every fresh trace, additionally export these schemas separately:

```sh
/usr/bin/arch -arm64 xcrun xctrace export --input CAPTURE.trace \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="metal-gpu-submission-to-command-buffer-id"]' \
  --output SUBMISSIONS.xml
/usr/bin/arch -arm64 xcrun xctrace export --input CAPTURE.trace \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="metal-gpu-execution-points"]' \
  --output POINTS.xml
python3 -B tests/dx12/analyze_msc_execution_overlap_trace.py \
  --toc TOC.xml --gpu GPU.xml --encoders ENCODERS.xml --stdout TARGET.log \
  --submissions SUBMISSIONS.xml --points POINTS.xml \
  --visibility pixel --samples 10 --iterations 1024 --output NEW-analysis.json
```

Use absolute output paths and a new analysis filename. The fixed batch's
`capture-manifest.json` records the actual six invocations and 30 exports.
This analyzer supports the inspected Xcode 27.2 AGX schemas and channel IDs.
It does not support coalesced encoders, incomplete point streams, other GPU
backends or conflicting submission IDs. Agreement between these derived tables
does not make them independent hardware measurements. Occupancy, bandwidth,
register pressure and shader instruction timelines remain **NOT_RUN**.

## Delivery audit

Gate 0 and Gate 1 passed with the model/source evidence and design above.
Gate 2 adds the attribution fix and regression harness. Gate 3 passed the 16
parser cases, five historical replays with their expected acceptance/rejection,
and all three fresh pairs. Gate 4 checks the focused diff, Python syntax,
retained failures and report consistency. No C++/HLSL/build configuration
changed, so recompilation and new Metal-validation runs are **NOT_RUN** here;
the prior executable's validated semantics and hashes are preserved.

Changed files are the trace analyzer, its standalone regression script, this
design/evidence report, and status/reproduction updates in
`docs/msc-execution-overlap.md` and `docs/D3D12_MSC_HLSL_PERFORMANCE_WORK.md`.
No public Feature Level, Shader Model, Tier, WineMetal ABI or production
synchronization policy changes. Regression risks are limited to the analysis
tool's stricter required inputs and platform-specific schema assumptions.
Commit only these five files using the existing `test(d3d12): ...` convention;
leave the user's `.gitignore` edit intact and do not push.

**Next recommended slice:** Design the smallest safe Compute→Render fence-stage
refinement, beginning with an audit of actual Vertex inputs, UAV/resource
dependencies and aliasing. A source/test dependency contract must precede any
opt-in scheduling experiment; the current global Vertex wait remains in place.
The broader MSC performance/HLSL task remains incomplete.

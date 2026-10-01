# Addition multi-export failure oracle

## Task Analysis

Hypothesis: a failed second export must not publish a child or mutate its parent,
while successfully converted exports may remain cached for a fresh-child retry.
Evidence: creation multi-export and single-export addition probes exist, but their
intersection is not covered. A parent seed sharing the first export would hide
initial converter work.
Expected effect: add 40 bounded two-export addition modes, reusing exact ordered
compiler/load traces and inherited identifier/stack checks.
Risk: seed cache contamination; use a stage distinct from both child exports.
Validation: host aggregation tests, both configured builds and Metal validation
invocation receipts; self-review before local commit. No GPU tracing or feature
level promotion is implied. Larger/order-varied sets remain outside this probe.

## Task Result

Implemented 40 required modes: six stages plus AH/CH hints, each with control,
second-library/function-load failure, unsupported query and second-pass failure.
The parent seed differs from both child exports. Existing exact traces check
first-export progress, null child publication, same-parent fresh-child retry and
layer-specific cache reuse; parent checks now also reject FirstExport leakage.

Validation (2026-10-02): both Meson configurations rebuilt successfully; each
passed 3/3 Meson tests. Python host tests passed 52/52. Normal and no-private
invocation receipts each passed 439/439, including 110/110 addition modes.
Compared against previous receipts, all 399 old modes remain and the exact 40
new modes were added. Both receipts have PASS build provenance and API/GPU
validation enablement markers in every invocation; no failed-assertion,
Shader Validation Error or GPU Validation Error markers were found.
Receipts: `build/fl12-add-multi-normal.json` and
`build-no-private/fl12-add-multi-no-private.json` (ignored build outputs).
Normal DLLs were restored; fresh normal provenance passed.

### Standards

Independent read-only review against 5afdc2b found no concrete bugs or hard
violations. Two nonblocking judgment calls remain: string-prefix mode decoding
and duplicated matrix enumeration. Narrow baseline reuse is retained; independent
expected-mode sets remain in the host tests.

### Spec

Independent review found no implementation discrepancy or scope creep. Pending
runtime evidence was subsequently supplied by both receipts above. Metal
validation skill guided enablement and stderr inspection; code-review skill
separated the two review axes. These are compiler/load/publication probes, not
GPU raytracing correctness. Backend isolation remains PARTIAL; FL12_0/FL12_1
remain FAIL with no advertised capability changes. Next: audit remaining
isolation requirements before selecting the next semantic gate.

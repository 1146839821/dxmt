# Closure infrastructure consolidation

Requested priority: after the AIR indirect VB checkpoint, consolidate closure
infrastructure before continuing feature slices or running full qualification.
Starting branch feat/d3d12-1, HEAD 4dfe46a5. All items below are pending unless
explicitly backed by fresh implementation and evidence. No capability promotion
is authorized by this plan; temporary gates remain experimental opt-ins.

Progress checkpoint: gate classification and raw/structured watchlist wording
are corrected in report schema 2, with 58 Python tests and 16 host tests per
build passing. Optional Mesh/DXR results stay visible without affecting mandatory
FL aggregates. This closes only that classification slice; provenance, capability
isolation and full category registration remain pending. See D3D12_FL12_GATE.md.

Resource audit continuation now separates current AIR implementation/partial
qualification from the MSC gap and preserves historical packed-tail/LOD/native
observations. Four matching-cache compute oracles and two PE-loader trace runs
pass with experimental gates explicitly disabled. Initial cache PE mismatches
were corrected and preliminary results excluded. Actual Unix-image provenance,
runner-level experimental isolation and complete resource matrices remain open.
See D3D12_RESOURCE_AUDIT_CONTINUATION_2026-10-07.md.

Experimental capability environment isolation is implemented in run_fixture:
explicit zero overrides, per-process controlled_environment and report policy.
60 Python tests and actual parent-opt-in/default-child capability probes pass
on both cache Wine runtimes. Production opt-ins remain intact. Loaded-module
enforcement/prefix provenance reconciliation, timestamp and profiling are still
pending; policy metadata alone is not module or GPU conformance proof.

Actual PE observation is now enforced per Wine fixture: import-aware required
modules, exact staged-executable PID, canonical staged paths and matched hashes.
65 Python tests plus both actual Wine capability controls and binary preflights
pass; unused stale prefix DLLs no longer force deployment. Separate mandatory
PE and Unix evidence rows prevent copied-file-only qualification. Actual Unix
image observation and native Windows module evidence are still UNVERIFIED.
See the 2026-10-08 checkpoint in D3D12_FL12_GATE.md.

Unix identity preparation, baseline 1c2d3de1: add a default-off diagnostic unixcall
that receives the Windows PID from PE process attach and reports getpid plus
dladdr of its own native function. Expected effect: correlate the selected PE
process with the actual Unix image, rather than helper dyld logs or installed-file
assumptions. Risk: mixed bridge versions; append the call without renumbering and
require matched cache binaries during validation. Production remains silent when
the diagnostic gate is unset. Validate PID/path/hash correlation and negative
cases before permitting the Unix provenance gate to PASS. This is identity
evidence, not an in-memory attestation or GPU semantic acceptance.

Unix identity implementation checkpoint: both matched cache runtimes now pass
target-correlated PE/Unix provenance with diagnostic unixcall 203, dladdr/getpid
and observed-image file hashes. Both feature-support probes satisfy the three
provenance rows; aggregate FL12 gate remains FAIL. Both full builds, 66 Python
tests and 16 host tests per build pass. Default-off and pure-PE controls pass.
Native Windows/wow64 and in-memory attestation are not claimed. Proceed to the
timestamp oracle and root-feedback performance work before full qualification.

Timestamp checkpoint (2026-10-08): original native GPU resolve remains red.
post-complete/post-cpu diagnostics preserve first failures while testing a later
GPU resolve after observed completion; each passes 600 second-result comparisons
against the actual native samples. CPU resolve is not required for post-complete
success. Sentinel rejection/self-test and API-validation run strengthen the
oracle. This is a continuation candidate, not production closure. Next validate
nonblocking completion-handler ordering and queue-safe consumption; performance
and full Typed/MinMax/Tiled/format/raster qualification remain pending.

## Task analysis

Current implementation has separate DXBC/AIRCONV and DXIL/MSC paths, bounded
readback fixtures, a fail-closed Python gate, historical resource audits, temporary
SM6.6/FL12_0 opt-ins and a standalone Metal timestamp diagnostic. Existing focused
successes do not close the complete matrices. Preserve all prior audit evidence
and runtime-specific limitations instead of overwriting historical conclusions.

Evidence inspected at the starting HEAD:

- tests/dx12/dx12_fl12_gate.py build_report includes mesh/state-object/ray
  failure-injection rows in FL12_0 requirements and backend_isolation aggregation.
  The master prompt separates those optional features from mandatory FL closure.
- Its tiled_raw_structured_buffer watchlist still describes a blanket flat-VA
  sparse ABI gap despite the implemented AIR sideband and targeted GPU probes.
  DXIL/MSC and broader AIR acceptance are still unimplemented/unqualified.
- verify_build insists on matching prefix system32 DLLs as well as the selected
  cache runtime. Consolidation must establish actual module provenance without
  deploying DLLs into the game/prefix or weakening provenance checks.
- docs/D3D12_FL12_0_CLOSURE_STATUS.md retains a stale next-priority reconciliation
  audit and dated pre-raster/indirect claims. New updates must supersede only
  proven scopes; reconciliation evidence is not to be discarded or rerun blindly.
- tests/dx12/metal_timestamp_resolve_probe.m is diagnostic, not FL acceptance.
  Zero timestamp failures remain an unresolved oracle issue.

Hypothesis: explicit mandatory/optional evidence classification, reproducible
provenance and capability isolation, a trustworthy timestamp oracle and measured
root-feedback costs will make full qualification meaningful and prevent false
promotion. Expected effect: accurate gates and reproducible matrices. Risk:
moving optional checks must not hide mandatory failures, and neither experimental
advertisement nor benchmark shortcuts may be treated as conformance evidence.

## Execution order and acceptance evidence

1. Repair gate classification and stale watchlist. Keep optional regressions in
   the report, not mandatory FL aggregates. Add regression tests proving mandatory
   failures cannot pass, optional failures remain visible, and missing/partial
   semantic matrices cannot become PASS from API bits or focused tests alone.
2. Update resource audit/current closure ledger by backend and evidence scope.
   Read the checklist and Round 2 audit completely before changes. Preserve
   packed-tail/array normative boundaries, lifetime limits and native observations.
3. Isolate experimental capabilities in qualification runners. Establish explicit
   default-off controls and separate opt-in reports with observed loaded-module,
   build/runtime/compiler/GPU/OS provenance. Do not mutate the game/prefix.
4. Resolve timestamp oracle from native sampling/resolve and DXMT query evidence.
   A zero/invalid counter is failure or UNVERIFIED, never replaced by an invented
   GPU duration. Distinguish CPU submission timing from GPU execution timing.
5. Benchmark root-feedback transport: same-build controls, feedback vs ordinary,
   root vs table-only, direct/indirect and varying registered-buffer count. Record
   warmup/repetition, correctness, CPU snapshot/lock/fan-out and GPU costs where
   trustworthy. Revisit sort A/B and gate production counters after profiling.
6. Run complete Typed/MinMax/Tiled/format/raster qualification on normal and
   no-private, with explicit DXBC/AIRCONV vs DXIL/MSC coverage and independent
   readback expectations. Register full categories into the repaired gate only
   when coverage and current evidence actually prove them. Native Windows oracle,
   absent hardware/runtime cases and blocked semantics remain explicit gaps.

Self-review and local repo-style commits are required per logical task. No push,
game restart, prefix deployment, or FL12 promotion is implicit in this work.
Fresh ROTTR performance/tessellation acceptance remains a separate outstanding
deliverable and is not satisfied by standalone GPU fixtures.

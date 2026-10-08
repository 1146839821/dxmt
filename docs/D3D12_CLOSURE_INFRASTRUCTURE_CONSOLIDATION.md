# Closure infrastructure consolidation

AIR logic-op implementation checkpoint (2026-10-08): no-private now lowers all
sixteen Boolean operations to ordered UINT attachment reads and fragment output,
with component masks, versioned compiler capability and WOW64 argument conversion.
Both full builds/host suites pass. Final no-private AIR numeric and two-draw
API-validation chains pass 16 each; normal passes both 16-case backends plus
16 AIR chains. Old native safely rejects and ordinary/feedback regressions pass.
See D3D12_AIR_LOGIC_OP_LOWERING.md. MSC still rejects all 16 operations; MSAA,
complete format/MRT/write-mask/raster/native/WOW64 qualification remains open.
OutputMergerLogicOp advertisement is unchanged. This supersedes the blanket AIR
no-private implementation gap below, not the complete closure requirements.

Raster Boolean qualification checkpoint (2026-10-08): all 16 logic ops now have
independent RGBA8_UINT four-channel expectations on both DXBC and DXIL, registered
into the mandatory raster ledger. Normal passes 32/32; no-private rejects all
32 PSOs with E_NOTIMPL, with target PE/Unix provenance matched. Final reruns,
ordinary controls and both host suites (85 gate tests) confirm the result.
See D3D12_LOGIC_OP_QUALIFICATION_2026-10-08.md. No-private LogicOp is a real
implementation gap; full raster/format coverage remains open, not promoted by
normal's Boolean subset pass. No renderer or capability change.

MinMax lifetime oracle checkpoint (2026-10-08): the previous in-flight red
violated both volatile descriptor immutability during submitted execution and
direct-list resubmission rules. The repaired fixture uses distinct lists,
allocators and immutable sampler heap versions, preserving GPU-gated overlap and
completed-list sequential reuse. Both variants pass two deployed repeats each
(220 dispatches), target PE/Unix/compiler provenance and both host suites.
See D3D12_MINMAX_INFLIGHT_CONTRACT_AUDIT.md. No production waits or residency
changes; full qualification remains open. This supersedes the observation-boundary
interpretation below, not the remaining coverage gaps.

Explicit-LOD fractional clamp repair checkpoint (2026-10-08): ordinary MSC
SampleLevel now explicitly applies sampler bias/limits then resource clamp using
the existing private ordinary sampler/texture. A wrong reserved last-mip oracle
is corrected per §5.8.5. Frozen EXE/PE/compiler with old/new native fails/passes;
both variants' 20 Tiled and 52 MinMax executions pass, plus no-private Tiled API
validation and host suites (82 gate tests). See D3D12_FRACTIONAL_CLAMP_REPAIR.md.
Complete Tier/FL promotion is still blocked. Broader deployed-dispatch in-flight
checks fail on old and new native in repeated matched comparisons, exposing a
pre-existing observation-boundary issue. Next prioritize that asynchronous
volatile/private-binding audit; do not claim full lifecycle regression success.

Tiled qualification checkpoint (2026-10-08): standalone and FL12 gates now
share a 20-case provenance-controlled matrix, replacing stale AIR-no-sideband
classification and preventing --only subsets from printing Tier2 PASS. Both
variants return 19 PASS and actual fractional ResourceMinLODClamp FAIL;
no-private API validation reproduces the same semantic failure. A deployed
DXC contrast does not repair it. Both host suites pass (81 gate tests). See
D3D12_TILED_QUALIFICATION_2026-10-08.md. MSC status sideband, packed-tail physical
semantics, complete resource/stage/format/logical-width and native Windows gaps
remain explicit. Next implementation target is the now-reproduced fractional
clamp defect, not relabeling failure as expected rejection. No Tier/FL promotion.

MinMax numeric checkpoint (2026-10-08): a separate 52-case compute runner now
checks actual MIN/MAX values, independently from rejection contracts. Both
variants pass all executions (104), and no-private passes 52 additional
API-validation cases. MSC compiler modules are observed with AIR gates off;
AIR opt-ins are recorded explicitly. Default children clear both AIR gates,
and both 13-case rejection/ordinary controls pass despite parent opt-ins.
Both host suites pass, including 77 gate tests. See
D3D12_MINMAX_QUALIFICATION_2026-10-08.md. Category remains PARTIAL with explicit
anisotropic, format/address/filter, implicit/bias/derivative, graphics/array,
indirect/lifetime, sparse and Windows coverage gaps. No renderer/capability
change or full gate PASS. Next prioritize the stale Tiled runner integration and
fresh sparse semantics, then complete format/raster and remaining MinMax coverage.

Typed qualification checkpoint (2026-10-08): full fresh runs exposed the gate's
missing compiler deployment rather than a new renderer defect. The new explicit
--typed-compiler-dir stages DXC/validator beside the temporary D3D12 module and
requires actual compiler PE path/hash evidence, with inherited override paths
cleared. Both complete Typed runners now pass 1,584 format/view contract cases
(576 numeric GPU cases); undeployed 132/12 DXIL failures remain recorded. A
separate no-private API-validation full DXIL matrix passes. Both MinMax
rejection/control matrices and host suites pass (72 Python gate tests). See
D3D12_TYPED_QUALIFICATION_2026-10-08.md. Additional-format API support remains
FALSE and arbitrary views/lifetimes/stages are not qualified. Next register
real MIN/MAX numeric acceptance separately from rejection controls, then Tiled
and format/raster matrices. No full gate run or FL12 promotion is claimed.

Fixed-root optimization checkpoint (2026-10-08): bounded recording-time numeric
VA unions prune fresh submission snapshots without changing overlap precedence,
live owner resolution or the full GPU-selected/unknown/overflow path. Final-build
paired A/B (12 cases, 6,144 measured submissions) reduces 1,028-entry median CPU
from 918 to 293 us and GPU interval from 290 to 8.8 us; four-entry results are
unchanged at observed granularity. Both full builds, host suites, direct/indirect
sparse and hull/domain regressions, union/owner tests and separate API/provenance
checks pass. See D3D12_ROOT_FEEDBACK_PERFORMANCE.md. Full registry acquisition
still scales with registry size. Next priority is the actual complete mandatory
Typed/MinMax/Tiled/format/raster qualification, not additional benchmark variants;
FL12_0, full timestamp contracts and independent sort/counter audit remain open.

Root-feedback performance checkpoint (2026-10-08): frozen-build paired workloads
now expose costs hidden by caller-only timing. At 1,028 registered buffers,
feedback median process CPU/GPU interval is about 918/290 us per submission
versus control 273/9 us, while caller enqueue remains around 10 us. Twenty-two
timing cases validate GPU output and fresh queries; separate worker-accounting,
provenance, no-private API and sparse/remap correctness checks pass. No production
instrumentation was added. See D3D12_ROOT_FEEDBACK_PERFORMANCE.md for raw evidence,
variance, timing scope and remaining attribution/qualification limits. Address
the full-registry fixed-root cost without weakening indirect/remap correctness;
full mandatory matrices and the independent sort/counter audit remain open.

Latest checkpoint (2026-10-08): ordered production worker and on-demand timestamp
segments turn the same original D3D12 oracle from 188/200 failures to 0/200.
Enhanced GPU consumer/CPU gate/list Reset/canary runs pass repeatedly on both
variants, alongside 64-resolve pool pressure, asynchronous error notification,
clock calibration, allocator and resource/CopyTiles regressions. Both full builds,
16 host tests per build and 68 gate Python tests pass. The new bounded timestamp
gate probe passes with actual PE/Unix provenance; it does not close full query
qualification or promote FL12_0. See D3D12_TIMESTAMP_RESOLVE_ORDER.md. Next move
to root-feedback CPU/GPU profiling, including the new worker rather than just
caller timings; full Typed/MinMax/Tiled/format/raster qualification remains open.

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

Production timestamp seam checkpoint (2026-10-08): ResolveTimestamp is now a
dedicated recording/replay encoder with owned sample/destination native objects
and explicit allocator destruction. Both full builds and 16 host tests per build
pass. A shader-free real D3D12 diagnostic reproduces zero-end timestamps at
123/200 (normal) and 76/200 (no-private/API validation), preserving canaries and
ordinary copies. This is a high-reproduction production oracle, not a fix or a
failure-rate A/B. The next change must introduce ordered deferred segment commit
across all queue entry points while retaining allocator completion until the
final segment. Timestamp closure and the remaining performance/qualification
requirements are still open. See D3D12_TIMESTAMP_RESOLVE_ORDER.md.

Completion-handler diagnostic checkpoint (2026-10-08): native callbacks submit a
later GPU resolve without callback waits or CPU counter resolution. Fenced GPU
copy consumption and exact native-pair comparisons pass 800 iterations, then a
final 200-iteration API-validation run; original failures remain red (17 and 2,
respectively). Strict native build/self-test pass. The collector still waits,
so asynchronous production submission is not qualified. See
D3D12_TIMESTAMP_RESOLVE_ORDER.md for evidence and limits. Next advance the
production queue-safe continuation, not more diagnostic permutations; timestamp
closure, root-feedback performance and full resource/raster qualification remain
open.

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

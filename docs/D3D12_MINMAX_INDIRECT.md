# MinMax indirect compute integration

## GPU-produced argument/count Task Analysis

### Current Branch / Baseline / Local Commits

`feat/d3d12-1`; read-only `origin/feat/d3d12` at `e147c710`;
110 local commits since that baseline; current HEAD `9a95568`. Worktree clean.
Previous goal turn made production progress, not a wait or status-only turn.

### Current State / Existing Implementation / Relevant Files

The selected MinMax variant supports reflected indirect root updates. ExecuteIndirect
already supplies GPU argument/count addresses, and the shared GPU resolver clamps
count to MaxCommandCount. Existing compute barriers and registered-buffer snapshots
cover producer visibility and root-address residency. Relevant files: command list,
signature, allocator, queue, private dispatch materialization and minmax diagnostic.

### Existing Tests / D3D12 Contract / Missing Pieces

Existing two-command probe uses CPU-written upload arguments and no count buffer.
Missing evidence: an actual GPU producer, zero/partial/over-limit counts, nonzero
argument/count offsets and repeated closed-list submission. The consumer must use
GPU-written bytes with no CPU readback/substitution before ExecuteIndirect.

### DXBC / AIRCONV Impact / DXIL / MSC Impact / Shared Runtime Impact

Use a DXBC SM5 producer through AIRCONV and the existing DXIL MinMax MSC consumer;
do not reroute either backend. Exercise the shared barrier/resolver/count path;
only change production code if source/readback evidence exposes a real defect.

### Hypothesis / Evidence / Expected Effect

The existing path is sufficient after the prior private-template integration.
Source already forwards GPU addresses and clamps counts. A bounded independent
readback oracle should remove uncertainty and identify the next real production gap.

### Risks / Minimal Implementation Plan

Avoid an ordinary producer accidentally selecting MinMax, stale root/PSO bindings,
zero-count false positives, or CPU staging mistaken for GPU-produced arguments.
Extend only the existing focused root diagnostic with a GPU producer/count mode.

### Validation Plan / Capability Impact

Reconfigure both builds, build the diagnostic and full default targets, verify
native library hashes, run GPU count 0/1/7 with MaxCommandCount 2 and unchanged
CPU-argument regression. Verify producer bytes/count plus independent MIN/MAX and
sentinel outputs, and recorded template immutability. No full matrix, capability
promotion, game/performance/tessellation acceptance or timestamp fix.

## Root-updating task analysis

- Hypothesis: the existing per-command MSC resolver can patch a submission-owned
  MinMax template without changing its hidden bindings or descriptor snapshots.
- Evidence: typed-origin dispatch already clones the resolver payload and allocates
  writable per-command TLABs; MinMax materialization owns its hidden tables but
  currently lacks those payload fields and replay relocation.
- Expected effect: share replay relocation and admit reflected constants/CBV/SRV/
  UAV updates using the MinMax compiler root, preserving closed-list immutability.
- Risk: template/PSO mismatch, allocation overflow, stale root values, lost hidden
  pointers or GPU-selected buffer lifetime. Keep combined private variants rejected.
- Validation: both configured full builds; two-command actual MIN/MAX readback with
  partial constants and distinct CBV/SRV/UAV addresses; existing focused regressions.
  This is not full MinMax or FL12_0 qualification.

## Task Analysis

- Hypothesis: non-root-updating indirect dispatch can inherit the submission-
  owned MinMax TLAB and existing snapshot/resource lifetime contract.
- Evidence: PreDispatch rejects every selected indirect variant; the allocator
  restores the application PSO after resolving ICB commands. Direct MinMax
  materialization already produces the private TLAB and declares resources.
- Expected effect: route non-updating ExecuteIndirect through that same binding,
  restoring MinMax PSO and reflected threadgroup dimensions after the resolver.
- Risk: wrong PSO/TLAB pairing; static descriptor rereads; root-updating commands
  falsely admitted without per-command private state. Keep those fail-closed.
- Validation: both full builds, actual indirect reduction readback, existing
  direct/static/volatile/lifetime regressions. No full matrix or FL promotion.

Root-updating MinMax indirect remains a required subsequent implementation,
not an excluded final requirement. Timestamp investigation remains independent.

## Result

Non-updating indirect DISPATCH now inherits the submission-owned private MinMax
TLAB, and the resolver restores the selected MinMax PSO and reflected threadgroup
dimensions. Direct and indirect calls share descriptor snapshots, residency and
completion lifetime; no new descriptor reread or shader backend fallback.

Both configurations reconfigured, explicitly built the optional diagnostic, then
completed full default builds. Four fresh final executions (one/two sampler pairs
times both builds) pass actual indirect MIN/MAX results 16/240, distinguishing
ordinary linear 128. Each resets the output to a sentinel before execution and
checks untouched output where applicable. Existing eleven-root direct/static/
volatile/RS1.0, in-flight and private/ordinary restoration regressions also pass.

The new indirect GPU probe runs in mode 0 only: these are not static or volatile
indirect acceptance claims. Initial wrong-DXC-path and stale optional-executable
failures were corrected, not counted as new test evidence. Preliminary ordinary
linear indirect passes were superseded by the real reduction oracle.

Evidence: `/Users/zhangbo/.cache/dxmt-minmax-indirect.IfKxx9/*-reduction-{1,2}.log`.
Root-updating indirect remains explicitly rejected; broader indirect command/count
and descriptor-update acceptance remains open. No full MinMax/default qualification,
FL12_0 promotion, timestamp repair, game benchmark or tessellation acceptance.

Both host suites pass 5/5. Main-agent Standards/Spec review checked selected-PSO/
reflection pairing, inherited private TLAB preservation, unchanged snapshots and
root-update rejection. No independent reviewer was available. MSC integration
guidance informed PSO/reflection/TLAB matching; compilation remains through the
existing validated selected-DXC/MSC path. Diff whitespace check passed.

## Root-updating result (supersedes the rejection boundary above)

MSC MinMax indirect DISPATCH now admits reflected root constants, CBV, buffer SRV
and buffer UAV updates. The recording payload/template remain immutable; each
submission owns its cloned resolver payload, patched private template and writable
per-command TLAB storage. The existing GPU resolver preserves hidden MinMax/table
pointers while updating application roots. Typed-origin and MinMax share the replay
relocation map; submission binding vectors retain both buffer kinds to completion.
Existing registered-buffer snapshots cover GPU-selected root addresses. Combined
typed-origin/MinMax variants and skipped-binding routes remain rejected.

Both configurations completed reconfiguration, optional diagnostic builds and full
default builds. Final focused runs use native libraries whose hashes match current
build outputs. Eight process executions pass: two root-update probes, two existing
typed-origin multi-command probes, and four one/two-pair MinMax regressions. Each
root probe submits the same closed list twice and checks two commands updating a
partial constant plus distinct CBV/SRV/UAV addresses: 86/310 and 98/322, unchanged
constant 31, untouched sentinel lanes and byte-identical recorded payload/template.
The root-update fixture is RS1.1 with static descriptors and dynamic MIN/MAX sampler
objects. It does not qualify volatile/static-sampler/RS1.0 root-updating indirect,
GPU-produced argument/count buffers, overlapping submissions or root-state reset.

Evidence: `/Users/zhangbo/.cache/dxmt-minmax-indirect-roots.zR4thc/final-*.log`.
An initially reused native overlay had a different hash and rejected the typed
fixture before GPU execution. It was replaced by matching existing task-owned
overlays; preliminary runs are not the final evidence. Both host suites passed 5/5.
No game/prefix DLL deployment, Steam changes, game/performance/tessellation results
or capability promotion. Timestamp failure remains independent and unresolved.

Main-agent Standards/Spec self-review against `3db72b6` checked layout validation,
allocation overflow, private-pointer preservation, buffer read/write residency,
completion ownership, unchanged static/live snapshot behavior and failure paths.
No blocking findings; no independent reviewer available. MSC compilation/integration
guidance kept compiler-root reflection, selected PSO and per-command TLAB matched.
Whitespace validation passed. Full indirect and MinMax qualification remains open.

## GPU-produced argument/count Task Result

### Branch / Baseline / Local Commit / Changed Files

`feat/d3d12-1`; read-only `origin/feat/d3d12`. Local commit message:
`test(d3d12): cover GPU-generated MinMax indirect counts` (hash reported in handoff).
Changes: `tests/dx12/dx12_minmax_dispatch.cpp`, this document and closure status.

### Implementation / Backend and Shared Runtime Impact

Added `--gpu-count` to the existing focused root diagnostic. Its SM5 producer is
compiled to DXBC and asserted to select AIRCONV; the consumer is asserted MSC.
No environment toggling or backend substitution between producer and consumer.
Producer writes commands at GPU offset 16 and count at GPU offset 112. Both
ExecuteIndirect inputs are the DEFAULT buffer, never CPU staging/readback.
No production defect was found: the existing resolver/barrier/count path works
with the previously implemented private TLAB integration. Production code unchanged.

### Tests Added / Tests Run / Runtime Results

Counts 0/1/7 with MaxCommandCount 2 are written by GPU on three submissions of
the same closed list. The argument buffer is cleared before each producer, outputs
are reset to sentinels, and resource states return to their starting states.
Count 0 leaves all output untouched; count 1 writes only 86/310; count 7 writes
86/310 and 98/322 and cannot execute beyond the two allocated commands. Full
command bytes and count are checked by GPU readback after the consumer; the
recorded resolver payload/template stay byte-identical after each submission.

Both configurations reconfigured, built the optional target and completed full
default builds before staging. Native-library hashes match their current outputs.
Four final process executions pass: both GPU-count probes (six GPU submissions)
and both unchanged CPU-root probes (four GPU submissions). Both host suites pass
5/5. Evidence: `/Users/zhangbo/.cache/dxmt-minmax-gpu-count.omOFHE/`.

### Self-review / Known Limitations / Capability Status

Main-agent Standards/Spec review against `9a95568`: no blocking findings. Checked
compiler/blob lifetime, repeated-list resource states, nonzero offsets, effective
count clipping, independent producer byte/count and reduction/sentinel oracles,
and explicit backend isolation. No independent reviewer available. MSC guidance
informed the producer/consumer binding and unchanged reflected private TLAB checks.
Diff whitespace validation passed.

Status: DXMT_LOCAL_PASS for this bounded static-descriptor RS1.1 compute contract;
full MinMax capability PARTIAL. This supersedes only the GPU-produced argument/
count evidence gap above, not broader descriptor modes, cross-queue/overlap,
GPU-selected late resource lifetime, predication or root-state-reset qualification.
No Metal validation run, native Windows oracle or full matrix in this task.

### Feature Level Impact / Git Status / Push Status / Next Recommended Task

FL11_1 unchanged; FL12_0 and FL12_1 not promoted. No timestamp fix, game launch,
benchmark or tessellation acceptance. Only task-owned cache artifacts staged;
no game/prefix DLL deployment or Steam/wineserver changes. Commit locally after
self-review, keep unrelated work intact. Push status: NOT PUSHED.

Next: implement the remaining MinMax graphics binding/dispatch integration rather
than expanding this compute count probe into the full acceptance matrix.

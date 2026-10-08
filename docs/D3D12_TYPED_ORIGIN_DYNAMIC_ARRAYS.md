# Typed origin dynamic finite arrays

## Runtime Task Analysis

- Hypothesis: existing Null snapshots supply zero-count state, and dynamic
  root constants select the correct finite SRV/UAV origins without new replay.
- Evidence: snapshot validation accepts Null and materialization writes zero
  state. No real dynamically selected array origin readback has been performed.
- Expected effect: accept initialized dynamic ranges and unused Null candidates.
- Risk: testing only slot 0 or identical views hides dynamic state selection.
- Validation: select slots 0/1 via a root constant with differing SRV/UAV
  origins; compare full-buffer results, out-of-view read/write guards, and
  partially uninitialized arrays through static and volatile range paths.

## Task Analysis

- Hypothesis: finite legacy binding ranges can select private per-slot origin
  records at runtime while preserving typed operations and the resource class.
- Evidence: constant array slot mapping works end to end; current resolver
  rejects nonconstant register operands despite known finite range identities.
- Expected effect: uniform dynamic SRV/UAV array indices use their own view
  origins/counts; all potentially accessed slots retain submission state.
- Risk: dynamic CBV index overflow, reading private/native descriptor ranges
  out of bounds, losing bounds guards after CFG insertion and compaction.
- Validation: real DXC dynamic array fixture, native/ABI agreement, LLVM/DXC
  verification and both full builds. Runtime dynamic origin readbacks and
  partially initialized dynamic ranges remain required for complete acceptance.

## Compiler implementation result

Finite legacy uniform dynamic register operands now select their corresponding
origin/count state. All candidate slots of a dynamic access survive compaction;
constant accesses still keep only their actual slot. The selected private CBV
index is clamped to record zero when outside the finite range, and an independent
handle-valid predicate suppresses the typed load/store/atomic. Original native
handle indices are also clamped to the application range's first register before
handle creation, preventing an out-of-range descriptor lookup before the guard.
Lowering cache version is 3. Nonuniform/modern/unbounded handle flows remain open.

The real selected-DXC fixture dynamically indexes Inputs at t3 and writes u6.
Native/ABI checks retain t3/t4/u6 and produce valid guarded IR. Both builds emit
identical dynamic IR. The rebuilt container passed full selected-DXC assembly
and validation. Existing constant-array IR remains byte-identical to the prior
validated output; no redundant dynamic guard is added to constant accesses.
Both reconfigured full builds and host suites (4/4 each) passed.

Main-agent self-review checked unsigned range subtraction, safe private and
native indices, contiguous remapped candidate records, original index dominance,
typed operation guards and retained rejection/publication contracts. LLVM skill
informed structural verification. This is production compiler progress, not
runtime dynamic GPU acceptance, partially initialized dynamic-range admission,
MSC conversion, capability/default enablement, independent review or FL12_0.
Evidence: `/Users/zhangbo/.cache/dxmt-typed-dynamic.ViDLcN`.

## Dynamic runtime acceptance

The production path accepts finite uniform dynamic SRV/UAV indices and partially
uninitialized ranges without additional replay code. The new original SM6.0
fixture selects both Inputs/Outputs via b0 root constants; private origin state
selection is therefore genuinely runtime-dependent rather than a constant slot.

Both builds passed slot 0 and slot 1 selection with RS1.1 static and RS1.0
volatile ranges, plus both range modes with unused slot 0 left uninitialized
(twelve final dynamic dispatches). SRV origins differ (0/1) and UAV origins
differ (4/3). Results are respectively 28 at physical output word 4 and 58 at
word 3, with every other word unchanged. Out-of-view reads contribute zero and
out-of-view writes preserve neighboring sentinel words. Both builds also passed
static and volatile constant-array regressions (four dispatches).

Fresh task-local PE/Unix loads were confirmed. Original DXIL is prepared,
validated and MSC-compiled by the actual production dispatch path using the
selected DXC directory; no game/prefix libraries were replaced. Both
reconfigured full builds and host suites (4/4 each) passed.

Main-agent self-review checked exact probe mode parsing, root-constant layout,
distinct slot oracles, all-word comparison, unused Null candidates and resource
lifetime through GPU completion. MSC integration skill informed layout/lifetime
checks. This is focused compute uniform dynamic-array acceptance, not nonuniform
or modern typed handles, unbounded arrays, complete matrices, default capability
enablement, game performance, independent review or FL12_0.
Evidence: `/Users/zhangbo/.cache/dxmt-typed-dynamic-gpu.Sng08G`.

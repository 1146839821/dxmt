# Typed origin dynamic finite arrays

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

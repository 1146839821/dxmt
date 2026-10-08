# Typed buffer origin finite arrays

## Runtime Task Analysis

- Hypothesis: existing per-register root resolution transports compact array
  records to distinct origin/count state without requiring unused slots valid.
- Evidence: compiler reports only t4/u6; no array GPU origin readback exists.
- Expected effect: validate the real dispatch or repair any runtime rejection.
- Risk: wrong slot/base/count can look correct with aligned or repeated data.
- Validation: t4 FirstElement 1/count 1 and u6 FirstElement 3/count 1; input
  out-of-view read must return zero and output out-of-view write must preserve
  sentinel. Compare every output word; unused t3/u5 slots stay uninitialized.

## Task Analysis

- Hypothesis: finite typed SRV/UAV ranges can expand into the existing per-slot
  origin records; constant handle index chooses the corresponding record.
- Evidence: lowering rejects count != 1 and requires index == base register.
  Root location and submission state already consume register identities.
- Expected effect: preserve distinct FirstElement/NumElements adjustment for
  each finite array slot without replacing native typed operations.
- Risk: range overflow, conflating slots, incorrect private state indices,
  publishing partial bindings for invalid indices.
- Validation: real DXC arrays with nonzero register bases, native/ABI agreement,
  LLVM validity, both full builds and focused compiler validation. Runtime
  array origin readbacks remain required before broad acceptance/default enable.

Self-review requirement: array declaration alone must not force unused descriptor
slots to be valid. Compact private state to records with actual typed accesses,
retaining declaration order for the accessed records and remapping access indices.

## Compiler implementation result

Finite legacy typed ranges now expand within the existing 64-slot contract;
constant indices resolve to their exact slot with widened range bounds checks.
The private origin state is compacted to actual accesses before IR insertion.
Original application metadata and typed load/store/atomic operations remain
unchanged; each operation receives the appropriate remapped origin record.
Lowering cache version is now 2 to separate the changed state-index contract.

Both reconfigured full builds and host suites (4/4 each) passed. The real SM6.0
HLSL fixture declares Inputs[2] at t3 and Outputs[2] at u5, accesses the second
slot of both, and produces precisely two bindings: SRV t4 and UAV u6. Native
and exported ABI preparation agree on IR and records, including undersized/
overlapping output-buffer rejection. Both builds emit identical transformed IR.
Selected DXC assembly and full container validation passed. A real dynamically
indexed variant rejects at the native preparation boundary in the normal build.

Main-agent self-review checked array/register arithmetic, record compaction,
access remapping, untouched typed operations, cache separation and fail-closed
publication. LLVM skill informed structural verification. No MSC compilation,
runtime GPU array-origin acceptance, default enablement, feature promotion or
independent-review claim in this checkpoint. Runtime FirstElement/NumElements
readbacks are required next. Evidence:
`/Users/zhangbo/.cache/dxmt-typed-origin-array.O67J56`.

## Runtime focused acceptance

The production array origin path passes actual GPU full-buffer readback in both
normal and no-private builds, for RS1.1 static ranges and RS1.0 volatile ranges
(four final dispatches). No extra production replay path was required.

The original shader accesses only Inputs[1]/Outputs[1] at t4/u6. Heap entries
for unused t3/u5 remain uninitialized. SRV FirstElement=1/NumElements=1 reads 41;
its next-element read contributes zero despite an underlying value of 99.
UAV FirstElement=3/NumElements=1 receives 58 at physical word 3. A write to view
element 1 must not modify physical word 4. Every word in the 256-byte output is
compared, including distinct original values 11/41/99 and all sentinel words.

Fresh task-owned runtime/app directories loaded the current PE and Unix DLLs;
the selected DXC directory enabled production preparation/validation and MSC
conversion of the original container. Game/prefix DLLs were not replaced.
Both reconfigured full builds and host suites (4/4 each) passed.

Main-agent self-review checked independent first/count oracles, uninitialized
unused slots, heap offsets, GPU completion before readback, all-word comparison
and both range-flag paths. MSC integration skill informed table layout and
resource-lifetime checks. This completes focused constant finite-array origin
acceptance, not dynamic indices, full format matrices, capability default
enablement, game acceptance or FL12_0. Evidence:
`/Users/zhangbo/.cache/dxmt-typed-array-runtime.vY8xxz`.

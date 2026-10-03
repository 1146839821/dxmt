# Typed buffer origin finite arrays

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

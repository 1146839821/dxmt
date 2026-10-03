# MinMax indirect compute integration

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

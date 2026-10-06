# Task Analysis

Baseline: feat/d3d12-1 at 176d008, clean. The prior logging macro repair
prevents formatting disabled DEBUG messages but not work surrounding those
messages in GetMSCDescriptorTableAddress and AddTextureSRV.

Hypothesis: disabled descriptor diagnostics still incur atomics and table
walks. Evidence: both trace counters increment before DEBUG tests its level;
the first 128 table queries also read up to four descriptor entries.
Expected effect: skip those diagnostics entirely at Info and above.
Risk: diagnostic sequence numbers now count only enabled diagnostic work;
descriptor address calculation and writes must remain unconditional.
Validation: inspect gates, build both variants and run existing logging units.

# Task Result

Both diagnostic blocks are guarded by the same immutable logger threshold as
DEBUG. Descriptor writes, resource ownership and returned addresses remain
outside the guards. Debug/Trace retain their existing 128-message budget.
This is not an assertion that all residency counters are gated, and not a
performance measurement or FL capability change.

Both configurations were reconfigured before full builds; normal and no-private
builds completed successfully. Existing six logging-level units passed in each
configuration. They validate threshold/macro behavior, not direct interception
of these descriptor counters. Self-review checked both counter increments and
the table walk are inside the guards, while return-address calculation and
SetMSCDescriptor remain outside. git diff --check passed. No GPU/game test,
new residency-counter coverage or FPS claim. Evidence retained in
/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE/descriptor-gate-*.log.

The next production binding gap is typed graphics indirect: PreDraw rejects
typed-origin SkipResourceBinding/AllowTypedOrigin combinations. It must gain
submission-owned record/TLAB handling without relaxing rejection before that
path exists. Broader FL12_0 qualification remains open.

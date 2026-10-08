# Nonuniform finite typed arrays

## Task Analysis

- Hypothesis: existing per-lane dynamic origin selection already carries the
  correct state; preserving DXIL's nonuniform flag admits divergent handles.
- Evidence: the lowering only rejects the true flag; it does not depend on
  uniform values for safe native indices, CBV state indices or operation guards.
- Expected effect: finite legacy nonuniform typed SRV/UAV arrays retain native
  nonuniform metadata and per-lane origin/count handling.
- Risk: converter scalarization or shared state selection across lanes.
- Validation: a two-lane group uses NonUniformResourceIndex, distinct view
  origins and full-buffer readback through static/volatile paths; native/DXC
  validity, both full builds and uniform regression. No capability promotion.

## Task Result

Legacy finite typed ranges now admit constant true nonuniform flags without
rewriting them. Existing lane-local safe handle/state indices and operation
guards are reused; lowering cache version advances to 4.

- Both native lowerers produce identical LLVM-verified IR and four binding
  records (t3/t4/u5/u6); original and lowered SRV/UAV handles retain `i1 true`.
- Selected DXC validates the regenerated complete container.
- Both full default builds succeed after reconfiguration; host tests pass
  4/4 in each variant, and `git diff --check` passes.
- Four actual nonuniform GPU dispatches pass: normal/no-private, each with
  RS1.0 volatile and RS1.1 static ranges. Full-buffer oracles require word3=58
  and word4=28. The slot1 out-of-view store overlaps slot0's legitimate output,
  so preserving 28 also checks that the guard protects another lane's result.
- Four uniform dynamic slot0/slot1 GPU regressions pass across both variants.
- Fresh load logs identify the task-owned D3D12 DLL and Unix winemetal library.
  Evidence: `/Users/zhangbo/.cache/dxmt-typed-nonuniform.9kLq9A`.

Standards self-review: bounded change, existing selection path reused, no new
backend fallback. Spec self-review: flag preservation, per-lane state and full
sentinel oracle confirmed. Main-agent review only; independent agents were not
available. Modern/heap/unbounded handles, nonuniform atomics, default enablement
and full format/lifetime matrices remain unverified. No FL/SM promotion, game
deployment, tessellation acceptance or performance benchmark.

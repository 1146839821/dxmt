# Typed-origin submission binding

## Task Analysis

- Hypothesis: immutable recording recipes plus independent per-execution native
  bindings preserve static observation and volatile submission-time updates without
  modifying allocator-owned commands or metadata already referenced by the GPU.
- Evidence: queue submission objects survive until command-buffer completion;
  existing descriptor snapshots retain coherent payloads/references under a lock.
- Expected effect: direct compute dispatch can use the prepared origin PSO and
  private tables, with logical origin/count records matching those descriptors.
- Risk: in-flight list reuse, wrong table extents, stale native allocations, loss
  of flags, restoring ordinary PSO/TLAB state and cloned command ABI correctness.
- Validation: both builds, focused production direct-dispatch readback, static/live
  observation and repeated execution checks; self-review before local commit.

The path is explicitly selected by `DXMT_TYPED_ORIGIN_DXC_DIRECTORY` (absolute
Windows directory containing the chosen DXC compiler/validator). Default dispatch
is unchanged. Unsupported shader/root preparation fails recording, without backend
fallback. Indirect-dispatch integration and broad shader/format matrices remain open.

Static resource-table slots are captured once while recording and never enter
PendingDescriptorUse. Unique volatile slots are resolved under the heap lock at
submission; native allocation/replay occurs outside it. Each execution owns a new
buffer containing reflected TLAB, 16-byte origin/count records and private resource
tables. Its native references and allocation snapshots survive GPU completion via
the queue submission. Allocator-owned commands and prior binding buffers are never
patched. The enabled path clones its compute command chain; ordinary replay remains
one existing bulk call. This has not been profiled and is not a performance claim.

## Task Result

Implemented the opt-in production direct-compute replay path. Static snapshots
retain the exact native texture view and allocation generation; volatile snapshots
are resolved once per execution. Invalid static typed bindings fail recording;
resolved texture-load clamp restrictions are checked at the correct observation
time. Ordinary and indirect paths retain their existing state and routing.

Current-build validation (2026-10-02):

- Normal and no-private builds succeeded; Meson tests passed 3/3 in each.
- Focused R16_FLOAT / FirstElement=4 GPU contracts passed 7/7 in each build:
  static, copied static, static-overwrite negative oracle, RS1.0 live update,
  initially unavailable live descriptor, repeated in-flight execution, and invalid
  static descriptor rejection. Output words and backing-buffer bytes are checked.
- Repeated execution uses a GPU launch gate, distinct origins/data/output buffers,
  and both output readbacks, so shared-generation overwrite is observable.
- Default aligned DXIL dispatch passed with the opt-in directory unset.
- Runtime receipts: `build/typed-origin-dispatch-final.log`,
  `build-no-private/typed-origin-dispatch-final.log`, and
  `build/typed-origin-default-regression.log`. Loader receipts identify staged
  native D3D12 and matching isolated winemetal/ntdll runtime paths.
- Standards and Spec independent final-delta reviews found no actionable issues;
  their review was source-only. `git diff --check` passed.

This is seven focused contracts, not the complete typed-UAV format matrix. Mixed
texture-clamp/sampling regression, indirect dispatch, modern/dynamic handle
provenance and broad formats remain open. No full Metal validation, game/tessellation
acceptance, profiling, capability promotion or installed-game DLL deployment.

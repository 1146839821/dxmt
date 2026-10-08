# Modern typed origin handles

## Task Analysis

- Hypothesis: finite modern binding/annotation chains can feed existing typed
  origin selection without changing shader model or handle ABI.
- Evidence: MinMax already validates modern signatures and constructs annotated
  private CBVs; typed origin currently rejects all modern handles.
- Expected effect: native modern typed accesses reuse per-lane origin guards.
- Risk: annotation mismatch, ambiguous ranges, unsafe raw-handle users and
  incorrect private CBV properties; keep unsupported provenance fail-closed.
- Validation: native IR verification, selected DXC complete-container validation,
  both builds, focused actual dispatch and legacy regression before admission.

## Task Result

Finite `createHandleFromBinding` / `annotateHandle` typed SRV/UAV chains now
resolve against class/space/register interval metadata. Component annotations
must match metadata; raw typed users must be consistently annotated. Multiple
identical annotations on one raw handle are supported. Safe-index insertion
precedes the raw binding; existing per-access origin/count guards stay lane-local.
Private CBVs use modern handles with kind13 and the state byte size. Preparation
accepts validated SM6.0 through SM6.6 without downgrading; cache version is 5.

Evidence: `/Users/zhangbo/.cache/dxmt-typed-modern.FpJDbH`.

- Both reconfigured full builds and host suites (4/4 each) pass.
- Native lowerers agree byte-for-byte on IR/bindings. Selected DXC validates the
  regenerated complete SM6.6 container; final IR matches that validated output.
- Both native tools reject component mismatch, raw unannotated typed access
  and binding/metadata interval mismatch: three negative cases per build.
- Final libraries pass four actual SM6.6 divergent two-lane GPU readbacks:
  static/volatile in normal/no-private builds, with complete-buffer oracles.
  Four legacy SM6.0 GPU regressions pass. Initial fresh load logs identify
  task-owned D3D12/Unix libraries. No game DLL or prefix file replacement.

Standards self-review: bounded change, existing guards reused, optional fixture.
Spec self-review: no backend fallback, shader-model or nonuniform-flag rewriting;
provenance negatives and numeric readbacks pass. Main-agent review only;
independent reviewers unavailable. Heap/unbounded/aliased handles, full
format/operation/lifetime acceptance and default enablement remain open.
No capability promotion, fresh game benchmark or tessellation acceptance.

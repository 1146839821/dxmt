# UINT4 offline typed-origin lowering

## Task Analysis

Current branch: `feat/d3d12-1`; task baseline: `7c4f3d5`, clean worktree.
Remote baseline: `origin/feat/d3d12`; merge base `85bb2dd`.
Recent local commits add bounded scalar UINT/SINT/FLOAT/UNORM lowering and
readback, without production enablement. Existing adapter and native probe are
in `tests/dx12/`; production runtime and AIRCONV remain out of scope.

Hypothesis: UINT4 accesses can preserve the existing aggregate load and full-mask
store while shifting coordinates in texels, not scalar components. Evidence:
fresh `typed_uav_4_0.cso` dump has `vector<unsigned int, 4> ` class, component 5,
four ResRet component extracts, `shl 2` and `or 1/2/3` scalar output coordinates.
Expected effect: opt-in offline UINT4 load/copy-store with hidden origin/count
CBV, reusing reflected binding and residency. Missing pieces: vector resource
classification, bounded extract widths, output arithmetic and multi-lane oracle.
Risks: confusing texel/byte/lane units; widening scalar/status acceptance;
lane swaps hidden by repeated values; accepting vector atomics or other types.

Minimal implementation: UINT4 only; retain FLOAT4/SINT4/UNORM4 rejection and
scalar UINT output. Native RGBA8/16_UINT cover padding, distinct lane values,
logical OOB, zero count, whole-buffer sentinels and original-shader controls.
RGBA32_UINT and partial masks remain outside this readback milestone.
Validation: reconfigure both builds, parser and Meson tests, complete DXC-valid
containers, native GPU readback with API/shader validation and scalar regression,
then independent standards/specification reviews and local commit, never push.
D3D12 contract: element origins/counts are texels; OOB load returns zero and OOB
store must not modify memory. Native evidence is not production acceptance.
DXBC/AIRCONV, shared runtime, capabilities and FL11_1/12_0/12_1 are unchanged.
LLVM/MSC compilation/integration/validation skills constrain IR and reflected ABI.

## Task Result

### Branch / Baseline / Changed Files

`feat/d3d12-1`, task baseline `7c4f3d5`, remote baseline `origin/feat/d3d12`.
This document and three test files: `dxil_origin_transform.hpp`,
`dxil_origin_transform_test.cpp`, `msc_typed_buffer_padding_probe.c`.
Local commit contains this completed result; its hash is reported at handoff.

### Implementation / Tests Added

UINT4 accepts the exact DXC vector class spelling and component 5, retaining
scalar UINT output. Aggregate guarded load/zero merge and full-mask copy-store
reuse existing lowering; origins/counts remain texels. Extract width is tracked
per load, not inferred from another vector declaration in the module. UINT4
permits lanes 0–3 and bounded output `shl 2` / `or 1/2/3`; scalar lane 1 and all
status lane 4 accesses remain rejected. Vector atomics are explicitly rejected.
Added parser positives/negatives include SRV writes, partial masks, component
mismatch, vector outputs, incorrect arithmetic and other vector component types.

The native probe now distinguishes channels from element bytes. RGBA8/16_UINT
uses four distinct cyclic lane values, exact scalar output comparisons and full
backing comparison for copy-store and sentinels. Existing scalar formats retain
one channel explicitly; the final builds introduce no compiler warnings.

### Tests Run / Runtime Results

Fresh results for normal and no-private build configurations:

- Reconfiguration, target compilation and Meson: 3/3 tests per build;
  parser 79/79 per build. Parser fixtures are not DXIL/GPU validation.
- Complete DXC input/output container contracts: 32/32 total, eleven accepted
  and five rejected per build. UINT4 UAV/SRV outputs validate before native
  compilation; unchanged FLOAT4/SINT4/UNORM4, SNORM and SINT atomic inputs reject
  with no output file. All input hashes stay unchanged.
- Native GPU positive cases: 1470/1470 per build, including 280 new UINT4 cases
  (140 per RGBA format) and 1190 scalar UINT/SINT/FLOAT/UNORM regressions.
  Both UAV/SRV paths cover origins 0/1/4/257/260, logical counts
  8/0/1/3/4/5/7 and both MSC bounds-check settings. Four lanes are compared,
  including zero-count/OOB aggregate zero and suppressed OOB copy stores.
- Original UINT4 shader controls: 40 cases per build. All 24 aligned cases
  match; all 16 padded cases mismatch, with successful GPU completion required.
  Positive/control receipts detect no Metal validation or pipeline errors.
- Unchanged production prefix, R8G8B8A8_UINT: DXIL FirstElement 1 rejects during
  recording (`MSC typed-buffer view unavailable`, `Close` failure); aligned DXIL
  FirstElement 4 and DXBC FirstElement 1 pass GPU readback. All three expected
  contracts pass. No production DLL replacement or deployment was performed.

Ignored receipts: `build/vector-origin-containers.json`,
`build/vector-origin-native-final.json`, and
`build/vector-origin-production-final.json`, including command arrays and logs.
Both native builds run with `MTL_DEBUG_LAYER=1`, `MTL_SHADER_VALIDATION=1` and
`MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1`. Compile the existing
`dxil_roundtrip`, `dxil_origin_transform_test`, and
`msc_typed_buffer_padding_probe` targets after `meson setup --reconfigure`.
Lower existing `typed_uav_4_0.cso` / `typed_uav_srv_4.cso` via
`--lower-typed-origin` with a new output path and absolute Windows DXC directory.
Run probe modes `--origin-cbv-rgba8uint-oob` and
`--origin-cbv-rgba16uint-oob`; original input shaders with corresponding modes
without `-oob` provide the controls.

### Self-review / Impact / Limitations

Independent standards and specification reviews found no blocking defect.
The optional other-vector rejection coverage suggestion was implemented; final
self-review also removed implicit channel initializers and reran affected builds
and the full native matrix. `git diff --check` passes.

DXBC/AIRCONV and shared production runtime are unchanged. This finite UINT4
copy/load corpus is NATIVE_OBSERVED, not full mandatory-set acceptance or two
production-backend validations: both native builds use the same installed MSC.
RGBA32_UINT GPU readback, partial stores, vector atomics, other vector types,
production hidden CBV/root integration, descriptor generations, cache identity
and D3D barrier integration remain UNVERIFIED/outside scope. No game acceptance
or performance test was performed for this diagnostic-only change.

Capability status: PARTIAL overall; bounded native UINT4 corpus passes.
Feature level impact: FL11_1, FL12_0 and FL12_1 unchanged, no promotion.
Git status at handoff: task files committed; no unrelated changes at task start.
Push status: NOT PUSHED. Next recommended bounded task: SINT4 typed-origin
load/copy-store and sign-extension GPU oracle; production unaligned views remain
fail-closed until integration and lifecycle semantics are separately validated.

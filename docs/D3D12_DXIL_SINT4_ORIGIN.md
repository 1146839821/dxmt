# SINT4 offline typed-origin lowering

## Task Analysis

Branch `feat/d3d12-1`; clean task-start worktree, baseline `74025f4`.
Remote baseline `origin/feat/d3d12`, merge base `85bb2dd`.
Recent local commits provide scalar UINT/SINT/FLOAT/UNORM and vector UINT4
offline diagnostics. Existing adapter/probe/parser tests reside in `tests/dx12/`;
no production implementation or capability promotion is included here.

Hypothesis: SINT4 reuses UINT4 coordinate guards, aggregate OOB zero and full-mask
copy-store while preserving signed component metadata. Evidence: fresh
`typed_uav_5_0.cso` has `vector<int, 4> `, component 4 and four i32 extracts;
scalar output remains UINT component 5. Expected effect: bounded offline SINT4
copy/load validated with actual MSC and native RGBA8/16_SINT formats.
Missing pieces: precise signed vector classification and probe format entries.
Risk: treating signed metadata as UINT, losing sign extension, mixing texel and
channel units, accepting vector atomics/status lanes or other vector types.

Minimal implementation: accept SINT4 with exact metadata; reuse vector width and
output arithmetic, retain scalar UINT output and existing atomic rejection.
Validation: both builds reconfigured/compiled, parser boundaries and complete
DXC input/output validation, exact per-lane output plus full backing byte
readback, zero/OOB counts, aligned/padded original-shader controls, all earlier
scalar/UINT4 regressions. Signed minima, maxima, -1 and a positive value come
from independent fixed CPU bits, not observed GPU output. Partial stores,
RGBA32_SINT and signed/vector atomics stay outside this readback milestone.

D3D12 contract: origins/counts are texels; narrow SINT loads sign extend to i32,
OOB loads return zero and OOB stores leave memory unchanged. Native readback
does not establish production root/descriptor/cache/barrier integration.
DXBC/AIRCONV, shared runtime, capability flags and feature levels stay unchanged.
LLVM/MSC compilation/integration/validation skills constrain IR and reflected
ABI. Independent standards/spec reviews then local commit, never push.

## Task Result

### Implementation / Changed Files

This document and three test files: `dxil_origin_transform.hpp`,
`dxil_origin_transform_test.cpp`, `msc_typed_buffer_padding_probe.c`.
SINT4 requires signed vector class plus metadata 4; output remains scalar UINT
with metadata 5. Shared `is_vector` controls width/arithmetic/atomic exclusion
for both supported integer vector types. Aggregate guards, full-mask store and
OOB zero merge are reused unchanged; no signed value rewriting is introduced.
RGBA8/16_SINT reuse the existing signed CPU oracle and four-channel byte layout.

Added parser cases cover UAV/SRV four-lane access, metadata mismatch, scalar
width despite a vector declaration, status lane, partial store, vector atomic,
output type, SRV write and unsupported shift. FLOAT4/UNORM4 stay rejected and
SNORM4 now has an explicit resource rejection fixture. Parser fixtures alone
do not prove valid DXIL or GPU semantics.

### Tests Run / Runtime Results

Fresh normal and no-private configurations both reconfigured and compiled the
existing roundtrip/parser/native probe targets, without compiler warnings.

- Meson 3/3 and parser 89/89 per build.
- Full DXC container contracts: 34/34 total, thirteen accepted and four rejected
  per build. SINT4 UAV/SRV regenerated outputs validate; FLOAT4/UNORM4, scalar
  SNORM and signed scalar atomic inputs reject with no output. Original input
  SHA256 values remain unchanged.
- Native positive GPU readback: 1750/1750 per build, comprising 280 new SINT4
  cases and 1470 prior scalar/UINT4 regressions. Each new format covers UAV/SRV,
  origins 0/1/4/257/260, counts 8/0/1/3/4/5/7 and both bounds-check settings.
  Each lane's exact sign-extended output and the entire backing allocation match,
  including suppressed OOB stores, aggregate zero loads and sentinels.
- Original SINT4 shader controls: 40 cases per build; 24 aligned matches and
  16 padded mismatches. GPU completion without error is required before result
  classification; exit status alone is not proof. The control commands pass
  unmodified `typed_uav_5_0.cso` / `typed_uav_srv_5.cso`, not lowered outputs.
  Modes without `-oob` bind the same host origin CBV, unused by original shaders.
  `CompileMSCProbe` compiles the supplied CSO without applying this adapter;
  thus that host binding mode does not implicitly turn controls into positives.
- Metal API/shader validation enabled; receipts detect no pipeline/validation
  errors. Both configurations use the same installed MSC and Apple M4 GPU.
- Unchanged production prefix, R8G8B8A8_SINT selected contracts: 3/3. DXIL
  FirstElement 1 rejects while recording (`MSC typed-buffer view unavailable`,
  `Close` failure); aligned DXIL FirstElement 4 and DXBC FirstElement 1 pass GPU
  readback. No production DLL deployment/replacement was performed.

Ignored receipts contain commands and full output:
`build/sint4-origin-containers.json`, `build/sint4-origin-native.json`, and
`build/sint4-origin-production.json`; build/configuration logs use
`sint4-origin-`. Reconfigure with `meson setup --reconfigure`, compile
`dxil_roundtrip`, `dxil_origin_transform_test`, `msc_typed_buffer_padding_probe`,
then run `meson test --print-errorlogs` in each configuration. Roundtrip uses
`--lower-typed-origin` with a new output path and absolute Windows DXC directory.
Native modes are `--origin-cbv-rgba8sint-oob` and
`--origin-cbv-rgba16sint-oob`; modes without `-oob` supply the original-shader
controls. Validation environment is `MTL_DEBUG_LAYER=1`,
`MTL_SHADER_VALIDATION=1`, `MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1`.

### Standards

No documented-standard violation. Removed the reported unreachable component-4
test branches. One possible duplicated-test-construction maintenance suggestion
is deferred; it does not change the acceptance/rejection contracts.

### Spec

Signed metadata, texel coordinates, sign-extension oracle, whole-buffer checks,
negative controls and fail-closed production policy are preserved. The initial
control-entry concern was checked against supplied original CSO paths, the
non-rewriting compile helper and complete GPU receipts. SNORM4 parser coverage
was added. The independent specification reviewer withdrew the control-entry
finding after checking the actual inputs and receipts: no remaining spec blocker.
Review summary: Standards zero hard violations, one optional maintenance
suggestion deferred; Spec zero remaining findings.

### Limitations / Capability / Git Status

Completed only this bounded native diagnostic milestone: NATIVE_OBSERVED for
the finite load/copy-store corpus, overall capability status PARTIAL. RGBA32_SINT,
partial masks, signed/vector atomics, broader shader grammar, production hidden
CBV/root integration, descriptor generations, cache identity and D3D barriers
remain outside scope/UNVERIFIED. Two native build configurations do not prove
two production-backend acceptances. No game/performance acceptance was run.
DXBC/AIRCONV and shared production runtime are unchanged. FL11_1, FL12_0 and
FL12_1 are unchanged; no capability promotion. Unaligned production views remain
fail-closed. Task baseline `74025f4`, remote baseline `origin/feat/d3d12`;
local commit hash and clean-worktree check are reported at handoff. NOT PUSHED.
Next bounded task: FLOAT4 typed-origin lowering and native float/half readback.

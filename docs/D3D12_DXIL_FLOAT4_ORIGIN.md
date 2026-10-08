# FLOAT4 offline typed-origin lowering

## Task Analysis

Branch `feat/d3d12-1`, clean task baseline `7847612`; remote baseline
`origin/feat/d3d12`, merge base `85bb2dd`. Existing scalar and integer-vector
diagnostics are tests-only. Hypothesis: FLOAT4 uses the same vector texel guards
with f32 aggregate loads and full-mask stores, preserving component 9 rather
than normalized 14/13. Fresh DXC dump is the source evidence. Expected effect:
offline float4 UAV/SRV copy-load/store, not production integration.

Relevant files: adapter, parser tests and native typed-buffer probe under
`tests/dx12/`. Missing pieces: float vector class/envelope/width classification,
four float-to-i32 bitcasts and native RGBA16/32_FLOAT entries. Risks: normalized
metadata accepted accidentally, lane/index units, half conversion masked by
epsilon, declaring padding proof for inherently aligned 16-byte texels.
Plan: exact finite CPU half/float bit patterns, per-lane and full backing checks;
RGBA16 padding controls, RGBA32 explicitly aligned-only when queried alignment
divides its texel size. Existing padding-required formats retain their gate.
No general NaN/denormal/rounding claim. Validate both builds, complete containers,
native positive/control matrices and earlier regressions with API/shader
validation; independent code-review standards/spec axes then commit, no push.
LLVM and MSC compile/binding/validation skills constrain IR, reflection and ABI.
DXBC/AIRCONV, shared runtime, production fail-closed policy and FL/capabilities
remain unchanged. OOB loads zero the aggregate; OOB stores do not modify memory.

## Task Result

### Implementation / Changed Files

This document and the existing adapter/parser/native probe under `tests/dx12/`.
FLOAT4 accepts the exact DXC vector class spelling and component 9, requires f32
typed accesses, tracks four extract lanes per load and preserves bitcasts.
Scalar UINT output remains required; UNORM4/SNORM4, partial stores, integer
accesses on FLOAT4 and atomics stay rejected. Guarded aggregate zero/OOB
non-write and full-mask copy-store reuse existing logic.

RGBA16/32_FLOAT use independently fixed half/float bits for 0.5, -2, 1.5, 32;
four cyclic distinct lanes and entire backing bytes are compared exactly.
No epsilon, observed-GPU-derived oracle or normalized substitution is added.
The alignment check permits a wide texel that is a multiple of native alignment.
Only RGBA32_FLOAT may report aligned-only success, and only when nonzero queried
alignment divides 16; all previous formats still require padded cases. The
distinct `PROTOTYPE_ALIGNED_MATCHED` status never claims padding proof.

### Tests Run / Runtime Results

Fresh normal and no-private configurations:

- Reconfigured and compiled roundtrip/parser/probe targets without compiler
  warnings. Meson 3/3 and parser 100/100 per build.
- Full DXC container contracts: 36/36 total, fifteen accepted and three rejected
  per build. FLOAT4 UAV/SRV outputs validate before MSC; UNORM4, scalar SNORM and
  scalar SINT atomic inputs reject without output. Input hashes remain unchanged.
- Native Metal GPU positives: 2030/2030 per build, including new FLOAT4 280 and
  earlier scalar/integer-vector regressions 1750. Each new format covers UAV/SRV,
  origins 0/1/4/257/260, counts 8/0/1/3/4/5/7 and both bounds-check settings.
  Exact four-lane output and whole backing allocation match, including OOB zero,
  suppressed stores and sentinels.
- Original RGBA16_FLOAT shader control: 20 cases per build, twelve aligned
  matches and eight padded mismatches. Original RGBA32_FLOAT control: twenty
  aligned matches, zero padded cases, explicitly aligned-only evidence. Apple
  M4 queried alignment is 16 bytes. Successful GPU completion is required, not
  just a particular process exit code. Original CSOs are supplied directly;
  the additional host origin CBV is unused by them.
- API and shader validation enabled; native receipts detect no validation or
  pipeline errors. Both builds use the same installed MSC, not independent
  production shader backends.
- Unchanged production prefix: four selected contracts pass. R16G16B16A16_FLOAT
  DXIL FirstElement 1 rejects during recording/Close; aligned DXIL FirstElement
  2 and DXBC FirstElement 1 pass GPU readback. R32G32B32A32_FLOAT DXIL FirstElement
  1 passes aligned readback. No production DLL deployment/replacement occurred.

Ignored receipts with commands/logs: `build/float4-origin-containers.json`,
`build/float4-origin-native.json`, `build/float4-origin-production.json`.
Reproduction: `meson setup --reconfigure` each build, compile `dxil_roundtrip`,
`dxil_origin_transform_test`, `msc_typed_buffer_padding_probe`, then
`meson test --print-errorlogs`. Lower existing `typed_uav_3_0.cso` and
`typed_uav_srv_3.cso` with `--lower-typed-origin`, a fresh output path and absolute
Windows DXC directory. Probe modes are `--origin-cbv-rgba16float-oob` and
`--origin-cbv-rgba32float-oob`; modes without `-oob` use original control CSOs.
Environment: `MTL_DEBUG_LAYER=1`, `MTL_SHADER_VALIDATION=1`,
`MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1` before device creation.

### Standards

Main-agent review found no documented-standard blocker; the existing tests-only
layout and bounded component/format tables are retained. Similar negative fixture
construction is a maintenance concern, deferred without changing test contracts.

### Spec

Main-agent review checked signed/unsigned/normalized isolation, per-load width,
exact f32/half oracle, texel versus channel units, original controls and the
explicit format/alignment guard. No confirmed implementation blocker. Both
independent code-review agents failed due to account usage limits before issuing
reviews: independent review is NOT COMPLETE, and is not represented as passed.
Summary: self-review Standards no hard violations, one maintenance concern;
self-review Spec no confirmed defect. `git diff --check` passes.

### Limitations / Capability / Git

NATIVE_OBSERVED applies only to this finite native Metal copy/load corpus.
Overall capability remains PARTIAL, not full mandatory typed-UAV acceptance.
RGBA32_FLOAT supplies no padded negative evidence on this GPU. NaN, denormals,
general rounding/quantization, partial stores, vector atomics, UNORM4 and broader
IR grammar are not validated here. Production hidden root/CBV integration,
descriptor generations, cache identity and D3D barriers remain UNVERIFIED.
No game or performance acceptance was run. LLVM/MSC/validation skills preserved
the reflected ABI and these evidence boundaries. DXBC/AIRCONV, shared runtime,
feature flags and FL11_1/12_0/12_1 are unchanged; unaligned production views remain
fail-closed. Task baseline `7847612`, branch `feat/d3d12-1`; local commit hash and
worktree status are reported at handoff. NOT PUSHED.
Next bounded task: UNORM4 metadata/origin lowering and normalized GPU readback.

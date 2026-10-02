# UNORM4 offline typed-origin lowering

## Task Analysis

Clean baseline `9e1f3d8`, branch `feat/d3d12-1`. Hypothesis: finite UNORM4
copy/load uses existing FLOAT4 texel guards, with component 14 retained.
Evidence: fresh DXC dump of `typed_uav_7_0.cso` has the float vector class,
f32 load/full-mask store and component 14. Expected effect: tests-only offline
lowering and native RGBA8/16_UNORM readback, not production integration.
Risks: normalized metadata lost, lane swaps, quantization hidden by epsilon,
OOB stores touching padding. Reuse independent scalar raw/nearest-float bit
oracles, cyclic distinct lanes and whole-allocation comparison. Validate both
builds, full containers, prior regressions, original-shader padding controls,
and production fail-closed contracts. Self-review before local commit; no push.
LLVM and MSC compile/binding/Metal validation skills preserve guarded IR and
reflected ABI. No general normalized arithmetic/rounding claim. SNORM, partial
stores and vector atomics remain outside scope. No capability or FL promotion,
DLL deployment, game acceptance or performance claim.

### Current Branch

`feat/d3d12-1`.

### Baseline

Remote `origin/feat/d3d12`, merge base `85bb2dd`; task baseline `9e1f3d8`.

### Local Commits Since origin/feat/d3d12

Recent bounded origin work: scalar SINT `b7f6eac`, scalar UNORM `7c4f3d5`,
UINT4 `74025f4`, SINT4 `7847612`, FLOAT4 `9e1f3d8`. Retain these changes.

### Current State

Clean task start. Production unaligned MSC typed-buffer views fail closed.

### Existing Implementation

Bounded DXC text adapter validates whole containers before and after lowering;
hidden origin/count CBV, logical bounds and unsigned-wrap guards already exist.

### Relevant Files

`tests/dx12/dxil_origin_transform.hpp`, `dxil_origin_transform_test.cpp`,
`msc_typed_buffer_padding_probe.c` and this document.

### Existing Tests

Parser, full-container roundtrip and native scalar/integer-vector/FLOAT4 probes.

### D3D12 Contract

Indices are texels, not channels. Preserve normalized conversion metadata;
OOB loads zero the aggregate and OOB stores leave the backing allocation intact.

### DXBC / AIRCONV Impact

None: no compiler/backend fallback or production shader change.

### DXIL / MSC Impact

Tests-only UNORM4 classification and finite native readback.

### Shared Runtime Impact

None: descriptor lifetime, root ABI, barriers and production views unchanged.

### Missing Pieces

Component 14 vector classification, native UNORM4 format entries/modes and
specific parser acceptance/rejection coverage.

### Risks

See hypothesis above: metadata loss, lane units, quantization and OOB padding.

### Minimal Implementation Plan

Reuse FLOAT4 guards and scalar UNORM values; add only bounded type/format cases.

### Validation Plan

Reconfigure both builds before compiling; parser/Meson, fresh full containers,
GPU exact bits/backing memory, original controls and production rejection.

### Capability Impact

PARTIAL diagnostic evidence only; no FL or capability increase.

## Task Result

### Branch

`feat/d3d12-1`.

### Baseline

`origin/feat/d3d12`; task baseline `9e1f3d8`.

### Local Commit

Local hash reported at handoff; commit includes only this bounded milestone.

### Changed Files

This document and the three test-side files listed above.

### Implementation

UNORM4 accepts the exact DXC float-vector class with component 14, preserves
that metadata and requires f32 typed access. Existing four-lane extraction,
aggregate zero, logical bounds, unsigned-wrap and full-mask store guards apply.
Output remains scalar UINT. Native RGBA8/16_UNORM reuse raw values
`0,1,128,255` / `0,1,32768,65535`, with independent nearest-float32 bits:
`0,3b808081,3f008081,3f800000` / `0,37800080,3f000080,3f800000`.
CPU arithmetic independently reconfirmed these bits this task. Cyclic distinct
channels and full backing bytes detect swaps and writes outside the logical view.

### DXBC / AIRCONV Impact

Unchanged; selected production DXBC regression passes.

### DXIL / MSC Impact

Offline diagnostic only. No production lowering or shader backend crossover.

### Shared Runtime Impact

Unchanged; no DLL deployment or Wine prefix replacement.

### Tests Added

UNORM4 UAV/SRV positives and metadata preservation; integer load/atomic,
status extraction, partial store, SNORM metadata, vector output, SRV write and
per-handle scalar width rejection. Synthetic fixtures are parser evidence only.

### Tests Run

- Normal/no-private builds freshly reconfigured and compiled. Meson 3/3 and
  parser 108/108 per build; no compile warnings.
- Fresh full DXC container contracts 38/38 total: seventeen accepted and two
  rejected per build. UNORM4 UAV/SRV added; scalar SNORM and SINT atomic still
  reject without output. Input hashes unchanged. Outputs validate before MSC.
- Native Metal positives 2310/2310 per build: UNORM4 280 plus earlier 2030
  regressions. Each new format covers UAV/SRV, FirstElement 0/1/4/257/260,
  logical counts 8/0/1/3/4/5/7 and both MSC bounds-check settings.
- Original UNORM4 controls 40 cases per build: 24 aligned matches, 16 padded
  mismatches. Both queried alignments are 16 bytes on Apple M4. Original CSOs
  are passed directly, not lowered; their unused host CBV does not fix padding.
- API/shader validation enabled before device creation, stderr reporting on;
  no detected validation, compilation or pipeline errors in native receipts.
- Production selected contracts 3/3: R8G8B8A8_UNORM DXIL FirstElement 1 rejects
  at recording/Close; DXIL FirstElement 4 and DXBC FirstElement 1 pass GPU readback.

Ignored command/log receipts: `build/unorm4-origin-containers.json`,
`build/unorm4-origin-native.json`, `build/unorm4-origin-production.json`.
Reproduce by reconfiguring each build, compiling `dxil_roundtrip`,
`dxil_origin_transform_test`, `msc_typed_buffer_padding_probe`, then Meson test.
Lower `typed_uav_7_0.cso` / `typed_uav_srv_7.cso` to fresh paths with
`--lower-typed-origin` and the absolute Windows DXC directory.
Probe `--origin-cbv-rgba8unorm-oob` / `--origin-cbv-rgba16unorm-oob` with lowered
inputs; use original inputs and modes without `-oob` for controls.
Set `MTL_DEBUG_LAYER=1`, `MTL_SHADER_VALIDATION=1`,
`MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1` before launching the native executable.

### Runtime Results

Exact per-lane f32 output and entire raw backing allocation match, including
OOB zero and suppressed stores. Both builds share the installed MSC runtime;
this is not independent production backend coverage or native Windows evidence.
RGBA32_FLOAT regression remains aligned-only on this GPU, not padding proof.

### Standards

Main-agent self-review: no hard documented-standard violation. Existing bounded
enum/format tables retained. Repeated synthetic negative construction remains
a possible maintenance smell, not a correctness blocker.

### Spec

Main-agent self-review: no confirmed defect. Verified metadata 14 isolation,
f32 access matching, texel/channel units, unchanged guards, exact CPU oracle,
original controls and production fail-closed behavior. Strengthened width test
to provide a valid scalar class declaration and assert the lane-width failure.
Independent review remains NOT COMPLETE: prior reviewer attempts hit account
usage limits; do not represent the fallback self-review as independent approval.
Summary: Standards zero hard violations, one maintenance concern; Spec zero
confirmed defects. LLVM/MSC/validation skills preserved ABI and evidence scope.

### Known Limitations

Finite normalized copy roundtrip only, not arbitrary store quantization,
clamping, NaN, denormal or general IR support. SNORM, partial stores, vector
atomics and status lane extraction remain rejected. Production hidden-CBV/root
ABI, descriptor generations, cache identity and D3D barriers remain UNVERIFIED.
No game or performance acceptance performed.

### Capability Status

PARTIAL overall. NATIVE_OBSERVED for this finite Metal diagnostic corpus only.
Not a full mandatory typed-UAV format-set acceptance or production alignment fix.

### Feature Level Impact

FL11_1: unchanged. FL12_0: unchanged. FL12_1: unchanged.
No feature flags or Shader Model report promoted.

### Git Status

Only the four scoped files staged; final clean status confirmed at handoff.

### Push Status

NOT PUSHED.

### Next Recommended Task

Audit production hidden-CBV/root ABI integration eligibility, descriptor
generation/lifetime, cache identity and barrier requirements before considering
opening unaligned production views. Existing eligibility guards stay closed.

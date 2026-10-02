# Static DXIL reduction root samplers

## Task Analysis

- Hypothesis: qualified private MinMax compute PSOs can admit original static
  reduction roots without initializing or executing an ordinary MSC pipeline.
- Evidence: private roots already preserve static sampler descriptions and
  locations; dispatch materialization already transports static descriptions
  into the pair state. Admission is blocked earlier by AIR-only root creation
  and ordinary MSC layout requirements at PSO creation and recording.
- Expected effect: actual static MIN/MAX and mixed-pair DXIL Dispatch through
  the existing private lowering, reflected layout and completion-owned binding.
- Risk: gate removal must not expose the point surrogate to ordinary MSC or AIR
  consumers. A private-only PSO must reject recording if its opt-in is removed,
  and unsupported shader conversion must fail instead of falling back.
- Validation: both configured builds, focused one/two-pair static GPU
  readbacks, original root/state preservation, defaults-off and ordinary MSC
  rejection, AIR gate isolation and existing dynamic/typed guard regressions.

The ordinary MSC layout rejection stays intact. No FL or capability promotion,
full MinMax matrix or game acceptance is implied by this task.

## Runtime contract

With `DXMT_MINMAX_DXC_DIRECTORY` selected, static non-anisotropic reduction
root creation no longer requires the AIR switch. Original root bytes, static
sampler descriptions, register order and RS1.0 volatile conversion remain
unchanged. The native point surrogate is not ordinary MSC admission.

For an explicit static reduction root, compute PSO creation immediately
prepares and compiles the existing qualified DXIL/MSC MinMax variant. No
ordinary PSO is compiled first and failures never fall back. The PSO owns the
original bytecode and root and is marked private-only. Changing or removing
the selected directory cannot silently select an ordinary PSO at recording.

Qualified dispatch uses the private reflected root directly, omitting ordinary
layout initialization and ordinary TLAB upload. The existing recording and
submission paths preserve static state, descriptor-range flags, unique live
observations and completion-owned resources. Ordinary MSC root layout and
typed-origin preparation continue to reject static reduction roots. AIR
recording independently requires `DXMT_ENABLE_AIR_MINMAX=1`, even when MSC
opt-in admitted the root object.

This task does not expand the qualified shader envelope beyond float
Texture2D SampleLevel compute. Unsupported shader operations, ExecuteIndirect,
combined private variants and directly indexed roots remain outside admission.

## Task Result

Both configured full builds and their four registered host suites pass. The
isolated one/two-pair D3D12 probes pass in normal and no-private builds:

- RS1.1 static MIN/MIN, MAX/MAX, MIN/MAX, MIN/ordinary and RS1.0 MAX/MIN
  roots execute without a sampler heap. Focused red-channel results are
  16, 240 and 128 as appropriate; the RS1.0 live resource empty-set overwrite
  yields zero. These five new root scenarios have five executions each across
  four probes: **100 static reduction numeric executions**, not 100 independent
  feature cases or complete format/LOD coverage.
- Original root blobs, filter, non-default MinLOD/MaxLOD and bias survive.
  Materialized pair states preserve the original LOD bounds. The kernels use
  explicit level zero; this is not a full sampler LOD or implicit-bias oracle.
- Caller root/PSO references are released before the new static scenarios
  execute. Fence-controlled overlapping replay still passes. This proves
  caller-release behavior, not removal of every other command-list owner.
- Root and PSO creation with opt-in removed reject, ordinary MSC layouts and
  typed-origin root/PSO preparation reject, and recording the private-only PSO
  with opt-in removed rejects. No invalid negative dispatch is submitted.
- Existing six-root static/live behavior and private-to-ordinary same-encoder
  restoration pass. Overall the four probes execute 220 main numeric dispatches
  plus eight restoration dispatches; the 100 new static executions are included.
- Unsupported shader preparation has no fallback. Origin-only typed views
  still reject at static recording/live materialization; ordinary MSC dynamic
  and static readbacks pass (255), AIR static/dynamic minimum pass (16), and
  default-off dynamic descriptor rejection and independent AIR static gate
  rejection pass. Existing typed-origin contracts pass 14 per build.

Receipts: `/Users/zhangbo/.cache/dxmt-minmax-static-dxil.CavS0w/{normal,no-private}/`
with `pair-{1,2}.final.log`, `typed-origin.final.log`,
`unsupported-shader.final.log`, `typed-rejection.final.log`,
`ordinary*.final.log`, `defaults-off.final.log`, and `air-*.final.log`.
Initial probe logs identify the matching physical winemetal runtime; final
loader logs identify the staged D3D12 DLLs. Final staged/source DLL SHA-256:

- normal: `ff304eed17ecf3e212ad1943cbfb1015b50b87e5f6ba7e4e0f682be07ee676ff`
- no-private: `da30d4d3ff0855ca1c2f8a8dd81b977edcd41f0b268f4f98e957c08d59cf3c63`

Main-agent self-review checked consumer separation, failed-PSO publication,
root/cache identity, reflected layout selection and strong lifetime ownership.
No independent agent review was run. The MSC skills informed reflection-based
binding and retention of the original root instead of compiler-only sampler
normalization; actual conversion/readback succeeded with the original filters.
Installed game DLLs were not changed. Full MinMax matrices, broader operations,
shapes/formats, indirect/direct-indexed and production qualification remain
open. No Metal validation, fresh game/performance/tessellation acceptance or
FL/SM/capability promotion is claimed.

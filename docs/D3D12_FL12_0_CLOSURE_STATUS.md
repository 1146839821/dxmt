# FL12_0 closure checkpoint

## Current snapshot (2026-10-02, baseline 89d0180)

This section supersedes the historical checkpoints below. The implementation
is not yet in a tests-only closure phase. No percentage or completion date can
be justified while required semantics are still missing.

Task Analysis for this refresh:

- Hypothesis: stale checkpoint wording understates implemented AIR MinMax paths
  and understates the existing typed-view acceptance workload.
- Evidence: current OPTIONS still disables additional typed UAV loads and tiled
  resources; no-private also disables LogicOp. The gate requires 144 base, 126
  UAV-view and 126 SRV-view cases per backend, and seven unregistered GPU matrices.
- Expected effect: separate production gaps, existing reruns and undefined new
  matrix coverage so subsequent tasks target missing behavior first.
- Risk: required case counts are acceptance targets, not fresh passing results;
  focused opt-in AIR readbacks do not establish complete MinMax support.
- Validation: source/gate audit and gate unit regression; no new GPU acceptance.

| Production gap | Current boundary | Next closure work |
| --- | --- | --- |
| Typed UAV | Production origin lowering and bounded direct/non-updating-indirect probes exist; additional-format declaration remains FALSE | Default production enablement, remaining indirect/provenance/view/lifetime contracts, then complete matrices |
| MinMax | Opt-in AIR explicit/gradient/implicit operations and known-format resource/instruction clamps exist; focused GPU readbacks pass | Broader operation/format/shape contracts; cube, anisotropic, meaningful feedback and DXIL implementation remain open |
| Tiled Tier 2 | Declared NOT_SUPPORTED | Packed mips, mapping, feedback, filtering/LOD, lifetime and synchronization closure |
| LogicOp (no-private) | OPTIONS reports FALSE in this variant | Implement a compliant path or explicitly exclude this variant from the FL12_0 claim; tests alone cannot close this gap |

Binding and normal-build LogicOp still need full semantic acceptance. ROV and
conservative rasterization remain later FL12_1 work, not FL12_0 prerequisites.

### Remaining test workload: known versus not yet bounded

- Existing typed matrices: 144 base + 126 UAV-view + 126 SRV-view = 396 cases
  per backend/variant. Both backends and both builds require **1,584 case
  executions**, excluding policy/API/residency probes and future contracts.
  These are reruns of existing cases, not 1,584 new tests or verified passes.
- Seven complete GPU categories are still unregistered in the closure gate:
  raster, formats, mandatory DXBC shaders, mandatory DXIL shaders, DXBC
  tessellation, DXIL tessellation, and geometry/stream output. They need
  explicit coverage dimensions and independent readback oracles before a
  defensible total of new cases can be given.
- MinMax, tiled, binding/LogicOp, synchronization/lifetime and fresh game
  regressions are additional acceptance work, not included in those 1,584.
  Compiler-failure tests, host units and a running game process cannot replace
  these GPU semantics or prove tessellation/performance acceptance.

Execution order remains gaps first: wire actual AIR clamp behavior next,
including empty-set defaults and static-recording/live-submission validation;
then close remaining MinMax/backend and typed contracts, resolve tiled Tier 2,
and run the complete acceptance matrices. Add only focused regressions needed
to validate each implementation change during this phase. Do not repeat full
matrices after every descriptor/ABI preparation checkpoint.

Instruction-clamp admission is now connected. Current next actions are the
remaining MinMax production operations/backends, not another status-only or
full-matrix rerun.

Task Result: refreshed current status and corrected the gate's stale MinMax
diagnostic without changing any gate status or capability. Gate units pass
55/55. Main-agent Standards/Spec self-review only; independent review remains
unavailable. No production lowering, runtime deployment or game test in this
refresh.

Follow-up (2026-10-03): actual AIR clamp/empty-set CFG generation is now connected
to the production reduction helper (`D3D12_MINMAX_CLAMP_LOWERING.md`). This is
structural lowering plus existing sampling regression evidence, not nonzero-clamp
GPU admission: instruction and recording/submission rejection remain intact until
host validity and actual numeric clamp dispatches pass. The snapshot's remaining
workstreams and full acceptance counts are unchanged.

Resource-clamp follow-up (2026-10-03): known AIR descriptors now pass nonzero
resource clamps and empty-set dispatches (`D3D12_MINMAX_CLAMP_ADMISSION.md`).
Both builds pass fifteen focused GPU cases and recording/live validity probes,
including nonzero view origin and CPU-only descriptor copies. Instruction
clamps/feedback and DXIL reduction remain rejected. Unknown-format GPU admission
coverage and full MinMax acceptance are still unverified; no FL promotion.

Instruction-clamp follow-up (2026-10-03): supported AIR SampleGrad/pixel
Sample/SampleBias admit clamps (`D3D12_MINMAX_INSTRUCTION_CLAMP.md`). NULL status
destinations are normalized to absence; actual feedback stays rejected. Both
builds pass 12 gradient and 8 pixel clamp GPU cases, with qualification/sentinel
checks preventing false-zero acceptance. This closes bounded AIR operation
admission, not full MinMax, unknown-format/shape matrices or FL12_0. No promotion.

## Task Analysis

Baseline: `e220380`, branch `feat/d3d12-1`. User requests continued work and
an evidence-based estimate of remaining FL12_0 validation. This checkpoint
reprioritizes capability closure ahead of another tests-only recipe decoder.

- Hypothesis: repeated host diagnostic coverage is not the remaining bottleneck.
- Evidence: current gate requires missing GPU matrices; device OPTIONS still
  reports tiled support absent and additional typed UAV loads FALSE.
- Expected effect: finite acceptance workstreams rather than an indefinite
  sequence of failure-injection and serialization tests.
- Risk: historical runtime results may not describe current binaries.
- Validation: current source audit and host gate regression; no fresh full GPU
  gate, deployment, benchmark or capability promotion in this checkpoint.

## Remaining workstreams

| Workstream | Evidence / gap | Closure evidence |
| --- | --- | --- |
| Typed UAV | Additional-format declaration FALSE; unaligned MSC views still rejected; offline origin lowering and recipes are not production integration | Production lowering, root/cache identity, binding and lifetime integration; complete DXBC/DXIL format and view readback matrices |
| Min/Max filtering | Gate describes rejection only, implementation absent; native Apple9 path blocked pending shader emulation | Both backend implementations and reduction/filter/LOD edge readbacks, or an explicit architecture limitation |
| Tiled Tier 2 | Device declares NOT_SUPPORTED; resource audit leaves packed-tail and raw/structured feedback design gaps | Mapping, packed mips, feedback, filtering/LOD, lifetime and synchronization closure before declaration change |
| Raster / formats | Two complete mandatory GPU matrices not registered in gate | Independent pixel/readback oracles and format coverage |
| Mandatory shader paths | Two complete backend GPU matrices not registered | DXBC/AIRCONV and DXIL/MSC separately verified, no fallback |
| Tessellation / GS + SO | Three complete GPU matrices not registered | HS/DS domain, partitioning, indexed/instanced paths and stream-output semantics; fresh game regression separately |
| Binding / logic ops | API minima only yield PARTIAL in current gate | Full resource-binding and logic-op semantic evidence; API bits alone cannot pass |

The gate explicitly leaves **seven** matrix categories UNVERIFIED: raster,
formats, DXBC mandatory shaders, DXIL mandatory shaders, DXBC tessellation,
DXIL tessellation, and geometry/stream output. This is a count of categories,
not a count of remaining test cases. Their acceptance dimensions must be
defined before estimating a finite total. Some categories may share fixtures,
but cannot inherit PASS from host compiler-failure tests.

Existing typed-UAV coverage alone comprises 144 cases per backend per variant:
18 formats with five texture shapes and three buffer offsets. A rerun in both
normal and no-private variants is **576 case executions**, not 576 new tests,
and does not cover all later view/lifetime contracts. Historical results in
`D3D12_TYPED_UAV_FORMATS.md` are not a fresh current-build acceptance result.

## Next bounded implementation task

### Current implementation delta (2026-10-02)

The historical typed-origin integration gap above has narrowed: validated DXIL
lowering, reflected compiler roots, cached native compute PSOs and opt-in direct
dispatch submission bindings are now connected. Both build variants pass seven
focused GPU contracts, including distinguishable repeated in-flight execution.
See `D3D12_TYPED_ORIGIN_DISPATCH_BINDING.md`. This does not close complete typed
UAV support: default enablement, full indirect integration, shader provenance coverage
and the full format/view matrices remain outstanding. Non-updating indirect
compute is now connected and passes the same seven focused contracts in both
builds (`D3D12_TYPED_ORIGIN_INDIRECT.md`); broader indirect combinations remain
unverified. Min/Max, tiled Tier 2 and
the seven mandatory GPU matrix categories remain closure work; no FL promotion.

Min/Max implementation now has a float non-anisotropic explicit-LOD AIR primitive
for 2D/array/3D, with native kernel readback and LLVM signature-linkage evidence
(`D3D12_MINMAX_AIR_PRIMITIVE.md`). This is not yet wired to D3D12 sample instructions
or sampler state, has no sparse feedback, and does not implement the DXIL path.
The existing Min/Max rejection and gate status remain unchanged.

The next bounded step now connects opt-in static root samplers to real DXBC
SampleLevel execution (`D3D12_MINMAX_STATIC_INTEGRATION.md`). Both variants
pass focused Min/Max readbacks and fail-closed defaults/dynamic/MSC/SampleGrad
probes. This narrows the AIR integration gap, not the full reduction requirement:
dynamic sampler state, remaining AIR operations, feedback and DXIL still need
implementation. The gate remains unpromoted; seven complete matrix categories
are still outstanding. The earlier primitive-only wording above is historical.

Static reduction filter/LOD operands now come from the shared 32-byte GPU sampler
descriptor (`D3D12_MINMAX_SAMPLER_STATE_ABI.md`); ordinary dynamic descriptors
preserve complete state and copies retain it. This prepares dynamic lowering,
but does not admit dynamic reduction or close consumer/lifetime safety. The
focused mixed-filter clamp probe and ordinary AIR/MSC regressions pass in both
variants. Full Min/Max and all remaining closure workstreams stay open.

Sampler lifetime groundwork is now connected (`D3D12_SAMPLER_OBSERVATION.md`):
recording-time static references, submission-owned volatile observations and
atomic heap snapshots. Eight focused AIR/MSC observation executions pass across
both builds without accumulating live generations in closed encoders. This does
not admit dynamic reduction; its consumer lowering, remaining operations,
feedback and DXIL implementation are still open. Broad indirect/direct-indexed
and concurrent mutation matrices remain unverified.

Opt-in dynamic AIR SampleLevel reduction is now integrated
(`D3D12_MINMAX_DYNAMIC_INTEGRATION.md`). The independent
`DXMT_ENABLE_AIR_MINMAX_DYNAMIC=1` gate defaults off. Both variants pass dynamic
minimum/maximum 16/240, same-PSO descriptor switches 16/240/128/16 and focused
static/live ownership and unsupported-consumer/resource-clamp rejection. This
narrows the dynamic AIR gap; shader-wide qualification is conservative and does
not close the remaining sample operations, feedback, DXIL, indirect/direct-indexed
or full format/shape/per-range acceptance requirements. No FL/SM promotion.

Opt-in AIR reduction now also lowers supported explicit-gradient SampleGrad
through the shared reduction helper (`D3D12_MINMAX_GRAD_INTEGRATION.md`). Both
variants pass seven multi-mip numeric probes and twelve focused regressions each.
This closes a bounded AIR operation gap, not Min/Max or FL12_0: implicit sampling,
clamps/feedback, DXIL and complete GPU matrices remain outstanding. Feature-level
declarations are unchanged.

AIR reduction now admits supported float 1D/1D-array SampleLevel and SampleGrad
under the same opt-in gates (`D3D12_MINMAX_1D_INTEGRATION.md`). Both variants pass
focused real D3D12 minimum/maximum, distinguishable array-slice and multi-mip
gradient readbacks. This extends dimensions, not full MinMax closure; remaining
operations, clamps/feedback, DXIL and complete matrices are still outstanding.

AIR Pixel Shader Sample/SampleBias reduction now also lowers through the shared
gradient/tap helper (`D3D12_MINMAX_IMPLICIT_INTEGRATION.md`). Both variants pass
focused static/dynamic spatial extrema, implicit mip selection and additive-bias
pixel readbacks. Ordinary sampling and existing explicit operations regressions
pass. Cube, anisotropic, clamps/feedback, DXIL and complete matrix closure still
remain; opt-in gates and feature-level declarations are unchanged.

Clamp groundwork now transports view-mapped OOB default components through
unused AIR texture descriptor word 2 (`D3D12_MINMAX_CLAMP_DEFAULTS.md`). Both
variants pass bounded format constants and real AIR/MSC descriptor storage/copy
regressions. Actual clamp empty-set lowering is not yet connected, so resource
and instruction clamp admission remains rejected; this is not MinMax closure.

Prioritize the production typed-buffer origin contract, using
`D3D12_TYPED_ORIGIN_PRODUCTION_AUDIT.md` as the integration gap list. Keep
unsupported views rejected until real production readback succeeds. Integrate
the recipe into the actual lowering/root/cache/binding flow before crediting
offline results. A decoder is needed only if the selected cache persistence
design requires it; canonical serialization alone does not establish artifact
integrity or shader claim validity.

Then resolve Min/Max and tiled architecture feasibility, and register the seven
mandatory GPU matrices. Only after all required semantics and provenance pass
should feature declarations and the FL12_0 gate be reconsidered. ROV and
conservative rasterization are later FL12_1 work, not this checkpoint's FL12_0
closure target.

## Task Result

Documentation-only checkpoint. No production code, capability, shader routing,
DLL deployment or Wine prefix changes. Standards self-review: bounded scope,
current source distinguished from historical reports. Spec self-review: no
test-count percentage, FL promotion or GPU acceptance claim. These are main
agent reviews, not independent review evidence.

Validation: `meson setup --reconfigure build` succeeded; direct gate unit suite
passed 55/55; Meson `dx12-fl12-gate-unit` passed 1/1 (the same suite, not extra
coverage). `git diff --check` passed. No full runtime gate rerun. No push.

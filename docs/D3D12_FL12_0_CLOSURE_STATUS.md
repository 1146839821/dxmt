# FL12_0 closure checkpoint

## Typed pre-raster shader ABI update (2026-10-05)

Version-tagged shared origin intervals, compacted CBV sizing, GS/HS/DS DXC
preparation/conversion and visibility/deny resolution are implemented. Both
builds pass 32 local/shared VS/GS/HS/DS MSC artifact conversions, 46 native
structural processes and 400 native typed graphics draws plus ordinary
restoration checks. Old runtimes preserve local layout and explicitly reject
the new shared tag. See `D3D12_TYPED_ORIGIN_PRERASTER_ABI.md`.

This removes a necessary shader ABI blocker, not the pre-raster runtime gap:
typed emulation PSO/capture/replay wiring and actual GS/tessellation GPU oracles
remain next. Four production workstreams, seven complete GPU categories,
capability declarations and game/performance/tessellation acceptance unchanged.

## Native depth-only typed VS implementation update (2026-10-04)

Native typed VS now supports absent-PS depth-only pipelines without a fabricated
fragment stage. The existing vertex-only origin ABI and single TLAB are reused.
Normal/no-private, SM6.0/6.6 and explicit/deployed compiler runs pass 416 typed
depth draws and 416 ordinary restoration draws; 192 additional typed VS/PS draws
and their ordinary restoration checks pass. The same depth fixture fails before
GPU submission against the previous DLLs. See `D3D12_TYPED_ORIGIN_DEPTH_ONLY.md`.

This supersedes the depth-only gap below, not full typed or FL12 qualification.
Remaining pre-raster stages, indirect graphics, broad formats/provenance/lifetime
matrices and compiler distribution remain open. Static no-reread is source-only
evidence. Four production workstreams and seven complete GPU categories remain
open; capability declarations and game/performance/tessellation status unchanged.

## Native typed VS/PS implementation update (2026-10-04)

Native DXIL vertex origin preparation/conversion and VS/PS shared table capture
are now connected. Combined shaders receive separate private CBV record intervals
through two submission-owned TLABs; single typed-stage draws retain one TLAB.
Focused GPU readbacks pass on normal/no-private for SM6.0/6.6, explicit/deployed
DXC, VS-only and combined stages, disjoint same-register tables, live resource/
origin/count updates, OOB reads, UAV guards and ordinary restoration. Matching
embedded roots also pass. See `D3D12_TYPED_ORIGIN_VERTEX_PIXEL.md` for exact scope.

This narrows a real production gap, not full typed or FL12 acceptance. Depth-only
typed VS (no PS), remaining pre-raster stages, indirect graphics, full formats/
provenance/lifetime matrices and unconditional compiler distribution remain open.
Static no-reread remains source-inspected, not an independent timing GPU oracle.
The four production workstreams and seven complete GPU categories remain open;
no FL/capability promotion, game benchmark or fresh tessellation acceptance.

## Current snapshot (2026-10-03, static DXIL MinMax dispatch checkpoint)

MinMax indirect follow-up: non-root-updating DXIL DISPATCH now inherits the
submission-owned private TLAB and restores the reflected MinMax PSO/threadgroup
size (`D3D12_MINMAX_INDIRECT.md`). Four focused one/two-pair GPU executions across
both builds pass real MIN/MAX reduction and existing direct/lifetime regressions.
Root-updating DISPATCH now also uses submission-owned per-command private TLABs;
both builds pass focused two-command partial-constant/CBV/SRV/UAV GPU readbacks
and typed-origin shared-replay regressions. Broader indirect descriptor/count,
overlap and reset contracts remain open. A subsequent bounded GPU producer/count
probe passes counts 0/1/7 with maximum 2 and nonzero offsets on both builds,
including explicit AIRCONV producer/MSC consumer isolation and actual parameter/
count plus reduction readbacks. No new production defect was found; this removes
that narrow evidence gap, not the broader indirect or MinMax qualification gap.
The four production workstreams and FL qualification remain unchanged.

MSC graphics indirect follow-up: ordinary DRAW/DRAW_INDEXED root updates now
reuse reflected compute TLAB encoding and restore ordinary vertex inputs
(`D3D12_MSC_GRAPHICS_INDIRECT.md`). Primary focused work yields 25 full GPU passes
and three zero-timestamp failures despite correct data. Query qualification stays
open; VB/IB updates, emulated indirect roots and complete graphics acceptance
remain open. This narrows an implementation gap without changing capability
declarations, the four production workstreams or complete matrix workload.

Graphics indirect residency follow-up: ordinary AIR root-updating draws now share
compute's submission-owned allocation snapshots and have explicit resolver-write
visibility (`D3D12_GRAPHICS_INDIRECT_RESIDENCY.md`). Both builds pass focused DRAW
and DRAW_INDEXED late-CBV/gated-lifetime readbacks plus graphics/compute regressions.
MSC graphics TLAB integration remains open. This does not reduce the four production
workstreams, 1,584 typed matrix executions or seven missing complete GPU categories;
no full capability, game, performance or tessellation acceptance is claimed.

Embedded compute root follow-up: Typed origin now retains implicit shader roots
and preserves explicit-root override precedence
(`D3D12_TYPED_ORIGIN_EMBEDDED_ROOT.md`). Both builds pass focused embedded,
override and explicit regression readbacks; different bound roots are rejected.
Graphics, root-updating indirect, full typed acceptance and unconditional compiler
distribution remain open; no feature-level or matrix-status promotion.

Automatic compute selection follow-up: deploying a `dxmt-dxc` compiler/validator
folder beside D3D12 now permits reflected typed-buffer shaders to select the
origin variant without an environment override
(`D3D12_TYPED_ORIGIN_AUTO_SELECTION.md`). Both builds pass focused typed and
ordinary compute readbacks. Distribution packaging, graphics, embedded roots,
root-updating indirect and full qualification remain open; this is not complete
default feature enablement or a change to the acceptance workload below.

Latest typed-origin follow-up: finite legacy dynamic arrays now retain and admit
nonuniform handle flags. Four fresh divergent two-lane GPU readbacks and four
uniform regressions pass across both builds (`D3D12_TYPED_ORIGIN_NONUNIFORM.md`).
This narrows a binding-provenance gap; it does not change the four production
workstreams, 1,584 existing typed case executions or seven missing complete GPU
categories below. Default qualification and complete format/lifetime acceptance
remain open.

Modern typed follow-up: finite SM6.6 binding/annotation chains now reuse guarded
origin/count selection, including divergent lane indices
(`D3D12_TYPED_ORIGIN_MODERN.md`). Both builds pass focused actual static/volatile
readbacks and legacy regressions. Heap/unbounded provenance, default enablement
and complete typed acceptance remain open; the four workstreams and complete
matrix workload below are unchanged.

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
| Typed UAV | Production origin lowering and bounded direct/indirect compute probes exist, including MSC root constants/CBV/SRV/UAV updates; additional-format declaration remains FALSE | Default production qualification, broader indirect/graphics and remaining provenance/view/lifetime contracts, then complete matrices |
| MinMax | Opt-in AIR operations/clamps and DXIL float Texture2D SampleLevel/static roots plus bounded dynamic SampleGrad compute dispatch have focused GPU readbacks | Broader operations/formats/shapes, cube/aniso/feedback, indirect and production qualification remain open |
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

Execution order remains gaps first: close remaining production MinMax/backend
and typed contracts, resolve tiled Tier 2,
and run the complete acceptance matrices. Add only focused regressions needed
to validate each implementation change during this phase. Do not repeat full
matrices after every descriptor/ABI preparation checkpoint.

Instruction-clamp admission is now connected. Current next actions are the
remaining MinMax production operations/backends, not another status-only or
full-matrix rerun.

SampleGrad follow-up: DXIL gradient LOD, bias state and instruction constraints
now reuse the production SampleLevel footprint (`D3D12_MINMAX_DXIL_GRAD.md`).
Both builds pass five actual dynamic D3D12 compute readbacks, alongside native
compiler/state regressions. This closes a bounded operation gap, not full
MinMax; fresh static-gradient, dynamic-clamp and broader contracts remain open.
The four production workstreams, 1,584 typed reruns and seven complete GPU
categories above are unchanged. No feature-level promotion or game benchmark.

Texture2DArray follow-up: the private DXIL SampleLevel/SampleGrad compute path
now admits float array textures and reuses X/Y footprint/gradient lowering while
preserving the layer operand (`D3D12_MINMAX_DXIL_ARRAY.md`). Both builds pass
50 native array dispatches in total and six actual D3D12 MIN/MAX/gradient
readbacks with distinct layer contents. This closes a bounded shape gap; static
array/view-origin/update coverage and broader MinMax contracts remain open.
The four production workstreams and full acceptance workload remain unchanged;
no feature-level promotion or game benchmark.

One-dimensional follow-up: DXIL float Texture1D/Texture1DArray SampleLevel and
SampleGrad now reuse the dimension-parameterized private compute footprint
(`D3D12_MINMAX_DXIL_1D.md`). Both builds pass twenty final production 1D
readbacks total, including distinct array layers, mip selection and the
two-nonzero-derivative max-axis oracle; eight fresh 2D/array readbacks pass.
The remaining broader MinMax/default qualification, typed, tiled and LogicOp
workstreams and full acceptance workload are unchanged. No FL promotion.

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

DXIL follow-up (2026-10-03): internal float Texture2D SampleLevel footprint
lowering emits actual conditional point taps, scalar component extrema and
scalar PHIs. Eight offline containers pass full DXC validation, MSC compilation
and focused native GPU red-channel readbacks (`D3D12_MINMAX_DXIL_LOWERING.md`).
Root/descriptor/submission qualification and point-view state transport are not
yet connected; production DXIL reduction remains rejected. No ABI/capability or
FL promotion, and this is not full MinMax acceptance.

DXIL binding follow-up (2026-10-03): compiler-side pair qualification and private
runtime state branches now pass two fully DXC-validated/MSC-compiled containers,
fifteen single-pair and fifteen two-pair loop native dispatch scenarios
(`D3D12_MINMAX_DXIL_BINDING.md`). Shared texture clamps/defaults are consistent
across pairs; sampler state remains independently indexed. Both builds produce
identical IR and pass host suites. D3D12 native export, root augmentation, PSO
selection and recording/submission ownership are still not connected. This is
compiler preparation, not production DXIL admission. The four production
workstreams, seven missing full GPU categories and 1,584 existing typed case
executions above remain the closure workload; no FL promotion or game benchmark.

DXIL preparation follow-up (2026-10-03): an optional Wine/Unix reduction export
and the actual selected-DXC shader preparation helper now regenerate validated
one/two-pair compute artifacts in both builds
(`D3D12_MINMAX_DXIL_PREPARATION.md`). Existing typed-origin float/CFG regressions
pass in both isolated runtimes; Wine-generated MinMax artifacts pass MSC and
thirty focused native GPU dispatches. Augmented/reflected roots, PSO selection
and recording/submission ownership remain open, so production DXIL MinMax still
is not admitted. No capability, full-matrix or game acceptance change.

MinMax root follow-up (2026-10-03): actual RootSignature objects now cache
augmented/reflected space2 roots by pair count; application indices and range
flags survive and pair identities resolve to heap or ordinary static samplers
(`D3D12_MINMAX_COMPILER_ROOT.md`). Both builds pass five focused Wine
root/location scenarios and existing typed-origin root regressions. This closes
compiler-root/location preparation, not PSO/submission integration or static
reduction admission. Production DXIL MinMax dispatch and complete GPU acceptance
remain open; no capability promotion or game benchmark.

Current MinMax integration follow-up (2026-10-03): actual opt-in DXIL/MSC direct
compute dispatch now connects validated shader preparation, reflected compiler
roots, cached native variants, root staging, coherent static/live tables,
private pair state, completion-owned residency/lifetime and immutable queue
replay (`D3D12_MINMAX_DISPATCH_BINDING.md`). Both builds pass focused one/two-pair
numeric readbacks, fence-controlled overlapping execution, private-to-ordinary
restoration and exact typed-view rejection. This narrows the DXIL runtime gap;
it does not establish default production or full MinMax admission. Static
reduction roots, broader operations/formats/shapes, cube/aniso/feedback, indirect
and production qualification remain open. The four implementation workstreams and 1,584 typed case
executions above remain; seven complete mandatory GPU categories remain
unregistered. No new game/tessellation benchmark or FL promotion.

## Task Analysis

### Historical static DXIL reduction-root audit (2026-10-03)

- Hypothesis: static reduction-root admission cannot be closed by removing the
  ordinary MSC layout rejection alone.
- Evidence: root initialization currently admits static reduction through the
  AIR-specific opt-in only. `InitializeMSCLayout` then rejects
  `HasAIRReductionSamplers`; both compute PSO initialization and `PreDispatch`
  require that ordinary layout before the private dispatch can execute.
  `PrepareRootInternal` preserves application static sampler descriptors when
  serializing the augmented root. Its reflection result therefore is not yet
  evidence of qualified static reduction conversion or execution.
- Expected effect: implement a qualified private compute route through root,
  PSO and dispatch admission, preserving original API descriptors and blob
  identity, instead of broadening ordinary MSC consumer admission.
- Risk: sharing an ordinary layout or native surrogate with unqualified
  graphics, raytracing or typed-origin consumers can silently change sampling
  semantics. Any compiler-only normalization must remain separate from the
  application's original sampler state and cache identity.
- Validation: current source/call-path audit only. No new GPU pass, source
  implementation, capability promotion or game benchmark in this continuation.

Next focused implementation acceptance should cover static MIN, MAX and mixed
ordinary/reduction pairs, original descriptor/blob preservation, defaults-off
rejection and unsupported-consumer rejection in both builds. These are new
focused obligations, not additional completed cases or a full MinMax matrix.
The remaining 1,584 typed executions and seven complete GPU categories above
are unchanged. Count implementation workstreams, not commits or accumulated
focused dispatches, when assessing distance to FL12_0.

Implementation follow-up: qualified explicit static reduction roots now create
private-only DXIL/MSC MinMax compute PSOs and execute through the private
reflected layout (`D3D12_MINMAX_STATIC_DXIL.md`). Both builds pass 100 new focused
static reduction numeric executions, including RS1.0 and mixed pairs, original
root/state preservation, caller root/PSO release and independent consumer/gate
rejection. Ordinary MSC layout and typed-origin rejection remain intact. This
closes the bounded static-root admission gap, not full MinMax: broader
operations/shapes/formats, feedback, indirect/direct-indexed and default
production qualification remain. The four implementation workstreams, 1,584
existing typed reruns and seven complete GPU categories remain outstanding.

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

## Follow-up: MinMax pixel compiler prerequisite

`D3D12_MINMAX_GRAPHICS.md` records stage-aware DXIL pixel preparation, visible
binding resolution and MSC fragment conversion. Normal/no-private full builds
and focused regressions pass, including 16 native render PSO creations and
existing compute GPU readbacks. This does not execute a graphics draw: production
graphics PSO/private binding/recording/submission and pixel reduction readback
remain open. Feature declarations and FL12_0 acceptance are unchanged; compiler
success must not be counted as completion of the mandatory graphics matrix.

The runtime follow-up shares stage-aware MinMax descriptor capture/materialization
between compute and standard vertex/pixel graphics. Both builds verify static
vs volatile replacement and vertex-only table retention, with compute GPU
regressions intact (`D3D12_MINMAX_GRAPHICS.md`). Production graphics variant
selection/render replay and real pixel readback still remain open; no FL gate or
capability promotion follows from these binding tests.

Graphics replay is now connected for bounded DXIL/MSC direct pixel MinMax
SampleLevel/SampleGrad (`D3D12_MINMAX_GRAPHICS.md`). Normal/no-private fresh GPU
readbacks pass static/volatile tables, static reduction samplers, repeat closed
list submissions and ordinary linear draw restoration. This closes the direct
runtime wiring gap, not full MinMax qualification: graphics indirect, pre-raster
sampling, emulation, implicit operations and broader resource/matrix coverage
remain open. Feature declarations and FL12_0 promotion remain unchanged.

Inherited non-updating DXIL/MSC MinMax graphics ExecuteIndirect DRAW/DRAW_INDEXED
is now connected: the GPU resolver restores the matching private PSO while the
ICB inherits submission-private vertex/fragment TLAB bindings. Both variants pass
focused pixel readbacks for CPU-provided counts 0/1/7, clipping to MaxCommandCount
2, repeat closed-list submissions and ordinary draw restoration. Root/VB/IB
updating signatures stay rejected. Graphics root-update private TLABs and
GPU-produced arguments/counts remain open; this is not full MinMax or mandatory
graphics matrix closure (`D3D12_MINMAX_GRAPHICS.md`). No capability promotion.

MinMax graphics root-only updating DRAW/DRAW_INDEXED now uses augmented-root
reflection and submission-private cloned payload/per-command TLABs. Both builds
pass focused pixel readbacks for partial root constants plus CBV/SRV/UAV address
updates, zero/clipped CPU-provided counts, immutable closed-list replay and
ordinary state restoration (`D3D12_MINMAX_GRAPHICS.md`). GPU-produced graphics
arguments/counts, in-flight overlap, same-address root-VA remap and complete
MinMax semantics still remain open. No feature-level or capability promotion.

Bounded DXIL pixel MinMax Sample/SampleBias now reuses explicit gradient/LOD
lowering. Nonzero sampler bias also reaches private SampleLevel and ordinary
MSC shaders (SamplerLODBias compiler compatibility enabled). Both builds pass
focused two-mip pixel readbacks for implicit and explicit sampling, direct and
indirect draws, descriptor snapshots and ordinary state restoration, plus
root-buffer/compute regressions (`D3D12_MINMAX_GRAPHICS.md`). Pre-raster sampling,
cube/aniso/feedback, full resource/filter qualification and mandatory matrices
remain open; this does not promote MinMax declarations or FL12_0/FL12_1.

Standard DXIL graphics VS SampleLevel/SampleGrad MinMax now shares the augmented
root/TLAB with PS using disjoint private pair intervals and per-stage application
visibility. Both builds pass focused VS/PS, VS-only and same-register/different-
visibility pixel readbacks plus existing graphics/compute/root regressions
(`D3D12_MINMAX_GRAPHICS.md`). Fragment-less depth PSO creation is verified, not
depth GPU output. GS/HS/DS/mesh/SO emulation and full resource/filter/matrix
qualification remain open. No capability or feature-level promotion follows.

Bounded MSC GS/HS/DS SampleLevel/SampleGrad MinMax now reaches companion PSO
creation and Object/Mesh/Fragment submission-private TLAB/residency, including
the hull/domain bind point. Private draw configurations use their own reflection;
the common descriptor materializer preserves static/volatile range semantics.
Both full builds and focused new-stage GPU readbacks pass, including ordinary
restore, indexed/direct repeat submissions and a user patch constant
(`D3D12_MINMAX_MSC_PRERASTER.md`). This supersedes the previous bounded emulation
wiring gap, not full pre-raster/MinMax qualification: native mesh/SO, emulated
indirect, cube/aniso/feedback and broader resource/view/lifetime semantics remain
open. The four production workstreams, 1,584 existing typed executions and seven
mandatory complete GPU categories remain unchanged. No capability/FL promotion.

AIR float Cube/CubeArray feedback-free, zero-offset SampleLevel reduction now
uses cube-specific face/seam/corner footprints and existing root/sampler/view
bindings (`D3D12_MINMAX_CUBE.md`). Both builds pass focused signed native helper
footprints and real D3D12 interior/edge/corner/array/mip/point-tie readbacks.
Cube gradients/implicit/feedback and DXIL cube lowering remain closed; broader
view/clamp/lifetime and full resource/filter qualification remain open. This is
an explicit-LOD production increment, not full Cube/MinMax or FL qualification.

AIR float Cube/CubeArray feedback-free, zero-offset SampleGrad now projects
direction derivatives to primary-face space before existing Gram-matrix LOD and
reduction sampling (`D3D12_MINMAX_CUBE_GRAD.md`). Both full builds, focused IR
and real D3D12 gradient/tie/radial readbacks pass. This supersedes the prior AIR
explicit-gradient rejection, not implicit Cube, DXIL Cube or full MinMax
qualification. View/clamp/filter/format/lifetime and mandatory FL gates remain
open; no capability/FL promotion.

AIR bounded float Cube/CubeArray pixel Sample/SampleBias now reuse primary-face
gradient projection and the existing reduction helper, with quad derivatives
and ordinary dynamic samples kept before descriptor-controlled branches
(`D3D12_MINMAX_CUBE_IMPLICIT.md`). Both full builds and focused real pixel
readbacks pass, including radial cancellation, sampler/instruction bias and
gate-off ordinary sampling. This supersedes the prior AIR implicit Cube
rejection; independent DXIL Cube and full resource/filter/format/lifetime /
view/clamp qualification remain open. No capability/FL promotion.

Independent DXIL Cube primary-face gradient LOD is now available as a private
compiler primitive (`D3D12_MINMAX_DXIL_CUBE.md`), with verified IR and focused CPU
arithmetic probes in both builds. Production Cube/CubeArray binding remains
rejected: direction-space footprints, seams/corners, regeneration/MSC/GPU and
captured-view admission are not completed. This is not closure of the DXIL Cube
gap and does not change any MinMax/capability/FL status.

Independent explicitly qualified DXIL Cube/CubeArray SampleLevel footprint
primitive now emits face-interior point directions, edge remapping and guarded
three-face corner union (`D3D12_MINMAX_DXIL_CUBE.md`). Both builds pass verified
IR and focused synthetic-CFG footprint/routing probes. Production binding still
rejects Cube kinds5/9: regeneration validation, MSC and real GPU acceptance are
not completed. This narrows the compiler gap, not full Cube/MinMax or FL closure.

Bounded float, feedback-free, zero-offset DXIL Cube/CubeArray paired binding now
reaches independent cube footprints/gradients and the captured Cube MSC view
(`D3D12_MINMAX_DXIL_CUBE.md`). Final matching builds pass focused actual compute
readbacks in both configurations, including modern SM6.6 handles and AIR/planar
regressions. Pixel Sample/SampleBias has regenerated DXIL, exact xyz derivative
IR checks and offline MSC conversion. The subsequent production-root graphics
fixture now passes bounded Cube/CubeArray Sample/SampleBias GPU readbacks in both
builds: interior +Z mip0, mip1 selection, array-layer poison, static/live
descriptors, direct/indirect/indexed draws and ordinary sampling restore. Bias
uses equal target-cube mip1 faces and does not independently qualify face routing
or reduction versus ordinary filtering there. This
supersedes the prior kind5/9 binding rejection, not full Cube/MinMax qualification.
Anisotropic/feedback, broader format/view/clamp/lifetime and mandatory FL matrices
remain open. FL11_1 unchanged; FL12_0/FL12_1 unpromoted. Full objective active.

Independent statically resolved DXIL f32 comparison consumers now survive MinMax
shader regeneration (`D3D12_MINMAX_COMPARISON_COEXISTENCE.md`): exact nonvariadic
SampleCmp/SampleCmpLevelZero signatures and sampler comparison metadata/modern
annotation bits are qualified before mutation; compare calls remain native MSC,
not reduction pairs. Both builds pass legacy/modern CS/PS container regeneration,
DXIL validation and offline MSC plus regular Cube GPU regressions. At that
compiler milestone, mixed comparison/reduction GPU output was unverified; no
sampler/filter or FL capability was promoted by compiler compatibility alone.

The subsequent bounded production-root compute fixture now passes native
SampleCmpLevelZero plus independent regular Min/Max/ordinary sampling
(`D3D12_MINMAX_COMPARISON_GPU.md`). Both builds execute legacy/modern DXIL:
four processes, 48 exact readback groups, two uniform depth values on opposite
sides of the reference, both comparison directions and repeated closed-list
volatile sampler updates. This is DXMT_LOCAL_PASS, not native Windows evidence,
full comparison/PCF coverage or full MinMax qualification. A pre-sampling
no-private allocator-reset failure passed an unchanged-binary retry; at that
milestone the cause was unresolved and sampling used separate upload/dispatch storage.
Pixel/static comparison paths and the broader production workstreams remain
open. FL11_1 unchanged; FL12_0/FL12_1 disabled; full objective active.

The allocator follow-up (`D3D12_ALLOCATOR_GPU_COMPLETION.md`) now replaces
CPU-worker-dependent Reset rejection with retained per-queue GPU completion
markers. Identical final executables fail against cached baseline DLLs in both
configurations, then pass4096 cycles across four current-runtime processes.
Distinct-list cross-queue/later-use blocking guards pass; registration unwind and
no-copy GPU backing-buffer reuse are repaired. Earlier invalid same-list overlap
runs are excluded. Forced OOM/late-worker injection, buffer identity fingerprint
and performance cost remain unverified. This closes the bounded Reset race, not
full allocator/lifetime or FL qualification; full production objective stays active.

### Typed-origin pixel production path (2026-10-04)

[Task record](D3D12_TYPED_ORIGIN_PIXEL.md): bounded opt-in DXIL native VS/PS draws
now reuse compute origin descriptors/records and submission materialization.
PIXEL/ALL table visibility and pixel root denial are honored; ordinary PS cannot
bypass typed-VS rejection. Static capture/live submission timing and immutable
private command replay are preserved. Final same-binary old-DLL failure/current
pass verified on normal and no-private; SM6.0/6.6 pass64 private and64 ordinary
GPU draws plus VS-root, live replacement and boundary guards. Compute/root,
allocator and MinMax compute/graphics regressions pass. Broader stages/formats,
default selection, parallel lifetime timing and game/performance acceptance remain
open. No capability or FL promotion; full production objective remains active.

### Typed-origin deployed graphics selection (2026-10-04)

[Task record](D3D12_TYPED_ORIGIN_GRAPHICS_SELECTION.md): validated DXIL graphics
now selects the existing pixel repair from DXC deployed beside D3D12 without an
environment override. Ordinary shaders remain ordinary; absent deployment keeps
prior compatibility, present broken compiler fails explicitly, valid override
takes priority. A late MinMax switch cannot bypass retained origin selection.
Final same-binary old-DLL failure/current pass and64 automatic private GPU draws
are verified on normal/no-private and SM6.0/6.6; ordinary, explicit, missing/broken,
late-switch, compute and MinMax graphics controls pass their expected outcomes.
Default distribution policy, VS/PS shared records and full typed/FL matrices
remain open. No capability promotion; full production objective stays active.

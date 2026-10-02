# FL12_0 closure checkpoint

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

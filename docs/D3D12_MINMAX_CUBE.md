# Task Analysis

## Current Branch / Baseline / Local Commits

`feat/d3d12-1`, clean at `52a0c6c`; 119 local commits beyond
`origin/feat/d3d12` (`e147c710`). Merge base `85bb2dd2`.

## Current State / Existing Implementation

MinMax has bounded 1D/2D/3D and array paths, plus MSC pre-raster wiring.
Cube/CubeArray remain excluded by AIR reduction helper and consumer lowering,
and DXIL pair preparation. Ordinary cube sampling and view binding already exist.

## Relevant Files / Existing Tests

`src/airconv/shaders/air_minmax.metal`, `nt/air_builder.cpp`,
`nt/dxbc_converter_base.cpp`, consumer classification in `dxbc_converter_cfg.cpp`,
`src/winemetal/unix/dxil_minmax_binding.cpp`; existing `air_minmax_ir`,
`air_minmax_gpu` and `dx12_texture_sampler` probes.

## D3D12 Contract

Reduction uses the contributing ordinary-filter footprint, not averaging.
Cube edges cross to adjacent faces; corners involve three faces. Cube direction
gradients require face-space projection before LOD calculation, not a 3D volume
gradient. Source: [D3D11.3 functional specification, sections 7.18.11–12](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).

## Hypothesis / Evidence / Expected Effect

AIR can reuse its current reduction/mip policy with a cube-specific face/tap
mapper and existing point-sampler ABI. Existing native cube point sampling
provides exact texel-center accesses without introducing a new descriptor view
or mixing shader families. Evidence: helpers currently reject cube at selection;
ordinary converter already preserves float3 direction and cube-array layer.
Expected effect: eliminate the cube explicit-LOD production gap first, then
connect direction-gradient lowering and the separate DXIL backend.

## DXBC / AIRCONV Impact

Add cube and cube-array footprint helpers; connect explicit-LOD consumer
qualification and clamped helper linkage after focused GPU verification.

## DXIL / MSC Impact

Still a separate implementation. Do not admit cube in DXIL merely because AIR
supports it. Follow with DXIL face/gradient lowering and captured-view binding.

## Shared Runtime Impact

Reuse existing cube views, static/volatile descriptor observation, sampler
metadata and resource-clamp/default-component handling. No ABI merge.

## Missing Pieces / Risks

Face orientation, edge row reversal, three-face corners, zero-weight exclusion,
mip boundaries, array/view origin, resource clamps and shader helper ABI.
Gradient/implicit, anisotropic and feedback remain guarded until implemented.
No approximation of an ordinary 2D address mode at cube seams is acceptable.

## Minimal Implementation Plan / Validation Plan

Use face basis projection with Z/Y/X tie precedence. Remap a single-edge tap at
the boundary into the adjacent face's nearest row/column; reduce the three real
corner texels for a double-edge tap. Point taps use face texel centers, avoiding
projective drift from sampling directions beyond an edge. Reuse min/mag/mip
policy and conditional contributing-tap accesses. Extend native helper and
linked-IR probes, then existing D3D12 sampler harness for Cube/CubeArray explicit
LOD interior/edge/corner readbacks on both builds. Reconfigure/build both full
variants before isolated staging. Self-review and local commit; no push.

## Capability Impact

No FL/SM/capability promotion, no game deployment or full matrix acceptance.
The full FL12_0 → FL12_1 goal remains unchanged and active.

## Additional Task Analysis: point-filter face ties

- Hypothesis: seam remapping must only apply to linear footprints. At an exact
  face tie a point footprint can land at normalized coordinate 1 on the selected
  face; forwarding that tap to an adjacent face would override Z/Y/X selection.
- Evidence: `cube_level` currently routes even non-linear taps through
  `cube_tap`. The current real seam probes use linear filters, so cannot detect
  this point-filter distinction.
- Expected effect: a point footprint clamps to its selected face's edge texel;
  linear footprints keep adjacent-face/corner remapping.
- Risk: accidentally suppressing linear seam taps or admitting cube gradients.
- Validation: add MIN/MAX point-filter tie probes for Cube and CubeArray;
  repeat focused linear seam/mip/array and existing backend regressions.

# Task Result

## Branch / Baseline / Changed Files

`feat/d3d12-1`, task baseline `52a0c6c`, remote baseline unchanged at `e147c710`.
Production changes are limited to AIR's helper, builder, SampleLevel lowering
and consumer classification. Tests extend existing `air_minmax_ir`,
`air_minmax_gpu` and `dx12_texture_sampler`; this document and the closure ledger
record the bounded result. The local commit is reported at handoff; no push.

## Implementation / Backend and Runtime Impact

Float, zero-offset, feedback-free AIR Cube/CubeArray SampleLevel now reuses
existing sampler state, cube views, runtime reduction selection and resource
clamp/default-component handling. Private helper symbols have matching linked
cube/cube-array signatures. Cube gradients, implicit sampling and feedback
remain rejected. DXIL cube preparation is unchanged and remains closed; DXBC
does not route to MSC and DXIL does not route to AIRCONV.

The cube-specific footprint mapper uses selected-face texel centers. Linear
seams remap at the face boundary; double-edge taps reduce three real corner
texels. Point taps clamp inside the selected face, preserving Z/Y/X tie
precedence. Noncontributing upper spatial/mip taps remain conditional. No new
descriptor ABI, shared-runtime source changes or shader-family fallback.

## Tests Added / Tests Run / Runtime Results

Evidence directory: `/Users/zhangbo/.cache/dxmt-minmax-cube.hpyorE`.
Both configurations were reconfigured and both full default builds completed
after optional focused builds, before final isolated DLL staging. No game or
prefix DLL deployment, Steam/wineserver restart or process management.

- Linked AIR helper/clamp IR probes pass **10/10 per configuration**, including
  cube/cube-array signatures and nonzero-offset rejection without IR mutation.
- Extended native helper GPU probes pass **268/268 per configuration** on Apple
  M4. Each includes 92 prior results plus 176 new Cube/CubeArray results: six
  face interiors, twelve signed off-center edges, eight signed corners, six
  exact texel centers, six fractional-mip and six exact mip-boundary footprints,
  each MIN/MAX and both texture kinds. Array layer 1 is distinct. All four
  components are checked; Y-face-specific B extrema expose the otherwise
  intermediate third corner face. Hand-enumerated expected texel sets do not
  invoke the production projection/remapping algorithm.
- Final Windows set comprises **52 successful processes** (`*-acceptance.log`):
  40 new Cube/CubeArray readback processes, two expected static Cube SampleGrad
  PSO rejections, eight AIR regressions and two MSC tessellation regressions.
  These counts exclude earlier diagnostics/retries and do not count negative
  probes as GPU dispatches. The two MSC regressions each verify 16 submissions.
- Host Meson suites pass **5/5 per configuration**. No complete GPU matrix,
  Metal validation, native Windows hardware oracle or game benchmark was run.

D3D12 face interior MIN/MAX returns 16/240; +X/+Z linear edge 16/192;
+X/+Y/+Z corner 16/80; fractional two-mip reduction 8/240. CubeArray layer 1
matches these results while layer 0 contains the distinguishable value 7.
Static cube samplers return 16/240. Exact +X/+Z point-filter ties return 80 for
both reduction modes and both texture kinds. Existing same-PSO ordinary/reduction
switching returns 16/240/128/16; 1D-array gradients return 192, major-axis gradient
LOD returns 96, and the static 2D regression returns 16.

Reproduce a final normal-build point-tie probe:

```sh
WINEDEBUG=-all MVK_CONFIG_LOG_LEVEL=0 \
DXMT_ENABLE_AIR_MINMAX_DYNAMIC=1 DXMT_ENABLE_AIR_MINMAX=1 \
DXMT_SHADER_CACHE_PATH=/Users/zhangbo/.cache/dxmt-minmax-cube.hpyorE/repro-cache \
WINEDLLOVERRIDES='d3d12,dxgi,winemetal=n,b' \
WINEDLLPATH=/Users/zhangbo/.cache/dxmt-minmax-cube.hpyorE/normal \
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
/Users/zhangbo/.cache/dxmt-minmax-cube.hpyorE/runtime/bin/wine \
  /Users/zhangbo/.cache/dxmt-minmax-cube.hpyorE/normal/dx12_texture_sampler.exe \
  --dxbc --minimum-cube-array-edge-point
```

Use `runtime-no-private`/`no-private` for the other variant. Native footprint
command: `build/tests/dx12/air_minmax_gpu src/airconv/shaders/air_minmax.metal`
(or `build-no-private/tests/dx12/air_minmax_gpu`).

Final staged/build SHA1: D3D12 normal `79c8440f75e91870b473dc3653faed782cd712f6`,
no-private `5582c8d1d41d4b7508a9d56d616803d56c8af6b9`; native winemetal normal
`8c0653675aa5a0debfb8a6c16fe45555c45672fd`, no-private
`1b8e2dbd852da455446cea3e771ba512ddca07c3`.

## Standards

Main-agent self-review and an independent read-only Standards reviewer found no
hard breach. Two optional maintenance suggestions remain: sharing mip-policy
selection across footprint shapes, and replacing the test compiler's positional
shape/case parameters with a probe description. The explicit shape-local policy
and existing harness style are retained for this production increment.

## Spec

The initial independent source review identified partial seam/orientation and
clamp/footprint evidence. The added native fixture closes the focused signed
helper orientation, third-corner and zero-weight/mip coverage gaps; its oracle
was independently source-reviewed without an actionable finding. D3D12 view
origins/resource clamps and mixed min/mag filters remain unqualified for Cube.
Native helper probes do not establish those host contracts. Reviewers did not
run GPU tests; their source reviews are not independent runtime confirmations.
Main review fixed the point-tie defect and kept all unsupported operation/backend
guards. `git diff --check` passes. Standards: two optional findings; Spec: one
remaining qualification area (D3D12 cube view/clamp/filter contracts).

## Known Limitations / Capability Status / Feature Level Impact

Status: **PARTIAL**, an AIR explicit-LOD production increment, not full cube or
MinMax acceptance. DXIL Cube/CubeArray, direction-gradient/implicit operations,
anisotropic filters, residency feedback, broader view/clamp/filter/format and
lifetime contracts remain open. Full mandatory matrices and fresh game evidence
are also open. FL11_1 unchanged; FL12_0/FL12_1 remain unpromoted. The full goal
remains active.

## Git / Push Status / Next Recommended Task

Finish local commit after final evidence audit; **NOT PUSHED**.
Next close cube direction-gradient and independent DXIL footprint/binding gaps,
then remaining mandatory resource/filter contracts before broad matrix reruns.

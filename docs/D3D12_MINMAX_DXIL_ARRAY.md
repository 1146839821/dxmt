# DXIL MinMax Texture2DArray

## Task Analysis

- Hypothesis: the existing two-dimensional SampleLevel footprint and SampleGrad
  LOD lowering can serve Texture2DArray without filtering across array layers.
- Evidence: tap emission rewrites coordinate operands 3/4 only; array coordinate
  operand 5 is copied. Dimension results retain width/height and mip count in
  the same positions. Current compiler and native-view qualification reject arrays.
- Expected effect: admit float Texture2DArray through the existing private
  production compute route, preserving layer coordinates in both branches.
- Risk: shader resource arrays are not texture array layers. Register-array,
  feedback, integer, multisample and other existing exclusions must remain.
  Native uploads and D3D12 subresource footprints must distinguish the layers.
- Validation: compile array fixtures with selected DXC, regenerate/validate DXIL,
  compile with MSC and read back distinct-layer results; production D3D12
  MIN/MAX dispatches in both builds, plus existing 2D regressions. No full matrix,
  capability promotion, installed DLL replacement or game-performance claim.

Reference: [DXIL resource and sampling specification](https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/main/docs/DXIL.rst).

## Task Result

Float Texture2DArray legacy SampleLevel/SampleGrad pairs now qualify through the
existing private DXIL compute path. Resource kind 7 was confirmed in selected
DXC output. The cloned private point view preserves that kind; both ordinary
and reduction samples keep coordinate operand 5 unchanged. Gradient LOD still
uses only X/Y extents and derivatives. Native binding accepts 2D-array views
while preserving integer/depth/multisample and existing state exclusions.
The private cache version is 3; the 32-byte state layout is unchanged.

Fresh evidence directory: `/Users/zhangbo/.cache/dxmt-minmax-array.iMXmT6`.

- Both Meson configurations regenerated, full default builds completed, and
  host suites passed 4/4 each.
- Array SampleLevel and SampleGrad transformed IR is byte-identical across
  variants; final rebuilt tools reproduce the validated artifacts. LLVM units
  and ten qualification probes pass, including register-array/status/unsupported
  cube rejection and duplicate-pair merging. Single-pair transformation asserts
  every generated sample retains the original layer operand.
- Both regenerated containers pass full selected-DXC validation and MSC 4.0.1
  Apple9 compilation. Native GPU tests pass 15 SampleLevel and 10 SampleGrad
  state scenarios per build: 50 array dispatches total. These include ordinary
  branches, sampler/resource constraints, defaults and gradient bias.
- Production D3D12 tests pass MIN=16, MAX=240 and gradient LOD=96 per build:
  six array dispatches total. The gradient resource uploads eight subresources
  (four mips per layer). Every layer-zero mip is 7, distinct from layer-one
  expected values; this detects dropped layer selection.
- Existing 2D two-pair native cases pass 15 dispatches per build; fresh
  production 2D gradient dispatch passes 96 in each isolated runtime.
- DLL source/stage SHA-256 matches and loader traces identify the task-owned
  app DLLs and Unix winemetal. No installed-prefix/game DLL replacement.

Fixture recipe: compile `tests/dx12/minmax_array.hlsl` with `cs_6_0`, adding
`MINMAX_ARRAY_GRAD=1` for gradients and `MINMAX_ARRAY_STRUCTURED=1` for D3D12
structured-output dispatch. Native IR modes are `binding` and `binding-grad`;
regenerate with `dxil_roundtrip.exe --assemble-typed-origin-ir`, then MSC with
`--minimum-gpu-family=Apple9 --textureMinLODClamp` and fresh reflection.
Native GPU modes are `--binding-array` and `--binding-array-grad`.
Production modes are `--minimum-2d-array`, `--maximum-2d-array`, and
`--minimum-2d-array-grad`, with `DXMT_MINMAX_DXC_DIRECTORY` selecting DXC and
AIR MinMax gates disabled. The Meson array fixture target emits structured
SampleLevel; the same source generates the gradient fixture via the macro.

Standards self-review: reuse existing qualification, footprint, binding and
probe conventions; no new tap implementation or unrelated mutation. Fixed the
test-tool sample-count guard before indexing. Spec self-review: distinct-layer
native and production readbacks prove bounded compute integration, not complete
array filtering coverage. Main-agent self-review only; no independent review.

Remaining: array static-root/live-update/view-origin/fractional-layer coverage,
broader formats/operations/shapes, cube/aniso/feedback, indirect and default
production qualification. Existing resource-register arrays remain rejected.
No capability/feature-level promotion, full acceptance matrix, Metal-validation
claim, tessellation acceptance or fresh game-performance result.

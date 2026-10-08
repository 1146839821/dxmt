# DXIL MinMax one-dimensional textures

## Task Analysis

- Hypothesis: parameterizing spatial dimension count in the existing reduction
  footprint and gradient helper admits float Texture1D/Texture1DArray without a
  second implementation or treating array layers as spatial coordinates.
- Evidence: current loops use two dimensions unconditionally and qualification
  allows resource kinds 2/7 only; 1D-array layer occupies coordinate operand 4.
  Existing D3D12 probe already uploads and verifies distinct 1D array layers.
- Expected effect: connect 1D explicit-LOD and gradient operations through the
  private DXIL production compute route, preserving excluded contracts.
- Risk: absent dimension/gradient operands must never enter arithmetic; 1D
  array layers must not receive address/offset/filtering operations. MSC maps
  logical 1D views to native 2D views, so actual binding must be checked.
- Validation: selected DXC fixtures and full regenerated DXIL validation;
  actual D3D12 MIN/MAX/gradient readbacks in both builds; 2D/array compiler and
  GPU regressions. No full matrix, default enablement or feature-level claim.

## Task Result

The private legacy float compute route now qualifies Texture1D and
Texture1DArray (resource kinds 1/6, confirmed in selected-DXC fixture metadata).
The existing footprint helper takes a validated spatial dimension count: 1D
uses two tap sites per mip and never rewrites the array-layer coordinate.
1D gradient LOD directly uses log2(max absolute scaled derivative); 2D retains
the normalized major-axis implementation. Common state, clamps, bias,
descriptor/root staging and submission ownership remain shared.

MSC maps these logical types to native 2D/2DArray views. The existing D3D12
resource/SRV constructors already use those types, so no new runtime native
view exception is needed. The compiler cache version is 4; no state-layout or
public capability change.

Evidence: `/Users/zhangbo/.cache/dxmt-minmax-line.owPBHn`.

- Both configurations regenerated; both final full builds completed; host
  suites pass 4/4 each. LLVM units include pre-mutation invalid-dimension
  rejection and existing status/dominance/gradient checks.
- Six fixtures (1D/array SampleLevel and SampleGrad, multi-mip gradient and
  two-nonzero-derivative gradient) produce identical transformed IR in both
  variants. Full selected-DXC regeneration/validation and MSC 4.0.1 Apple9
  compilation pass for all six final containers.
- Qualification probes preserve unsupported resource-register arrays, feedback,
  cube and other exclusions. Duplicate pairs produce ten SampleLevel sites
  for two 1D consumers instead of eighteen for two 2D consumers. The tool checks
  layer operand 4 for 1DArray and operand 5 for 2DArray.
- Actual D3D12 dispatch passes ten scenarios per build, twenty final 1D GPU
  readbacks total: non-array MIN/MAX 16/64, array MIN/MAX 192/240 for both
  SampleLevel and SampleGrad, gradient mip selection 224 and two-nonzero-
  derivative selection 32. Array layers contain distinct values.
- Fresh 2D/2DArray dispatch regression passes four scenarios per build (eight
  readbacks total). Array transformed IR remains byte-identical to the previous
  validated lowering in both builds.
- Loader traces identify task-owned app DLLs and Unix winemetal; staged D3D12
  and Unix binaries match final source-build SHA-256. Installed prefix/game
  DLLs were not replaced. Initial rank-one-gradient outputs are retained as
  intermediate evidence, not final acceptance artifacts; final logs/artifacts
  have `.final` in their names.

Fixture recipe: compile `tests/dx12/minmax_line.hlsl` with `cs_6_0`; define
`MINMAX_LINE_ARRAY=1` for arrays and `MINMAX_LINE_GRAD=1` for gradients.
`MINMAX_LINE_DERIVATIVE=0.23` produces the mip-1 oracle; setting both
`MINMAX_LINE_DERIVATIVE` and `MINMAX_LINE_DERIVATIVE_Y` to `0.15` produces
the mip-0 oracle. Native IR modes are `binding`/`binding-grad`; fully regenerate
with `dxil_roundtrip.exe --assemble-typed-origin-ir` before MSC conversion.
Actual D3D12 modes are the existing `--minimum/maximum-1d[-array][-grad]`,
`--minimum-1d-grad-lod`, and new DXIL-only `--minimum-1d-grad-axis`.
Use `DXMT_MINMAX_DXC_DIRECTORY` and disable AIR MinMax gates for DXIL evidence.

Main-agent Standards/Spec self-review completed; no independent review.
Reused shared helpers instead of duplicating filtering; corrected the rank-one
floating-point assumption and verified distinct layer/mip values. Remaining
full acceptance includes broader sampler/address/offset/format/view/lifetime,
static/indirect paths, other shapes/operations and default production
qualification. This is bounded integration, not full MinMax or FL12_0 closure.
No Metal-validation, tessellation, fresh game or performance acceptance claim.

Reference: [Direct3D sampling LOD rules, section 7.18.11](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).

### Review-driven Task Analysis

- Hypothesis: the generalized Gram determinant can round positive for rank-one
  1D gradients and incorrectly use a root-sum-square footprint.
- Evidence: a one-dimensional texture requires max absolute X derivatives;
  both derivatives 0.15 at width 8 select point mip 0, whereas root-sum-square
  selects mip 1. Existing gradient tests have one zero derivative.
- Expected effect: return log2(max absolute scaled derivative) directly for 1D.
- Risk: preserve zero/Inf rules and leave the 2D normalized algorithm unchanged.
- Validation: a production 1D two-nonzero-derivative oracle must read 32, not 224;
  rebuild both variants and rerun the focused paths.

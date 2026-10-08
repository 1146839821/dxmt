# DXIL MinMax Texture3D integration

## Task Analysis

- Hypothesis: the normalized gradient algorithm can include a third spatial
  axis while keeping two screen-space derivative vectors and the existing
  dimension query. This prepares the real shared lowering for Texture3D.
- Evidence: the gradient helper currently rejects dimension 3 and stores two
  components only. The production pair state has AddressU/V but no AddressW;
  opening Texture3D qualification now would discard W addressing semantics.
- Expected effect: implement shared three-dimensional gradient emission without
  admitting an incomplete Texture3D sampler contract.
- Risk: reading Z derivatives or depth incorrectly; changing 1D/2D output;
  confusing LLVM structural validity with numeric GPU acceptance.
- Validation: native LLVM unit checks the depth extraction and six absolute
  derivative operations, no fast-math and module validity in both builds;
  existing lower-dimensional compiler/GPU regressions remain required for
  eventual integrated admission. No Texture3D GPU/capability claim here.

## Remaining integration

The preparation checkpoint below is historical. The production integration
result at the end supersedes its Texture3D rejection status.

## Production integration Task Analysis

- Hypothesis: pack AddressV/W into separate bytes of the existing address word;
  explicitly decode them for the shared 1..3-axis footprint, retaining the
  32-byte CBV stride and all sampler bias/clamp/default contracts.
- Evidence: legal address modes fit in 1..5; current state transports only U/V.
  The point tap loops already use spatial_dimensions but store only two axes.
- Expected effect: qualify float Texture3D SampleLevel/SampleGrad and native 3D
  views through the existing production compute path.
- Risk: old artifacts must not consume packed V as a raw address; bump cache
  version. W clamp/wrap must use the original native sampler, not copied V.
  Volume depth is not an array-subresource count; upload/copy must reflect this.
- Validation: two full builds, sampler state/pair probes, full selected-DXC/MSC
  validation, actual MIN/MAX/depth-gradient/W-address readbacks, and lower-
  dimensional regressions. No full matrix or capability promotion.

## Gradient preparation result

The existing gradient emitter now accepts three spatial axes with three-element
derivative storage. It reads view depth and both Z derivatives in the same
normalized algorithm; the footprint helper and production pair qualification
still reject Texture3D. This is structural preparation, not numeric 3D GPU
acceptance or a completed Texture3D implementation.

Both configurations regenerated and both full default builds completed. Native
LLVM units pass with six absolute-derivative operations, one depth extraction,
no fast-math and valid IR. The invalid-dimension pre-mutation test now uses 4.
The two builds emit identical unit IR. Existing 1D two-nonzero-derivative and
2D-array gradient lowering remains byte-identical to the previous validated
artifacts. Host suites pass 4/4 in each build.

Evidence: `/Users/zhangbo/.cache/dxmt-minmax-volume.eMrsBO`.
Main-agent self-review checked loop bounds, declaration/IR validity, unchanged
lower-dimensional output and retained production rejection. No independent
review, runtime deployment, fresh GPU dispatch, MSC conversion, capability
promotion or game-performance result in this preparation checkpoint.

## Production integration result

Float Texture3D SampleLevel/SampleGrad now use the existing qualified compute
pair path. AddressV/W occupy separate bytes of the same 32-byte state; cache
version 5 separates the new decoding contract. Three axes emit eight conditional
spatial taps per mip and retain the native texture3D view and sampler lifetime.

Both reconfigured full builds and native LLVM units passed. Three transformed
fixtures passed selected-DXC container regeneration/validation and MSC Apple9
compilation with reflection. Fresh isolated runtime copies loaded the new PE
and Unix libraries; no installed game or prefix DLLs were replaced.

Each build passed five actual volume readbacks: MIN 16, MAX 240, three-axis
gradient LOD 160, W wrap 16 and W clamp 64. The gradient oracle distinguishes
the normalized major axis from raw derivative maximum (96) or ignored depth
(32). Each build also passed five lower-dimensional readbacks: 2D-array
MIN/MAX/gradient, 1D two-nonzero-derivative gradient and 1D-array gradient;
sampler-state and pair-binding probes passed. Host suites passed 4/4 each.

Main-agent self-review checked address packing/cache invalidation, pre-mutation
type rejection, metadata shape, conditional tap bounds and volume mip/depth
copy footprints. Evidence: `/Users/zhangbo/.cache/dxmt-minmax-volume-binding.V8iGyn`.
This is bounded compute float sampling acceptance, not complete MinMax coverage,
FL12_0 promotion, independent review, tessellation or game-performance acceptance.

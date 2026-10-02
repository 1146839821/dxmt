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

AddressW state transport, three-axis footprint/eight conditional spatial taps,
texture-view qualification, selected-DXC/MSC regeneration and distinguishable
volume GPU readbacks are still required before production admission.

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

# Task Analysis

Baseline f2c3f13. Continue the original FL12_0 gaps-first objective.

- Hypothesis: AIR's existing 1D-to-2D storage remap can reuse reduction kernels
  if logical dimensionality controls tap enumeration independently of storage.
- Evidence: 1D coordinates/gradients already remap into float2; reduction rejects
  logical 1D and 1D-array even though the native storage is 2D/2D-array.
- Expected effect: close the 1D operation gap without mixing shader backends.
- Risk: Y border/address modes must not affect a logically 1D sample; array slice
  and offsets must be preserved; qualification must not precede GPU validation.
- Validation: build both variants, then focused minimum/maximum, array-slice,
  gradient/mip and Y-border readbacks before admitting the new dimensions.

The instruction-clamp candidate was examined first. D3D11.3 sections 5.8.6 and
5.9.4.5.4 require out-of-view clamp accesses to return defined-component zeros
and missing-component defaults. Simply clamping to the final mip is incorrect.
Current resource metadata carries array length and clamp, not a component mask.
Do not enable that candidate until the default-component contract is resolved.
Source: https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm

# Task Result

Logical-dimensional kernel support is implemented. Compiler-derived bit 6
selects one-axis tap enumeration through existing 2D/array storage; Y is fixed
at 0.5 and Y offset is zeroed. Sampler descriptors never supply this shape bit.
The lowering helper derives it from the logical texture kind, but consumer
eligibility still rejects 1D until the next end-to-end integration checkpoint.

Both winemetal.so builds completed. Both native GPU probes pass 92/92 on Apple
M4, including the existing 88 results and four new minimum/maximum results.
The new probes use actual 2x1 storage, distinct array layers, border addressing,
Y coordinates -10/+10 and Y offsets +17/-17. All four components are checked.
They confirm logical tap dimensionality and array selection in the production
kernel source, not Windows descriptor binding, gradient LOD or full 1D support.

Main-agent Standards self-review checked flag separation, reuse and fixture
descriptor restoration; Spec self-review checked synthetic-Y isolation and
unchanged admission. git diff --check passes. Independent review unavailable;
no independent-review claim. No FL/SM promotion, game deployment or full MinMax
closure. Next: connect 1D SampleLevel/SampleGrad qualification and run isolated
D3D12 readbacks including real descriptor transport and multi-mip gradients.

## D3D12 integration checkpoint

Baseline 3b6e417. The existing hypothesis/risk/validation above also guides this
integration: broaden reflection and lowering together, then require actual
Windows descriptor/PSO/dispatch readback. The earlier rejection statement is
historical; supported no-feedback SampleLevel and no-instruction-clamp SampleGrad
now admit float 1D and 1D-array under the existing opt-in gates. The shared helper
preserves logical dimension independently of native 2D/2D-array storage. Padded
1D gradient vectors have Y=0, so existing parallel-gradient LOD handling uses
the largest X derivative scaled by view width.

Both production builds succeed. Isolated host/Unix DLL copies, with dyld path
confirmation and shader cache disabled, pass nine D3D12 executions per variant:
SampleLevel and SampleGrad Min/Max return 16/64 on a two-texel 1D resource;
both operations return 192/240 on slice 1 of a distinguishable two-slice array;
an 8-wide four-mip resource with derivative 0.23 selects mip 1 and returns 224.
The analytic expected LOD is log2(8*0.23), about 0.88; point mip selection is 1.

Four focused regressions per variant pass: existing 2D same-PSO switches,
nonorthogonal multi-mip gradients, static instruction-clamp PSO rejection and
MSC consumer resolver rejection. These rejection checks are not successful GPU
dispatches. Meson passes 3/3 per variant; git diff --check passes. Main-agent
Standards review checked subresource footprints, array unions and unchanged
ordinary lowering; Spec review checked logical/native shape separation, gradient
scaling, retained clamp/feedback rejection and no backend fallback. No blocking
finding; independent review unavailable. No full matrix, game/performance run or
FL/SM promotion. Static 1D sampler and broad address/filter/lifetime combinations
remain outside this focused acceptance; complete MinMax and FL12_0 remain open.

# Task Analysis

## Current Branch / Baseline
feat/d3d12-1 / origin/feat/d3d12; starting point 14cc788, clean worktree.
## Local Commits / Current State
Typed-origin direct/indirect integration retained. Min/Max still rejected.
## Existing Implementation / Relevant Files
AIR sampling emits ordinary native sample operations; D3D12 sampler conversion
rejects reduction modes. AIR helpers already use linked Metal-generated bitcode.
## Existing Tests / D3D12 Contract
Existing semantic sampler repro is red; rejection coverage is not semantic support.
Reduction takes componentwise extrema of contributing taps, excluding zero weights.
[Microsoft functional specification, Min/Max filtering](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
## DXBC / AIRCONV Impact
Implement the explicit-LOD non-anisotropic reduction primitive in the AIR backend.
## DXIL / MSC Impact
None; needs independent DXIL lowering, not AIR helper fallback.
## Shared Runtime Impact
None yet. Keep sampler rejection until ABI and feedback integration is complete.
## Missing Pieces / Risks
Mixed filters, mip weighting, address modes and zero-weight taps are central.
Host point-sampler creation, exact filter/LOD metadata, operation-specific lowering,
cube/1D/depth/anisotropy, sparse feedback and DXIL lowering remain required.
## Minimal Implementation Plan / Validation Plan
Implement the real sample kernel, linkable AIR primitive and native Metal readback
probe. Build both variants; run focused kernel checks, not full acceptance matrices.
## Capability Impact
No declaration or D3D12 acceptance changes.

Hypothesis: point samples at selected texel centers can implement non-anisotropic
reduction without native Apple10 reduction support. Evidence: AIR exposes explicit
LOD sampling and dimension queries. Expected effect: a usable reduction backend
primitive, not a replacement full-support target. Risk: incorrect contributing
footprints/LOD or helper ABI. Validation: actual GPU probe plus AIR linkage checks.

# Task Result

## Branch / Baseline / Local Commit
feat/d3d12-1 / origin/feat/d3d12; see the commit containing this report.
## Changed Files / Implementation
AIR shader asset, precise Metal bitcode generation, context linker, AIRBuilder
entry point, native Metal and LLVM-linkage probes, build registration and docs.
The primitive covers float 2D/2D-array/3D explicit-LOD, non-anisotropic extrema,
independent min/mag flags, nearest/linear mip selection, offsets and zero-weight
tap exclusion. The first contributing value seeds the accumulator; no infinity
seed corrupts an all-NaN footprint. The caller supplies finite coordinates, an
unbiased/unclamped point sampler carrying address/border modes, a precomputed
minification flag and already biased/sampler/resource-clamped LOD.

Unsupported kinds/types/operand ABI return None. Linked helper definitions are
required by the probe; declaration-only or bitcast-signature calls do not pass.
Each helper import is skipped when its definition is already present.
## DXBC / AIRCONV Impact
New backend primitive is available but is not yet invoked by the DXBC sample
instruction converter. Generated AIR bitcode uses no fast-math assumptions.
## DXIL / MSC Impact
Unchanged; no fallback or shared shader lowering. Independent implementation open.
## Shared Runtime Impact
None. D3D12 sampler rejection is unchanged, and no installed runtime DLL is deployed.
## Tests Added / Tests Run / Runtime Results
Normal and no-private configurations and D3D12/native Winemetal builds succeeded.
LLVM-linked helper signatures passed 3/3 in each build; module verification passed.
Native Metal probes passed 88/88 in each build directory on Apple M4. They cover
mixed filters/minification selection, mip interpolation, exact-center exclusion,
array/3D reduction, NaNs, homogeneous signed zeros, wrap/mirror/zero-border,
positive/negative offsets and an interior integer mip boundary with outliers.
These are two runs of the same native primitive probe, not separate no-private
D3D12 GPU acceptance. The GPU probe compiles the production helper source using
safe math; it does not execute the linked LLVM probe's metallib or a D3D12 shader.
Receipts: each build's `minmax-ir.log`, `minmax-gpu.log` and Meson test log.
Meson regression passed 3/3 in each build; `git diff --check` passed.
## Standards
Two optional maintainability findings addressed: named flag bits and case records.
Final independent source review found no outstanding findings.
## Spec
Two focused validation gaps addressed with NaN/zero/address/offset/mip probes.
Final independent source review found no actionable errors; full integration stays
explicitly pending. Reviews did not themselves build or run the probes.
## Known Limitations
No D3D12 end-to-end Min/Max pass. No sampler ABI, instruction-converter wiring,
implicit/gradient/bias lowering, sparse feedback, 1D/cube/depth/half/anisotropy,
DXIL implementation, native Windows oracle or complete format/address matrices.
Mixed signed-zero ordering and precision-edge behavior still require full semantic
audit; the current sign-aware checks use homogeneous zero footprints.
## Capability Status / Feature Level Impact
PARTIAL implementation; FL11_1/FL12_0/FL12_1 and shader-model reports unchanged.
## Git Status / Push Status
Only this task's files committed locally; NOT PUSHED.
## Next Recommended Task
Wire exact sampler state and sample instructions to this AIR primitive, including
feedback handling; implement DXIL independently. Keep the full section 15 target.

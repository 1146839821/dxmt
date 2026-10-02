# Task Analysis

Baseline b43c8d7. Implement the missing reduction clamp operation before
expanding acceptance matrices.

- Hypothesis: an AIR control-flow wrapper around the existing reduction helper
  can preserve tap/filter behavior while preventing any sample for an empty view.
- Evidence: the root binding now supplies view-mapped OOB defaults. D3D11.3
  5.8.5 explicitly says a clamp beyond the last view mip, including 5.1 for
  view mips 0..5, returns OOB defaults. Flooring the clamp is incorrect here.
- Expected effect: resource/instruction constraints apply after sampler clamps;
  min/mag selection uses the final constrained LOD. Empty views return mapped
  defaults instead of sampling the last mip.
- Risk: nested runtime sampler branches and PHI predecessors, fractional last
  mip boundary, unknown/null descriptors, implicit derivatives and legacy ABI.
- Validation: native LLVM verifier/branch checks and focused production AIR
  regressions in both builds. Existing host admission remains fail-closed until
  descriptor validity and actual clamp dispatch acceptance are connected.

Reference:
https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
Sections 5.8.5, 5.8.6 and 5.9.4.5.4. Keep feedback rejected; no synthetic
residency result. Do not promote feature declarations from primitive evidence.

# Task Result

The production AIR reduction path now calls a clamped wrapper when the root
binding supplies default components; legacy D3D11 bindings retain the existing
primitive. The wrapper queries the view mip count, compares the view-space
constraint strictly against lastMip, and branches to mapped float4 defaults or
the existing tap helper. Its defaults branch contains no sample/helper call.
The active branch applies the constraint after sampler clamps and recomputes
minification from the final LOD. SampleGrad/Sample/SampleBias pass their merged
resource/instruction constraint through the shared lowering; implicit derivatives
and the speculative ordinary sample remain outside sampler-state divergence.
The no-tap guarantee applies to the wrapper's empty branch, not to that existing
speculative ordinary sample in dynamic implicit sampling.

AIRCONV_VERSION is 28 so persistent AIR caches cannot reuse earlier generated
shaders. The linked Metal helper ABI, descriptor stride and MSC path are unchanged.
This checkpoint deliberately keeps instruction/reflection and recording/submission
clamp admission rejected. Descriptor validity/unknown/null handling and genuine
nonzero-clamp dispatch acceptance are still required before removing those guards.

Both builds complete with matching isolated host/Unix staging. Native IR tests
pass 6/6 in each build: three original linked-helper contracts and three wrapper
contracts covering strict comparison, defaults branch without calls, active
helper linkage, PHI predecessor correctness and no-mutation unsupported-kind
rejection. These are structural IR checks, not clamp GPU numeric acceptance.
Both Meson suites pass 4/4.

Six focused executions per build pass: implicit minimum 16, implicit bias 96,
same-PSO switch 16/240/128/16, 1D gradient mip 224, instruction-clamp PSO rejection
and resource-clamp resolver rejection. The final two are rejection checks, not
dispatches. Shader cache disabled and dyld paths verified in
`clamp-lowering-{pixel,bias,switch,grad,clamp,resource-clamp}.log` under the isolated
runtime and its no-private subdirectory. No game DLL deployment or full matrices.

Main-agent Standards self-review: shared operation wrapper, unchanged helper ABI,
explicit legacy behavior and cache invalidation. Spec self-review: fractional
last-mip exception, clamp ordering, final min/mag classification and no synthetic
feedback. The llvm skill guided CFG/PHI checks; code-review guided two-axis review.
Independent review remains unavailable. git diff --check passes.

Next: host validity contract with static recording-time capture and volatile
submission validation, then actual nonzero resource/instruction clamp GPU
readbacks including full default-component vectors and fractional boundaries.
Only then remove the relevant fail-closed guards. Full MinMax/FL12_0 remains open.

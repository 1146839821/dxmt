# Task Analysis

Baseline 43f551b. Continue the full FL12_0 gaps-first objective.

- Hypothesis: Pixel Shader Sample/SampleBias can reuse explicit-gradient LOD and
  the existing reduction tap helper, preserving the ordinary AIR sampling path.
- Evidence: decoded instructions already expose coordinates, offsets and bias;
  SampleGrad reduction has real multi-mip GPU evidence.
- Expected effect: remove two actual AIR operation gaps, not add another matrix.
- Risk: implicit derivatives evaluated inside descriptor-dependent branches;
  sampler plus instruction bias order; feedback/clamp and unsupported shapes.
- Validation: actual pixel draw/readback, distinguishable multi-mip bias probes,
  ordinary and supported-explicit-operation regressions in both builds.

Derivatives and the ordinary sample result must be evaluated before reduction
branching. This retains ordinary semantics even with nonuniform sampler flags.
The extra unused ordinary sample on the dynamic reduction path is an opt-in
performance cost, not a correctness shortcut; optimization requires separate
evidence. Static reduction does not generate it. Texture-size queries, Gram
matrix and logarithms are emitted only in the reduction branch, while quad
derivatives and the original ordinary sample precede descriptor branching.
Derivative AIR declarations are marked convergent, matching the existing
implicit sample declaration's optimizer contract.
No backend fallback, feedback fabrication, or feature-level promotion.

Primary reference: D3D11.3 sections 7.18.11 and 22.4.15-16:
https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm

# Task Result

Supported float 1D/array, 2D/array and 3D Pixel Shader Sample/SampleBias lowering
is connected under existing reduction gates. Reflection rejects feedback,
instruction clamps, unsupported shapes and non-pixel implicit consumers.
Reduction bias is emitted in the order LOD + sampler bias + instruction bias;
ordinary lowering retains its previous bias argument. The callback-based shared
branch helper delays reduction-only LOD work without moving quad derivatives
inside descriptor-dependent control flow. No backend routing changes.

Both production builds complete. Matching regular DLL copies were staged only
in the isolated runtime; dyld log paths confirm variant-specific Unix binaries.
Shader cache disabled. Final source passes nine focused implicit pixel cases per
variant (18 GPU draws/readbacks): static/dynamic minimum 16, dynamic maximum
240, static/dynamic derivative-driven point mip 1 = 224, static/dynamic +0.75
instruction bias selecting mip 2 = 96, sampler +0.75 with instruction -0.75
returning mip 1 = 224, ordinary dynamic spatial average = 128. Four uniform
mip colors are 32/224/96/160; expected LOD log2(8*0.23) is about 0.88.
Tests inspect RGB readback, not a full format/component/shape matrix.

Six regressions per variant pass: ordinary GS triangle, null texture query,
SampleLevel same-PSO switch, 1D SampleGrad multi-mip, instruction-clamp static
PSO rejection, and MSC consumer resolver rejection. Rejection checks are not
successful GPU dispatches. Both reduction gates disabled still permit ordinary
dynamic implicit sampling returning 128 in each variant. Meson passes 3/3 each.

Main-agent Standards review checked shared lowering, callback lifetime, resource
cleanup, bounded uploads and keeping opt-in pixel cases out of legacy default
runs. Spec review checked pixel-only eligibility, quad derivation before branch,
bias order, unchanged ordinary sampling, and conservative feedback/clamp guards.
Three self-review improvements were implemented and rerun: preserve bias addition
order, keep expensive reduction LOD calculation out of the ordinary branch,
and mark derivative calls convergent. Final verification logs are isolated
`implicit-verified-*` logs, after the convergent-attribute build completed.
git diff --check passes. Independent review unavailable; no independent-review
claim. llvm skill guided the SSA/branch ordering review. GPU acceptance is 2D
only here; other admitted dimensions reuse previously checked primitives but
have not received implicit pixel end-to-end matrix coverage.

Open: Cube, anisotropic reduction, instruction/resource clamps and default
components, residency feedback, DXIL reduction, nonuniform-indexed sampler
matrices, full formats/shapes and performance evidence. No game run, full Metal
validation, capability promotion or claim of full MinMax / FL12_0 closure.

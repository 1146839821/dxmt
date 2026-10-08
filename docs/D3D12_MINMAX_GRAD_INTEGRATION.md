# Task Analysis

Baseline af625af, clean feat/d3d12-1. Add actual AIR SampleGrad reduction next;
do not replace the FL12_0 goal with a smaller acceptance target.

Hypothesis: texture-size-scaled explicit derivatives can produce isotropic LOD
and reuse the existing point-tap reduction helper and descriptor observation.
Evidence: SampleGrad already decodes coordinates/gradients and ordinary AIR
sampling; reduction currently supports only SampleLevel.
Expected effect: remove a real AIR operation gap, including mixed use of
SampleLevel and SampleGrad with one descriptor/PSO.
Risk: non-orthogonal gradients, bias/clamp order, zero/parallel/NaN/Inf gradients,
normal branch behavior, broadening consumer eligibility before lowering works.
Validation: focused independent LOD oracle, minimum/maximum actual GPU readback,
same-PSO ordinary/reduction switch and retained fail-closed feedback/MSC paths.

Primary spec: [D3D11.3 section 7.18.11](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
Scale by largest-view-mip dimensions. Isotropic LOD uses the major-axis length;
non-orthogonal gradients need Jacobian/ellipse handling with skip cases. Apply
sampler bias then MaxLOD then MinLOD; choose minification after sampler clamps.

# Task Result

Opt-in AIR SampleGrad admission is implemented. No capability promotion.

Added AIRBuilder gradient-LOD emission for 2D/array/3D float-vector derivatives:
query view-mip-zero dimensions, normalize scaled derivatives, form the 2x2 Gram
matrix, use its major eigenvalue for nonparallel footprints, and fall back to
maximum derivative length for zero/parallel cases. Perpendicular vectors already
give the same result. Zero gradients preserve negative infinity; scaled-gradient
overflow preserves positive infinity. Normalization avoids finite squared-length
overflow. This first checkpoint had no SampleGrad caller or GPU oracle evidence.

Current delta: SampleGrad now uses the shared reduction branch/clamp/PHI helper
also used by SampleLevel. Consumer reflection accepts supported no-feedback,
no-instruction-clamp float 2D/array/3D gradients. Qualification names now describe
reduction sampling, with the original public bit name retained as an alias.
The unsupported fixture now uses SampleGrad with instruction clamp, rather than
expecting every plain SampleGrad to fail.

Eight first reachability executions pass (static/dynamic minimum/maximum x both
variants) with GPU results 16/240. These have one mip and do not validate LOD
calculation, mip selection, bias, non-orthogonal gradients or special-value cases.
Multi-mip numeric oracles now pass in both variants (14 executions): nonparallel
nonorthogonal gradients choose mip 2 (96), bias -0.75 chooses mip 1 (224),
parallel and perpendicular gradients choose mip 1 (224), zero gradients choose
mip 0 (32), MinLOD 2.25 wins over MaxLOD 0 (96), and MaxLOD 0.25 chooses mip 0
(32). Four uniform mip colors are 32/224/96/160. Expected mip choices are analytic
constants, not a duplicate implementation of the emitted Gram-matrix algorithm.
The nonorthogonal and parallel cases distinguish naive or incorrectly applied
ellipse LOD selection. These tests cover 2D point mip selection, not every filter
combination, array/3D shape, special value or approximation boundary.

Final focused regression: 12 executions per variant, all exit 0. Four static /
dynamic Min/Max SampleGrad GPU readbacks return 16/240; existing SampleLevel
same-PSO switches return 16/240/128/16; ordinary AIR/MSC return 255. Instruction
clamp static PSO, instruction clamp dynamic consumer, MSC consumer and resource
clamp resolver rejection remain fail-closed. Default-off dynamic rejection and
descriptor-copy checks pass. The initial constant-zero instruction clamp fixture
was optimized away by D3DCompile; using 0.5 retains the unsupported instruction.
Rejection/descriptor checks are not additional successful GPU dispatches.

Both builds completed; isolated copies of matching host/Unix binaries were used,
with dyld log confirmation. Shader cache disabled. Meson passes 3/3 per variant;
git diff --check passes. Main-agent Standards review checked shared lowering,
public-bit alias, fixture bounds and branch/PHI ownership; Spec review checked
LOD order, ordinary branch preservation and conservative eligibility. Independent
review unavailable due agent usage limit; no independent-review claim.

Remaining: implicit derivatives, instruction/resource clamps, feedback, DXIL
reduction and full shape/filter/special-value matrices. NaN/Inf/overflow behavior
has not received numeric GPU acceptance. No game/performance/Metal-validation
run in this step. Full Min/Max and the original FL12_0 goal remain incomplete.

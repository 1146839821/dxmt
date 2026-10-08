# Task Analysis

## Pixel GPU follow-up analysis

Baseline c9313e6, clean worktree. Hypothesis: the existing graphics fixture can
exercise the admitted Cube bindings with DXIL-only VS/PS, without a new runtime
path. Evidence: compute readback is qualified, while pixel evidence stops at
regeneration and offline MSC. Expected effect: real pixel Sample/SampleBias
draws using application roots, static/live descriptors and private TLABs.
Risk: Cube face/subresource upload, array-layer poison, render-target array size,
derivative-dependent LOD and ordinary sampler restoration. Reuse the existing
graphics fixture, choose an interior +Z footprint with independent analytical
readback, and retain the existing planar regression. No capability promotion.
Validation must include both full builds and current-DLL isolated GPU runs;
fixture source or offline compilation alone does not close this gap.

Branch `feat/d3d12-1`; read-only remote baseline `origin/feat/d3d12` e147c710,
merge-base 85bb2dd; 125 local commits at analysis baseline c9313e6. Existing
implementation: independent DXIL lowering and captured Cube view binding;
relevant files/tests: `dx12_minmax_fragment.cpp`, Cube pixel HLSL and optional
Meson DXC targets. DXBC/AIRCONV and shared production runtime are unchanged.
Missing piece: actual pixel GPU draw evidence, not further compiler admission.
Full goal and mandatory FL gates remain unchanged.

## Binding follow-up analysis

Baseline 94a1cac, clean worktree. Hypothesis: resource-kind qualification plus
the independent cube footprint/gradient primitives can use the existing captured
Cube view and submission-private pair ABI. Evidence: compiler rejects kinds5/9;
host pair preparation also rejects Cube/CubeArray types. Expected effect:
bounded float, feedback-free, zero-offset cube SampleLevel/SampleGrad and pixel
Sample/SampleBias reach MSC and isolated GPU dispatch. Risks: preserve original
resource metadata and array layer, derive all three xyz axes (not layer), reject
invalid cube operands before mutation, retain correct captured view and ordinary
branch behavior. Validate regenerated DXIL and real GPU readbacks in both builds
before reporting runtime acceptance. No AIR linkage or capability/FL promotion.

## Follow-up footprint analysis

Baseline 879867c, clean worktree. Hypothesis: retaining Cube resource kinds
while reconstructing interior texel-centre directions can express independent
DXIL reduction footprints without a flattened 2D view. Evidence: existing
SampleLevel lowering interprets xyz as planar coordinates; binding remains
closed for kinds5/9. Expected effect: explicit qualified Cube lowering only,
with edge remapping and three-face corner reduction. Risks: face orientation,
point face ties, excluded tap execution and Cube offset legality. Validation:
native LLVM verification and focused structural checks, both builds; production
binding must stay closed until regenerated DXIL/MSC/GPU qualification. No AIR,
descriptor ABI, capability, game deployment or feature-level change.

## Branch / Baseline / Local Commits

`feat/d3d12-1`, clean `a8e52b0`, 122 local commits beyond read-only
`origin/feat/d3d12` (`e147c710`). Full FL12 objective remains active.

## Current State / Existing Implementation / Relevant Files / Existing Tests

`dxil_minmax_binding.cpp` rejects resource kinds Cube/CubeArray before private
module mutation. `dxil_minmax.cpp` has planar/volume Gram-matrix gradient LOD and
SampleLevel footprints. Existing native `dxil_minmax_ir` checks signatures,
transactional rejection and verified module structure. AIR already has Cube
support, but its executable or shader ABI must not be reused by DXIL.

## D3D12 Contract / Hypothesis / Evidence / Expected Effect

Cube gradients need primary-face quotient projection with Z/Y/X ties and
half-side scaling, then the existing isotropic LOD math. Evidence: current
gradient code blindly scales xyz with dimensions, and current footprint code
interprets sample xyz as normalized planar/volume coordinates. Merely accepting
resource kind5/9 would therefore be wrong. Expected effect: establish independent
DXIL projection first, then Cube direction/tap lowering and binding admission.

## DXBC / AIRCONV Impact / DXIL / MSC Impact / Shared Runtime Impact

No AIR changes or cross-backend reuse. Extend the private DXIL LOD helper with
an explicitly qualified cube shape, emitting DXIL scalar operations only. Keep
runtime resource-kind admission closed until footprints and GPU validation.
No shared descriptor ABI, captured-view substitution or capability changes.

## Missing Pieces / Risks / Minimal Implementation Plan

Full Cube footprint, seams/corners and subsequent shader regeneration / MSC
validation / GPU readback are still required; this increment is not completion
of the Cube gap. Signed projection, tie selection, dimension queries and reject
before mutation are risks. Reuse normalized Gram matrix; cube has two projected
axes scaled by the same side width, not a volume depth or array count.

## Validation Plan / Capability Impact

Add native IR signature/shape and transactional rejection checks for Cube /
CubeArray-oriented calls. Reconfigure and compile focused target and full builds
in both variants. No runtime/game deployment or claim of MSC/GPU acceptance.
Self-review and local commit, no push. Full MinMax remains PARTIAL.

# Task Result

## Implementation / Changed Files / Backend and Runtime Impact

Independent DXIL gradient LOD helper now has an explicitly qualified cube mode:
signed-major quotient projection, Z/Y/X tie precedence, half-side scaling,
common width for both projected axes and existing normalized Gram matrix.
Rejects incompatible dimensions, undefined direction/gradient components and
nonzero offsets before mutation. Existing callers default to non-cube mode.
No AIR/executable reuse, runtime binding or footprint changes.

Files: `src/winemetal/unix/dxil_minmax.cpp/.hpp`, existing
`tests/dx12/dxil_minmax_ir.cpp`, this record and closure ledger. This is a
compiler primitive increment, **not DXIL Cube sampling admission**.

## Tests Added / Tests Run / Runtime Results

Both configurations reconfigured; focused native target and both full default
builds complete. Host suites **5/5 each**. Existing native IR checks remain
passing; two new cube-oriented callsites preserve their layer operand, query
only side width, avoid fast-math and reject invalid requests without mutation.

Independent Spec source review identified that operation counts alone did not
lock down projection semantics. Added a test-only evaluator of the emitted
scalar SSA graph (not another projection implementation) and thirteen analytical
inputs per callsite. **26 CPU numerical probes per build, 52 total**, pass:
six signed faces LOD3 at side8/tangent2, positive/negative radial LOD−Inf,
direction10/tangent20 LOD3, negative off-axis LOD1, Z/Y two-axis ties
`0.5*log2(20)` and three-axis tie LOD2. Invalid evaluator operations fail the
test. The evaluator uses host double arithmetic: these probes are not complete
float32 precision, NaN/Inf, DXIL validation, MSC or GPU acceptance.

Evidence `/Users/zhangbo/.cache/dxmt-dxil-cube-lod.eJNayZ/normal-numeric.ll`
and `no-private-numeric.ll`. Both final native commands return0, followed by
LLVM15 `opt -passes=verify -disable-output` return0. Reproduce:

```sh
build/tests/dx12/dxil_minmax_ir > /tmp/dxmt-dxil-cube-lod.ll
/usr/local/opt/llvm@15/bin/opt -passes=verify -disable-output /tmp/dxmt-dxil-cube-lod.ll
```

Use `build-no-private` for the second configuration. No DLL deployment, Wine,
Steam/process management, MSC compilation, GPU run or game benchmark this
increment. The compilation skill informed the preserved MSC boundary;
LLVM skill informed the IR verification and arithmetic checks.

## Known Limitations / Capability Status / Feature Level Impact

PARTIAL primitive only. Production binding still rejects Cube kinds5/9;
existing calls do not enable cube mode. Full footprint/seam/corner lowering,
regenerated DXIL validation, matching captured-view binding, runtime admission
and real MSC/GPU readback are required next. This does not close the DXIL Cube
gap or full MinMax. FL11_1 unchanged; FL12_0/FL12_1 unpromoted. Full goal active.

## Standards

Main self-review and independent Standards source review: no documented hard
breach or actionable optional smell. Defaults preserve existing callers and
the separate DXIL compiler path; runtime/capability changes are absent.

## Spec

Initial independent review found one projection-semantic test coverage gap.
The added CPU SSA probes address it; follow-up source review confirmed expected
values and closed the finding, with no remaining actionable implementation
finding for this primitive. Reviewers did not execute tests; all runtime /
numerical commands above were run by the main agent. Full Cube admission remains
incomplete as explicitly recorded. `git diff --check` passes.

## Git / Push Status / Next Recommended Task

Local commit reported at handoff; NOT PUSHED. Continue independent Cube
footprint lowering and bind it only after regeneration/MSC/GPU verification.

# Follow-up Task Result: independent footprint primitive

Explicit qualified Cube mode in `LowerReductionSampleLevel` now preserves the
original Cube handle and array layer. Scalar DXIL operations implement signed
face bases with Z/Y/X ties, interior point centres, exact edge projection to an
adjacent face, and guarded three-face corner union. Existing planar callers
default to non-cube mode. All three generated offsets are undefined, and input
nonzero offsets / undefined xyz / incompatible dimensions reject before mutation.
No AIR linkage, descriptor ABI or production binding admission was added.

Both Cube/CubeArray-oriented native functions verify: 24 static point-call
sites, 24 conditional branches and three dimensions queries per function;
handle/layer preservation and no fast-math. A test-only generated-CFG evaluator
adds 14 reduction-value and 10 exact-routing probes per function: **48 probes
per build, 96 total**, in addition to existing gradient probes. Routing probes
use spatially distinct values on every signed face and assert exact visited
texel identities with multiplicities for positive/negative seams and three-face
corner union. Native samples execute on block visitation even when their result
does not affect the extrema. Unknown evaluator operations fail the test.

Both native executables and LLVM15 `opt -passes=verify -disable-output` pass.
Evidence: `/Users/zhangbo/.cache/dxmt-dxil-cube-footprint.68NMy9/normal.ll`
and `no-private.ll`. Both focused and full default builds finish; host suites
remain 5/5 in each configuration. These CPU double-arithmetic / synthetic-sampler
checks are not float32 precision qualification, regenerated DXIL validation,
MSC acceptance or real GPU semantics. No Wine/game run or DLL deployment.

Standards source review found no hard breach; clarified inverse-basis names.
The optional parallel-basis-table smell is retained: compact fixed scalar lookup
tables keep normal/U/V transform data visible, with signed-face routing probes.
Spec review identified two test gaps (executed tap tracking and spatial face
orientation / corner union); added exact routing coverage as described above.
Follow-up source review closed both findings. The evaluator rejects unsupported
operations when evaluated and SampleLevel calls without component extracts;
it is not a general interpreter that rejects every unused unknown call.
Reviewers inspect source only; test commands are run by the main agent.

Production resource kinds5/9 remain rejected. Next: connect paired binding and
all-three-axis implicit gradients, then regenerated DXIL validation, MSC compile
and isolated real GPU readbacks before opening runtime admission. Full MinMax,
FL12_0 and FL12_1 remain unqualified. Local commit only; never pushed.

# Binding Follow-up Task Result

## Implementation / Backend and Runtime Impact

`LowerReductionSamplerBindings` now qualifies float Cube/CubeArray kinds5/9
for feedback-free, zero-offset SampleLevel/SampleGrad and pixel Sample/SampleBias.
Cube sites explicitly propagate the qualified cube shape to both independent
DXIL primitives. Direction xyz and all explicit xyz derivatives must be defined;
all three offsets are checked before module mutation. Implicit derivatives use
xyz ahead of the descriptor predicate, never CubeArray W. Original/private
resource kind metadata and layer coordinates remain intact.

Host pair preparation admits Cube/CubeArray alongside existing texture shapes,
using the same captured native object, zero private clamp metadata and existing
submission-owned TLAB/residency/sampler lifetime. No flattened views, native view
reacquisition or shared ABI change. Static/live descriptor observation is unchanged.
DXBC remains AIRCONV-only; DXIL remains MSC-only. No capability or FL promotion.

## Tests / Evidence / Runtime Boundaries

Both configurations reconfigured and full default builds complete. Final host
suites pass 5/5 each, native primitive probes remain passing, and LLVM15 verifies
the generated modules. Actual selected-DXC container probes cover Cube and
CubeArray SampleLevel, SampleGrad, modern SM6.6 handles, and pixel Sample plus
SampleBias. Cube negatives check all three nonzero offsets, undefined xyz and
six undefined gradient operands: rejection preserves complete printed module IR
and binding output. Pixel tests assert the exact original `(derivative opcode,
xyz operand)` multiset, branch placement, removed old sampling declarations and
layer preservation, not only a derivative count.

Final evidence root: `/Users/zhangbo/.cache/dxmt-dxil-cube-binding.v4Oh5A`.
Only `verified-*-final.log` GPU results are counted: **96 GPU readback processes**,
48 per configuration, all exit0:

- 39 Cube/CubeArray explicit-LOD/gradient cases each, including static samplers,
  interior/edge/corner/array/mip/point ties, signed gradients, radial cancellation,
  scale invariance and discriminating Z/Y LOD ties.
- Two modern SM6.6 handle cube/array gradient cases each.
- Four DXIL planar ordinary/MIN/MAX/static regressions each.
- Three AIR Cube regressions each with both AIR gates explicitly enabled.

Each DXIL dispatch reaches selected-DXC regenerated-container validation and
the real MSC 4.0.1 Apple9 compiler/pipeline. Four separate pixel preparation
processes (two shapes, two builds) pass full regenerated-container validation
and export checks; their four outputs also pass offline MSC Apple9 conversion
with reflection. **Pixel GPU output was not tested.** Offline conversion uses
automatic layout, not proof of the production reflected pixel TLAB. Broader
pre-raster Cube execution, formats/views/clamps/filter/lifetime and mandatory
matrices remain open. No Metal validation run, game benchmark or game deployment.

## Runtime Reproduction and Loader Finding

Initial diagnostics ran a stale installed PE: the copied overlay retained
`x86_64-windows/d3d12.dll` as a symlink to the installation. Wine's displayed
module path alone was insufficient. Replacing only task-cache PE links with
regular current-build copies made the new boundary logs appear and the red
readback return16. All temporary `[DEBUG-CUBE-PAIR]` instrumentation was removed
before final builds. Earlier failed/diagnostic/pass runs are excluded above.
Original cache links/files were moved to `.pre-final` / `.before-final4` backups;
no installed Wine DLL, prefix DLL, Steam or game process was changed.

Final staged PE and overlay PE SHA1 match their build: normal D3D12
`17ed2d27a26588b52c3663d2d9452bfcd25ca747`, no-private
`b5f21626fb955df042173e4ec149c664105cd9b4`. Native winemetal also matches:
normal `169fd26f1cc391811e260606010bf1d5a173e374`, no-private
`4786e8425c0ad9d1889a96733e4dde65726078e2`.

Compile `minmax_cube.hlsl` with `cs_6_0`, `MINMAX_CUBE_CASE=0..11`, optional
`MINMAX_CUBE_ARRAY=1` and `MINMAX_CUBE_GRAD=1`. Case values mirror the existing
sampler harness. Modern probes use `cs_6_6`, case4 and gradients. Pixel fixtures
use `ps_6_0`. Meson registers four optional compute baseline and two pixel targets;
focused seam/LOD fixtures above use explicit macro selections, not a full matrix.
Example final runtime command (AIR gates unset for DXIL):

```sh
WINEDEBUG=-all MVK_CONFIG_LOG_LEVEL=0 \
WINEDLLOVERRIDES='d3d12,dxgi,winemetal=n,b' \
WINEDLLPATH=/Users/zhangbo/.cache/dxmt-dxil-cube-binding.v4Oh5A/verified-normal \
DXMT_MINMAX_DXC_DIRECTORY='Z:\Users\zhangbo\Documents\Vibe-Codeding\dxmt\tools\dxc\bin\x64' \
DXMT_SHADER_CACHE_PATH=/Users/zhangbo/.cache/dxmt-dxil-cube-binding.v4Oh5A/repro-cache \
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
/Users/zhangbo/.cache/dxmt-dxil-cube-binding.v4Oh5A/runtime/bin/wine \
  /Users/zhangbo/.cache/dxmt-dxil-cube-binding.v4Oh5A/verified-normal/dx12_texture_sampler.exe \
  /Users/zhangbo/.cache/dxmt-dxil-cube-binding.v4Oh5A/cube-grad-8.dxil \
  --minimum-cube-grad-negative-radial
```

## Standards

Independent source review: zero documented hard breaches; one optional duplicated
validation smell. Retain boundary preflight plus primitive checks so all sample
sites reject before mutation while the independently callable primitives remain
defensive. Main self-review and `git diff --check` pass. Compiler/binding skills
guided Cube-type preservation and runtime evidence; diagnosis skill guided the
isolated loader red/green loop. No executable-family fallback was added.

## Spec

Initial source review found missing pixel fixtures; added both shapes, Pixel
preparation mode and container tests. Follow-up review found no actionable Spec
defect and confirmed exact xyz derivative operands. Reviewers did not run tests;
all stated numerical/runtime checks were executed by the main agent. This closes
the bounded explicit Cube binding/reachability gap, **not full Cube or MinMax**.

## Git / Next Task

Local repository-style commit only, never pushed. Full goal remains active.
Next production gap: real DXIL Cube pixel Sample/SampleBias GPU draw acceptance,
then remaining MinMax anisotropic/feedback and full resource/filter contracts.

# Task Result: production Cube pixel follow-up

## Branch / Baseline / Local Commit / Changed Files

`feat/d3d12-1`; task baseline c9313e6, read-only remote baseline e147c710.
This local test commit extends `tests/dx12/dx12_minmax_fragment.cpp`, adds
`minmax_cube_fragment.hlsl` and five optional Meson shader targets, and updates
this result plus the closure ledger. No runtime or capability source changes.

## Implementation / Backend and Shared Runtime Impact

Reuse the actual production graphics PSO, application root signature, private
pair binding and direct/indirect/indexed draw path. VS and PS are independently
compiled DXIL SM6.0; no mixed executable family or AIR fallback. Cube-array
coordinate chooses cube1. Mip0 +Z has texels 16/64/192/240 (replacement
32/80/176/224); every wrong face/cube is poison7. All six target-cube mip1
faces contain96 (replacement144); wrong array cube0 remains7 at mip1.
Render targets remain single-slice. InputView centralizes SRV shape construction.
Cube flags require exactly PS/VS/DXC-directory/option arguments.

Sample uses nonzero projected derivatives and an interior footprint, testing
minimum/maximum versus analytical ordinary bilinear output. SampleBias adds4,
selecting mip1, testing mip/resource/array-layer selection. Static samplers and
static/volatile descriptor modes retain the existing observation-time assertions,
recorded-template immutability and repeated submission checks. Ordinary sampling
is restored after a private reduction draw, using the original resource.

## Tests Added / Tests Run / Runtime Results

Both builds reconfigured, focused graphics target built, then full default builds
completed before final staging. Host suites **5/5 each**. Selected repository DXC
compiled one VS and four Cube/CubeArray Sample/SampleBias PS fixtures; production
preparation validates regenerated DXIL and MSC4.0.1 creates the real GPU PSOs.

Final evidence root `/Users/zhangbo/.cache/dxmt-dxil-cube-pixel.p6NAkj`:
`full-final-{normal,no-private}.log`, `test-final-{normal,no-private}.log`, and
only `final-{normal,no-private}-{cube-sample,cube-bias,array-sample,array-bias,planar}.log`
count as final runtime evidence. **10 processes return0: eight Cube tests and two
planar regressions**. Each completes four root/sampler modes and three draw kinds
(direct, indirect non-indexed, indirect indexed): **96 Cube readback groups plus
24 planar groups**, not 120 independent process runs. Each group covers repeated
submissions (direct2, indirect counts0/1/7). Negative stage/root/compiler and
unsupported indirect binding-update checks remain intentional rejection evidence,
not unexpected runtime errors.

Actual Apple M4 GPU results: Sample reduction16/240 for original static samplers,
replacement32/224 for live texture/static sampler, live dynamic MAX224; ordinary
output `16 + 48*fx + 176*fy`, `fx/fy = .25 + (pixel+.5)*.125`. SampleBias reads96
or live replacement144, with ordinary restore96. Array cube0 poison never reaches
the tested output. All final processes pass the exact per-pixel oracle.

Current D3D12 SHA1: normal `cf3e9a3824e2eb88aee1663d46f56aaad7beaf0e`, no-private
`d7e43f0dec220eea97f338ada82e2512d88648d2`. Build, staged adjacent DLL and overlay
`x86_64-windows` copies match. Native winemetal build/overlay hashes match:
normal `169fd26f1cc391811e260606010bf1d5a173e374`, no-private
`4786e8425c0ad9d1889a96733e4dde65726078e2`. Only cache overlays are updated;
installed Wine/prefix DLLs, games, Steam and wineserver state are not managed.

Reproduce the corrected final Cube-array bias run, with both AIR gates unset:

```sh
WINEDEBUG=-all MVK_CONFIG_LOG_LEVEL=0 \
WINEDLLOVERRIDES='d3d12,dxgi,winemetal=n,b' \
WINEDLLPATH=/Users/zhangbo/.cache/dxmt-dxil-cube-pixel.p6NAkj/normal \
DXMT_SHADER_CACHE_PATH=/Users/zhangbo/.cache/dxmt-dxil-cube-pixel.p6NAkj/repro-cache \
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
/Users/zhangbo/.cache/dxmt-dxil-cube-pixel.p6NAkj/runtime/bin/wine \
  /Users/zhangbo/.cache/dxmt-dxil-cube-pixel.p6NAkj/normal/dx12_minmax_fragment.exe \
  /Users/zhangbo/.cache/dxmt-dxil-cube-pixel.p6NAkj/array-bias.dxil \
  /Users/zhangbo/.cache/dxmt-dxil-cube-pixel.p6NAkj/vertex.dxil \
  'Z:\Users\zhangbo\Documents\Vibe-Codeding\dxmt\tools\dxc\bin\x64' \
  --cube-array-bias
```

## Self-review / Standards / Spec

Main self-review and diff whitespace check pass. Independent Standards review:
zero hard breaches or actionable smells. Spec review found no wrong oracle or
scope creep, requested final evidence (recorded above), and identified the bias
coverage boundary below. Reviewers did not run GPU tests. MSC integration skills
guided correct Cube-view type and production TLAB acceptance; diagnosis skills
kept the incorrect initial bias oracle separate from production defects.

Initial diagnostic bias runs failed because other target-cube mip1 faces were
poison7. A linear 1x1 cube footprint can legitimately reach those faces; ordinary
output85/91 and MIN7 did not support the original96 oracle. Corrected fixture
keeps all target-cube mip1 faces uniform while retaining wrong-layer poison.
Those initial logs are retained but excluded from final counts. No production
fix was justified by that test-data defect.

## Known Limitations / Capability Status / Feature Level Impact

This is bounded **DXMT_LOCAL_PASS**, not full Cube/MinMax qualification. Bias's
uniform target mip1 does not independently discriminate face selection or
reduction versus ordinary filtering; Sample mip0 supplies those complementary
checks. All signed faces, seams/corners, fractional LOD/clamps, anisotropic,
feedback, wider formats/views/lifetimes and pre-raster Cube remain incomplete.
No native-Windows oracle, Metal validation/trace or game performance/tessellation
acceptance was run here. FL11_1 unchanged; FL12_0/12_1 unpromoted; full goal active.

## Git / Push Status / Next Recommended Task

Local repository-style test commit after review; **NOT PUSHED**. Next prioritize
remaining production MinMax operations and resource/filter contracts, not a
full Cube matrix or capability-number changes.

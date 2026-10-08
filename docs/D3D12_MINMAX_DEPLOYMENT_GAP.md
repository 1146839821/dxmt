# MinMax deployed compiler production gap

Audit baseline: 23354edf. Current working tree was clean before inspection.
This is a source-backed implementation task, not a capability promotion.

Task Analysis:

- Hypothesis: deployed compiler selection must be retained by the PSO and reused
  at command recording; changing only PSO admission leaves draws/dispatches
  rejected or accidentally on the ordinary sampler surrogate.
- Evidence: compute Initialize rejects static reduction roots when
  DXMT_MINMAX_DXC_DIRECTORY is empty; graphics Initialize does the same.
  PreDraw separately rejects requires_minmax_variant when its environment
  directory is empty. Thus existing sampling/readback primitives do not provide
  default production selection even with a deployed compiler.
- Expected effect: share validated deployment discovery with typed-origin, then
  retain the selected MinMax directory for both compute and graphics. Explicit
  override wins; missing deployment remains explicit unsupported; broken
  deployment must fail rather than silently use ordinary point surrogates.
- Risk: typed-origin/MinMax coexistence, late environment changes, DXBC routing,
  absent deployment compatibility and changing immutable variant identity.
- Validation: same binaries against baseline/current DLLs, environment unset
  with valid/missing/broken deployment, static MIN/MAX and mixed roots, compute
  and graphics numeric readbacks in both builds. Preserve backend isolation,
  rejection boundaries and static/live descriptor timing.

Current source seams:

1. `d3d12_typed_origin.cpp`: SelectTypedOriginCompilerInternal discovers
   dxmt-dxc beside the module, but returns a directory only for typed-buffer
   declarations. Calling this unchanged for a sampling-only shader cannot
   select a MinMax compiler. Extract deployment discovery without copying its
   typed-resource reflection policy or skipping compiler validation.
2. `d3d12_pipeline_compute.cpp`: static reduction-root admission reads only the
   environment; GetMinMaxVariant stores a private directory and validates cache
   identity. Recording needs an immutable selection accessible from the PSO.
3. `d3d12_pipeline_graphics.cpp`: static reduction admission similarly requires
   the environment; private minmax_dxc_directory_ already owns variant identity.
4. `d3d12_command_list.cpp`: PreDraw/PreDispatch must use retained selection when
   no override exists, and must preserve typed-origin/MinMax incompatibility
   guards. Do not relax requires_minmax_variant without selecting its binding
   and private shader path.

Task Result / self-review: this audit identifies a multi-consumer production
selection gap, not missing numerical sampling primitives. No renderer code or
capability was changed and no new runtime pass is claimed. Existing timestamp
diagnosis remains unresolved; it is not the prerequisite for implementing this
selection path. Full FL12_0/FL12_1 objective remains active.

## Implementation checkpoint

The audit-only result above is superseded by production changes: shared
deployment discovery now feeds static-reduction root admission and compute/
graphics PSO preparation. Selected paths are retained on the PSO and reused by
PreDispatch/PreDraw when the override is absent. Explicit incompatible late
selection fails via variant identity; typed-origin coexistence and anisotropic
rejection remain. Discovery checks pair availability; shader preparation still
loads and validates the compiler/validator before publishing a private PSO.
Ordinary roots do not automatically enter the private path. Dynamic-only
reduction heaps without a reduction static root still require explicit selection.

Verification, in matching cache-only PE/native overlays:

- Both reconfigured full builds passed; final root/fixture rebuilds passed.
  Host tests passed 12/12 in each build.
- Genuine environment-unset compute deployment probes passed five reduction
  roots x five executions per variant: MIN/MAX/mixed/ordinary coexistence,
  immutable list replay, in-flight results and incompatible late override guards.
- Explicit override controls passed eleven roots x five executions per variant
  in separate directories without deployed DXC, preserving absent-deployment
  rejection assertions.
- Graphics deployment modes2/3 passed real direct/indirect/indexed readbacks
  in both variants, retaining existing binding-update rejection checks.
- Separate missing-deployment directories failed root creation with E_NOTIMPL;
  directories containing invalid compiler/validator files passed availability
  admission but failed PSO creation with E_FAIL, both variants. No ordinary
  sampler-surrogate fallback was observed.
- Evidence: `/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE/minmax-*` logs.
  Initial failed probes and fixture corrections are retained. A preliminary
  old-DLL invocation was not the final identical fixture, so it is excluded
  from exact baseline/current A/B qualification. That named check remains open.

Standards self-review: no hard violations, one nonblocking duplicated-policy
heuristic across compute/graphics selection. Spec review: root-admission and
contradictory-test blockers were fixed; no remaining source findings. Reviewers
were read-only; final runtime outcomes were checked by the main agent. MSC
integration/compilation skills preserved root/TLAB reflection and routing.
No prefix/game deployment, feature reporting, FL promotion, or push. Full
qualification (including exact A/B and broader dynamic/stage coverage) remains
incomplete; this checkpoint implements default static-root production selection.

## Dynamic-root implementation analysis (baseline c955dae9)

Hypothesis: qualifier-approved sampling PSOs must retain a private variant even
when their current samplers are ordinary, because volatile slots may become
reduction samplers after recording. Evidence: AddSampler still requires an
override; ordinary MSC recording/submission reject reduction metadata. Expected
effect: discover deployment for non-typed sampling PSOs, prepare existing MinMax
variants, and select them only on successful qualification. E_NOTIMPL preserves
ordinary native sampling, with existing reduction guards still rejecting use;
other compiler/validation failures remain explicit. Sampler admission discovers
deployment without changing anisotropic/finite checks. Risk: ordinary sampling
compatibility, typed coexistence, extra compile cost, volatile replacement and
unsupported-consumer rejection. Validation: all deployed compute roots, dynamic
graphics replay/ordinary controls, both builds, missing/broken deployment and
unsupported ordinary/reduction controls before committing.

Dynamic checkpoint: both builds and host tests (12/12 each) pass. Environment-
unset compute now passes eleven roots x five executions in both variants;
graphics dynamic/static modes pass existing direct/indirect/indexed and ordinary
sampling numeric checks. The final executable was unchanged across cached
previous-production/current DLL-native pair switches: old exit1, new exit0 in
both variants, logs `minmax-dynamic-final-ab-{old,new}-{normal,np}.log` under the
reconciliation cache. EXE SHA256 normal:
`5a5624f31956e9a0eed40ffeeec986f5790e49cde57ce300ba7b7e9aed45b734`;
no-private:
`cbd2d1f075fff7b798bdf3a993272126fdb8167603e5163ddcd609517fbe264d`.
The prior production pairs are recoverably retained in `minmax-dynamic-baseline-*`.
Current pairs were restored after comparison. Unsupported-consumer controls and
final source review remain pending; no completion or capability promotion is
claimed for this dynamic implementation checkpoint.
# Ordinary sampling preservation checkpoint

Hypothesis: retaining anisotropic sampler parameters is insufficient after the
application sampling operation has been collapsed to scalar SampleLevel.
The ordinary branch now retains the original intrinsic and application handles;
only the reduction branch normalizes its footprint. Original descriptor bias,
sampler clamps, texture metadata and shader capability flags remain intact.
DXC COM-operation E_NOTIMPL is mapped to E_FAIL separately from semantic
qualification rejection, including reflection and operation-result failures.

Validation: both normal and no-private full builds and the four explicit MinMax
fixtures compile successfully. Both host suites pass 12/12. Normal deployed
compute passes eleven roots times five executions, including the ordinary
anisotropic live-descriptor iteration (both builds). Normal graphics passes four
SampleLevel root modes and four implicit Sample root modes. Native direct-heap
ordinary numeric execution passes and reduction is rejected during recording
without submission. The implicit fixture must run from an app directory without
deployed DXC: its static-root negative control deliberately expects missing
deployment. A first run beside deployed DXC failed this environment assumption;
the isolated explicit-directory rerun passed. Logs are in the recoverable cache
`dxmt-reconciliation.ZLDvwE/minmax-native-ordinary-*`.

This is not final acceptance: directional gradient/implicit GPU comparison,
injected DXC operational E_NOTIMPL and final code review
remain required before committing automatic dynamic selection. SM6.6/FL12_0
temporary experimental environment gates are the next requested task after this
work closes; neither capability is promoted by this checkpoint.

Follow-up review found a nonuniform instruction-clamp hazard: implicit ordinary
sampling must execute before the injected empty-view branch, rather than only
in its nonempty successor. The call now dominates both successors. Both full
builds pass after this correction, and the normal implicit SampleBias fixture
passes all four root modes (`minmax-clamp-implicit-bias.log`). The existing
constant-clamp fixture does not prove varying-clamp quad semantics; that targeted
comparison remains open. Standards review reports no hard violations and one
nonblocking duplicated auto-selection-policy smell across compute/graphics.

Additional controls: normal sampler contracts pass (72; private MinMax 28 plus
7 rejects), root controls pass, and no-private deployed graphics passes all four
root modes. A corrupt present validator now rejects PSO creation with E_FAIL
(`minmax-validator-after-normal.log`), rather than the previous successful PSO
followed by Close failure. The test-only `dxc_factory_notimpl` DLL injects a
loadable DxcCreateInstance export returning E_NOTIMPL. Compiler-factory injection
rejects the PSO but may stop in classification; validator-factory injection with
a real compiler targets the MinMax preparation boundary separately. The DLL is
not installed or built by default and is deployed only in isolated cache folders.

Varying-clamp GPU checkpoint: `USE_VARYING_CLAMP` alternates clamp 0/3 in
adjacent quad lanes of a two-mip view. `--deployed-varying-clamp` passes all four
root modes on normal, with both reduction and ordinary private draws, ordinary
ANISO=16, zero default components on empty lanes and numerical checks on active
lanes. Evidence: `minmax-varying-clamp-deployed-normal.log`. The first explicit
run restored the native PSO for ordinary drawing and observed 96 rather than
zero on its out-of-view lanes (`minmax-varying-clamp-normal.log`); it is not a
passing native/private equivalence result. The deployed test checks the wrapper
contract and the inserted-branch behavior, not native MSC empty-clamp correctness
or the complete directional anisotropic footprint. No-private repetition remains.

Both-build follow-up: no-private varying-clamp repetition passes all four roots.
`USE_DIRECTIONAL_GRAD` / `--deployed-directional-grad` adds a 16:1 explicit
gradient footprint at the symmetric center of the 2x2 lower mip with a distinct
1x1 upper mip and descriptor bias 1. Ordinary ANISO=16 must return the lower-mip
mean 128, while scalar major-axis LOD selects upper-mip 96; reduction retains
its expected 96/144 upper-mip results. Normal and no-private pass all four root
modes, direct replay and counted indirect draws (0/1/7). Logs:
`minmax-directional-{normal,np}.log`. This is focused directional evidence, not
full anisotropic reduction support (still rejected) or the complete format matrix.

## Final implementation review

The checkpoint blockers above are superseded by the subsequent directional,
varying-clamp and validator-factory tests. With a real compiler, the injected
validator DxcCreateInstance reaches MinMax preparation and returns E_FAIL to PSO
creation (`minmax-validator-factory-notimpl-normal.log`); no ordinary fallback or
submission occurs. This does not inject failures from a validator instance's
Validate method or IDxcOperationResult methods; those mappings are source-reviewed.
Final Standards review has no hard violations, with the existing nonblocking
compute/graphics policy duplication suggestion. Final Spec review finds no
remaining blocking implementation or focused-test validity issue.

Task Result: commit automatic dynamic deployment selection and its compatibility
repairs. The original native PSO is retained for unsupported qualification;
broken selected compiler operations fail explicitly. SO/mesh and direct-heap
consumers do not select this wrapper. Ordinary sampling preserves application
intrinsics/descriptor semantics, while reduction alone uses scalar normalization.
Known matrices and native MSC out-of-view clamp behavior remain open; no aggregate
capability qualification is claimed. The separately requested experimental gates
were committed as ab0ec5a0 and remain default off. No game/prefix deployment,
process management or push was performed.

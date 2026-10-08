# Typed qualification with explicit compiler deployment

Baseline: 784946ed, feat/d3d12-1. No capability promotion, production renderer
change, game/prefix deployment or push is part of this task.

## Hypothesis / evidence / expected effect / risk / validation

The current gate stages DXMT DLLs but not the deployed DXC/validator pair used
by automatic typed-origin compute selection. Hypothesis: this selects the old
native-view path, not the implemented origin-aware production path. Fresh full
matrix executions reproduce 132 PASS / 12 FAIL on DXIL in both builds, with
matched PE/Unix provenance: R16/R8 scalar buffer FirstElement 4/260 fails Close
because the exact native view is unaligned. DXBC is 144/0. Explicit selection of
the same repository DXC changes only compiler availability and yields 144/0
on both builds. This identifies a qualification deployment gap, not a new
renderer arithmetic defect. The no-deployment result remains a compatibility
limitation and is not waived or relabeled PASS.

Expected effect: reproduce default automatic deployment selection in a clean
temporary application, without inherited compiler overrides. Risk: compiler
substitution, helper loader records, hidden environment selection, and enabling
deployment in rejection-control fixtures could create false acceptance. Require
both compiler DLLs, target-process path/hash observations and missing/wrong/
mutated/other-process negative tests. Keep ordinary and rejection controls in
their original no-deployment environments.

## Implementation and scope

The FL12 runner accepts --typed-compiler-dir. Only the complete DXIL numeric
typed-UAV matrix stages that directory's dxcompiler.dll and dxil.dll in
dxmt-dxc beside the staged d3d12.dll. No environment override enables the path;
existing module-relative production discovery selects it. The runner explicitly
clears DXMT_TYPED_ORIGIN_DXC_DIRECTORY and DXMT_MINMAX_DXC_DIRECTORY for every
qualification child. The application/compiler source path and staged SHA256
values are recorded separately from DXMT runtime hashes. Actual target-process
PE observations require both nested compiler paths as well as DXMT modules;
Unix runtime observation retains its existing independent requirement.

Missing compiler files or unavailable Wine/runtime provenance are UNVERIFIED,
never silently omitted or replaced. Without the option, the old no-deployment
path still runs and can FAIL. View-contract fixtures remain native-view controls,
including explicit unaligned rejection; their PASS is not proof that every
arbitrary view is supported. This does not bundle a compiler into installation
packages or qualify native Windows, shader stages, all offsets or lifetimes.

## Current evidence

Final results are recorded separately from diagnostic/preliminary runs under
/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE. The preliminary
qualification-typed-deployed-* JSON had a shadowed source-directory field;
actual staged paths/hashes were correct, but those files are excluded from
final provenance claims. A regression assertion now checks source-directory
identity and requires compiler_deployment=null for undeployed fixtures.

Compiler pair SHA256:

- dxcompiler.dll: b86a738ece4c05dbe2d9bbb29668a2ccb28a0740773c9b027cdc61e8d07b4d75
- dxil.dll: 058f2f52a680c38b223a5615b7df969def21f77e577562e5099d971dede992de

Final evidence: qualification-typed-final-{normal,no-private}.json both PASS
all nine runner cases with target PE/Unix provenance, including both compiler
modules in the complete DXIL case. Parent directory overrides deliberately held
invalid values; children observe empty overrides and use automatic deployment.
qualification-typed-final-api-no-private.json passes all 144 DXIL numeric cases
with Metal API Validation Enabled and verified source/module identities. This
is a separate correctness run, not GPU shader validation or performance data.
qualification-minmax-contract-final-{normal,no-private}.json pass all 13
rejection/ordinary-control cases without compiler deployment. The original
qualification-typed-{normal,no-private}.json preserve the full undeployed
132/12 result. Compiler source/hash provenance of the earlier explicit-override
diagnostic is not claimed by the final runner.

Both fixture builds pass; renderer/build graph are unchanged by this runner
patch. Both 16-test host suites pass, including 72 Python gate tests, and
git diff --check passes. New negative tests cover incomplete compiler pairs,
missing validator observations, unexpected paths, helper-only observations,
mutated staged DLLs, compiler source metadata, inherited directory overrides,
and limiting deployment to the complete DXIL numeric case.

The complete fixture covers 18 formats, six UAV shapes and three buffer offsets
(144 cases per backend/build), plus 126 UAV and 126 SRV view contracts per
backend/build. View contracts include intentional rejection, not exclusively
GPU submissions. Full format/view totals are 1,584 contract cases across both
builds; the numeric format matrices account for 576 GPU cases.

The API policy remains additional=0. Even a complete numeric matrix does not
override the API/GPU conjunction in the gate or establish all typed semantics.
MinMax's 13-case rejection/control runner remains separate from real MIN/MAX
acceptance. Tiled, full format/raster, full backend stage/lifetime semantics and
native Windows qualification remain open. No full FL12 gate PASS is claimed.

Self-review uses Standards and Spec axes on the working diff from 784946ed,
with this report and closure consolidation as requirements. Main-agent only;
independent review is not claimed. Diagnosis separated compiler absence from
renderer failure; validation is correctness evidence, not performance evidence.

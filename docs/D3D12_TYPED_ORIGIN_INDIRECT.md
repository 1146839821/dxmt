# Task Analysis

## Current Branch
feat/d3d12-1, clean at task start.
## Baseline
origin/feat/d3d12; local starting point 1f06870.
## Local Commits Since origin/feat/d3d12
Existing lowering, compiler-root, variant-cache and direct binding commits retained.
## Current State
Direct typed-origin dispatch passes focused GPU contracts in both builds.
## Existing Implementation
Indirect compute inherits buffers/PSO but its resolver restores the ordinary PSO.
## Relevant Files
Command list, command allocator, typed UAV fixture.
## Existing Tests
Seven direct origin contracts with output/backing-buffer readbacks.
## D3D12 Contract
Non-updating indirect signatures inherit current compute bindings.
## DXBC / AIRCONV Impact
Unchanged routing and native pipeline selection.
## DXIL / MSC Impact
Select the prepared variant consistently for resolver threadgroup size and PSO restore.
## Shared Runtime Impact
Reuse existing per-submission binding lifetime and command replay.
## Missing Pieces
Indirect dispatch currently bypasses origin preparation.
## Risks
Resolver overwrites PSO; TLAB inheritance and repeated in-flight execution.
## Minimal Implementation Plan
Return the selected variant from PreDispatch and supply it to indirect encoding.
## Validation Plan
Both builds; reuse seven GPU contracts with indirect execution, plus direct regression.
## Capability Impact
None. Resource-updating MSC signatures remain unsupported.

Hypothesis: inherited TLAB remains valid across the resolver, provided the selected
variant is restored afterward. Evidence: resolver binds only buffer slot 30 and
ICB inherits both buffers and PSO. Expected effect: indirect typed-buffer accesses
use the same per-execution snapshots as direct dispatch. Risk and validation above.

# Task Result

## Branch / Baseline / Local Commit
feat/d3d12-1 / origin/feat/d3d12 / see the commit containing this document.
## Changed Files
Command list, allocator header/implementation, typed UAV fixture and status docs.
## Implementation
PreDispatch publishes its selected variant only on successful recording. The
indirect resolver uses its reflected threadgroup size and restores its native PSO
before ICB execution. The ICB inherits the same TLAB/descriptor bindings; existing
per-submission origin generations remain immutable until completion.
## DXBC / AIRCONV Impact
Ordinary pipeline selection is unchanged when no variant is supplied.
## DXIL / MSC Impact
Non-root-updating indirect compute participates in opt-in origin preparation.
## Shared Runtime Impact
No new replay protocol, descriptor observation time or lifetime owner.
## Tests Added
The seven origin contracts also run through ExecuteIndirect: static, copied,
static-overwrite negative oracle, volatile update, initially unavailable volatile,
distinguishable in-flight reuse, and invalid static descriptor rejection.
## Tests Run / Runtime Results
Both builds succeeded. Both Meson suites passed 3/3. Both staged GPU runs passed
14/14 (seven direct and seven indirect); output and backing-buffer bytes checked.
Receipts: `build/typed-origin-indirect.log` and
`build-no-private/typed-origin-indirect.log`. Loader traces select staged native
D3D12 and the isolated matching winemetal runtime. No installed game DLL changed.
The default aligned DXIL case passed with the opt-in directory unset
(`build/typed-origin-indirect-default.log`). `git diff --check` passed.
## Known Limitations
This fixture uses one indirect command without a count buffer/predication. Broad
indirect combinations and mixed-pipeline regression remain unverified here.
Root-updating MSC signatures remain rejected by the existing guard. Default
enablement and complete shader/format coverage remain open; no Metal validation
or game/performance acceptance was run.
## Capability Status / Feature Level Impact
PARTIAL; no FL11_1/FL12_0/FL12_1 or shader-model declaration changes.
## Standards
No hard violations. One minor adjacent-boolean API smell was addressed with a
DispatchMode enum. This review influenced the fixture interface, not runtime behavior.
## Spec
No actionable integration findings. Both independent reviews were source-only.
## Git Status / Push Status
Only this task's files staged for local commit. NOT PUSHED.
## Next Recommended Task
Close shader provenance/default-path integration and Min/Max filtering implementation,
then run the complete mandatory GPU matrices.

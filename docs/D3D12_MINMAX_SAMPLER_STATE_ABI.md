# Task Analysis

## Current Branch / Baseline
feat/d3d12-1 / origin/feat/d3d12; starting point 6189552, clean worktree.
## Local Commits / Current State
Static opt-in SampleLevel is connected. Dynamic reduction remains rejected.
## Existing Implementation / Relevant Files
Sampler descriptors have two handles, bias metadata and an unused qword. AIR
static reduction state currently travels separately as compiler constants.
## Existing Tests / D3D12 Contract
Public static Min/Max readback and rejection fixtures exist. Dynamic descriptor
state must retain exact filter/bias/clamp bits through creation and copying.
## DXBC / AIRCONV Impact
Decode reduction operands from the existing GPU descriptor, rather than baking
static filter/clamp operands into the shader; retain compile-time consumer checks.
## DXIL / MSC Impact
MSC entries stay independent and static emulation roots remain rejected.
## Shared Runtime Impact
Write complete AIR state for both ordinary dynamic and static samplers with
unchanged 32-byte stride. Do not admit dynamic reduction before consumer safety.
## Missing Pieces / Risks
Dynamic runtime branching, unsupported-consumer validation, sampler observation
and retention are still needed. An ABI mismatch could misread existing heaps.
## Minimal Implementation Plan / Validation Plan
Define shared bit/layout contract; pack exact native descriptors; load GPU state
in real static SampleLevel lowering. Validate readback and state roundtrip/copy.
## Capability Impact
No capability or Feature Level change; no dynamic reduction acceptance claim.

Hypothesis: a single descriptor state contract removes the static-only operand
path required by dynamic lowering. Evidence: qword 3 is unused in both layouts.
Expected effect: actual GPU-visible filter/LOD operands with no stride expansion.
Risk: ordinary sampler metadata or MSC descriptor corruption. Validation: exact
bit checks and focused real D3D12 regression in both variants.

# Task Result

## Branch / Baseline / Local Commit
feat/d3d12-1 / origin/feat/d3d12; see the commit containing this report.
## Changed Files / Implementation
Shared 32-byte AIR sampler state layout; host static and dynamic descriptor
packing; real AIR binding decode and SampleLevel operands; explicit MSC bias
mask; focused fixture and documentation. No descriptor stride increase.
Metadata low32 preserves bias float bits, upper32 carries named filter flags.
The fourth qword preserves MinLOD/MaxLOD float bits in low/high halves.
Static reduction shader now reads flags/clamps from its GPU descriptor rather
than compiler constants. Root classification still gates eligible consumers.
The reduction marker is reserved for later dynamic runtime dispatch; no current
branch is credited to it. Ordinary dynamic descriptors write clamp state, but
their native sampler remains authoritative and unused AIR loads optimize away.
## DXBC / AIRCONV Impact
Existing reduction helper consumes SSA values decoded from GPU sampler state.
Native D3D11 descriptor layout and binding code remain unchanged.
## DXIL / MSC Impact
Independent MSC entries retained; static bias explicitly masks low32. Reduction
roots still fail E_NOTIMPL and no backend fallback is introduced.
## Shared Runtime Impact
Dynamic ordinary and static samplers now write deterministic complete state.
Existing descriptor copies assign all 32 AIR bytes and all 24 MSC bytes. Invalid
reduction writes still clear both entries. Only isolated test DLLs were staged.
## Tests Added
Exact bias/clamp halves checked against independent float bitcasts. CPU source
copies into an empty reference visible heap and a different-state destination;
all seven AIR/MSC qwords must match. This catches omitted copying or stale state.
A mixed min-linear/mag-point GPU probe uses bias=-0.5, MinLOD=0.25>MaxLOD=0:
MinLOD wins and post-clamp minification must return 16, not point-sample 240.
## Tests Run / Runtime Results
Both configurations and builds succeeded. Each variant passed 11 public cases:
static Min/Max, mixed-filter clamp, two copy-and-rejection cases, ordinary
static/dynamic AIR and MSC readbacks, MSC reduction and AIR SampleGrad rejection.
These are focused contracts, not the complete sampling matrix. Matching isolated
Unix winemetal.so paths are recorded by the loader. Logs: state-final-*.log under
/Users/zhangbo/.cache/dxmt-origin-boundary.UEy0AS and its no-private subdirectory.
Meson regression 3/3 and AIR helper linkage 3/3 passed per build; diffcheck passed.
## Standards
Independent review found the nondiscriminating copy oracle; fixed with distinct
destination state, empty reference heap and all-word comparison. No production
bug or hard documented-standard violation found. Tuple/field-index duplication
remains an optional maintenance smell, not part of the runtime acceptance claim.
## Spec
Independent review found the absent explicit MSC mask and weak state validation.
Both were addressed. Real GPU output now verifies descriptor-sourced filter/clamp
operands. Complete LOD/precision/format/address and lifetime matrices remain open.
Follow-up reviews found no remaining hard issue. The mixed-filter probe verifies
the combined decode/clamp/minification path, not independent shader bias or
MaxLOD decoding: MinLOD dominates that input. No broader acceptance is inferred.
## Known Limitations
This is production state transport, not completed dynamic Min/Max support.
Dynamic reduction still rejects. Runtime branching, per-consumer safety for AIR
unsupported operations and MSC, sampler retention/volatile observation, feedback
and independent DXIL lowering are still required before admission.
No Metal validation, Windows oracle, game benchmark or full capability gate run.
## Capability Status / Feature Level Impact
PARTIAL. FL11_1/FL12_0/FL12_1 and Shader Model advertisement unchanged.
## Git Status / Push Status
Only task changes committed locally after review; NOT PUSHED.
## Next Recommended Task
Implement dynamic descriptor dispatch together with host consumer validation and
sampler observation/retention. Do not simply admit point samplers in shared heaps.

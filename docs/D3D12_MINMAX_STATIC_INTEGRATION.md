# Task Analysis

## Current Branch / Baseline
feat/d3d12-1 / origin/feat/d3d12; starting commit e36c983, clean worktree.
## Local Commits / Current State
Prior AIR reduction primitive and typed-origin integration preserved.
## Existing Implementation / Relevant Files
Static root sampler descriptors are available during AIR binding setup. Native
point helper exists; sampler conversion currently rejects reduction. Relevant
files: root signature, AIR binding/converter, descriptor-use observation and fixture.
## Existing Tests / D3D12 Contract
Existing sampler fixture provides real DXBC SampleLevel and native readback.
Static sampler state is immutable and can be specialized during compilation.
## DXBC / AIRCONV Impact
Carry exact static filter/LOD state into SampleLevel lowering and reject unsupported
operations/types/feedback through the compiler error result, without fallback.
## DXIL / MSC Impact
Explicitly reject emulation roots; do not supply point samplers to MSC shaders.
## Shared Runtime Impact
Opt-in static sampler admission only; dynamic sampler creation remains rejected.
Validate unsupported ResourceMinLODClamp at static recording/live submission times.
## Missing Pieces / Risks
Dynamic state, other operations, precision/format matrices, feedback and independent
DXIL implementation remain mandatory work. LOD/filter ordering must follow spec.
## Minimal Implementation Plan / Validation Plan
Connect a real public static root + DXBC PSO + Dispatch path behind
DXMT_ENABLE_AIR_MINMAX=1. Run minimum/maximum readbacks, default rejection and MSC
negative checks in both builds; self-review before local commit.
## Capability Impact
No capability or Feature Level change; this is opt-in partial integration.

Hypothesis: compile-time static sampler specialization avoids a premature dynamic
sampler ABI change. Evidence: the root binding setup owns the decoded static state.
Expected effect: real SampleLevel calls use the tested helper. Risk: silently
accepting unsupported consumers, feedback, or stale descriptor clamp state.
Validation: compiler failure propagation and public API/readback cases.

# Task Result

## Branch / Baseline / Local Commit
feat/d3d12-1 / origin/feat/d3d12; see the commit containing this report.
## Changed Files / Implementation
Root sampler admission, AIR root binding state, SampleLevel lowering, compiler
error propagation, helper linking, descriptor observation and sampler fixture.
DXMT_ENABLE_AIR_MINMAX=1 admits non-anisotropic static reduction samplers only.
The exact root blob remains unchanged; native sampler uses point filtering while
AIR specializes original min/mag/mip/filter and LOD state. Shader bias precedes
sampler clamps, MinLOD takes precedence, and min/mag selection follows the clamp.
[Microsoft functional specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
## DXBC / AIRCONV Impact
Float 2D/array/3D SampleLevel uses the linked reduction primitive. Unsupported
operations, kinds/types and feedback return compiler errors, not fake success.
Helper parse/link failure now returns None to the same compiler error channel.
## DXIL / MSC Impact
Explicit emulation-root layout returns E_NOTIMPL; no AIR fallback. Draw/dispatch
layout rejection also fails recording rather than silently omitting work.
## Shared Runtime Impact
Nonzero ResourceMinLODClamp is conservatively rejected for bound texture SRVs,
at recording for static descriptors and submission for volatile descriptors.
Reduction roots force observation even with AIR residency tracking disabled.
This can reject unused bound SRVs; narrow precision/view semantics remain open.
Only isolated test DLL/runtime copies were staged; installed game DLLs unchanged.
## Tests Added
Public MSC PSO rejection assertion and real DXBC SampleGrad compiler-rejection
assertion, with required failure HRESULT and null PSO publication.
## Tests Run / Runtime Results
Normal and no-private builds succeeded. Each variant passed Min/Max GPU readback
(16/240), repeated Min after Max, default static rejection, opt-in dynamic
rejection, MSC PSO rejection and AIR SampleGrad rejection. Min/Max positives
were run before the final fixture-only addition; production binaries unchanged.
Meson regression passed 3/3 per build. Linked AIR signatures passed 3/3 per build;
native Metal primitive passed 88/88 per build directory (same native probe,
not a separate D3D12 no-private acceptance matrix). git diff --check passed.
Receipts: isolated runtime directories contain verified-static-*,
verified-default-*, verified-dynamic-*, verified-msc-* and verified-air-reject-*
logs under /Users/zhangbo/.cache/dxmt-origin-boundary.UEy0AS and its no-private
subdirectory. DYLD loader records confirm matching isolated winemetal.so paths.
Initial batch failures loaded the wrong Unix runtime: inherited DYLD variables
were not preserved through env. Explicit env argument assignments fixed this;
no shader-cache correctness claim or cache implementation change resulted.
## Standards
Independent review found no hard standards violations or confirmed bugs.
Two optional smells remain: raw private flag bits and duplicated clamp predicate.
## Spec
Independent review found incomplete edge-case validation and ignored helper
link failure. The latter is fixed; SampleGrad rejection now has runtime evidence.
Main-agent follow-up reviewed the bool link result and call removal on failure.
Remaining LOD-order, feedback/type and static/live clamp rejection probes are
explicitly unverified, not credited as complete acceptance.
## Known Limitations
Dynamic samplers, other sampling operations, feedback, 1D/cube/depth/anisotropy,
DXIL reduction implementation and complete precision/format/address/LOD matrices
remain open. Native Windows oracle, Metal validation and game benchmark were
not run. No full Min/Max or FL12_0 gate acceptance is claimed.
## Capability Status / Feature Level Impact
PARTIAL opt-in implementation. FL11_1, FL12_0, FL12_1 and Shader Model reports
unchanged. Full Min/Max remains an FL12_0 implementation blocker.
## Git Status / Push Status
Task changes committed locally after review; NOT PUSHED.
## Next Recommended Task
Dynamic reduction sampler state and remaining AIR operations, independent DXIL
implementation; advance implementation gaps before expanding full matrices.

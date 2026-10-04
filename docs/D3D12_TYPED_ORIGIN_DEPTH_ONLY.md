# Task Analysis

## Current Branch

feat/d3d12-1.

## Baseline

Clean 9cea2c7; origin/feat/d3d12 e147c710. Previous turn made production progress
by connecting native typed VS/PS with actual GPU readbacks.

## Local Commits Since origin/feat/d3d12

133 at task start.

## Current State

Typed VS requires a PS in GetTypedOriginVariant, although ordinary native graphics
and the retained render template already support a nullable fragment function.

## Existing Implementation

Vertex preparation/conversion, stage-specific visibility, immutable cached PSOs,
single-stage submission TLAB and shared static/live descriptor retention exist.

## Relevant Files

Graphics variant creation, graphics diagnostics, dedicated public depth fixture,
HLSL, Meson test target and closure checkpoint.

## Existing Tests

VS-only with an ordinary PS, combined VS/PS, root, compute/indirect and MinMax.
No actual absent-PS typed depth-write GPU oracle.

## D3D12 Contract

PS may be absent for depth-only rasterization. Typed VS must receive correct
origin/count and root constants, honor ALL/VERTEX visibility and descriptor flags,
and preserve normal depth-only PSO/root restoration. No fabricated fragment stage.

## DXBC / AIRCONV Impact

None. DXBC remains AIRCONV; DXIL remains MSC; no backend fallback.

## DXIL / MSC Impact

Treat absent PS as nonparticipating: do not reflect, prepare, compile or load it.
Reuse the native nullable-fragment pattern and original render/depth configuration.

## Shared Runtime Impact

Existing vertex-only single TLAB, immutable submission storage, record-time static
snapshots and unique live submission observations remain unchanged.

## Missing Pieces

Remove the mandatory PS assumption and validate actual absent-PS depth writes.

## Hypothesis

The existing native vertex-only origin ABI is sufficient when fragment is absent.

## Evidence

GetTypedOriginVariant rejects empty original_ps_ and unconditionally creates its
library/function. Native ordinary/MinMax paths already accept a missing fragment.

## Expected Effect

Native typed VS depth-only draws execute without a dummy PS or new binding ABI.

## Risks

Reflecting empty bytecode, null function/library dereference, lost depth formats/
state, ordinary fallback behavior, visibility and static/live regressions.

## Minimal Implementation Plan

Make absent PS selection/preparation/library/function optional. Add a public D32
depth-readback fixture with bounds-sensitive typed VS, repeat live view updates,
ordinary depth-only restoration, RS1.0/1.1 and ALL/VERTEX table visibility.

## Validation Plan

Both reconfigurations, focused and full builds before cache-only staging. Same
fixture against baseline/current DLLs; SM6.0/6.6 explicit/deployed DXC depth
readbacks, ordinary-only no-compiler check, existing VS/PS/shared regressions,
host tests, independent readonly self-review and local commit only.

## Capability Impact

None. Wider typed formats/stages, indirect graphics, lifetime/matrix qualification
and the other production workstreams remain open. No game/prefix writes/restarts.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

`9cea2c7` (native typed VS/PS); readonly origin reference remains
`e147c710eabc0a8d1530f5d2e21d18b0eb114be4`.

## Local Commit

The commit containing this record; resolve with `git log -- this-file`.

## Changed Files

`src/d3d12/d3d12_pipeline_graphics.cpp`, `d3d12_command_list.cpp`,
`tests/dx12/meson.build`, `dx12_typed_origin_depth.cpp`,
`typed_origin_depth.hlsl`, this record and the FL12_0 closure checkpoint.

## Implementation

Absent PS is nonparticipating in reflection, preparation, conversion and library/
function creation. Native render/depth templates retain a nullable fragment
function. Untyped depth-only shaders still select the ordinary path before private
root acquisition. Graphics diagnostic wording no longer assumes pixel-only use.

## DXBC / AIRCONV Impact

None; permanent DXBC/AIRCONV and DXIL/MSC routing is unchanged.

## DXIL / MSC Impact

Native typed VS can execute depth-only PSOs without a dummy PS. No new lowering
ABI, record layout, shader family fallback or capability advertisement.

## Shared Runtime Impact

Existing vertex-only single TLAB, static recording snapshots and live submission
capture remain unchanged. Ordinary depth-only state restoration is GPU checked.

## Tests Added

Public D32_FLOAT readback fixture: bounds-sensitive R32_UINT VS loads, root
constant guard, nonindexed/indexed draws with nonzero start index, RS1.0/1.1,
ALL/VERTEX visibility, static/live descriptors and sequential resubmission.
Live updates change resource, FirstElement and NumElements after recording.
Each typed draw is followed by an ordinary draw in the same encoder. Explicit
paired DenyVS control verifies rejection before submission; every root denies PS
access. Compiler-free ordinary mode exercises the unchanged absent-PS path.

## Tests Run

Both Meson reconfigurations, focused targets and full default builds passed before
staging. Host suites passed 5/5 on each build. Eight depth processes cover both
builds, SM6.0/6.6 and explicit/deployed compiler selection: 52 typed draws each,
416 total, plus 416 ordinary restoration draws. Two compiler-free ordinary runs
pass 32 actual ordinary draws. Two same-fixture baseline runs fail as expected
before GPU submission. Shared graphics regressions pass 192 typed draws plus
192 ordinary restoration draws. Root contracts, four direct static/live compute
runs, two root-updating indirect compute runs and two MinMax graphics runs pass.

## Runtime Results

Evidence: `/Users/zhangbo/.cache/dxmt-origin-depth.rAHcpF` (`final-*` logs,
`final-hashes.txt`, build/configuration/host logs and cache-only runner scripts).
Typed depth is 0.15625 initially and 0.25 after the live view update; ordinary
restoration is 0.75. DenyVS reports `typed-origin vertex root access is denied`
before Close fails; the exact positive counterpart passes. Previous DLLs reject
typed depth preparation. Final staged D3D12 SHA1 values:
normal `d81c09ba0d1cfb8f009504d839f06050a454ffdd`, no-private
`5b1dd79239ef4ceae8e4b5a9c3f68f1fdec447be`. Both depth fixtures have SHA1
`3d2ada01b314269899534883878907797b3ed144`; baseline uses that same fixture.

## Standards Review

Independent readonly review found no remaining hard violation or actionable smell.
The initial boolean-argument concern was addressed with named case configuration
and explicit root-version/mode enums. Main reviewed the final diff and evidence.

## Spec Review

Independent readonly review found no remaining bounded implementation issue.
DenyVS has an explicit positive counterpart and causal resolver diagnostics.
Mutating static descriptors during their required stability interval would not be
a legal no-reread oracle; no such test or inference is claimed.

## Known Limitations

Native VS only, no PS. Remaining GS/tessellation/mesh/SO, indirect graphics,
MinMax coexistence, broad formats/provenance and lifetime/distribution acceptance
remain open. Resubmission is sequential and fence-completed, not overlapping.
Static no-submission-reread remains source-inspected, not independently GPU timed.
No game benchmark, fresh tessellation acceptance or complete GPU matrix claim.

## Capability Status

TypedUAVLoadAdditionalFormats remains disabled; no capability promotion.

## Feature Level Impact

FL11_1 unchanged; FL12_0/FL12_1 remain disabled and unqualified. Four production
workstreams and seven complete GPU categories remain open.

## Git Status

Only the task files above changed before staging; final status checked after commit.

## Push Status

Local commit only; no push, game/prefix deployment or process management.

## Next Recommended Task

Connect typed-origin records to the existing MSC emulated GS/tessellation paths,
starting with their shared pre-raster binding ABI, then focused real GPU oracles.
Continue production gaps before expanding the complete qualification matrices.

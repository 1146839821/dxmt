# Task Analysis

## Current Branch

`feat/d3d12-1`.

## Baseline

Clean `a8a061f`; readonly origin reference `e147c710`.

## Local Commits Since origin/feat/d3d12

134 at task start. Previous turn made verified production progress.

## Current State

Native typed VS/PS and absent-PS draws work. MSC GS/tessellation typed variants
are still rejected. Emulation stages share TLABs, unlike native VS/PS.

## Existing Implementation

MinMax already has shared stage intervals, retained emulation templates and
reflection-derived companion PSOs/configurations. Typed lowering indexes records
from zero and preparation/conversion/resolution accept only compute/VS/PS.

## Relevant Files

Typed DXIL native lowering/thunk contract, PE preparation, root resolver,
converter cache/API and focused lowering/preparation tests.

## Existing Tests

Native structural ABI tests, validated DXC preparation, compute/native graphics
GPU readbacks. None proves typed GS/tessellation draw correctness.

## D3D12 Contract

Stage-visible tables and deny flags must be honored independently even for equal
registers. A shared hidden CBV must give each stage a disjoint, bounded interval.
Static descriptors remain recording snapshots; volatile descriptors remain live.

## DXBC / AIRCONV Impact

None. No backend fallback or shader-family mixing.

## DXIL / MSC Impact

Add explicit version-tagged shared record layout and GS/HS/DS preparation and
conversion. Preserve the zero-layout legacy ABI and native VS/PS behavior.

## Shared Runtime Impact

No draw integration in this prerequisite commit. Existing MinMax companion
construction/replay will be reused next, not replaced by a new emulation engine.

## Missing Pieces

Shared record offsets/counts in lowering, artifact identity/cache key, pre-raster
stage preparation/conversion and root resolution; then PSO/capture/replay wiring.

## Hypothesis

Global record intervals permit existing object/mesh TLABs to serve VS/GS/HS/DS
without extra per-stage TLABs or a second emulation ABI.

## Evidence

LowerTypedBufferOrigins emits stage-local indices/CBV sizes. MinMax's graphics
path already recompiles each stage with global intervals before companion creation.

## Expected Effect

Provide the missing production shader ABI required to wire typed emulation.
This is a prerequisite, not substituted GS/tessellation GPU acceptance.

## Risks

Old-runtime compatibility, malformed tags/overflow, modern CBV annotations,
dynamic-index fallback crossing intervals, stale cache keys and stage deny flags.

## Minimal Implementation Plan

Version-tagged bounded lowering layout, artifact offset/count and cache identity,
general stage conversion with existing emulation flags/layout, pre-raster resolver
deny checks, structural ABI and DXC-validated pre-raster preparation evidence.

## Validation Plan

Reconfigure both builds before focused/full builds. Exercise legacy/modern DXIL
lowering, shared interval bounds/atomic rejection, DXC reassembly/validation for
GS/HS/DS and existing native graphics GPU regression using cache-only overlays.
Self-review, local commit only. Finish runtime PSO/capture/replay next.

## Capability Impact

None. FL12_0/12_1 and additional typed UAV capability remain unqualified/disabled.

# Task Result

## Branch

`feat/d3d12-1`, result recorded 2026-10-05.

## Baseline

`a8a061f`; readonly origin reference remains `e147c710`.

## Local Commit

The commit containing this record; resolve with `git log -- this-file`.

## Changed Files

Typed-origin preparation/artifact/root resolution and converter API/cache;
winemetal typed lowering/header transport contract; native structural and root
fixtures; new preraster conversion fixture/HLSL/Meson target; this record and
the FL12_0 closure checkpoint.

## Implementation

Zero reserved retains local layout. Nonzero layout uses explicit
`DXMT_MSC_TYPED_ORIGIN_LAYOUT_TAG | (record_count << 8) | record_offset`.
Counts are bounded to 64 and offset/count are validated before publication.
Actual compacted bindings determine local CBV size and shared interval capacity;
unused static array declarations do not inflate the interval. All generated
record loads add stage offset; dynamic invalid indices safely select the stage's
first record while independent access guards suppress the typed operation.
Modern annotations, CBV metadata and vector-array size use total record_count.

Artifacts retain offset/count, lowering version is 7 and both fields join the
conversion cache key. GS/HS/DS preparation validates matching DXIL envelopes,
reassembles and validates the transformed container with selected DXC. General
MSC conversion forwards input layout/emulation flags in their proper argument
positions; native compute/VS/PS wrappers preserve stage validation. Root resolver
accepts GS/HS/DS visibility and rejects corresponding denied root access.

## DXBC / AIRCONV Impact

None. DXBC remains AIRCONV; DXIL remains MSC, with no mixed family or fallback.

## DXIL / MSC Impact

The necessary shared pre-raster shader ABI is implemented. MSC produces separate
local/shared VS/GS/HS/DS artifacts; emulated VS also supplies a stage-in metallib.
The existing emulation PSO/capture/replay runtime is not connected by this task.

## Shared Runtime Impact

No new draw path, heap observation point or lifetime policy. Static descriptors
remain recording snapshots, volatile descriptors retain live submission capture.
Existing native single/split TLAB and ordinary restoration regressions pass.

## Tests Added

Native ABI checks now cover tagged layout errors, output capacity/aliasing,
transactional failure, local CBV sizing after pruning, tight shared array interval
offset6/count8 for two actual records from four declarations, dynamic safe indices
and exact SRV/UAV consuming-access identity. Dynamic expected bases/range lengths
and boundary indices come from the fixture contract, not the emitted answer.

The preraster PE fixture validates local/shared artifacts, exact binding/layout,
wrong-stage and invalid-interval atomic rejection, root resolution, MSC bytecode
and emulated VS stage-in output. HLSL contains real VS/GS/HS/DS typed accesses,
including HS control-point and patch-constant functions. Root fixture now checks
all compute/VS/PS/GS/HS/DS table visibilities and corresponding deny flags.

## Tests Run

Both reconfigurations, focused and complete default builds passed; host tests
passed 5/5 each. Final authoritative build logs are `final4-*`.
46 native structural processes pass across both builds, legacy/modern handles,
static pruning/tight intervals and dynamic mapping. Modern provenance negative
controls reject component mismatch, missing annotation and metadata mismatch.
16 PE processes yield 32 local/shared MSC conversion passes across both builds,
SM6.0/6.6 and VS/GS/HS/DS. This is conversion evidence, not PSO/draw acceptance.

Four depth-only regression processes pass 208 typed/ordinary restoration draws;
four native VS/PS stage processes pass 128 typed/ordinary restoration draws;
four pixel processes pass 64 typed/ordinary restoration draws: 400 typed plus
400 ordinary restoration draws total. Root contracts, four direct static/live
compute runs, two root-updating indirect compute runs and two CubeArray SampleBias
MinMax graphics runs also pass. No game benchmark or complete matrix run.

## Runtime Results

Evidence: `/Users/zhangbo/.cache/dxmt-origin-preraster.X21KJd`.
`preraster-*`, `lowering-*`, `reg-*` and `old-runtime-*` are final runtime logs.
Earlier failed exploratory runs were corrected and are not acceptance evidence.
Both old-runtime controls use the same final PE fixture: local GS conversion
passes; shared preparation explicitly rejects tag with native error9/invalid
argument. No silent stage-local fallback or GPU submission in that control.

Final staged/build D3D12 SHA1:
normal `03325a6d21d6e5e27ea735a08e68a3c1fcc7d598`, no-private
`af9c03319b5ce5e8e167b33cd6c875f8dc559728`.
Native winemetal SHA1:
normal `73a17078f5acb4bf85ea30be4dd3a69a1b92fa7f`, no-private
`69f047651e3b2681c6facfd00687f5459b7c8211`.
Both final PE fixtures share SHA1 `5c27ff227e528bc7317dcdac67f5d8830e49df81`.
See `final-hashes.txt`. Final4 relinking changed PE/DLL hashes; main restaged and
reran conversion, old-runtime controls and all GPU regressions afterwards.

## Standards Review

Independent readonly review found no hard violation. Neutral template layout
names addressed the naming concern; separate DXIL-kind/MSC-stage switches retain
boundary-specific validation. Dynamic true-branch and actual SRV/UAV consumer
identity checks closed the fixture concerns. No remaining actionable finding.

## Spec Review

Independent readonly review identified premature pre-pruning interval sizing;
main moved sizing after compaction and added tight/local size oracles. Main also
caught and fixed swapped converter forwarding arguments; the fixture requires
the previously missing stage-in artifact. Re-review found no remaining bounded
issue. Reviewers did not independently execute main's build/GPU tests.

## Known Limitations

This is the production shader ABI prerequisite, not typed GS/tessellation draw
support. GetTypedOriginVariant still rejects emulation. Shared global intervals
must next be assembled across actual active stages and materialized coherently
with retained application tables; companion PSOs and reflection-derived draw
configuration must be selected at submission. No Metal function/pipeline creation
or pre-raster GPU draw is qualified by the conversion fixture.

Broader format/provenance/lifetime/distribution acceptance, mesh/SO, indirect
graphics and MinMax coexistence remain open. Static no-reread remains source-only
evidence; native resubmission regression is sequential/fence-completed.

## Capability Status

No capability promotion; TypedUAVLoadAdditionalFormats remains disabled.

## Feature Level Impact

FL11_1 unchanged. FL12_0/FL12_1 stay disabled/unqualified; four production
workstreams and seven complete GPU categories remain open.

## Git Status

Only task files were modified before staging; final status checked after commit.

## Push Status

Local commit only. No push, game/prefix DLL deployment or process management.

## Next Recommended Task

Wire stage-global typed records into retained MSC geometry/tessellation templates,
reuse the existing companion factory/configuration and object/mesh/HullDomain
replay paths, capture all active stage-visible tables, then validate real GS and
HS/DS draws with disjoint same-register bindings and late live view updates.
Continue production gaps; do not substitute additional conversion tests for this
runtime work or claim full FL acceptance from the existing focused regressions.

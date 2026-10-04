# Task Analysis

## Current Branch

feat/d3d12-1.

## Baseline

Clean 3eb4790; origin/feat/d3d12 e147c710. Previous turn made production progress
by implementing vertex root resolution. This task connects actual vertex draws.

## Local Commits Since origin/feat/d3d12

132 at task start.

## Current State

Native pixel origin correction and compiler deployment selection exist. Typed
vertex draws still fail before submission. Vertex root resolution now exists.

## Existing Implementation

Each lowered shader indexes its own 16-byte records from zero via private b0,
space1. One submission buffer currently holds one TLAB and pixel records.
Descriptor capture already includes ALL, VS and PS tables for graphics.

## Relevant Files

Typed shader preparation/conversion, graphics variant, submission materializer,
render replay and pixel GPU fixture/HLSL.

## Existing Tests

Pixel static/live origin arithmetic and ordinary restoration, compute origin,
root visibility/flags/transactional tests. Typed VS is currently a negative.

## D3D12 Contract

Native VS/PS tables resolve per stage, including colliding registers with disjoint
visibility. Static descriptors are captured at recording and never reread at
submission; volatile slots are uniquely resolved and retained under the heap lock.
GPU-visible submission storage is immutable until completion.

## DXBC / AIRCONV Impact

None. DXBC stays AIRCONV; only validated DXIL uses MSC. No fallback.

## DXIL / MSC Impact

Prepare/convert vertex origin shaders, concatenate VS then PS records and keep
stage-local record indices by binding two TLABs against one augmented root.

## Shared Runtime Impact

Shared tables and residency retention, separate vertex/fragment TLAB offsets.
Compute layout remains unchanged. No lock is held during encoder fan-out.

## Missing Pieces

Vertex preparation/conversion, dual-stage immutable variant and TLAB replay.

## Hypothesis

Two stage-local hidden CBV pointers allow reuse of the existing lowering ABI.

## Evidence

Current materializer writes one hidden pointer and render replay binds offset
zero to both stages. Graphics variant rejects any typed VS before preparing PS.

## Expected Effect

VS-only and combined native VS/PS typed-buffer draws receive correct origin
records without changing lowerer indices, application root layout or capabilities.

## Risks

Stage aliasing, shared table patching, root compatibility, ordinary restoration,
null/empty record intervals, submission offsets and resource lifetime regressions.

## Minimal Implementation Plan

Accept vertex preparation; reuse conversion machinery; independently select and
resolve each stage; concatenate records with a vertex count. Materialize separate
TLABs referencing each record interval; replay per-stage offsets. Extend GPU oracle
for VS-only, both stages, disjoint registers and repeated live submissions.

## Validation Plan

Both reconfigurations/focused/full builds before cache-only deployment. GPU readback
with poison and guards, ordinary restoration, baseline negative and host/root/
compute regressions. Independent readonly Standards/Spec reviews; local commit.

## Capability Impact

No FL or typed-format promotion. Wider formats, stages, indirect graphics and full
FL12 qualification remain open. No game/prefix deployment or process restarts.

# Task Result

## Branch

feat/d3d12-1.

## Baseline

3eb4790. Cache-only baseline DLL SHA1: normal
fc02e6b710111eaf838a23a518f9307552472b01; no-private
5d03d866f925d34cad7c7892bba1ae458c7d020b.

## Local Commit

The commit containing this record; resolve with git log for this file.

## Changed Files

Eight D3D12 production source/header files, pixel fixture/HLSL, this record and
FL12 closure checkpoint. No capability source, native lowerer or game files.

## Implementation

Prepare validated vertex DXIL and reuse the common typed-stage converter. Reflect
VS and PS independently before requesting private roots, so ordinary PSOs retain
S_FALSE without new private-root cost/budget requirements. Resolve typed stages
against their own visibility and concatenate VS then PS bindings/locations.

The immutable variant records the VS prefix count. Materialization validates both
stage counts and publishes only complete submission bindings. Shared table pointers
and application constants remain identical; when both stages are typed, the second
TLAB points its private CBV at the PS record interval. If only one is typed, retain
the original single-TLAB layout/cost. Render replay binds the corresponding offsets.
Embedded-root identity checks now compare raw root payloads, not a container against
a payload. Matching RS1.1 static/shared roots pass the focused GPU fixture.

## DXBC / AIRCONV Impact

None; no lowerer/backend selection changes or fallback. DXBC remains AIRCONV.

## DXIL / MSC Impact

Native VS-only and combined VS/PS typed origins are connected. Existing guarded
resource lowering, per-stage record indices, cache stage identity, compiler root
reflection and native render configuration are reused. Each stage remains bounded
to 64 records. Compute conversion uses the same arguments through a shared helper.

## Shared Runtime Impact

Static recording snapshots and unique submission live observations remain shared.
Heap resolution releases its lock before allocation/encoder use. Resource and
snapshot retention remain submission-owned; no PendingDescriptorUse or static
submission reread was introduced. No command stream, template or PSO storage is
modified during replay. Compute/indirect-compute layouts remain single-TLAB.

## Tests Added

The public fixture adds VS-only and combined modes, ALL versus disjoint VS/PS t0
tables, unequal record counts and FirstElement/count, repeated live changes to
both resource and view bounds, and independent VS/PS count/OOB arithmetic. VS
exports Load(0)+2*Load(1) through a noninterpolated varying; PS reads/stores two
lanes. CPU expectations use explicit known data, not shader answers. Poison,
prefix/suffix guards and ordinary PSO/root/TLAB restoration are checked.

The missing-VS-visible-table negative accepts only E_NOTIMPL/E_INVALIDARG at PSO
creation or subsequent Close failure; it never submits. Positive typed VS uses
the same shader bytes. Matching embedded mode is restricted to static/shared
roots; it does not claim broader override/serialization compatibility.

## Tests Run

Both Meson reconfigurations, focused builds and full default builds pass. Latest
host tests pass 5/5 per configuration. Focused MinMax fixture was rebuilt and full
default builds completed again before its cache deployment. Diff check passes.

Final GPU logs are final5-* under
/Users/zhangbo/.cache/dxmt-origin-vsps.Dws0kF:

- Eight stage processes: normal/no-private x SM6.0/6.6 x explicit/deployed DXC,
  each 32 typed draws, including VS-only and combined: 256 draws.
- Eight pixel regressions with the same build/selection cross-product, each
  16 typed draws: 128 draws. Missing vertex-visible table is rejected without GPU.
- Two matching embedded-root processes, eight typed draws each: 16 draws.
- Every typed draw also checks a same-encoder ordinary draw at pixel1: total
  400 typed and 400 ordinary restoration draws.
- Two root processes and four direct compute static/live full-buffer readbacks.
- Four root-updating indirect-compute multi/live full-buffer readbacks.
- Two Cube-array SampleBias MinMax graphics regressions, including direct and
  indirect counts 0/1/7 and ordinary restoration.
- Same final public test binary on baseline DLLs in both variants: expected exit1
  at typed VS recording, before GPU submission. All 18 current graphics processes
  exit0; both baseline processes exit1.

## Runtime Results

Final combined live shared replacement: VS value49, PS lanes49/0, RT542/497;
disjoint replacement: VS value75, PS lanes49/0, RT802/757. Alternates restore
independent origins/counts. UAV values include the separate +100 store term;
prefix/suffix poison guards and ordinary RT77 are preserved. Root/compute and
MinMax regressions also pass. Earlier exploratory logs are not final evidence.

Staged DLL SHA1 matches the final post-regression full build: normal
ecc6dd2a5e350b8129ad592a2719da5d69fa3d04; no-private
44993a6f6bab9486885ee9996edad0fdd8db2fe1. Final fixture SHA1:
normal 5b4a8e3323fac9eb40f694a946e8c28f8ac31ba0; no-private
d7e7c7ac0fd4a86ef6a512b596c3832b5c31fd07. Baseline uses identical per-build fixture
bytes. Native winemetal hashes remain unchanged. Reproduction scripts and complete
hash list remain in the evidence cache. No installed Wine/prefix/game DLL writes.

## Standards

Readonly review found no hard standard violation or concrete producer/consumer
ABI mismatch. Two P3 heuristic suggestions remain: centralize stage mappings and
encapsulate the stage record prefix/parallel arrays. Retained for this bounded
change: wrappers fix the internal mapping pairs, and explicit per-stage/count/
location checks protect the existing ABI without a wider abstraction redesign.
Final split-argument review found no additional issues.

## Spec

Readonly review initially found weak live origin/count coverage, now resolved by
independent late view updates and VS/PS bounds-sensitive reads. Static no-reread
is explicitly source-inspected only, not a GPU timing claim. Final review found
no remaining actionable bounded implementation issue; early PSO negative does
not mask positive GPU failures. Summary: Standards two nonblocking P3 judgements;
Spec zero remaining actionable findings, with the static timing evidence limit.

## Known Limitations

No depth-only typed VS (PS absent), emulated GS/tessellation/mesh, stream output,
predication/indirect graphics or MinMax coexistence admission. Broader required
formats, heaps/unbounded provenance, lifetime/concurrent matrices and unconditional
compiler distribution remain open. GPU resubmissions here wait for completion;
they do not prove overlap/reset lifetime or static capture timing. No fresh game,
performance or tessellation acceptance. This is not full VS/PS or FL12 qualification.

## Capability Status

TypedUAVLoadAdditionalFormats remains false; no capability promotion.

## Feature Level Impact

None. FL12_0/FL12_1 remain disabled; full objective remains active.

## Git Status

Precommit: only the twelve task files changed; diff check clean.

## Push Status

Not pushed.

## Next Recommended Task

Connect depth-only typed VS (no PS) using the native submission ABI, then continue
the remaining typed stages/formats and other production workstreams. Do not
replace those implementation gaps with a tests-only closure phase.

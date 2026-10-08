# Tiled qualification consolidation

Deployed-path repair checkpoint: D3D12_FRACTIONAL_CLAMP_REPAIR.md supersedes the
unresolved SampleLevel lowering and reserved clamp=1.5 oracle described below.
The latter must return OOB (0), not the last mip value. Corrected final fixture
fails against old native and passes against new with matched EXE/PE/compiler
and actual runtime provenance. --tiled-compiler-dir (standalone --compiler-dir)
enables the existing automatic private path only for that case. Without the
deployment option the native metadata limitation remains. All historical
results below are preserved with this explicit scope correction; no Tier2 closure.

Baseline 98d5183f. Hypothesis: reusing the provenance-controlled fixture runner
and separating implemented AIR scopes from remaining MSC/packed-tail/oracle gaps
will expose current sparse failures without falsely closing Tier 2. Evidence:
the legacy runner inherits ambient state, checks exit status only, repeats an
obsolete AIR-no-sideband blocker, skips actual clamp execution and can print
Tier2 PASS for --only subsets. Expected effect: one runnable matrix shared with
the FL12 gate, exact completion markers, actual PE/Unix identity and explicit
missing/blocked scopes. Risk: classification changes may hide failures or treat
partial selections as complete. Validation: both builds, numeric sparse/remap/
NULL/cross-queue and status fixtures, real fractional clamp, omission/failure/
subset tests and review. No capability promotion or renderer fix is inferred.

## Implementation and current result

The standalone dx12_tiled_tier2_gate now wraps the same 20-case manifest and
run_tiled_gpu_matrix used by the FL12 runner. It requires --runtime-dir, accepts
an explicit --wine and saves full JSON through --output. Every actual case uses
controlled capability/compiler/AIR environments, exact completion markers and
target PE/Unix provenance. Unselected cases remain UNVERIFIED; --only cannot
turn partial success into Tier2 PASS. Missing runtime is also UNVERIFIED.
The shared mandatory ledger adds tiled_mandatory_gpu_matrix.

The matrix executes resource metadata/boundaries, MSC reserved buffer/table/root
and texture/mip-array semantics, CopyTileMappings, cross-queue/remap/NULL,
DXBC texture Load/filtering/status and eleven AIR raw/structured feedback paths
(table/root SRV/UAV, indirect SRV/UAV, VS/PS/GS/HS/DS). AIR sideband is no longer
misclassified as absent. Full MSC raw/structured status sideband, packed-tail
physical semantics, fresh native Windows and complete dimension/format/stage/
logical-width alias qualification remain explicit nonpassing gaps. The existing
texture-status fixture already includes the filtering footprint, so it is not
executed twice under two names or counted as independent evidence.

Both fixture and missing shader-target builds pass. First-round missing-shader
results are retained, not accepted as semantic executions. The root SRV required
marker was corrected from 1234 to its actual CPU-seeded 0x12345678 (305419896);
the fixture's own value check had passed. The generic compute fixture's DXBC
text label checks container magic, not the executable chunk; it is not used as
proof of shader backend identity. MSC fixture targets are built with DXC; no
DXIL-to-AIR substitution is introduced by the runner.

Fresh qualification-tiled-complete-{normal,no-private}.json each contains 19
PASS and one genuine FAIL, with target PE/Unix provenance. Fractional clamp
returns ordinary values 1/1/1/1/3 instead of 1/2/3/4/3; the reserved texture
also ignores the fractional clamp instead of returning 1/2/3/3/3. Overall gate
and execution are FAIL. This supersedes the legacy fixed M1/MSC observation with
current M4/MSC 4.0.1 evidence; it does not overwrite historical resource audits.

Adding the same DXC deployment did not repair clamp on either variant. An early
no-private deployed contrast overlapped a subset probe and is excluded as
isolated evidence; qualification-tiled-clamp-deployed-api-repeat.json is the
subsequent serial repetition with compiler/PE/Unix identity and remains FAIL.
qualification-tiled-api-no-private.json again returns 19 PASS / clamp FAIL with
Metal API Validation Enabled. Correct API use is not correct clamp semantics;
no shader-validation or full native qualification is claimed.

The actual subset CLI test runs one AIR table case successfully but exits 1,
retains unselected rows and prints TIER2_GATE=NOT_SATISFIED. Evidence:
qualification-tiled-subset.{json,log}. All evidence is under
/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE. Both 16-test host suites pass,
including 81 Python gate tests; git diff --check passes. Tests cover all-case
registration, actual clamp failure, missing runtime, subset omissions, runtime
hash changes and mandatory FL12 evidence. No complete FL12 runner run is claimed.

Standards/Spec review is main-agent only, informed by code-review. Diagnosis
established a real red numerical oracle and disproved compiler absence as the
sole cause; Metal validation kept API and numerical outcomes separate. There is
no remaining actionable infrastructure finding, but the renderer clamp defect
is explicitly unresolved. Next implement the fractional-clamp semantic repair,
then continue the full resource/format/raster/MinMax requirements. No capability
change, production counter, game/prefix deployment, game restart or push.

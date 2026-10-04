# Task Analysis

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

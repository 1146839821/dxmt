# Task Analysis

## Current Branch

`feat/d3d12-1`.

## Baseline

Clean 1f38a4c; read-only origin/feat/d3d12 e147c710.

## Local Commits Since origin/feat/d3d12

127 at task start. Full FL12 objective remains active.

## Current State

Independent DXIL comparison consumers survive reduction lowering and MSC conversion.
Production-root GPU coexistence is not yet qualified.

## Existing Implementation

`dxil_minmax_binding.cpp` preserves qualified native comparison consumers while
collecting only regular reduction pairs. Runtime materializes pair descriptors
alongside the original application root and descriptor heaps.

## Relevant Files

`src/winemetal/unix/dxil_minmax_binding.cpp`,
`src/d3d12/d3d12_minmax_dispatch.cpp`, `tests/dx12/meson.build`,
the new comparison GPU fixture and HLSL.

## Existing Tests

`dxil_minmax_ir` comparison preflight and PE prepare/MSC conversion qualify compiler
admission. `dx12_minmax_dispatch` and Cube graphics fixtures qualify regular
reduction paths, not native depth comparison coexistence.

## D3D12 Contract

SampleCmpLevelZero compares the reference against mip0 depth. Comparison samplers
are independent of minimum/maximum reduction samplers. RS1.1 volatile sampler
descriptors may be changed before submission, after preceding uses complete;
static resource descriptors remain unchanged. Depth data updates require state
transitions and completed clears. Repeated submissions must consume the current
volatile sampler and resource data, not stale private pair materializations.

## DXBC / AIRCONV Impact

None; AIR gates are cleared and only supplied DXIL is executed.

## DXIL / MSC Impact

Production compute conversion and augmented root binding are exercised. No
compiler implementation, pair ABI or executable-family selection changes.

## Shared Runtime Impact

No runtime source changes. Cache overlays only; installed/prefix DLLs unchanged.

## Missing Pieces

Mixed comparison/reduction production GPU execution, depth above/below reference,
and independently checked sampling outputs. Pixel/other comparison operations,
static comparison samplers and broad format/view coverage remain out of scope.

## Risks

Zero/stale depth can mimic a single below-reference oracle; use 0.25 then 0.75.
Integer conversion can hide fractional comparison results; preserve float bits.
Fence-visible allocator reset may fail independently of sampling; keep separate
upload and dispatch storage and retain original diagnostic evidence.

## Minimal Implementation Plan

Add one optional public-API compute fixture and HLSL. Upload RGBA8, clear a real
D32 resource, record one dispatch list, vary reduction/comparison samplers across
completed submissions, update depth and repeat. Read distinct result words with
per-submit sentinels. No broad matrix or capability changes.

## Task Scope

Bounded compute SampleCmpLevelZero plus regular SampleLevel reduction on real
RGBA8 and depth resources, with independent output words and live sampler updates.

## Hypothesis

Original comparison bindings coexist with the private reduction pair argument buffer.

## Evidence

The previous task preserved comparison instructions and compiled legacy/modern
DXIL, but did not execute these instructions through the production root signature.

## Expected Effect

Expose incorrect comparison resource/sampler binding without broadening capabilities.

## Risk

Depth texture type, comparison direction and stale sampler snapshots may differ
from offline assumptions. Use a real depth resource and exact independent oracles.

## Validation Plan

Reconfigure both configurations; focused fixture and full builds; cache-only
runtime overlays; GPU readback; host suites; self-review; local commit without push.
Record each process exit code, normal/no-private configuration and legacy/modern
DXIL input, current PE/native hashes matching the overlay, MSC version and GPU
vendor/device from runtime logs. The fixture records requested FL and OPTIONS.
These are DXMT_LOCAL_PASS observations, not NATIVE_OBSERVED D3D12 results;
unexecuted comparison variants stay UNVERIFIED.

## Capability Impact

None. FL11_1 unchanged; FL12_0/12_1 remain disabled.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

Task 1f38a4c; read-only origin/feat/d3d12 e147c710.

## Local Commit

The local commit containing this record (`git log -1 --format=%H --
docs/D3D12_MINMAX_COMPARISON_GPU.md`); no history rewrite or push.

## Changed Files

`tests/dx12/dx12_minmax_comparison_gpu.cpp`, `minmax_comparison_gpu.hlsl`,
`tests/dx12/meson.build`, this record and the FL12 closure ledger.

## Implementation

One real public-API compute fixture creates the application RS1.1 root, real
RGBA8 SRV and depth-enabled R32 typeless resource with D32 DSV/R32 SRV. Native
SampleCmpLevelZero and private regular reduction use separate result words.
The same closed dispatch list executes twelve times: Min/Max/ordinary linear,
LESS_EQUAL/GREATER_EQUAL, depth0.25/depth0.75. Samplers are volatile and replaced
only after completed submissions; resource descriptor slots stay static, while
depth data updates have explicit transitions and completion. Raw comparison
float bits must equal exact0/1; reduction red must equal16/240/128. Each submission
initializes output sentinels and checks untouched words. Separate retained upload,
dispatch and depth-update allocator storage avoids conflating reset with sampling.

## DXBC / AIRCONV Impact

None. No DXBC shaders, AIR helper gates or mixed executable families.

## DXIL / MSC Impact

Real independent legacy SM6.0 and modern SM6.6 containers execute the production
MSC compute conversion/augmented-root binding path. No compiler source changes.

## Shared Runtime Impact

No runtime source changes. Current DLL/native copies staged only in task-cache
overlays. No game deployment, Steam/wineserver restart or installed DLL overwrite.

## Tests Added

Optional PE fixture and optional SM6.0 HLSL target. Modern SM6.6 input is explicitly
compiled from the same source with the selected DXC SDK. No broad matrix added.

## Tests Run

Both configurations reconfigured; focused fixture targets and final full default
builds completed before staging. Host suites5/5 each. Four final GPU processes
(normal/no-private × legacy/modern) exit0, twelve groups each: **48 groups across
four processes**, not48 independent process runs.

Evidence: `/Users/zhangbo/.cache/dxmt-comparison-gpu.RCJ6w1`, final build/host logs,
`final-{normal,no-private}-{legacy,modern}.log`, final input containers and matching
build/stage/overlay hashes in `hashes-{normal,no-private}.txt`.

Normal d3d12 SHA1 `5123b4eaa30b05b8c0985b26ea0910d2e82c93f1`, native
`aaf00b9871f4e59899d440fb0b5af7b833dc9403`; no-private d3d12
`5ebd5a6c9b35c9fe08481c0a3df97b5914bf0eb0`, native
`3cb8f7736b63e89bd823139228554a863da9b2f4`.

Run the staged fixture with `WINEDEBUG=-all`, the existing
`/Users/zhangbo/Documents/Vibe-Codeding/wineprefix`, and
`WINEDLLOVERRIDES=d3d12,dxgi,winemetal=n,b`. Select the matching cache
`runtime/bin/wine` or `runtime-no-private/bin/wine`, staged PE from
`normal/` or `no-private/`, then arguments `Z:\Users\zhangbo\.cache\dxmt-comparison-gpu.RCJ6w1\final-comparison-{legacy,modern}.dxil`
and `Z:\Users\zhangbo\Documents\Vibe-Codeding\dxmt\tools\dxc\bin\x64`.

## Runtime Results

**DXMT_LOCAL_PASS**, Apple M4 (Apple vendor), macOS27, MSC4.0.1; requested device
FL11_0 is a fixture creation argument, not a new advertised feature level.
OPTIONS show TiledResourcesTier0, TypedUAVLoadAdditionalFormats0 in both builds,
LogicOp1 normal/0 no-private. Every result and post-loop classification passed.
No NATIVE_OBSERVED Windows D3D12 run exists.

Self-review and readonly Standards/Spec reviews closed the missing analysis
inventory, fractional comparison oracle and zero/stale depth oracle findings.
Reviewers inspected source, not runtime execution; runtime evidence above was
collected by the main agent. Final diff check passed.

## Known Limitations

Bounded uniform depth2×2/mip0 compute SampleCmpLevelZero only. Pixel SampleCmp,
static comparison samplers, comparison PCF fractional/spatial oracles, broader
resource dimensions/formats/status/provenance and native Windows comparison
remain UNVERIFIED. This does not qualify full MinMax filtering.

Initial no-private run failed at allocator reset after upload/fence, before any
sampling. An unchanged-binary retry passed. Both original logs are retained
(`no-private-legacy.log`, `no-private-legacy-reset-retry.log`). Cause unresolved;
new fixture's separate storage isolates sampling and does **not** claim allocator
reset fixed or qualified. Initial integer-oracle runs are not counted as final
qualification. No performance/game/tessellation acceptance was run.

## Capability Status

PARTIAL overall; bounded compute coexistence DXMT_LOCAL_PASS.

## Feature Level Impact

FL11_1: unchanged.
FL12_0: disabled, mandatory production/semantic gaps remain.
FL12_1: disabled, further requirements remain.

## Git Status

Five task files selected for a local commit after final validation/review.
Final commit identity and clean status are verified after commit and reported in
the handoff. Full FL12 development objective remains active.

## Push Status

Not pushed. Remote baseline remains read-only.

## Next Recommended Task

Minimize and diagnose the no-private fence-visible allocator-reset failure with
a focused reproduction before changing lifetime/completion ordering. It remains
an unresolved production-contract observation, not a sampling failure. Then
continue the remaining MinMax production gaps before expanding full FL matrices.

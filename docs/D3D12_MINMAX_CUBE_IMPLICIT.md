# Task Analysis

## Branch / Baseline / Local Commits

`feat/d3d12-1`, clean `ef3477d`; 121 local commits beyond read-only
`origin/feat/d3d12` (`e147c710`). Previous turn completed Cube SampleGrad.

## Current State / Existing Implementation / Relevant Files / Existing Tests

`CreateImplicitReductionSample` already evaluates quad derivatives and ordinary
dynamic sampling before descriptor-controlled branching. Cube is excluded, and
the call to `CreateIsotropicGradientLOD` does not supply its direction.
Reuse the Cube projection and tap helper. Extend existing opt-in pixel cases in
`tests/dx12/dx12_graphics_sm5.cpp`, not MSC's fragment preparation harness.

## D3D12 Contract / Hypothesis / Evidence / Expected Effect

[D3D11.3 7.18.11 and 22.4.15-16](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm): derive direction, project on primary face, compute
LOD, add sampler then instruction bias, apply clamps and reduction footprint.
Hypothesis: connecting the existing direction-aware primitive eliminates the
implicit Cube gap without a new shader ABI. Evidence: explicit gradient Cube
projection and real readbacks passed in ef3477d.

## DXBC / AIRCONV Impact / DXIL / MSC Impact / Shared Runtime Impact

Admit pixel-only float Cube/CubeArray Sample/SampleBias with zero offsets and no
feedback. Keep derivatives outside dynamic reduction branches. DXIL remains
independent; no host runtime, descriptor ABI or backend fallback changes.

## Missing Pieces / Risks / Minimal Implementation Plan

Connect shape guard and direction parameter, maintain classification matching.
Fixture risks: cube slice/mip uploads, array layer poison, helper lanes and LOD
bias. Reuse normal/array cube views and existing root sampler setup. Add varying
direction and radial pixel inputs, static/dynamic MIN/MAX and ordinary cases.
Broader view/clamp/format/lifetime, feedback/aniso and DXIL Cube remain open.

## Validation Plan / Capability Impact

Reconfigure both builds, focused target then full builds before isolated DLL
staging. Real pixel readback plus earlier planar and gradient regressions.
Self-review and local commit, no push/game deployment/process management.
No FL/SM/capability promotion; full goal stays active.

# Task Result

## Branch / Baseline / Implementation / Changed Files

Task baseline `ef3477d`, remote baseline `e147c710`, branch `feat/d3d12-1`.
AIR converter admits bounded Cube/CubeArray pixel Sample/SampleBias and supplies
direction to existing projection; consumer classification matches the guard.
`dx12_graphics_sm5` adds opt-in cube shaders/cases, captured cube views, bounded
slice/mip uploads and distinguishable CubeArray layer0=7. No MSC or shared runtime
source changes, new shader ABI, fallback or executable-family mixing. This record
and closure ledger record the production increment.

## Tests Added / Tests Run / Runtime Results

Both configurations reconfigured, focused target then both full default builds
completed before isolated staging. Host Meson suites pass **5/5 each**.
Final evidence `/Users/zhangbo/.cache/dxmt-minmax-cube-implicit.UaZOHy/*-final.log`:
**68 fixture case passes with GPU readback**, 34 per configuration, across ten
final processes. This is a case count, not an exact submission/dispatch count
(the same-PSO sampler switch includes multiple observations).
Counts exclude first diagnostics. Each configuration verifies:

- 18 new implicit Cube/CubeArray pixel cases: static/dynamic MIN16/MAX240,
  direction-driven mip1=224, instruction bias +0.75 selecting mip2=96,
  sampler +0.75/instruction -0.75 returning mip1=224, radial cancellation
  returning mip0=32, ordinary dynamic128. CubeArray samples layer1, not poison
  layer0. These are not a complete Cartesian shape/filter matrix.
- Nine earlier planar implicit cases, ordinary triangle and null texture query.
- Three explicit/runtime regressions: negative radial Cube gradient112,
  Z-tie Cube gradient80, same-PSO sampler switch16/240/128/16.
- Both AIR reduction gates disabled: ordinary Cube and CubeArray pixel cases
  still return128, without reduction-enabled shader generation.

The varying direction is `(1, p.x*0.46, p.y*0.46)` at pixel `(0.5,0.5)`.
Projected face derivatives are0.23, cube side8 gives1.84, LOD≈0.88; point mip
selects1. Radial `(p.x,p.x*0.5,0)` cancels before LOD. Uniform mip reds are
32/224/96/160; spatial reds16/64/192/240. This checks production implicit
lowering with real quad/helper lanes, not merely shader compilation.

Final staged/build SHA1 D3D12 normal `692aced0da7e99c9413ec683533a3739ae2ebb53`,
no-private `a7532c549bf9bee15e0eddde1cf38bd588b5b7c1`; native winemetal normal
`154a52b8790052aa52b0748d0edab132974e9758`, no-private
`c0e48a4f1c8359c6b514f3921662ea10536e26b1`.

Example reproduce with a fresh isolated shader cache:

```sh
WINEDEBUG=-all MVK_CONFIG_LOG_LEVEL=0 \
DXMT_ENABLE_AIR_MINMAX_DYNAMIC=1 DXMT_ENABLE_AIR_MINMAX=1 \
DXMT_SHADER_CACHE_PATH=/Users/zhangbo/.cache/dxmt-minmax-cube-implicit.UaZOHy/repro-cache \
WINEDLLOVERRIDES='d3d12,dxgi,winemetal=n,b' \
WINEDLLPATH=/Users/zhangbo/.cache/dxmt-minmax-cube-implicit.UaZOHy/normal \
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
/Users/zhangbo/.cache/dxmt-minmax-cube-implicit.UaZOHy/runtime/bin/wine \
  /Users/zhangbo/.cache/dxmt-minmax-cube-implicit.UaZOHy/normal/dx12_graphics_sm5.exe \
  cube-implicit-bias-dynamic cube-array-implicit-radial-dynamic
```

## Known Limitations / Capability Status / Feature Level Impact

PARTIAL: bounded AIR Cube implicit production support, not full MinMax. DXIL
Cube remains independent/open; feedback, anisotropy, complete shapes/formats,
view/resource clamps, signed seam implicit footprints, nonuniform/divergent
sampler and lifetime qualification remain open. No native Windows oracle,
Metal validation, complete GPU matrix, fresh MSC-stage acceptance or game /
performance test this increment. No game/prefix DLL deployment or Steam /
wineserver restart/process management. FL11_1 unchanged; FL12_0/FL12_1 unpromoted.

## Standards

Main-agent self-review and independent Standards source review found no hard
documented breach. One optional maintenance suggestion: name/centralize the
numeric sampling case mappings instead of repeating them across mip/filter /
shader selection. Neighboring fixture style is retained for this increment.

## Spec

Independent source review found no confirmed implementation error or scope
creep; it checked derivative/ordinary-sample ordering, projection reuse, layer
handling, footprints and analytical LOD oracles. One qualification gap remains:
complete static/dynamic/ordinary combinations per cube shape are not covered.
Runtime evidence listed above was run by the main agent; reviewers did not run
GPU tests and their source conclusions are not independent runtime confirmation.
LLVM skill guided the SSA/control-flow review. `git diff --check` passes.

## Git / Push Status / Next Recommended Task

Local commit reported at handoff; NOT PUSHED. Next address the independent
DXIL Cube footprint/binding gap, then complete required resource/filter
contracts before broad matrix reruns. Full goal remains active.

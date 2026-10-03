# Task Analysis

## Branch / Baseline / Local Commits

`feat/d3d12-1`, clean baseline `acba9d8`, remote `origin/feat/d3d12`
at `e147c710`; 120 local commits. Preserve backend isolation and capability gates.

## Current State / Existing Implementation / Relevant Files / Existing Tests

Cube SampleLevel is implemented. `AIRBuilder::CreateIsotropicGradientLOD`
accepts only planar/volume gradients and has no direction argument. Cube
SampleGrad is rejected by consumer classification and converter lowering.
Reuse the existing Gram-matrix LOD calculation, cube footprint helper and
`air_minmax_ir` / `dx12_texture_sampler` tests.

## D3D12 Contract / Hypothesis / Evidence / Expected Effect

[D3D11.3 specification 7.18.11](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
requires Z/Y/X major-axis tie precedence, quotient-rule face derivatives and
half-side scaling. Direction-radial derivatives must cancel, unlike volume
gradients. Extend the internal LOD interface with optional direction. Project
onto the two minor axes; simultaneous face mirroring of both derivatives is
orthogonal and cannot change their Gram matrix. Use division by signed major
before forming the quotient derivative to avoid squaring the direction magnitude.

## DXBC / AIRCONV Impact / DXIL / MSC Impact / Shared Runtime Impact

Connect float, zero-offset, feedback-free Cube/CubeArray SampleGrad to existing
clamped reduction helper. No DXIL changes, fallback, runtime ABI or descriptor
changes. Existing bias/clamp/minification policy is reused.

## Missing Pieces / Risks

Direction sign/ties, radial cancellation, mip scaling, malformed IR operands.
Implicit cube, anisotropic/feedback and complete view/clamp contracts remain
open. No FL/SM promotion or game deployment.

## Minimal Implementation Plan / Validation Plan / Capability Impact

Implement projection before existing normalized Gram matrix; extend consumer
guard and focused IR checks. Add actual Cube/CubeArray gradient mip readbacks,
retain prior explicit-LOD regressions, verify both builds. Self-review and local
commit only after validation. Full MinMax remains PARTIAL.

# Task Result

## Implementation / Changed Files / Backend and Runtime Impact

AIRBuilder's internal gradient LOD API now accepts a cube direction and applies
signed-major quotient projection, Z/Y/X tie selection and half-side scaling
before the existing normalized Gram-matrix calculation. Consumer classification
and SampleGrad lowering admit float Cube/CubeArray with zero offsets and no
feedback. Existing sampler bias/clamp, runtime predicate, ordinary sampler
restore and cube reduction footprint paths are reused. No DXIL, shared runtime,
descriptor ABI, capabilities or FL/SM changes. Tests extend the existing IR and
D3D12 sampler harnesses; this record and closure ledger record the increment.

## Tests Added / Tests Run / Runtime Results

Both configurations reconfigured; focused targets and both full default builds
completed before final isolated staging. Host suites pass 5/5 each. Linked AIR
IR probes pass 12/12 each, including both cube gradient signatures and missing /
wrong-type direction rejection without IR mutation.

Final evidence: `/Users/zhangbo/.cache/dxmt-minmax-cube-grad.vCJxW3/*-final.log`.
**50 final Windows GPU readback processes**, 25 per configuration, all exit 0:
19 Cube gradient cases and six regressions per build. Earlier diagnostics are
excluded. Tangent2, direction10/tangent20, negative direction with independent
tangent and CubeArray layer1 select mip1=8. Positive radial4096 cancels and
returns mip0 MIN/MAX=16/240. Static cube gradients return16.

Self-review plus independent Spec review identified two test blind spots; added
negative-major radial-only/off-axis and primary-axis tie probes. Signed negative
radial-only and off-axis return112. Z/Y ties return80/32 using point mip filters;
incorrect X-major projection would select mip1=8. These avoid final-mip clamp
masking. Existing cube edge/corner/point tie, volatile sampler switch, planar and
1D-array gradient readbacks remain passing. Independent reviewers inspected
source only, not GPU executions; the added follow-up probes were main-reviewed.

Final staged/build D3D12 SHA1 normal `af4f3fd927229cf5199b52fba9bc869388bc793f`,
no-private `9fc6ba48b6875a605755fe97f853d64342691e58`; native winemetal normal
`2c29b176e39a61781a52c23caf631b51cc1f3ed0`, no-private
`c982f4a1719c6122eefa29ee2fab8617ffdb0f2a`.

Reproduce with isolated runtime and matching DLL directory, the two AIR opt-in
gates, fresh shader cache, existing Wine prefix and `--dxbc`:

```sh
WINEDEBUG=-all MVK_CONFIG_LOG_LEVEL=0 \
DXMT_ENABLE_AIR_MINMAX_DYNAMIC=1 DXMT_ENABLE_AIR_MINMAX=1 \
DXMT_SHADER_CACHE_PATH=/Users/zhangbo/.cache/dxmt-minmax-cube-grad.vCJxW3/repro-cache \
WINEDLLOVERRIDES='d3d12,dxgi,winemetal=n,b' \
WINEDLLPATH=/Users/zhangbo/.cache/dxmt-minmax-cube-grad.vCJxW3/normal \
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
/Users/zhangbo/.cache/dxmt-minmax-cube-grad.vCJxW3/runtime/bin/wine \
  /Users/zhangbo/.cache/dxmt-minmax-cube-grad.vCJxW3/normal/dx12_texture_sampler.exe \
  --dxbc --minimum-cube-grad-negative-radial
```

## Standards

No documented hard breach found. One optional maintenance finding: positional
cube case mappings repeat across source, gradients, CLI, expected values and mip
counts. Retain neighboring harness style for this focused increment.

## Spec

No confirmed implementation error or scope creep. Signed projection and
orthogonal face-orientation invariance were independently source-reviewed.
Two identified validation blind spots were addressed with discriminating GPU
probes. These are focused tests, not full signed-face/view/clamp/format matrices.
No native Windows hardware oracle, Metal validation, full GPU matrix, fresh
MSC-stage regression or game/performance acceptance was run this increment.

## Known Limitations / Capability Status / Feature Level Impact

PARTIAL: bounded AIR Cube SampleGrad production support, not complete MinMax.
DXIL Cube, implicit Cube operations, anisotropic/feedback and complete
view/resource-clamp/filter/format/lifetime contracts remain open. FL11_1
unchanged; FL12_0/FL12_1 unpromoted. No game/prefix DLL deployment, Steam or
wineserver restarts/process management. Full goal remains active.

## Git / Push Status / Next Recommended Task

Local commit reported at handoff; NOT PUSHED. Next connect implicit Cube
derivatives through this projection and address the independent DXIL Cube
footprint/binding gap; finish required resource/filter contracts before broad
matrix reruns.

# Task Analysis

## Current Branch

`feat/d3d12-1`.

## Baseline

Clean 8dd3c02; read-only origin/feat/d3d12 e147c710.

## Local Commits Since origin/feat/d3d12

130 at task start.

## Current State

Pixel origin correction exists but requires a per-process environment override.
Compute already discovers a compiler deployed beside the loaded D3D12 DLL.

## Existing Implementation

Shared DXC reflection selector identifies typed-buffer declarations, returns
S_FALSE for ordinary shaders or absent deployment, and reports present compiler
load/reflection failures. Graphics retains original shaders and application root
for lazy private variant creation. Meson has explicit DXC deployment input.

## Relevant Files

Graphics PSO state, pipeline initialization, PreDraw and focused pixel fixture.

## Existing Tests

8dd3c02 validates opt-in pixel GPU arithmetic, live/static ranges, VS constants,
ordinary restoration, unsupported typed VS, compute and MinMax regressions.

## D3D12 Contract

Supported typed-buffer draws must not depend on an application-specific override
when their required compiler is deployed. Unsupported consumers must not silently
execute an uncorrected private descriptor. Ordinary shaders remain ordinary.

## DXBC / AIRCONV Impact

None. Selector is only applied to validated DXIL graphics; no fallback.

## DXIL / MSC Impact

Reuse deployed selector for active graphics stages and retain selected directory
on the application PSO. Existing conversion and reflected binding ABI unchanged.

## Shared Runtime Impact

PreDraw chooses override first, otherwise retained deployment, and preserves
existing unsupported draw/MinMax rejection. No descriptor or replay changes.

## Missing Pieces

Graphics deployment selection and override-free recording selection.

## Risks

Changing ordinary PSOs, missing/broken deployment, unsupported typed VS or
emulated graphics, MinMax precedence, retained-directory allocation and lookup cost.

## Hypothesis

Compute's deployed reflection selector can select the existing pixel variant
without a new binding contract or shader lowering.

## Evidence

Graphics PreDraw reads only DXMT_TYPED_ORIGIN_DXC_DIRECTORY. Compute stores a
PSO-specific directory and discovers dxmt-dxc beside its loaded DLL.

## Expected Effect

Supported ordinary typed pixel draws use deployed DXC without an environment
override; ordinary and absent-deployment paths retain prior compatibility.

## Minimal Implementation Plan

Reflect active DXIL shaders at PSO initialization when neither override is set;
retain first typed stage's directory; reuse selection in PreDraw. Add auto and
ordinary-only modes to the existing independent GPU oracle.

## Validation Plan

Configure/focused/full builds on both configurations before task-cache staging.
Same-binary baseline/current auto A/B, absent-deployment negative, ordinary
without deployment, explicit override and auto GPU checks; shared regressions,
self-review and local commit. No game/prefix writes or push.

## Capability Impact

No promotion. Wider typed formats/stages/default distribution and full FL12
qualification remain incomplete.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

8dd3c02; read-only origin/feat/d3d12 e147c710.

## Local Commit

Local commit containing this record; resolve with `git log -1 --format=%H --
docs/D3D12_TYPED_ORIGIN_GRAPHICS_SELECTION.md`. No rewrite or push.

## Changed Files

`d3d12_device.hpp`, graphics pipeline, command list, pixel GPU fixture, this
record and the FL12 closure ledger.

## Implementation

Graphics initialization reuses the deployed DXC reflection selector after stage
and backend validation. With neither override set, it inspects present PS/VS/GS/
HS/DS declarations, retaining the first typed stage's absolute compiler directory
on its PSO. No executable-, working-directory- or repository-relative discovery.

PreDraw prioritizes an explicit override, otherwise borrows the immutable PSO
directory without a per-draw string copy. Override conversion allocation failure
marks recording failure. Existing lazy native pixel variant and descriptor/replay
mechanisms are reused. Unsupported typed VS/emulated graphics/indirect/predication
remain fail-closed. A late MinMax override cannot suppress an already-selected
origin repair: the existing combination guard rejects it. MinMax set before PSO
creation still suppresses automatic discovery, preserving the existing policy.

## DXBC / AIRCONV Impact

None. Only validated DXIL graphics enters discovery; no backend fallback.

## DXIL / MSC Impact

Deployment-dependent selection added, not new lowering, reflection layout or
cache ABI. Ordinary shaders do not acquire an origin variant.

## Shared Runtime Impact

Only graphics selection and error handling changed. Descriptor observation,
submission-owned binding and GPU completion lifetime remain unchanged.

## Tests Added

Existing pixel oracle gains `--auto`, `--ordinary` and `--minmax-switch` modes.
Auto clears both environment overrides. Ordinary uses only the VS constant root,
expects both pixels77, untouched UAV data and guards. Named DrawMode replaces
boolean mode combinations. Late-switch case checks Close rejection without GPU
submission; its specific combination-guard log is also required evidence.

## Tests Run

Both configurations reconfigured; focused and final full builds completed before
cache-only staging. Final host suites pass5/5 each. Final normal/no-private ×
SM6.0/6.6 auto processes pass16 private draws each (**64 private +64 ordinary
restoration draws**), including numeric typed SRV/UAV, guards, visibility, live
replacement, repeated completed submissions, VS constants and typed-VS rejection.

Explicit override processes pass in both configurations. Ordinary-without-DXC
processes each pass4 completed submissions/8 ordinary draws. A present malformed
compiler fails PSO creation; explicit valid override bypasses it and passes the
full16-submission typed oracle. Missing deployment preserves Close rejection for
non-aligned typed views in both configurations. Late MinMax cases reject in both.

Default compute origin arrays (static/live, four processes) and existing Cube-array
bias MinMax graphics (two processes) pass with final rebuilt DLLs.

## Runtime Results

Evidence: `/Users/zhangbo/.cache/dxmt-origin-graphics-auto.2K3x4b`.
`final2-exits.log` records all expected outcomes. `final2-auto-*`, `final2-override-*`,
`final2-ordinary-*`, `final2-absent-*`, `final2-broken-*`, `final2-baseline-*`,
`final2-minmax-switch-*`, compute/graphics/host logs and final build/hash records.

Same final fixture against 8dd3c02 DLLs fails auto Close on unavailable unaligned
views (both exit1); current deployed-DXC auto path passes (exit0). Auto compiler
argument is deliberately `Z:\not-used-in-automatic-mode`, not a real compiler
path. Both overrides are cleared by the fixture, so success is not override
evidence mislabeled as automatic selection.

Malformed compiler/control files and all deployment copies exist only under the
task cache. Absent cases use distinct compiler-free application/runtime overlays.
Late MinMax logs explicitly contain `pixel typed-origin indirect/skipped/MinMax
combination is unsupported`; exit status alone could also reflect compiler failure
and would not discriminate this guard.

Build/stage/overlay D3D12 SHA1 match: normal
`38d13da62afdd2ab239bb9b254809ff244b02af4`, no-private
`c73a42af865bd0f65994211fdfc14d138d525e04`. Final fixture A/B SHA1
`d0c3783b56e96fc4c7a5655d3d6ccf81d404340a` matches both variants and their baseline
copies; native build/overlay hashes match too. `run-pixel.sh` in the evidence
directory reproduces selection probes (variant, legacy/modern, kind, log prefix).

## Standards

Readonly review found an unguarded per-draw directory copy; changed to borrowing
the PSO-owned directory, with explicit override conversion guarded. A nonblocking
boolean-mode smell was replaced by DrawMode. Follow-up review closes both findings;
no remaining hard rule violations/blockers. This is not full PreDraw OOM coverage.

## Spec

Readonly review found no initial scope/implementation blocker. Late-switch review
confirmed fail-closed logic but noted exit-only evidence is ambiguous; final logs
were checked for the specific guard. Reviewers did not run GPU tests; main-agent
execution provides the results above. MSC integration skill preserved reflected
binding and reused the existing selection ABI rather than inventing a new one.

## Known Limitations

Automatic behavior requires a deployed compiler; this task does not change the
existing opt-in distribution/license policy. Wider formats, typed VS/PS shared
records, pixel origin arrays/OOB, emulated graphics, indirect/predication and MinMax
typed combinations remain incomplete. No forced OOM, complete lifetime matrix,
selection CPU benchmark, native Windows oracle, Metal validation/trace or games.

## Capability Status

TypedUAVLoadAdditionalFormats remains FALSE. No SM/capability promotion.

## Feature Level Impact

FL11_1 unchanged; FL12_0/12_1 remain disabled. Full production objective active.

## Git Status

At record creation, task changes await local commit. Final delivery verifies the
commit and clean worktree. No unrelated changes were present.

## Push Status

NOT PUSHED. No prefix/game DLL writes or process management.

## Next Recommended Task

Implement shared typed-origin records for VS/PS, preserving per-stage identities,
root visibility and submission timing. Continue wider typed formats and other
FL12 production gaps before expanding the complete matrix.

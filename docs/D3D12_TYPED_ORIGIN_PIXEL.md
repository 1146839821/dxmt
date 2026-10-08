# Task Analysis

## Current Branch

`feat/d3d12-1`.

## Baseline

Clean 49685f7; read-only origin/feat/d3d12 e147c710.

## Local Commits Since origin/feat/d3d12

129 at task start.

## Current State

Typed UAV additional formats remain disabled. Typed-buffer origin correction is
connected only to compute recording and submission, not pixel draw.

## Existing Implementation

Unaligned typed views have an aligned private descriptor plus origin/count.
Validated DXIL lowering adds origin correction; ordinary descriptors remain
empty rather than exposing padding. Compute uses a private reflected root and
submission-owned TLAB/table copies. Static slots are observed at recording;
unique volatile slots are resolved and retained at submission under the heap lock.

## Relevant Files

Typed-origin preparation/root/converter/binding, graphics pipeline, command list,
encoder and queue; focused public-API pixel GPU fixture.

## Existing Tests

Compute typed-origin preparation, roots, pipeline and array probes. Existing
MinMax render replay supplies the immutable per-draw command-copy pattern.

## D3D12 Contract

FirstElement is a typed-element offset, not a native texture-buffer alignment.
Root visibility and static versus volatile descriptor observation must be honored.
Submission copies must not mutate reusable recorded commands or application roots.

## DXBC / AIRCONV Impact

None. DXBC remains AIRCONV; no fallback or mixed-family graphics pipeline.

## DXIL / MSC Impact

Add bounded pixel shader preparation/conversion and a PSO-owned private variant.
Both VS and PS compile against the same augmented root. Reject typed-buffer VS,
emulated graphics, stream output, indirect and MinMax combinations for this slice.

## Shared Runtime Impact

Reuse typed-origin descriptor materialization and private render replay. Retain
resources through GPU completion. Normal and no-private paths share this code.

## Missing Pieces

Pixel container admission, stage-visible binding resolution, graphics variant,
recorded draw marker and submission replay binding.

## Risks

Native VS accessing a patched typed slot, root reflection offsets, live descriptor
replacement, sampler lifetime, ordinary/private PSO transitions and reused lists.

## Hypothesis

The existing compute origin ABI can correct pixel typed-buffer accesses without
a second descriptor implementation when recording is visibility-aware.

## Evidence

Preparation hardcodes compute Inspect; binding resolver accepts ALL only; render
replay handles MinMax only. Native descriptors deliberately omit unaligned views.

## Expected Effect

Opt-in ordinary DXIL pixel draws can address non-aligned typed SRV/UAV views
correctly; existing compute and ordinary descriptor semantics remain unchanged.

## Minimal Implementation Plan

Stage-aware preparation and resolution; shared binding variant; native VS/PS
private PSO; per-draw marker and immutable submission replay; focused GPU oracle.

## Validation Plan

Configure both builds, focused targets, full builds before cache-only deployment.
GPU sentinel readback and compute/MinMax regression, host suites, self-review,
local commit. No prefix writes, games, process management or push.

## Capability Impact

None. FL11_1 unchanged; FL12_0/12_1 and TypedUAVLoadAdditionalFormats remain disabled.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

Task 49685f7; read-only origin/feat/d3d12 e147c710.

## Local Commit

The local commit containing this record (resolve using `git log -1 --format=%H --
docs/D3D12_TYPED_ORIGIN_PIXEL.md`). No history rewrite.

## Changed Files

Typed-origin preparation, root resolution, binding and pipeline headers; shader
converter; graphics PSO; command list, encoder and queue; root/pixel fixtures,
HLSL and Meson targets; this record and closure ledger.

## Implementation

Validated pixel DXIL preparation retains application identity and publishes only
on success. Stage-visible resolution includes PIXEL/ALL tables and honors pixel
root denial. A private native VS/PS PSO uses one augmented compiler root; the VS
must not declare typed buffers. Ordinary PS returns S_FALSE only after checking
the VS. Application render configuration is reused, not reconstructed.

Typed compute/draw recorder entry points share descriptor capture/materialization
but preserve concrete variant pointers. Per-draw replay copies recorded commands,
selects the private PSO and TLAB, and retains submission-owned resources. Static
slots are never reread at submission; unique live slots use the existing short
heap-lock resolver. Samplers and root descriptors retain existing paths.

`DXMT_TYPED_ORIGIN_DXC_DIRECTORY` opts into the bounded graphics path. Indirect,
predication, GS/tessellation/mesh, stream output and MinMax combinations remain
fail-closed. Default/deployed graphics compiler selection is not added here.

## DXBC / AIRCONV Impact

None. Legacy shaders remain on AIRCONV; no backend fallback or mixed family.

## DXIL / MSC Impact

Pixel container/conversion support and native graphics variant added. Compute
preparation still defaults to ALL/compute and uses the same origin ABI. Cache
identity already includes stage, original/transformed bytes and compiler root.

## Shared Runtime Impact

Render replay now accepts typed-origin and MinMax markers independently. Binding
objects survive GPU completion; recorded nodes and application roots are immutable.
No allocator, fence or capability semantics changed in this task.

## Tests Added

Public-API `dx12_typed_origin_pixel`: R32_UINT SRV load plus UAV load/store at
FirstElement1, two pixels, poisoned prefix/suffix guards, ALL/PIXEL visibility,
static/volatile ranges, alternating live replacement and completed-list reuse.
VS b3 root constant gates geometry; private and ordinary roots have different
constant offsets. Each of sixteen private draws is followed by an ordinary draw
on the second pixel in the same encoder, checking PSO/TLAB/root restoration.
Typed VS + ordinary PS must fail Close without submission.

Root fixture additionally covers six visibility/deny combinations, preserved
descriptor flags/offsets and transactional rejection of unsupported resolver stage.

## Tests Run

Both configurations reconfigured; focused builds and final full default builds
completed before staging. Host suites pass5/5 each. Final normal/no-private ×
SM6.0/6.6 processes pass16 private draws each: **64 private +64 ordinary draws**,
plus four expected typed-VS rejection checks.

Both configurations pass typed-origin root checks, static/live compute-array
full-buffer readback, 1024-cycle allocator/fence regression each, MinMax comparison
compute (12 groups each) and Cube-array-bias MinMax graphics regression.

## Runtime Results

Evidence: `/Users/zhangbo/.cache/dxmt-origin-pixel.cUHnT2`.
`final-exits.log`, `final-green-*`, `final-compute-*`, `final-dx12_*`,
`final-minmax-*`, `final-host-*`, final build/configuration logs and hashes.

Final identical test binaries against 49685f7 DLLs fail Close on unavailable
unaligned typed views in both configurations (`final2-red-*`, exit1); current
DLLs pass (exit0). Initial first-probe footprint error was fixed and excluded;
old probe files/logs are retained. No invalid static-descriptor mutation is used.

Actual GPU oracle: static/original RT first pixel20, UAV120/136; replacement RT40,
UAV140/156; original restored on alternating live submissions. UAV boundary guards
stay10203040/50607080 and ordinary RT second pixel77. Wrong VS root constants move
the triangle off-screen, so these same readbacks also observe VS binding.

Build/stage/overlay D3D12 SHA1 match: normal
`85306c12a80aa6a79a28782c27eeec0e13508cfa`, no-private
`1d97a5496f3f35d7595b98b6c09de8ad45488a33`. Final same-binary A/B fixture SHA1
`3842e8e47285ac0051205d8bd2a9576c11705bf4` in both configurations. Native build and
overlay hashes also match. Only task-cache overlays were updated.

## Standards

Readonly review found no hard rule violations. The initial visibility-based
compute downcast was replaced by concrete typed recorder pointers. No remaining
blocking findings. The existing MinMax-named render template is intentionally
reused; renaming it is a nonblocking maintenance suggestion.

## Spec

Readonly review found typed VS rejection was bypassed by ordinary PS selection;
ordering was fixed and a negative fixture added. VS root-layout observation and
pixel root denial were added. Follow-up review found no remaining production
blockers within this bounded slice. Reviewers did not execute GPU tests; the main
agent ran the evidence above. MSC skills guided coherent root/reflection binding.

## Known Limitations

R32_UINT bounded ordinary VS/PS only is GPU-validated here. Wider typed UAV load
formats, other graphics stages, origin arrays in pixel, OOB semantics, default
selection, MinMax combinations and indirect/predication remain incomplete.
Static recording-only observation and parallel in-flight retention were source
reviewed, not independently timing-injected. No native Windows oracle, Metal
validation/capture, forced OOM, game acceptance or performance claim.

## Capability Status

TypedUAVLoadAdditionalFormats remains FALSE. No SM/capability promotion.

## Feature Level Impact

FL11_1 unchanged; FL12_0/12_1 disabled. Full production objective remains active.

## Git Status

At record creation, task changes await local commit; final delivery verifies the
resulting commit and clean status. No unrelated user changes were present.

## Push Status

NOT PUSHED. No game/prefix deployment or process management.

## Next Recommended Task

Continue typed-origin production gaps: graphics deployed/default selection and
VS/PS shared origin records, then wider typed formats. Do not substitute a full
test matrix or feature-level number changes for those implementations.

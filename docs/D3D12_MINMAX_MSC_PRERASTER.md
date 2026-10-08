# MSC pre-raster MinMax work item

## Task Analysis

Baseline: `feat/d3d12-1`, `0932b42`; remote baseline
`origin/feat/d3d12` at `e147c710eabc0a8d1530f5d2e21d18b0eb114be4`.
The initial worktree was clean, with 118 local commits since that remote
baseline. The analysis below records the pre-change evidence; implementation
and focused acceptance results are recorded separately below.

- Hypothesis: the standard VS/PS shared private-pair layout can support MSC
  geometry and tessellation without a second descriptor materializer, provided
  pipeline conversion and replay use the emulation ABI consistently.
- Evidence at baseline: `PrepareMinMaxVariant` retains only VS/PS inputs. Original-input
  capture excludes emulation. `RecordD3D12MinMaxBinding` explicitly rejects
  geometry/tessellation and accepts only Vertex/Pixel pair stages. Ordinary
  tessellation binds TLAB to Object/Mesh at both the normal and hull/domain
  bind points. `ReplayMinMaxRender` instead hardcodes Vertex/Fragment resource
  residency and normal TLAB bindings, although its command copier already
  accepts MSC geometry and tessellation draw commands.
- Expected effect: reuse shared pair intervals and submission-owned descriptor
  snapshots for pre-raster sampling; retain the existing MSC companion pipeline
  and draw helpers rather than introducing another emulation implementation.
- Risk: removing admission guards alone would publish an invalid ABI. Converted
  VS needs emulation flags and synthesized stage-in; HS/DS/GS reflection must
  remain compatible with the selected draw configuration. Capturing only sampling
  stages would omit descriptors used by other active stages. Root visibility,
  deny flags, static descriptor semantics and closed-list immutability must not
  regress. Native mesh, stream output and root-updating emulated indirect draws
  require their own explicit qualification and must not be admitted accidentally.
- Validation: focused native preparation/root visibility checks; real geometry
  and tessellation reduction readbacks with ordinary restore, static/volatile
  descriptors and repeated submissions in normal/no-private builds; existing
  VS/PS and compute regressions. Reconfigure and finish both full builds before
  staging isolated runtime overlays. No game/prefix deployment or feature-level
  promotion follows from these focused checks.

## Implementation sequence

1. Generalize stage preparation, root visibility and global pair intervals to
   Geometry/Hull/Domain; preserve Pixel-only implicit derivative admission.
2. Reuse ordinary MSC geometry/tessellation PSO assembly, feeding converted
   private-root libraries and their reflection, with retained stage-in layout.
3. Give private graphics artifacts explicit execution-stage metadata. Capture
   all active application-stage tables, then bind submission-owned TLAB and
   residency at Object/Mesh/Fragment, including the hull/domain bind point.
4. Verify selected variant draw configuration and tessellator table lifetime;
   retain fail-closed boundaries for unsupported indirect/native mesh/SO paths.
5. Add focused GPU oracles, finish both builds, review Standards and Spec, and
   commit the completed implementation locally without pushing.

## Additional Task Analysis

### Patch-signature admission finding

- Hypothesis: HS/DS preparation fails because `Inspect` rejects `PSG1`, not
  because MSC cannot compile the lowered tessellation stages.
- Evidence: the focused ordinary-build tessellation invocation fails before
  PSO creation with `unsupported input container envelope`; HS/DS containers
  have eight parts including `PSG1` at byte offset 0x120. A single-stage HS
  preparation regression reproduces the same failure without graphics setup.
  The program-kind mapping and source/output DXC validation remain separate
  checks; accepting a part does not by itself prove its semantics survive.
- Expected effect: admit patch-constant signatures only for HS/DS, whose DXIL
  metadata is rebuilt by the existing assembler and checked by DXC validation.
- Risk: broad admission for unrelated stages could discard malformed metadata.
  The focused GPU oracle must exercise a nontrivial user patch constant, not
  just successful conversion or constant tessellation factors.
- Validation: repeat the single-stage export/validator regression and real
  HS/DS reduction readbacks; retain fail-closed admission for other stages.

## Task Result

Bounded DXIL GS/HS/DS SampleLevel/SampleGrad MinMax now reaches production
graphics selection, the MSC companion PSO factory and submission replay.

- Preparation preserves GS/HS/DS program kinds and artifact labels. Shared
  private pairs use disjoint intervals across all sampling stages. Root
  resolution honors each stage's visibility and deny flag. HS/DS `PSG1`
  signatures pass the existing assembler and input/output DXC validator.
- Private VS conversion carries emulation flags and retained input layout for
  stage-in synthesis. Both eager static-sampler and lazy dynamic variants use
  the existing companion factory. Shared reflection/configuration helpers
  serve ordinary and private pipelines; saved templates hold no borrowed library
  handles. Temporary library references live through synchronous PSO creation.
- Descriptor capture includes all active application stages, not only stages
  sampling reduction pairs. The existing static/volatile descriptor snapshot
  materializer is reused: only volatile slots are reread at submission.
- Replay retains submission-owned buffers/resources, binds Object/Mesh/Fragment
  residency and TLAB, and adds Object/Mesh hull/domain TLAB for tessellation.
  Companion draw configuration comes from the selected private PSO reflection.
  Replay patches its private command copies, not the closed command-list nodes.
  Application tessellator tables retain their existing PSO/command-list lifetime.
- DXBC remains AIRCONV; DXIL remains MSC. Native mesh, stream output and MSC
  emulated indirect draws remain fail-closed, with no shader-family fallback.

### Focused verification

Fresh isolated evidence directory:
`/Users/zhangbo/.cache/dxmt-minmax-preraster.z5kxbr`.
No game/prefix DLLs were deployed and Steam/wineserver were not restarted.

Both configurations were reconfigured; optional focused executables and then
both complete default builds finished before DLL staging. Host suites pass 5/5
per configuration. Ten SM6.0 fixture artifacts were compiled using the selected
PE DXC with the same commands registered as optional Meson targets.

Final runtime set: **32 successful processes**, excluding intermediate failed
fixtures, the red-before-fix probe and earlier diagnostic/retry passes:

- Eight new graphics processes: GS/tessellation x SampleLevel/SampleGrad x
  normal/no-private. Each checks four root configurations, direct and indexed
  draws, and two submissions per closed list: **128 draw submissions**.
- Twelve standard graphics regression processes: pixel Level/Grad, combined
  VS/PS Level/Grad, indirect constants and indirect CBV/SRV/UAV updates on both
  builds: **480 draw submissions**. Total final graphics submissions: **608**.
- Six single-stage GS/HS/DS prepare/export/validator regressions; two existing
  root-layout processes (five RS1.0/1.1/static/unbounded cases each); two GPU
  producer/count compute MinMax and two typed-origin indirect readbacks.

New-stage probes assert shared pair offsets, stage labels, visible locations,
deny/wrong-visibility rejection and unchanged output on failure. GS samples the
central 2x2 footprint; HS samples its center and DS a clamped corner. A user
patch constant contributes 4/255 to the final blue output. Static MIN/MAX blue
oracles are 184 (GS) and 104 (tessellation); ordinary linear restoration gives
128 and 76. Dynamic sampler changes give 16 -> 240 and 20 -> 132. Pre-raster
texture tables use static descriptors, but their slots are not replaced in this
fixture: these readbacks do not distinguish a snapshot from a live reread of an
unchanged slot. Pixel outputs independently distinguish static/volatile texture
replacement. Repeated lists check immutable argument templates and command links,
not byte-for-byte GS/tessellation draw configurations.

Reproduce against a staged variant (choose `runtime-no-private`/`no-private`
for the second build):

```sh
WINEDEBUG=-all MVK_CONFIG_LOG_LEVEL=0 \
DXMT_SHADER_CACHE_PATH=/Users/zhangbo/.cache/dxmt-minmax-preraster.z5kxbr/repro-cache \
WINEDLLOVERRIDES='d3d12,dxgi,winemetal=n,b' \
WINEDLLPATH=/Users/zhangbo/.cache/dxmt-minmax-preraster.z5kxbr/normal \
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
/Users/zhangbo/.cache/dxmt-minmax-preraster.z5kxbr/runtime/bin/wine \
  /Users/zhangbo/.cache/dxmt-minmax-preraster.z5kxbr/normal/dx12_minmax_fragment.exe \
  /Users/zhangbo/.cache/dxmt-minmax-preraster.z5kxbr/level.ps.cso \
  /Users/zhangbo/.cache/dxmt-minmax-preraster.z5kxbr/level.vs.cso \
  'Z:\Users\zhangbo\Documents\Vibe-Codeding\dxmt\tools\dxc\bin\x64' \
  /Users/zhangbo/.cache/dxmt-minmax-preraster.z5kxbr/level.hs.cso \
  /Users/zhangbo/.cache/dxmt-minmax-preraster.z5kxbr/level.ds.cso --tessellation
```

For GS, replace the HS/DS arguments with `level.gs.cso --geometry`; for gradients
replace every `level` fixture name with `grad`.

Verified staged/build D3D12 SHA1: normal
`03757699f957c408d886c2a8bd17dd5b4329376b`; no-private
`3c52a48f3d6825bf480046d792630d11c6d78bbb`. Native winemetal SHA1:
`9301b71976fd07afe5fa595da1d8b7a03de2942a` /
`57692b94e01c5382f85f8dbde120d895d2bfe7ae` respectively.

### Self-review and remaining scope

The main agent reviewed Standards and Spec against `0932b42` and the task
analysis/Master Prompt. Review fixed the
artifact-stage assignment that previously mapped GS/HS/DS to Compute, preserved
invalid-stage failure behavior and confirmed companion/configuration reuse.
The diagnostic loop isolated the `PSG1` rejection before fixing it; the new user
patch-constant readback verifies more than just successful assembly. No temporary
production tracing was added. `git diff --check` passes.

Two independent read-only source reviewers then reviewed the same WIP diff and
new files; they did not independently run or confirm the GPU tests.

#### Standards

No hard breach or introduced bug was established. One optional Repeated Switches
maintenance suggestion concerns the consistent stage/program-kind/MSC mappings
in typed-origin preparation and conversion. The current mappings and invalid-stage
failure checks are retained; a shared traits refactor is not required for this
bounded implementation.

#### Spec

Two partial-validation findings remain: pre-raster static/volatile texture-slot
replacement is not tested, and the immutable replay checks do not snapshot the
new draw-configuration bytes or exercise distinguishable original/private
configurations. Source review confirms the shared descriptor materializer and
private-copy mutation, but that is not a substitute for these GPU/replay probes.
The readback claims above have been narrowed accordingly. No introduced production
bug or scope creep was established. A premature local-commit statement was also
removed before committing.

Review summary: Standards has one optional maintenance finding; Spec has two
remaining validation gaps and a corrected documentation claim. These are not
full qualification passes.

This closes the bounded emulation wiring gap, not full MinMax or tessellation
qualification. Simultaneous sampled VS in an emulated pipeline, adjacency and
instanced GS, broader tessellation domains/factors, stage-only sampling with an
unsampled PS, cube/aniso/feedback, full format/view/clamp/lifetime contracts,
in-flight overlap, emulated indirect and same-address root-VA remap still need
their appropriate implementation/qualification. No fresh game or performance
acceptance is claimed. FL declarations remain unchanged and the full goal is
active. Continue production resource/filter gaps before broad matrix reruns.
Local commit is the final handoff step; no push is authorized.

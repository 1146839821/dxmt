# DXIL reduction lowering

## Task Analysis

- Hypothesis: MSC can compile ordinary DXIL sampling operations after a DXIL
  footprint expansion; AIR helper linkage is not required or allowed.
- Evidence: MSC sampler descriptors transport a handle and LOD bias only;
  existing reduction metadata is private to the AIR descriptor layout.
- Expected effect: generate actual conditional point taps and component extrema
  before MSC rather than interpreting reduction as ordinary filtering.
- Risk: provenance, descriptor state, root augmentation, container validation,
  cache identity and point-sampler retention must be integrated before admission.
- Validation: native LLVM verification and structural checks first; regenerated
  DXIL/DXC validation, MSC compilation and GPU numeric readback remain mandatory.

## Implementation boundary

`LowerReductionSampleLevel2D` expands float Texture2D SampleLevel with separately
supplied filter bits, sampler LOD limits, resource clamp and default-component
mask. It emits two conditional mip footprints with four tap sites each, applies
integer texture offsets in texel space, and excludes zero-weight upper taps by
control flow. A resource clamp strictly beyond the last view mip returns default
components without sampling. Sampler clamping precedes resource clamping.

The caller must qualify texture/handle provenance and finite coordinates, provide
an unbiased/unclamped point sampler preserving address/border modes, and validate
the regenerated full DXIL container. Feedback/status consumers are rejected.
Generated taps use a separately supplied same-view texture handle whose MSC
resource clamp is zero: point-sampler clamping alone is insufficient. Original
resource clamp is carried separately in the lowering state. Coordinates are
normalized according to supplied D3D address modes before texel-space arithmetic;
outside clamp/border distances retain sufficient margin for every legal constant
offset. State values must dominate the sample in the same function; a late state
definition is rejected before mutation.
This helper does not yet connect that caller or enable any D3D12 capability.
Dynamic sampler observations, SampleGrad/implicit operations, other shapes and
numeric edge coverage remain part of the same unfinished DXIL workstream.

Opcode/signature source: [Microsoft DirectXShaderCompiler DXIL reference](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst).
Native structural checks are not DXIL validation, MSC acceptance or GPU proof.

## Validation checkpoint

Both normal and no-private native targets build; LLVM verification checks dynamic
state CFG, eight conditional tap sites, no meaningful feedback, and late-state
rejection. Existing Meson regressions pass 4/4 per build.

A real SM6.0 Texture2D SampleLevel container is transformed, reassembled and fully
validated with the selected DXC compiler/validator, then compiled by MSC 4.0.1 for
Apple9. A reflection-driven native Metal probe uses the compiled metallib, real
point-sampler/texture descriptors, explicit indirect resource residency and a
nonzero output sentinel. Focused minimum, maximum, sampler LOD, resource clamp
0.75 and past-last empty readbacks pass (16, 240, 96, 16, 0). These prove the
bounded offline lowering path, not D3D12 submission integration or full MinMax.
Three additional probes pass: maximum finite X under clamp-to-edge (192),
MIRROR with a positive integer offset (16), and negative MIRROR_ONCE coordinates
with that offset (16). Mirror phase/sign is retained until point-tap addressing.
Latest eight validated containers, metallibs, reflection and GPU logs are in
`/Users/zhangbo/.cache/dxmt-dxil-minmax.8j9pbL`. The native probe reads red only;
full RGBA/default-format/NaN and runtime admission matrices remain unverified.

DXC initially rejected LLVM-valid aggregate insertvalue emission. The corrected
implementation uses scalar extrema and scalar PHIs only, with no synthesized
DXIL aggregate results. Independent Spec review identified resource re-clamping,
coordinate overflow and state dominance risks; the interface/lowering changes
above address them before any runtime admission. Standards review found no hard
violations; shared diagnostic container-parser extraction remains advisory.
Follow-up independent Spec review reports no actionable errors under the stated
internal contract. Both variants generate byte-identical structural and eight
container-probe IR outputs. Native readback evidence is from the normal probe;
this is not a claim of separate D3D12 variant runtime acceptance.

Next production work: qualify resource/sampler handles, augment compiler roots
with private descriptor state and point-view bindings, validate regenerated
containers, version cache identity and retain observations for submission. Then
wire remaining operation/shape paths and run their focused GPU readbacks. Neither
the AIR ABI nor the MSC descriptor ABI is changed in this checkpoint.

# Task Analysis

## Current Branch

`feat/d3d12-1`.

## Baseline

`fc53082`, native MSC padding diagnosis. Preserve dirty DirectX submodule.
User authorized a **DXIL-side lowering feasibility assessment**, not capability
promotion or replacement of the shader backend. Commit locally; do not push.

## Local Commits Since origin/feat/d3d12

Latest `fc53082`, `3e94415`, `9f84f84`: native padding diagnosis, aligned-view
repair and conservative typed-UAV policy. No history rewrite planned.

## Current State / Existing Implementation

Production rejects unaligned typed views. MSC 4.0.1 ignores companion padding
for the tested typed SRV/UAV/atomic accesses. DXIL enters MSC via
`ConvertD3D12ShaderInternal -> CompileDXIL -> DXMTMSCCompileDXIL`.
There is no existing production DXIL transformation pass.

## Relevant Files / Existing Tests

`src/d3d12/d3d12_shader_converter.cpp`: classification, cache and MSC calls.
`d3d12_root_signature.cpp`: public root signature and reflected MSC layout.
`d3d12_descriptor_heap.cpp`, `d3d12_command_list.cpp`: owned views and residency.
Native padding probe, typed UAV/SRV matrix and submission resolver tests.

## D3D12 Contract

View origin, typed format conversion, logical length, load/store masks and
atomics must survive lowering. Descriptor contents are runtime data, not
immutable PSO input. Keep alias coherence, static recording-only semantics,
volatile final submission resolution, and RS1.0 compatibility.

## DXBC / AIRCONV Impact

None. Do not introduce a DXIL frontend in AIRCONV or fallback to its executable.

## DXIL / MSC Impact

Two candidate source-semantic prototypes: R32 raw-buffer addressing, and typed
coordinates corrected by an explicit view-origin CBV before entering MSC.

## Shared Runtime Impact

Assessment only. No Unix/PE runtime ABI or production descriptor changes.

## Missing Pieces / Hypothesis

R32 raw addressing should use the desired descriptor GPU VA directly. Typed
coordinate adjustment should preserve native format conversion, provided the
shader receives the same descriptor generation's origin and logical length.
Neither proves an automatic transform of arbitrary DXIL containers.

## Expected Effect / Risks

Avoid illegal Metal offsets and shadow copies. Raw lowering can lose dynamic
format semantics; typed-origin lowering can corrupt descriptor/root mapping
or OOB behavior. LLVM/container rebuilding and cache identity are independent
risks, not solved by a correct four-thread HLSL prototype.

## Minimal Implementation Plan / Validation Plan

Keep changes in opt-in tests. Compile six HLSL prototypes to DXIL with DXC,
then compile through the native MSC API. Compare complete input/output against
the existing independent CPU oracle; retain the original red controls. Inspect
unmodified AIR. Build normal/no-private probe targets and run Meson tests.
Review Standards and Spec before local commit.

## Capability Impact

None. Additional typed UAV support and FL12 status remain unchanged.

# Task Result

## Candidate comparison

| Candidate | Native GPU evidence | General-purpose limitation |
| --- | --- | --- |
| R32 raw buffer | 30/30 R32_UINT cases match | Does not cover arbitrary bound formats, channel conversion, subword stores or full atomic matrix |
| Typed coordinate + origin CBV | 30/30 R32_UINT cases match | Needs validated DXIL rewriting and internal metadata/root-layout mapping |
| Same typed-origin binaries, R8_UINT / R16_UINT | 20/20 per format | Integer SRV/UAV only; no subword atomics or normalized/packed conversion |

Each route tests UAV load/store, SRV load and atomic add, five FirstElement
values (0/1/4/257/260), with and without BoundsCheck. The descriptor's native
view is legally aligned. Raw input uses a plain companion buffer descriptor
with desired GPU VA and byte length; typed input retains companion encoding
and adds padding from a separate CBV. Full input prefix/suffix bytes are checked.
R8 tests 0/127/128/255; R16 tests 0/255/256/65535, including unsigned extension
and values above eight bits. These formats test SRV load and UAV load/store only.

The synthetic HLSL is freshly compiled to DXIL; **no existing DXIL binary has
been automatically transformed**. Successful prototype output is deliberately
labelled `PROTOTYPE_MATCHED`, never a completed D3D12 feature claim.

Fresh AIR shows raw-buffer GPU-pointer addressing and explicit CBV-loaded
padding added to typed coordinates. MSC itself still adds zero padding; the
new coordinate term comes from the synthetic DXIL input. No AIR rewrite.

## Recommendation

Prefer **typed-origin lowering** for the eventual general solution: it retains
the native typed format conversion and native typed atomics instead of rebuilding
them in raw byte operations. R32 raw lowering is a useful bounded comparison,
not a transparent replacement for a `Buffer<uint>` that may bind another format.
This is a design inference from the prototype and source contract, not full
format/OOB/alias validation. See the independently researched
[DXIL constraints](D3D12_DXIL_TYPED_BUFFER_LOWERING_RESEARCH.md).

## Proposed module and interface (not implemented)

Place a deep DXIL preparation module at the seam before MSC conversion:

```text
PrepareDXILForMSC(original container, original root signature, policy version)
  -> validated DXIL container
  -> internal MSC root signature and public-to-internal root mapping
  -> immutable per-resource origin/length metadata requirements
  -> explicit Unsupported/Invalid failure
```

The module owns handle provenance, coordinate rewriting, container rebuilding
and validator diagnostics. Callers must not learn DXIL opcode details. The
runtime adapter only supplies origin/length records and the internal reflected
layout. The existing 24-byte MSC entry remains unchanged; a separate metadata
buffer is an intentional internal ABI change that must be versioned.

Never expose the hidden parameter in the application's root signature or charge
it against public D3D12 parameter indices. The prototype's b0/space1 is not a
reserved production register. A real module must choose a collision-free
internal binding and hash that choice and lowering policy into cache identity.

## Required correctness before production

- Trace CreateHandle/CreateHandleFromBinding/CreateHandleFromHeap, annotations,
  arrays, nonuniform indices and PHI/select handle flow to the actual heap slot.
  A typed handle is opaque: descriptor metadata cannot simply be loaded from it
  using an undocumented DXIL pointer cast.
- Map table-relative indices through register-space/range/root-table offsets.
  Bindless indices require their own complete contract. Unknown handle flow
  rejects explicitly; no partially rewritten shader enters MSC.
- Apply origin to every typed load/store/atomic coordinate, not just one opcode.
  Preserve operation masks, returned status and exact atomics.
- Check logical index/length **before** padding. Wider native views expose prefix
  elements; native texture width and GetDimensions cannot substitute for logical
  NumElements. Preserve OOB-zero loads and dropped stores/atomics as required by
  the original operation; guard arithmetic overflow and representability.
- Snapshot static origin/view/length at recording; do not introduce a static
  PendingDescriptorUse or submission reread. Resolve volatile metadata and native
  views as one matching generation under the heap lock, retain once, then fan
  out outside it. RS1.0 still converts to volatile.
- Alias overlapping typed/raw views of the same allocation directly; no shadow
  copy. Validate visibility, barriers, typed/raw/atomic alias operations and GPU
  completion lifetime. This prototype does not test simultaneous aliases.
- Regenerate and validate DXIL/resource declarations and container metadata;
  preserve embedded/global/local root-signature policy, stage signatures and
  shader feature constraints. Never splice LLVM 15 bitcode into a DXIL container
  and assume that LLVM verification equals DXIL validation.
- Include original input, internal root signature, metadata ABI and lowering
  policy version in conversion/persistence cache identity. Reject stale entries.

## Next bounded implementation milestone

First prove a DXC-validated unchanged-container round trip, then implement an
**offline**, validator-backed binary-to-binary transform for
SM6.0 compute, one statically bound R32_UINT typed buffer, plus explicit origin
CBV injection. Reject heap handles, arrays, libraries, unsupported operations
and every unrecognised handle flow. Gate success on an existing input DXIL
container transformed without source HLSL, validated by DXC, compiled by MSC,
and checked by the same GPU oracle with adversarial offsets and logical OOB.

Only then add full formats, masks, dynamic descriptor mapping, alias stress and
runtime static/volatile integration. An offline pass is not a production fix.

## Validation and scope

Both probe builds compile; native Metal API/GPU validation is enabled for GPU
checks. R32 routes each match 30 cases; typed-origin R8/R16 each match 20.
Production source, runtime deployment,
capability gates and AIRCONV are unchanged. Native no-private probe builds are
not full no-private Wine runtime acceptance. No game/performance/tessellation
acceptance is claimed.

Final rerun: normal and no-private native probes each matched all 100 prototype
cases with API/GPU validation enabled and no observed diagnostics. Original
shaders retained 18 matched controls and 12/12 padded mismatches in each build.
Passing original shaders to `--origin-cbv` also failed all 12 padded cases and
returned 1: binding an extra CBV alone does not repair original shader code.
Both builds' Meson suites passed 2/2 (gate unit suite and AIRCONV double test).

## Reproduction

Compile the six opt-in DXC targets (RAW_R32=0/1 and KIND=0/1/2), or invoke
`tools/dxc/bin/x64/dxc.exe` through the existing Wine installation with
`-E main -T cs_6_0 -D RAW_R32=N -D KIND=K` on
`tests/dx12/dxil_typed_buffer_lowering.hlsl`. Outputs used here are
`build/tests/dx12/dxil_typed_buffer_lowering_N_K.cso`.

```sh
meson compile -C build msc_typed_buffer_padding_probe
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 \
  build/tests/dx12/msc_typed_buffer_padding_probe \
  build/tests/dx12/dxil_typed_buffer_lowering_0_0.cso \
  build/tests/dx12/dxil_typed_buffer_lowering_0_1.cso \
  build/tests/dx12/dxil_typed_buffer_lowering_0_2.cso --origin-cbv
```

Repeat with `--origin-cbv-r8uint` and `--origin-cbv-r16uint`. For `--raw-r32`,
use the three `1_K.cso` shaders instead. Repeat with the executable under
`build-no-private`; shader binaries remain identical. Reconfigure each build
before compiling, following its existing configuration.

## Self-review

Standards: independent source review found a weak subword oracle and stale
research citations. Fixed boundary vectors and references; final GPU reruns
passed. No remaining documented-standard violations identified.

Spec: independent review found that the original negative assertion accepted
any padded mismatch. It now requires all queried-alignment padded cases to
mismatch, with valid aligned controls and a positive case count. Final M4
evidence is 12/12; review confirmed closure. No production-support claim,
backend fallback, runtime deployment or capability promotion was introduced.

Changed files: prototype HLSL, native probe test modes, opt-in Meson targets,
this assessment and primary-source research. Local commit is the commit carrying
these reports; NOT PUSHED. Dirty DirectX submodule remains outside the commit.

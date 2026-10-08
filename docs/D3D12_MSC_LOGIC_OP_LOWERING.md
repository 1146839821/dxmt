# MSC programmable logic-op implementation boundary

## PSO integration checkpoint — 2026-10-08

Starting HEAD b5719375. No-private ordinary MSC pixel PSOs now call the validated
preparation API for shared UINT logic ops. Component widths follow the bound
RT formats. A local capability snapshot sets only this pixel conversion's
framebuffer-fetch space; the device snapshot and ordinary vertex compilation
remain unchanged. Existing conversion-cache hashing includes transformed DXIL
bytes and the feature-space configuration; the original pipeline key already
includes blend state and target formats. No untransformed shader is accepted as
a substitute when preparation fails.

Decoded application root spaces (ranges, root constants/descriptors and static
samplers) are checked for collisions before MSC compilation. A collision rejects
instead of changing application TLAB indices. Root-layout qualification across
nonempty roots remains pending. Initial MinMax/Typed private-variant composition
is explicitly rejected in this path and remains an implementation gap, not a
claim that logic ops only require resource-free shaders.

Both full builds pass. Both host suites pass 17/17, including the gate's scoped
compiler-deployment regression. Fresh no-private single-draw AIR and MSC cases
pass all sixteen operations each; sixteen MSC overlapping double-draw cases pass
with API validation enabled. Normal fixed-function MSC single/double-draw cases
also pass sixteen each; AIR sixteen-case regression passes. Actual loaded PE,
native and required DXC/validator provenance are checked. Normal fixed-function
cases are run without an unnecessary compiler deployment: staging a validator
that this path does not load correctly remains UNVERIFIED, not relabeled PASS.
Both feature-support contracts and both table/root-UAV-indirect feedback
regressions pass, without experimental capability opt-ins.

Evidence: gpu-matrix.json (no-private 48 PASS, normal compiler-provenance
UNVERIFIED preserved), gpu-normal.json (normal 32 PASS) and gate-regressions.json
in /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq. The initial regression invocation
used a nonexistent feature-query executable and retains UNVERIFIED; the corrected
feature-final.json uses dx12_feature_support.exe and passes both configurations.
--logic-compiler-dir explicitly
deploys the DXC pair only to DXIL matrix cases. Overall raster qualification
remains PARTIAL after numeric execution passes. No-private formal logic-op
advertisement remains FALSE; MSAA, full UINT/MRT/write-mask/depth/discard/native
qualification, private-variant composition and root-space collision retry remain
open. No shader-validation, game benchmark or PE32 GPU acceptance is claimed.

MSC compilation/integration skills informed feature-space isolation and binding
review; API validation and main-agent Standards/Spec self-review were used. No
independent review is claimed. Only cache runtimes were updated, without push.

The following sections retain the earlier preparation checkpoints chronologically.

Starting HEAD: dd2db4fb. Implementation remains open; this is a source-level
preparation checkpoint, not GPU qualification or permission to accept MSC PSOs.

## Hypothesis / Evidence / Expected effect / Risk / Validation

Hypothesis: inject UINT framebuffer loads and Boolean output operations into
DXIL, then compile using MSC's public framebuffer-fetch resource-space option.
Expected effect: eliminate the remaining no-private MSC logic-op rejection
without introducing application-visible descriptor bindings.

Evidence checked on disk:

- /usr/local/include/metal_irconverter_ext/Metal_HLSL.inc declares
  Texture2D<uint4> at t0 through t7 in the selected feature space. Its
  MTL_LOAD_FRAMEBUFFER macro is Load(int3(0,0,0)); the register selects the color
  attachment. This is not an ordinary externally bound texture.
- tests/dx12/framebuffer_fetch_sm6.hlsl and dx12_msc_framebuffer_fetch.cpp already
  exercise the compiler option with space 7. They only check metallib generation,
  not attachment ordering, Boolean operations or GPU readback.
- CompileDXIL in d3d12_shader_converter.cpp forwards the compiler feature space;
  the shader cache hashes that space. Changing a global capability configuration
  is insufficient to specialize a shader by operation and attachment format.
- d3d12_typed_origin.cpp provides a reusable preparation sequence: validate input,
  extract DXIL bitcode, invoke native lowering, assemble with the selected DXC,
  validate output and inspect the resulting container. Logic-op preparation must
  preserve the application signature rather than invent a descriptor-table ABI.

Risk: collisions with application register spaces, stale cache reuse across
operations/formats, confusing signature element IDs with target indices,
nonconstant component indices, missing resources/entry-point metadata and
unordered destination reads. Old native runtimes must reject missing lowering
support; accepting a PSO while leaving its shader unmodified is incorrect.

## Required implementation sequence

1. Build a native DXIL lowering boundary for pixel storeOutput.i32 sites. Resolve
   output signature semantic indices explicitly; create typed framebuffer SRVs in
   a collision-free feature space and apply component-width masks after each of
   the sixteen Boolean operations. Preserve unrelated outputs and control flow.
2. Reuse the validated DXC preparation pipeline for the rewritten container.
   Carry selected feature space, target formats and operation as shader-variant
   state, including persistence/cache keys and both sizing/output compile passes.
   Verify that MSC skips the feature resources in the application root layout.
3. Admit MSC no-private PSOs only after this real lowering succeeds and the
   device/compiler support checks pass. Keep normal fixed-function behavior and
   experimental capability advertisements unchanged.
4. Run independent single-draw and overlapping two-draw integer readback for all
   sixteen operations, with matched PE/native provenance and API validation.
   Regress ordinary MSC graphics and root/table binding. Preserve FAIL until the
   required MSC backend actually passes; then expand MSAA, UINT formats, MRT,
   write masks, discard/depth and full raster qualification.

## Output primitive checkpoint

LowerIntegerLogicOutput now rewrites a qualified scalar storeOutput.i32 using
the caller-supplied destination component. It implements all sixteen operations
and masks narrow component widths. Constant output IDs, row zero and valid
component indices are required; invalid inputs reject without modifying the
store. Signature mapping, framebuffer resource insertion and dominance proof
remain the module-level caller's responsibility.

Both build configurations pass 5,120 independent per-bit truth-table CPU checks
across 2/8/10/16/32-bit widths, negative input checks and nonconstant SSA module
verification. This primitive is compiled into the native preparation library,
but is not yet called by production shader preparation. No PSO or capability
admission changed. Container/DXC validation, MSC compilation and GPU ordering
are still unverified for this new primitive; no GPU, WOW64 or performance claim.

## Module injection checkpoint

LowerIntegerLogicOutputs now resolves signature IDs through UINT SV_Target
semantic indices, retains non-color outputs, appends framebuffer Texture2D SRV
metadata and inserts loads immediately before each selected store. It supports
legacy createHandle and SM6.6 createHandleFromBinding/annotateHandle. The chosen
feature space avoids all shader resource spaces; the eventual preparation caller
must also check application root-signature spaces before publishing a variant.
Resource roots and entry-point records are replaced, not mutated in place, to
preserve metadata shared with a clone. Failure requires discarding the private
module, and returns no selected compiler feature space.

Fresh DXC ps_6_0 and ps_6_6 shader inputs from graphics_logic_op_sm6.hlsl were
rewritten for XOR with RGBA8 widths. Both assembled containers passed full DXC
validation. MSC 4.0.1 compiled both results using the selected public feature
space; evidence is retained in /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq.
The host fixture additionally checks nonidentity signature-ID/attachment mapping
and both handle models. This is compiler evidence, not attachment-ordering GPU
acceptance. Production DXC preparation, root-layout checks, cache keys and PSO
integration are still required; no admission or capability declaration changed.
Both targeted builds and both host suites (17/17) pass. Main-agent Standards/Spec
self-review and diff whitespace checks pass; no independent review is claimed.

## Production preparation boundary checkpoint

PrepareD3D12LogicOpShader now shares the existing validated DXC preparation
sequence with Typed/MinMax. It dynamically resolves DXMTMSCLowerLogicOutputs,
rejects missing exports, validates and inspects the input container, transports
bitcode through the native boundary, assembles/validates the output and publishes
only after both calls return identical IR sizes and feature spaces. Embedded
application root bytes remain separate from the transformed shader.

The new transport is 184 bytes with fixed-width addresses and widths. Native
preparation checks ranges, parameter/input/output aliases, reserved fields and
widths, contains allocation exceptions and normalizes LLVM text to the selected
DXC dialect. Unixcall 204 is appended to both tables (the existing NULL slot
counts toward the index); no existing call index changes. WOW64 shares this
fixed-width transport, but no actual PE32 runtime acceptance is claimed.

Fresh isolated PE-to-Unix preparation passes on both configurations for ps_6_0
and ps_6_6, including malformed-input/invalid-operation artifact preservation.
All four fixture PE/native provenance checks pass. The final evidence is
prepare-qualified.json in /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq; earlier
prepare files retain failed packaging/index experiments. Required Wine builtin
postprocessing and matched cache PE/native deployment resolved the load/export
failures; the PE integration fixture caught an intermediate index mistake that
host-only tests could not catch. Only cache runtimes were updated.

This makes the preparation API available, not the production PSO call site:
root-space collision/layout checks, compiler/cache specialization, pipeline
admission and actual overlapping GPU draws are still pending. No capability
promotion, game deployment/restart or push occurred.
Both final complete builds pass (122 incremental targets each); both host suites
remain 17/17. Main-agent self-review retained the PE preparation regression
because a successful host-only lowering does not prove correct unixcall dispatch.

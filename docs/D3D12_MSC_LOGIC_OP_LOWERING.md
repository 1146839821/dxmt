# MSC programmable logic-op implementation boundary

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

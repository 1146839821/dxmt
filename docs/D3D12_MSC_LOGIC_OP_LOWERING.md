# MSC programmable logic-op implementation boundary

## MinMax composition qualification checkpoint — 2026-10-08

Starting HEAD fa9dab0e. Hypothesis: static MinMax-before-LogicOp and dynamic
LogicOp-before-MinMax transformations preserve both reduction behavior and ordered
attachment reads. Previous evidence covered only Typed+XOR. Expected effect:
replace the blanket missing combined-MinMax GPU evidence with a bounded pixel
qualification. Risk: constant inputs hide ordinary sampling, a frozen reduction
mode hides volatile descriptor rereads, or invalid list replay contaminates results.

The shared graphics fixture now samples a 2x2 UNORM texture whose red taps are
16/64/192/240, then emits UINT output into RGBA8_UINT. Independent CPU expectations
distinguish MIN=16, MAX=240 and center-linear=128 before applying each Boolean
operation to the cleared destination. All sixteen operations pass static MIN/MAX,
dynamic MIN/MAX and same-PSO MIN/MAX/LINEAR/MIN replay on both builds under Metal
API validation: 160 cases, 256 submissions. Each replay waits for its prior fence,
changes only a volatile sampler between executions, uses an increasing fence
value, and restores texture/attachment initial states in the immutable list.

Both full builds and host suites pass (17/17 each; 90 gate unit tests). The gate
adds eighty mandatory combined-MinMax cases and a fail-closed switch-failure unit
test. Source changes in this checkpoint are fixtures/gate only; no further
production repair was needed for the tested composition. Evidence:
minmax-logic-first.json, minmax-logic-switch.json and minmax-logic-api.json under
/Users/zhangbo/.cache/dxmt-msc-logic.6jblCq. Compiler/integration skills informed
feature-space and static-sampler checks; validation was enabled before device
creation. Main-agent Standards/Spec review checked descriptor mutation and replay
state contracts; no independent review or shader validation is claimed.
The final no-private gate passes all 162 executions and remains PARTIAL
(minmax-logic-gate.json), including the prior AIR/MSC, root-CBV and Typed cases.

This supersedes the missing combined-MinMax pixel evidence below, not complete
stage/shape/filter/root-layout coverage, simultaneous Typed+MinMax, Typed's other
operations, MSAA/full-format/MRT or native qualification. Formal capabilities,
experimental opt-ins and game deployments remain unchanged.

## Private-variant composition checkpoint — 2026-10-08

Starting HEAD d8533c26. Hypothesis: rebuilding a private root from the saved
application PS and device-default capabilities drops programmable LogicOp.
Evidence: GetTypedOriginVariant/GetMinMaxVariant previously used that pair;
the automatic Typed path was explicitly rejected during initial LogicOp PSO
creation. Expected effect: retain LogicOp across private-root recompilation.
Risk: feature-space collisions, accidentally applying pixel options to other
stages, or losing Typed/MinMax binding records when composing bytecode.

The PSO now retains logic-lowered PS bytes for draw-selected variants and supplies
its local framebuffer feature space to every pixel variant conversion. Static
MinMax composes after its prepared pixel shader and preserves its binding records;
future private spaces 1/2 are excluded from framebuffer-space selection. Other
stage/device capability snapshots and application root indices remain unchanged.
Existing conversion hashes include both resulting bytecode and feature-space
configuration. Simultaneous Typed+MinMax remains unsupported.

Both full builds and host suites pass (17/17 each; 89 gate unit tests). R32_UINT
Typed+XOR passes automatic and explicit compiler selection on both builds under
Metal API validation: 64 submissions, checking UAV side effects/guards, live
descriptor replacement, and ordinary-PSO restoration in the same encoder.
Evidence: composition-api.json under /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq.
The final no-private gate executes 82/82 successfully and remains PARTIAL
(composition-gate.json). The optional legacy negative control returns E_NOTIMPL
but lacks required validator-load provenance after early rejection; its
composition-legacy.json is UNVERIFIED, not an accepted compatibility oracle.
The earlier composition-final.json/composition-qualified.json are failed harness
runs, not qualification: the optional fixture needed explicit rebuilding, and
the explicit relative DXC path needed GetFullPathNameW normalization. Meson now
provides four reproducible shader targets for the mandatory composition cases.

Compiler and integration skills informed per-stage feature-space propagation and
private-root composition. Main-agent Standards/Spec self-review checked those
invariants; no independent reviewer or shader validation is claimed. MinMax
composition is implemented but lacks combined GPU acceptance evidence. Remaining
operations/private layouts, MSAA, full format/MRT and native qualification stay
open; no capability promotion or game deployment.

## Root-space reselection checkpoint — 2026-10-08

Starting HEAD b834ca55. Hypothesis: application roots can occupy a shader-free
framebuffer feature space, so rejecting the first selection unnecessarily blocks
valid PSOs. The PSO now collects descriptor-range, root-parameter and static-sampler
spaces, then retries lowering from the original shader with a strictly decreasing
ceiling. Application root indices and device-wide compiler state remain unchanged;
transformed bytecode and the selected local feature space retain cache isolation.
Risk: stale native code could ignore the hint or repeated lowering could compound
the transformation. The existing 184-byte transport encodes default as zero and
an explicit ceiling as ceiling+1; old native rejects nonzero instead of ignoring it.

Both full builds and host suites pass. All sixteen operations with two adjacent
reserved root spaces, a consumed CBV and CBV rebinding pass on both builds under
Metal API validation (32 executions). The expanded no-private gate executes
80/80 cases successfully but correctly remains PARTIAL for complete raster
qualification. Native ceiling 7, ceiling 0 and invalid-hint checks pass; a new PE
with the previous native binary rejects the collision PSO as expected.
Evidence: collision-first.json, collision-legacy.json and collision-final.json in
/Users/zhangbo/.cache/dxmt-msc-logic.6jblCq. Compiler/binding skills informed the
ABI and local feature-space handling; main-agent code-review checked bounded
retry and cache isolation. No independent review or shader validation is claimed.
This supersedes the collision-retry gap in older checkpoints below, not full root
layout, Typed/MinMax composition, MSAA, format/MRT or native qualification.
Formal capabilities and game deployment are unchanged.

## Nonempty root-CBV checkpoint — 2026-10-08

Starting HEAD 589bf096. The pixel shader now has a qualification entry that
actually reads UINT source values from b0. Its application signature has a
pixel-visible root CBV, not an unused placeholder parameter. Two-draw cases
rebind to a different 256-byte-aligned address containing a different source;
the CPU Boolean oracle uses that second value independently. This checks the
combined application TLAB binding and ordered framebuffer path rather than
merely accepting a nonempty root at PSO creation.

All sixteen operations pass both direct and rebinding cases on normal and
no-private builds (64 executions), with API validation enabled and matched PE,
native and required compiler/validator provenance. No production repair was
needed for this particular root layout. The gate now includes these thirty-two
MSC root cases, totaling sixty-four cases, and still cannot report complete
raster PASS. Both full builds and both host suites (17/17, 87 gate tests) pass.

Evidence: root-cbv-first.json, root-cbv-matrix.json and root-cbv-gate.json under
/Users/zhangbo/.cache/dxmt-msc-logic.6jblCq. Binding-model and compiler skills
informed the shader/root pairing; API validation and main-agent Standards/Spec
self-review were used. No independent review or shader validation is claimed.
This closes the tested root-CBV layout only: tables, static samplers, root
constants, collision retry, Typed/MinMax composition, MSAA/full-format/MRT and
native comparison remain open. Formal capabilities and game deployments remain
unchanged. The previous blanket nonempty-root gap below is now qualified by
this evidence, not removed for all root layouts.

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

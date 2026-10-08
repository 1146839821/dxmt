# AIR programmable logic-op implementation (2026-10-08)

Starting HEAD: dcbf516b. This is production AIRCONV lowering and PSO integration,
not merely a relaxation of the no-private rejection policy. Full FL12_0 remains
open, especially MSC logic-op lowering, multisampling and complete qualification.

## Hypothesis / Evidence / Expected effect / Risk / Validation

Hypothesis: UINT attachment reads plus ordered fragment Boolean operations can
implement the missing no-private AIR path without private Metal selectors.
Evidence: the previous matrix rejects all no-private PSOs; native Metal compiling
a uint4 color input produces air.render_target, air.raster_order_group and
air.compile.framebuffer_fetch_enable metadata. The existing AIR frontend had no
corresponding fragment-input variant or logic-op epilogue.

Expected effect: execute the real logic op in the fragment shader, preserving
destination ordering and component width. Risk: old runtime silently ignoring a
new chained argument, missing framebuffer-fetch metadata, incorrect high bits on
narrow integer formats, WOW64 parameter loss and accidental capability promotion.
Validation: sixteen independent Boolean GPU expectations, sequential overlapping
draws, normal regression, old-native rejection, API validation, actual loaded
module provenance and both host suites.

## Normative and compiler seams

- [D3D shared functional spec, 17.7](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm): renderable UINT targets, shared operation with independent blending disabled, component-width LSB behavior; upper-bit clamping was historically also permitted. This implementation uses LSB truncation.
- [Apple programmable attachment input sample](https://developer.apple.com/documentation/metal/implementing-order-independent-transparency-with-image-blocks): color attachment inputs with a raster-order group.
- The local native compiler reference is retained at
  /Users/zhangbo/.cache/dxmt-logic-op.ENEpxk/fetch.metal and fetch.ll. It is ABI
  evidence, not a DXMT GPU qualification test.

LLVM and shader-converter/integration skills informed the signature, Boolean
epilogue and render-pass contract review. No MSC compiler configuration is changed.

## Implementation

InputRenderTarget adds the typed color attachment input, deduplicated by target
index, with raster-order group 0. The pixel conversion enables framebuffer fetch
and appends the selected Boolean operation after ordinary output-register
conversion. Results are masked to 8, 16, 32 or packed 10/10/10/2 component widths;
NOT/NAND/NOR/EQUIV cannot leak high bits into Metal's narrow integer conversion.

SM50_SHADER_PIXEL_LOGIC_OP is a separate chained argument; the existing pixel-PSO
argument layout is unchanged. Pixel compiler capability is carried in the spare
third uint32 word of the existing stage union; the reflection ABI
does not grow. AIRCONV_VERSION is 32 to invalidate compiler caches. D3D12 checks
that capability after initialization before asking for lowering, so an older
native runtime rejects instead of silently compiling the ordinary shader.

The no-private graphics descriptor and stream validation admit only AIR pixel
shaders, UINT attachments, shared logic operation and one sample. Apple4-family
support is required at device validation/pipeline initialization. The native
fixed-function logic-op flag remains disabled. Other backends, sample modes or
formats remain fail-closed. Normal builds retain the existing fixed-function path.

WOW64 conversion now handles the new chained argument. Self-review also found
and repaired its pre-existing omission of pixel_formats in the pixel-PSO
argument. Explicit 52-byte/16-byte 32-bit ABI assertions compile; no fresh 32-bit
GPU acceptance is claimed.

## Current-source verification

- Both complete builds pass (initial 185 targets and final incremental builds),
  then both host suites pass 16/16 including 85 gate tests. Existing deprecation,
  return-path and libunwind warnings remain; a new unused-variable warning was
  removed before final builds.
- Final normal matrix: DXBC 16/16 and DXIL 16/16 numeric PASS; AIR two-draw chains
  16/16 PASS.
- Final no-private matrix: AIR 16/16 numeric PASS, compared with previous 16/16
  rejection. AIR two-draw chains 16/16 PASS with Metal API Validation Enabled
  observed in every process. MSC still rejects all 16 PSOs with E_NOTIMPL: the
  overall category remains FAIL rather than hiding that required backend gap.
- Packed-SM5 ordinary, feature-query contract, table buffer feedback and indirect
  root-UAV buffer feedback pass on both builds. All final target PE/Unix provenance
  checks pass; experimental capability and AIR MinMax opt-ins stay off.
- Current PE with archived pre-lowering native runtime rejects the AIR logic PSO
  with E_NOTIMPL and matched PE/Unix provenance. Cache native is restored afterward.
- Main-agent code-review Standards/Spec self-review and git diff --check pass.
  No independent review, shader validation or game benchmark is claimed.

Evidence directory: /Users/zhangbo/.cache/dxmt-logic-op.ENEpxk.
final-evidence.json is the final matrix/API-validation/regression evidence;
compatibility-final.json retains the final old-runtime rejection checkpoint. Earlier first,
matrix, api-validation and ordinary-regressions files are preliminary evidence.
Only cache runtimes were deployed; no prefix/game deployment, restart or push.

## Not closed

MSC output lowering, MSAA/sample-frequency semantics, all UINT formats/MRT/write
masks/depth/discard/geometry/tessellation interactions, full raster matrix,
native Windows comparison and WOW64 GPU runs remain outstanding. The no-private
OutputMergerLogicOp advertisement is still FALSE; no Tier, SM or FL promotion.
The next implementation target is MSC logic-op lowering, not relabeling its
rejections or using the AIR subset to claim both backends.

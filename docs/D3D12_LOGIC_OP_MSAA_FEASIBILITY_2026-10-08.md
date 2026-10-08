# Public MSC LogicOp MSAA mechanism evidence

Starting HEAD 439168f5. This is a backend-mechanism investigation, not D3D12
qualification or capability promotion.

Hypothesis: the public framebuffer-fetch input can access individual multisample
attachment values without changing the application's pixel shader to explicitly
consume SV_SampleIndex. Evidence before the probe: the public SDK macro declares
Texture2D inputs; MSC replaces them with AIR render-target inputs. The existing
CanLowerD3D12IntegerLogicOp rejects every sample count other than one.
Expected effect: establish a concrete integration route rather than remove the
guard without per-sample evidence. Risk: equal destination values conceal sample
broadcast; resolved readback conceals individual samples; correct colors conceal
changed shader invocation frequency or side effects.

## Current evidence

MSC 4.0.1 converts both ps_6_0 probe entries to AIR and metallib with framebuffer
space 7 and minimum GPU family Apple9. LLVM 15 disassembly shows an AIR
render-target input in both. The ordinary entry has no sample-ID input; the
SV_SampleIndex entry has sample-ID metadata and a live sample-dependent XOR.
No claim is made about invocation frequency from metadata alone.

On Apple M4/macOS 27.0.1, the native Metal probe seeds four distinct R32_UINT
samples: 0x12340000, 0x12340101, 0x12340202, 0x12340303. A second render encoder
loads that attachment and executes the MSC fragment. A compute encoder reads
each raw sample through texture2d_ms<uint>; no resolve is used. Ordinary fetch
returns seed[sample] XOR 240; explicit sample-ID fetch returns seed[sample] XOR
sample. All eight independent comparisons pass, including Metal API validation
enabled before MTLDevice creation. The input is not a constant multisample color.

This demonstrates the tested public MSC per-sample color-fetch mechanism. It
does not prove unchanged interpolation, derivatives, UAV side effects or shader
invocation counts; it does not exercise DXMT's D3D12 conversion/root/residency
path, AIRCONV, sample masks/coverage, all logic operations, other formats or
sample counts, or a native Windows oracle. Production admission and the 162-case
raster gate remain unchanged and full qualification remains PARTIAL.

## Reproduction

The optional Meson targets are logic_msaa_pixel_frequency.ps.cso,
logic_msaa_sample_frequency.ps.cso and msc_logic_msaa_probe under tests/dx12.
Reconfigure first and build the shader targets with -j1 when DXC runs via Wine.
For each input, run:

```sh
metal-shaderconverter input.ps.cso --framebuffer-fetch-register-space 7 \
  --minimum-gpu-family Apple9 -c -o output.air
llvm-dis output.air -o output.ll
metal-shaderconverter input.ps.cso --framebuffer-fetch-register-space 7 \
  --minimum-gpu-family Apple9 -o output.metallib
```

Pass the pixel-frequency and sample-frequency metallibs, in that order, to the
native probe. MTL_DEBUG_LAYER=1 enables API validation. Artifacts and observed
values are retained in /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq as msaa-pixel.*,
msaa-sample.* and msaa-native-api*.log. These artifacts are not committed.
Both optional Meson native binaries pass the same eight comparisons with API
validation; msaa-native-proof.json records their actual exit codes, output and
binary/metallib hashes. Both full builds and host suites pass (17/17 each,
90 gate unit tests). No Wine/game DLL deployment or process restart was performed.

MSC compiler/integration and LLVM IR skills informed the matched feature space,
render load action, IR inspection and raw-sample oracle. Main-agent code-review
checks the resource/ARC lifetime and evidence scope; no independent review or
shader validation is claimed.

## Next implementation requirements

Add a D3D12 per-sample readback path, preserve different initial sample values,
and exercise the normal/native LogicOp reference alongside no-private lowering.
Measure pixel-frequency shader side effects and interpolation/derivatives before
choosing any sample-frequency rewrite. Then qualify masks, partial coverage,
write masks, depth/stencil, multiple draws and private variants. Retain fail-closed
admission until the applicable implementation has corresponding GPU evidence.

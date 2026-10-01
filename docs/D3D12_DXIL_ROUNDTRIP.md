# Task Analysis

Baseline: `e063683`, branch `feat/d3d12-1`. Preserve the dirty DirectX submodule;
commit locally after review, never push. This is the next offline validation
step, not production typed-buffer lowering.

Hypothesis: DXC's own disassembler and assembler can preserve SM6.0 compute
IR/resource metadata without passing through LLVM 15 serialization, and the
result can pass full-container DXIL validation and independent MSC execution.

Evidence: bundled `dxcapi.h` exposes IDxcCompiler3, IDxcAssembler and
IDxcValidator; x64 dxcompiler.dll and dxil.dll are present. Prior source-semantic
GPU prototypes passed but did not transform an existing container.

Expected effect: establish the DXIL serialization gate required before a
bounded coordinate rewrite. No performance or capability effect.

Risk: regenerated containers may omit reflection/hash/debug/root parts;
textual identity is not arbitrary semantic equivalence. Restrict the experiment
to cs_6_0 and a whitelist of ordinary non-debug container parts, report part
sizes, and reject roots, libraries and unknown parts. Never overwrite an output.

Validation: full-container validator flags=0 on input and rebuilt output;
check call HRESULT and operation status; compare non-comment IR/metadata text;
execute rebuilt fixtures through the existing native MSC GPU oracle; exercise
invalid input, unsupported profiles/parts, and output-exists failure.

Implementation: opt-in Windows test tool, dynamic loading from an explicit
absolute DXC directory, no production links/caches/runtime deployment.

# Task Result

The bounded unchanged-IR serialization gate passed. No shader coordinates are
rewritten; there is no binary-transform pass or production integration.

## Versions and execution path

Bundled x64 DXC reports `1.9.2602.17 (21d28f727)`; headers/release notes identify
the 1.9.2602 package. DLL SHA256:

- dxcompiler.dll: `b86a738ece4c05dbe2d9bbb29668a2ccb28a0740773c9b027cdc61e8d07b4d75`
- dxil.dll: `058f2f52a680c38b223a5615b7df969def21f77e577562e5099d971dede992de`

Tool runs through the existing Wine installation. It loads the explicitly
selected dxcompiler.dll and dxil.dll, disassembles original DXIL with
IDxcCompiler3, sends the untouched text to IDxcAssembler, and validates both
containers with the dxil.dll IDxcValidator and flags=0. HRESULT and operation
status both gate success. Rebuilt containers then enter native MSC 4.0.1 on M4;
no AIR or LLVM 15 serialization is involved.

## Positive evidence

Final builds: normal and no-private opt-in tools compile without compiler
diagnostics. Nine existing cs_6_0 containers pass full input/output validation
and identical non-comment IR/metadata text: six source-semantic prototype
fixtures and three original companion-padding negative-control fixtures.
The final corpus uses the normal executable for typed fixtures and original
controls, and the no-private executable for raw fixtures.

| Prototype input | Input bytes | Rebuilt bytes |
| --- | ---: | ---: |
| typed UAV / SRV / atomic | 3432 / 3524 / 3372 | 3152 / 3244 / 3092 |
| raw UAV / SRV / atomic | 3096 / 3080 / 3032 | 2964 / 2952 / 2900 |

Part-size output shows regenerated STAT and DXIL payload lengths. Success
therefore means bounded IR/metadata textual identity plus validated execution,
**not byte-identical container preservation** or complete reflection/debug/root
round-trip support. Root/debug/unknown parts explicitly reject.

Rebuilt prototype containers match all 100 native GPU cases per build:
raw R32_UINT 30, typed-origin R32_UINT 30, typed-origin R8_UINT 20 and R16_UINT
20. Both normal and no-private probes enabled Metal API/GPU validation, with
no observed validation diagnostics. Full input sentinels and output checks
match; integer boundary vectors are unchanged from the preceding milestone.

Rebuilt original shaders retain 18 matched aligned controls and 12/12 padded
mismatches in each native build. Serialization does not repair MSC's original
padding behavior. Both Meson suites pass 2/2 (gate unit suite plus AIRCONV double
test). This is not full no-private Wine runtime or game acceptance.

## Negative checks

Malformed non-container HLSL, SM6.0 pixel shader, SM6.2 native16 compute, a shader
library, embedded root signature RTS0 and embedded debug ILDB all return 1
without creating output. The library rejects at the first unknown VERS part;
this is not evidence of RDAT rewriting. A missing input also returns 1.
Existing output and input-as-output both return 1; SHA256 remains unchanged.
Failure injected inside valid IR/PSV and validator-version mismatch are not
tested here. A malformed-input rejection before validation does not establish
validator diagnostics for every malformed module.

## Reproduction

Reconfigure before compiling each existing build configuration:

```sh
meson setup --reconfigure build
meson compile -C build dxil_roundtrip
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
WINESERVER=/Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wineserver \
  /Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wine \
  build/tests/dx12/dxil_roundtrip.exe \
  Z:/Users/zhangbo/Documents/Vibe-Codeding/dxmt/build/tests/dx12/dxil_typed_buffer_lowering_0_0.cso \
  Z:/Users/zhangbo/Documents/Vibe-Codeding/dxmt/build/tests/dx12/new_roundtrip_0_0.cso \
  Z:/Users/zhangbo/Documents/Vibe-Codeding/dxmt/tools/dxc/bin/x64
```

Output must not already exist. Repeat for the other five fixtures, then pass
the rebuilt paths to the native probe modes documented in
[the preceding assessment](D3D12_DXIL_TYPED_BUFFER_LOWERING.md#reproduction).
Repeat native execution with `build-no-private`'s probe. Ignored build artifacts
used for final checks reside under `build/dxil-roundtrip.U59Lxp/`.

## Self-review and next gate

Standards: independent source review found no actionable issues. COM out-pointer
ownership covers failure paths; module owners outlive interfaces; CREATE_NEW
prevents overwrites. Spec: independent review found no actionable deviations
from the bounded serialization gate. Runtime evidence above was added afterward.

LLVM/DXC guidance influenced the choice to use DXC's dialect-aware assembler,
not a generic LLVM 15 writer. The next task is the actual restricted SM6.0
binary-to-binary typed-coordinate transform: introduce collision-free private
CBV metadata, reject unknown handle flow, validate regenerated resources/PSV,
then test logical OOB and GPU execution. This remains unimplemented.

Production source, runtime ABI, capability reporting, shader caches, AIRCONV
and installed Wine runtime DLLs remain unchanged. Dirty DirectX submodule is
excluded. Local commit carrying this report is NOT PUSHED.

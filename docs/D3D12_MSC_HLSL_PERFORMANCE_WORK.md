# MSC Metal HLSL and performance work

## Baseline audit (2026-11-02)

Baseline: `1bd03dd5`, branch `feat/d3d12`; existing `.gitignore` change is
outside this work. The SDK headers are 4.0.3; the actual loaded dylib must be
identified by path and content hash, not by that header version. The Apple
4.0 beta 2 PDF is an architectural reference; the installed 4.0.3 SDK is the
implementation reference.

| Area | Current source observation | Evidence and action |
| --- | --- | --- |
| Compatibility flags | Zero is replaced with texture MinLOD compatibility; caller cannot explicitly request zero. Ordinary path passes the nonzero default. | `src/winemetal/unix/metalirconverter.c:252`; preserve normal default and design a private explicit-zero test path. |
| Targets | Detected GPU family and OS target are carried into conversion and cache key. | `src/d3d12/d3d12_msc_capabilities.cpp:60`, `src/d3d12/d3d12_shader_converter.cpp:269`; device compatibility and A/B performance remain to be measured. |
| Cache | In-process conversion cache and single-flight exist; disk cache is disabled because the loaded dylib lacks a trusted identity. | `src/d3d12/d3d12_shader_converter.cpp:201`; do not enable persistent cache without identity and integrity checks. |
| HLSL extensions | `Metal_HLSL.inc` is vendored. Special resource spaces are disabled in ordinary conversion; reflection and specialized function bridge APIs exist. | `src/d3d12/d3d12_shader_converter.cpp:1402`, `src/winemetal/unix/metalirconverter.c:279`; existing tests exercise reflection/conversion, not pixel GPU output. |
| Graphics/compute PSO | Existing ordinary pipelines load converted metallibs and create Metal functions and pipeline states. | `src/d3d12/d3d12_pipeline_graphics.cpp`, `src/d3d12/d3d12_pipeline_compute.cpp`; no independent specialization cache proof yet. |
| Raytracing | Ordinary compiler path configures full intrinsic masks, 16-byte max attributes and visible functions; specialized RT paths take the supplied attribute size but still use full masks. | `src/winemetal/unix/metalirconverter.c:942`, `:1461`, `:1588`. Intrinsic collection availability alone is not sufficient to narrow a pipeline-wide mask. |
| Performance | No current evidence establishes a safe optimization for these options. | Establish isolated cold/warm and cache/PSO baselines first. GPU counters, occupancy, overlap, bandwidth and unsupported instrumentation must be reported as NOT_RUN, never inferred. |

## Execution plan

1. Add a separate opt-in benchmark with reproducible identity, cold/warm
   samples, median/p95 and honest scope. Keep production hot paths unchanged.
2. Exercise SDK macros with the real compiler and existing WineMetal bridge;
   add GPU correctness probes for two specialized constants and framebuffer
   fetch with explicit resource spaces and negative controls.
3. Measure and compare only semantically equivalent workloads; record unavailable
   counters and runtime identity. Investigate zero-vs-default compatibility
   flags without changing the default behavior.
4. Assess root layout, target, cache, GPU overlap and RT optimizations against
   observed evidence. Leave unsupported or unmeasured changes disabled.

## First design card: isolated PSO baseline

**Goal:** measure cold and repeated compute-PSO creation through D3D12 and the
existing MSC path without adding production instrumentation.

**Scope:** one deterministic DXIL fixture, an empty root signature, per-process
cold creation followed by repeated in-process creation. Measure the complete
`CreateComputePipelineState` call, not its internal sub-phases. Use a separate
runner, isolated Wine prefix/cache and paired PE/Unix builds; record input and
runtime hashes, raw timings, median and p95. Verify that each PSO creation
succeeds. Distinguish a process-cold first call from later calls, without
claiming a verified cache hit solely from timing. Single-flight timing,
classification, conversion, reflection, Metal function, GPU and memory
sub-metrics require separate instrumentation and remain NOT_RUN.

**Risks:** OS/GPU/compiler caches and scheduling persist across processes;
results are an initial measurement, not an optimization claim. Do not compare
unverified runtime identities or treat an empty shader as a representative game.
**Rollback:** remove the isolated test and target; production path is unchanged.

## Implemented controlled paths and results

The HLSL fixtures include the existing, unmodified Apple SDK header. Their
special register spaces are test-only. `dx12_metal_hlsl_gpu` compiles DXIL via
WineMetal/MSC, checks function-constant reflection, specializes two Metal
functions using `UInt4`, creates Metal render PSOs and checks GPU readback.
The hybrid test uses a native MSL vertex/base fragment and an MSC fragment;
RGBA8Unorm, attachment 0, blending disabled, one sample, clear/store actions,
and two draws in one render encoder are the tested configuration. A deliberately
wrong pixel oracle returns failure. This proves the controlled WineMetal Metal
path on the tested host; it does **not** expose these extensions to ordinary
D3D12 PSOs or prove D3D12 specialization-cache isolation, descriptor binding,
all formats, cross-GPU support, or fallback behavior.

A private compile flag explicitly distinguishes zero compatibility flags from
legacy unspecified zero. Unspecified zero retains MinLOD; a missing MSC setter
causes the explicit request to fail rather than silently misconfigure it. The
existing cache key hashes both compile flags and compatibility flags. No D3D12
caller enables the new flag. Two test shaders without texture LOD pass for both
settings; this does not establish LOD equivalence or justify disabling MinLOD.
The new TG memory coherence flag remains disabled without a relevant concurrent
threadgroup-memory correctness test and a controlled performance comparison.

Environment: Apple M1 Max (Apple7), macOS 27.0.1, Wine 11.0, SDK API headers
4.0.3, `/usr/local/lib/libmetalirconverter.dylib` SHA-256
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.
This identifies the loaded file and does not establish its product version.
API and GPU validation were enabled in the pixel tests. Full evidence and file
hashes are in the isolated results directories; preserve these as raw data.

The compute PSO baseline was five separate process-first calls and 95 pooled
later calls. Median/p95/min/max: 11.0482/11.2038/10.9010/11.2038 ms first;
0.3606/0.5047/0.3357/0.6956 ms later. Serialized empty-root creation: median
1.5084 ms, p95 1.6526 ms. The empty compute shader is not representative of a
game; process-first is not guaranteed disk/driver cold. Cache hits, GPU output,
internal conversion phases and memory were not measured in that test.

For controlled fragment conversion, three new processes per setting were
alternated. The first Function Constant sizing calls had median/p95
10.7687/11.1087 ms with default flags and 10.6092/10.8424 ms with explicit zero;
second/output calls had 1.3420/1.6007 and 1.0596/1.2702 ms respectively. Fetch
sizing calls had 1.1485/1.2843 and 1.3145/1.3531 ms; output calls had
0.8400/0.8605 and 0.9557/0.9718 ms. Each sizing and output call performs a
conversion; these are not disjoint internal phases. Validation was enabled,
sample count is too small to infer a performance gain, and GPU timing, LOD
correctness and memory were not measured. Full raw samples and ranges are in
`result-metal-hlsl-final/summary.json` and `summary.md` outside the repository.

Reproduce with the build's own Wine/DXC prefix, then run with a separate,
compatible staged Wine/prefix (see `docs/DEVELOPMENT.md`):

```sh
meson compile -C build dx12_metal_hlsl_constants_fixture dx12_metal_hlsl_framebuffer_fixture dx12_metal_hlsl_gpu dx12_msc_pso_benchmark
python3 tests/dx12/run_metal_hlsl_gpu.py --stage STAGE --prefix PREFIX --build build --converter /usr/local/lib/libmetalirconverter.dylib --output NEW_OUTPUT --repeats 3
python3 tests/dx12/run_msc_pso_benchmark.py --stage STAGE --prefix PREFIX --build build --converter /usr/local/lib/libmetalirconverter.dylib --output NEW_OUTPUT --processes 5 --samples 20
```

Each runner requires an already prepared isolated stage, does not install into
user Wine, and verifies the current PE/Unix bridge against the build, converter
path/hash and loaded PE export bytes. Use separate new output directories and
avoid simultaneous incompatible Wine installations sharing a prefix.

## Apple performance recommendations and decisions

| Topic | Current evidence and decision | Further work |
| --- | --- | --- |
| Compatibility flags | Legacy MinLOD default retained; explicit zero opt-in tested only on non-LOD fragments. No speedup claim. | GPU LOD boundary A/B, metallib size, frame/GPU timing and repeated samples; do not enable TG coherence without proof. |
| Linear versus root layout | Explicit root signature remains required by D3D12 (`metalirconverter.c:984-999`). No verified equivalent linear conversion. | Build resource-binding equivalence probe before comparing indirection and CPU binding cost. |
| GPU and OS targets | Current detected Apple6–9 and OS mapped into compilation and key (`d3d12_msc_capabilities.cpp:35-60`, `d3d12_shader_converter.cpp:269-305`). Other GPUs/OS and A/B performance not verified. | Compare legal targets on each real device; do not promote minimum platform. |
| Argument buffer/occupancy | Root and table residency is tracked in command list (`d3d12_command_list.cpp:949-975`). No register or occupancy counters captured. | Capture representative vertex/fragment/compute counters and CPU binding cost before changing ABI. |
| Execution overlap | Compute and render resource use and residency are explicitly encoded (`d3d12_command_list.cpp:2250-2291`). No System Trace collected. | Compute-to-fragment timeline with state/barrier correctness before narrowing usage. |
| Input DXIL | Existing app shader bytes go directly through the MSC conversion path; new fixtures use DXC default optimized compilation. Debug/optimized GPU comparisons not run. | Controlled DXC -Od versus optimized inputs; do not recompile game shaders. |
| Root validation | Validation flags are passed through; D3D12 default is zero (`d3d12_shader_converter.cpp:262`, `metalirconverter.c:259`). SDK's implicit default and invalid-root comparison not measured. | Compare explicit zero/default/validation with invalid and valid roots; do not change checks. |
| In-process and persistent cache | Process cache plus single-flight exists, disk cache explicitly disabled (`d3d12_shader_converter.cpp:199-213,228-237,1812-1837`). Map has no evident capacity bound. Key includes stage, source, root, targets, spaces, configuration and flags, but loaded-dylib content identity is not trusted for persistent use. | Measure hits, waits and memory growth; validate content identity, corruption recovery and opt-in before disk cache. Function specializations require separate keys. |
| Function constants | Two values produce distinct correct pixels through direct Metal PSOs. No D3D12 production switch or specialization PSO cache. | Compare equivalent cbuffer/branch/specialization across update rates, CPU/GPU/memory; prove cache identity. |
| Framebuffer fetch / hybrid | Correct one-pass controlled hybrid readback on this device; no multipass reference or GPU time/bandwidth. | Compare equivalent multipass and fetch outputs, passes, CPU/GPU time and counters; gate formats, OS and device. |
| RT intrinsic masks | `IRObjectGatherRaytracingIntrinsics` is loaded but has no call site (`metalirconverter.c:47,425`); full masks retained in current paths. | Aggregate all pipeline shader requirements, validate SBT/hit groups/residency and A/B ray dispatch before narrowing. |
| RT attribute/mode/PSO | General conversion uses 16 bytes; state-object path rejects attributes >16 and specialized conversions receive configured size (`d3d12_raytracing_pipeline.cpp:411-418,456-458`, `metalirconverter.c:949,1468,1595`). Visible-function approach unchanged. | Verify per-state-object semantics, compile and dispatch timings before binary functions, specialized raygen or intersection-table changes. |

No public Feature Level, Shader Model or Tier is promoted. GPU counters,
occupancy, bandwidth, GPU frame/overlap timings, resident memory, RT pipeline
and dynamic-library-missing simulation are NOT_RUN. Neither a performance win
nor full production readiness is claimed. P1/P2 changes are withheld until
semantically equivalent benchmarks and error/fallback tests establish safety.

## Design card: private compatibility configuration and GPU probes

**Goal:** distinguish unspecified compatibility from explicitly supplied zero,
while validating SDK Metal HLSL extensions without changing ordinary D3D12.
**Baseline:** legacy `compatibility_flags == 0` selects MinLOD; both extension
spaces are disabled for normal conversion. **Evidence:** SDK flags are
`IRCompatibilityFlags` enum values, and `IRCompilerSetCompatibilityFlags`
accepts that type; DXMT's existing ABI carries a 32-bit value. Current SDK
values fit; this does not establish ABI compatibility for hypothetical future
bits. **Scope/boundary:** a new bit in the existing private compile options,
no structure-size change or game-facing switch; native code sends zero only when
explicitly requested and reports unsupported if the optional setter is absent.
Ordinary conversion, resource ABI and default settings stay unchanged.
**Non-scope:** promoting extensions, shader rewriting, cache persistence or
automatic flag selection. **Tests:** record forwarded setter arguments, compile
with legacy zero, explicit zero and MinLOD, render both extensions, include a
wrong-pixel control, and run existing ordinary D3D12 conversion/LOD and
single-flight probes. **Risk:** per-driver behavior and LOD corner cases remain
unverified for zero flags; do not use it to optimize games. **Rollback:** remove
private option and test harness; existing callers never set the new bit.
**Done:** builds, controlled GPU output and negative control, explicit/legacy
setter arguments, and ordinary-path smoke regressions pass with identities and
limitations recorded. Measurements alone do not satisfy production enablement.

Post-change complete compute PSO test: three first calls median/p95
11.1229/12.4831 ms and 27 later calls median/p95 0.3535/0.5107 ms. This is
an ordinary-path smoke check, not an optimization comparison: the small samples,
run order and uncontrolled system caches prevent a performance conclusion.
The existing concurrent probe created eight identical PSOs with one forwarded
MSC compile call, while the native single-flight and output-size tests passed.
Its first invocation was incorrectly configured to expect two calls and failed
its own assertion (actual: one); the corrected invocation passed. This result
does not measure individual wait times or memory growth. The existing fragment
LOD regression and its deliberately incorrect pixel oracle passed using the
real converter via a forwarding trace library.

An isolated forwarding-library trace confirms that the legacy zero, explicit
zero and existing nonzero configurations actually call MSC with 4, 0 and 4,
respectively. All three passed the GPU pixel probes, and the wrong-pixel
control failed as intended. The framebuffer test also renders the same known
color through a separate two-pass native-MSL texture-read path and compares the
same RGBA8 pixel. This is a correctness control using different shader code,
not a controlled GPU performance benchmark. The plain conversion run uses the
real dylib directly; tracing runs interpose a test-only forwarding dylib.
Native airconv typed-buffer and sample-LOD tests also passed. Geometry,
tessellation, other hardware/OS, GPU counters, full game workloads, D3D12
specialization-cache identity, root/descriptor resource tests for these special
spaces, and a full zero-flags LOD comparison remain NOT_RUN.

Reference: [Apple Metal shader converter documentation](https://developer.apple.com/metal/shader-converter/)
describes the DXIL-to-metallib path and lists Function Constants and Framebuffer
Fetch as converter features. Availability in that documentation is not proof
that DXMT exposes either feature to normal D3D12 applications.

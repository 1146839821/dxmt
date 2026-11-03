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

## GPU timing slice (2026-11-02)

**Design card.** Goal: add reproducible GPU command-buffer duration samples to
the existing fetch-versus-two-pass correctness probe. The existing WineMetal
`MTLCommandBuffer_property` exposes both GPU start and end timestamps; use it
only after completion and reject zero or non-monotonic timestamps. No bridge
ABI or production pipeline changes. Each process performs the existing
correctness draws, then 25 pairs, alternating the order; each draw is checked
by pixel readback and the first five pairs are excluded as warm-up. The runner
checks deployed binaries and actual loaded paths, records hashes and all raw
samples, and runs without validation. A separate validation-enabled run checks
both Metal layers and all 25 pairs. Rollback removes the test-only timing mode
and runner. Definition of done: positive timings, pixel correctness, runtime
identity, separate validation, full build, and recorded limits.

On Apple M1 Max/macOS 27.0.1 with the same dylib hash recorded above, three
processes produced 60 post-warm-up samples per path. Single pass: median
32,229 ns, p95 46,000 ns, range 24,083–990,750 ns. Two passes: median
69,000 ns, p95 79,791 ns, range 51,208–104,667 ns. The separate API and GPU
validation run passed all 25 pairs and produced no matching validation errors.
These are **whole command-buffer GPU durations**, not fragment-only timing,
GPU frame time or bandwidth. The workload is a 4×4 RGBA8 target, different
MSC/native fragment implementations, with synchronous CPU readback between
samples; one large single-pass outlier illustrates noise. Results justify no
production optimization or game-FPS claim. CPU encoding, memory and GPU counter
measurements remain NOT_RUN.

Reproduce after staging the paired build artifacts:

```sh
meson compile -C build dx12_metal_hlsl_gpu
python3 tests/dx12/run_metal_hlsl_gpu_timing.py --stage STAGE --prefix PREFIX --build build --converter /usr/local/lib/libmetalirconverter.dylib --output NEW_OUTPUT --processes 3
```

The captured samples are in `result-metal-hlsl-timing-20261102/summary.json`
and the validation log is in the same isolated result directory.
Apple documents the [GPU start/end timestamp contract](https://developer.apple.com/documentation/metal/mtlcommandbuffer/gpustarttime): values are read after completion and their difference describes command-buffer execution duration.

## HLSL constant comparison slice (2026-11-02)

**Goal and design:** compare a float4 HLSL cbuffer, an HLSL runtime branch
selecting two colors, and an `MTL_FUNCTION_CONSTANT` with equivalent outputs.
The first two use four D3D12 root constants in a cbuffer; the test serializes an
explicit root signature and writes the constant data inline at the offset
returned by `DXMTMSCGetRootSignatureLayout`. It checks both colors on the GPU.
Specialization uses the same values as opaque `UInt4` function constants. This
is an opt-in WineMetal test, not an automatic conversion of game CBVs. The
benchmark records function creation, render-PSO creation and command-buffer
GPU duration independently, plus actual loaded-code identity and hashes.
The wrong-pixel control must fail and API/GPU validation must pass separately.

**Scope and risks:** the 4×4 test uses root constants rather than a CBV
resource, creates one PSO per value for all modes, uses a fixed mode order,
and samples only two colors. It is not an update-frequency benchmark, cache
reuse study, memory measurement, representative workload or proof of a game
speedup. Timings have validation disabled, and the validation run is separate.
Rollback removes the standalone tests and fixture targets; production behavior
and capability declarations are unchanged.

Five separate process runs passed both pixels for each variant; a separate run
with Metal API/GPU validation and the deliberately incorrect expected pixel
control also behaved as expected. Function creation for the first value had
median/p95 0.0168/0.0176 ms for the direct cbuffer, 0.0111/0.0153 ms for the
HLSL branch, and 0.1571/1.4401 ms for specialization. The first render PSO
median/p95 was 1.8419/11.1864, 0.1739/4.8011, and 0.1276/2.7964 ms,
respectively. GPU timing had large outliers (including 205,666 ns and 410,500
ns in the specialization samples). Mode order and driver caches confound these
numbers, so no relative performance conclusion is justified; the HLSL branch
may also be flattened by compilation. All individual samples and the second
value's results are in `result-msc-cbuffer-compare/summary.json` and `.md`.

Reproduction requires the same isolated paired stage and fresh output directory:

```sh
meson compile -C build dx12_msc_cbuffer_compare dx12_metal_hlsl_dynamic_main dx12_metal_hlsl_dynamic_branch_main dx12_metal_hlsl_constants_fixture
python3 tests/dx12/run_msc_cbuffer_compare.py --stage STAGE --prefix PREFIX --build build --converter /usr/local/lib/libmetalirconverter.dylib --output NEW_OUTPUT --processes 5
```

## Constant update frequency slice (2026-11-02)

**Design card:** extend the opt-in constant comparison without touching the
D3D12 or WineMetal implementations. Create and retain two specialized Metal
PSOs, one for each value; the dynamic paths reuse one PSO. Compare 16 draws per
mode for low variation (eight identical values, then eight of the other value)
and high variation (alternate values every draw). Every draw copies the
current value into the root-constant TLAB on dynamic paths and validates the
pixel. This models explicit two-entry Metal PSO reuse, not DXMT's D3D12 PSO
cache. The runner records CPU encoder construction/commands and completed GPU
command-buffer time, with separate validation and a deliberately swapped
specialized PSO negative control. It drops the first two samples of each
sequence for summary, while retaining all samples. Fixed mode/policy order,
per-draw texture creation and synchronous pixel readback confound performance;
4×4 rendering and this small pool of values are not game workloads. CPU time
excludes parameter memcpy, submission, synchronization, PSO lookup/creation,
readback and resource allocation; GPU time is the whole command buffer. Memory,
cache hit rates and production behavior remain unmeasured. Rollback removes
this optional test mode.

Five processes produced 70 post-warm-up samples per mode/policy. Direct cbuffer
GPU median/p95: low 31,979/35,584 ns, high 30,937.5/33,542 ns. Dynamic
branch: low 33,000/39,417 ns, high 33,416.5/39,166 ns. Reused specialized
PSOs: low 32,770.5/39,458 ns, high 32,479.5/36,625 ns. CPU encoding median/
p95 (µs): cbuffer low 25.15/35.0, high 23.8/32.0; branch low 25.45/75.6,
high 23.2/37.9; specialization low 23.1/36.0, high 22.0/26.5. Samples are
pooled within each process, not independent processes; outliers and fixed order
preclude a general performance claim. Correct pixels passed on all draws; both
validation layers passed and the deliberate mismatched-PSO control failed as
intended. Full samples, exact artifact hashes and logs are in
`result-msc-frequency-final/` outside the repository.

Reproduce with the existing comparison runner's `--frequency` switch and a
fresh output directory. Ordinary paths are not modified, and D3D12 PSO cache
identity and resident memory remain NOT_RUN.

## MinLOD compatibility flag comparison (2026-11-02)

**Design card:** use the existing fragment SampleLevel LOD GPU regression and
its intentionally failing unpatched control under a test-only forwarding MSC
library. The wrapper records both DXMT's requested flags and the value actually
passed to MSC; in the experimental mode it forwards zero instead of 4. Each
mode runs in separate fresh Wine processes and isolated shader-cache paths.
The wrapper also records individual `IRCompilerAllocCompileAndLink` durations
and metallib bytecode sizes. Actual PE and Unix bridge paths, exported code and
hashes are checked by the existing LOD runner. This deliberately overrides the
compiler after DXMT chooses its production cache key; it is only valid as a
separate-process experiment and must not be used as a production flag switch.
No game path, resource barrier, residency or public capability is changed.
Rollback removes the experimental wrapper and runner.

Three runs per mode passed the normal and negative fragment oracles with Metal
API and GPU validation. The fragment case samples a five-entry matrix with
ResourceMinLODClamp 0, 0.5, 1 and 1.5 and a MostDetailedMip=1 view; it checks
both ordinary and reserved textures. Under **the enabled test-only sampler LOD
patch**, default and forced-zero results matched exactly: ordinary float-bit
outputs `3f800000 40000000 40400000 40800000 40400000`, reserved
`3f800000 40000000 40400000 00000000 40400000`. The test intercept confirmed
4→4 and 4→0 respectively. Both produced converter-reported metallib sizes
5500 and 5308 bytes for the two ordered compilation calls; identical size does
not imply identical bytecode.

The first and second ordered compile calls in each fragment run had default
median/p95 3.410/3.721 ms and 1.813/1.964 ms; forced-zero 3.557/3.603 ms and
1.695/1.779 ms. These are three samples, with validation enabled and unlabelled
vertex/fragment compilation order, not a performance conclusion. GPU time,
memory, implicit derivatives, other shader operations, devices and unsupported
cases remain NOT_RUN. The tested patch may itself supply necessary LOD
semantics, so this does not prove the flag is redundant generally; leave the
production MinLOD compatibility flag enabled. Raw outputs, hashes and negative
controls are retained in `result-msc-lod-flags-final/`.

Reproduce with a paired isolated stage and a new output directory:

```sh
python3 tests/dx12/run_msc_lod_flags.py --stage STAGE --prefix PREFIX --build build --converter /usr/local/lib/libmetalirconverter.dylib --output NEW_OUTPUT --repeats 3
```

## MSC minimum GPU target comparison (2026-11-02)

**Design card:** on a device that reports support for Apple6 and Apple7, set
only the opt-in HLSL probe's minimum family to 1006 or 1007. Verify that the
MSC runtime exposes the target setter and that the selected Metal device
supports the requested family; otherwise report a skip before conversion. A
test-only forwarding dylib records the actual `IRCompilerSetMinimumGPUFamily`
arguments. Each setting compiles identical Function Constant and framebuffer
fetch DXIL, checks both specialized colors and framebuffer pixels, and records
conversion and 25 paired GPU command-buffer samples (first five discarded for
GPU summaries). Run each in separate processes with validation disabled for
measurement, then run separately with API and GPU validation. An Apple9 probe
checks unsupported-device skip on this Apple7 device. Existing ordinary GPU
probe and intentional bad-pixel control are rerun. No production target choice,
cache key, user device requirement or public capability changes. Rollback
removes only this test mode, wrapper and runner.

On M1 Max/macOS 27.0.1, three processes per supported target passed all pixels;
validation runs passed and the Apple9 probe skipped as unsupported. Both
reported 4840-byte Function Constant and 4452-byte framebuffer metallibs;
identical sizes do not establish identical binaries. Whole GPU command-buffer
duration for the 4×4 single-pass fetch had Apple6 median/p95 33,250.5/38,500
ns and Apple7 33,083.5/40,833 ns (60 pooled post-warm-up samples per target).
The Apple7 samples contained large outliers. Conversion timing, min/max and raw
samples are in `result-msc-gpu-target/summary.json` and `.md`; there are only
three process-cold conversion samples per target. The workload uses fixed target
order, synchronous readback and test-only tracing; it is not representative of
game frame time or evidence for a production target change. OS deployment
target comparison, actual cross-device reuse, cache invalidation execution,
memory and GPU counters remain NOT_RUN. The source cache key records both GPU
family and OS fields (`d3d12_shader_converter.cpp:269-305`), but source
inspection is not a cross-device cache correctness test.

Reproduce with a paired isolated stage and fresh output:

```sh
meson compile -C build dx12_metal_hlsl_gpu dx12_metal_hlsl_constants_fixture dx12_metal_hlsl_framebuffer_fixture
python3 tests/dx12/run_msc_gpu_target_compare.py --stage STAGE --prefix PREFIX --build build --converter /usr/local/lib/libmetalirconverter.dylib --output NEW_OUTPUT --repeats 3
```

## Root signature validation flags (2026-11-02)

**Design card:** compare three MSC compiler states with the same cbuffer
fragment DXIL and explicitly serialized root signatures: a matching 4-dword
`b0` root constant and an empty root signature that cannot provide `b0`. A
test-only forwarding library distinguishes omitting the validation setter,
setting `IRCompilerValidationFlagNone` and setting
`IRCompilerValidationFlagValidateAllResourcesBound`. The DXMT bridge normally
calls the setter with zero; the test wrapper changes only the isolated process.
Every compilation creates a fresh MSC compiler. The runner checks loaded
PE/Unix/MSC paths and hashes, records all results and timing, and requires the
valid root to compile and the missing-resource root to fail. No production
flag, root-signature behavior or cache is changed. Rollback removes only the
standalone test and forwarding wrapper.

On M1 Max/macOS 27.0.1 with the recorded MSC dylib SHA-256, three processes per
mode each ran six missing-resource and six valid-root conversions. The valid
root compiled in every case. Missing `b0` was rejected in all cases but the
reported error differed: omitting the setter and enabling the bound-resource
flag returned DXMT error 5 / MSC `IRErrorCode=4` (root signature error);
explicit zero returned DXMT error 4 / MSC `IRErrorCode=7` (unsupported shader
feature). This is a current-runtime observation for one shader and malformed
binding layout, not a guarantee about every invalid root signature or the SDK's
default in other versions. It shows that omitted and explicit zero are not
interchangeable for error reporting, and that zero did not permit this shader
to compile successfully. Retain current production behavior.

After excluding each root's first call in each process, valid-root sizing-call
median/p95 were unset 0.8442/1.0011 ms, zero 0.8355/1.3347 ms, and bound
0.8382/1.1543 ms (15 pooled samples per mode). Fixed mode order, repeated same
input and small sample size preclude a claim of meaningful performance
improvement. Invalid-root samples, raw logs and identities are recorded in
`result-msc-root-validation/`; GPU execution, memory and game FPS are NOT_RUN.

Reproduce with a paired isolated stage and fresh output:

```sh
meson compile -C build dx12_msc_root_validation_compare dx12_metal_hlsl_dynamic_main
python3 tests/dx12/run_msc_root_validation_compare.py --stage STAGE --prefix PREFIX --build build --converter /usr/local/lib/libmetalirconverter.dylib --output NEW_OUTPUT --processes 3
```

## Input DXIL optimization and debug information (2026-11-02)

**Design card:** compile the same repository-owned cbuffer and branch HLSL as
`-Od`, `-O3`, and `-Od -Zi -Qembed_debug`; compare the first two using the
existing root-constant Metal GPU probe and exact pixel checks. Time the two
separate `DXMTMSCCompileDXIL` sizing/output calls and record DXIL/metallib
sizes. Test embedded-debug input separately, both with and without the MSC
ignore-debug option; check that the latter setter exists before requesting it.
Use paired isolated runtime files, preserve negative controls and run Metal
validation separately. Do not rewrite application DXIL or change production
ignore-debug defaults. Rollback removes test fixture variants and measurements.

Three independent processes each for `-Od` and `-O3` passed all pixel checks,
validation and the incorrect-pixel control. For the direct cbuffer fixture,
DXIL sizes were 3132 and 3028 bytes; metallib was 4484 bytes for both. Branch
fixture DXIL sizes were 3284 and 3088 bytes; metallib sizes were 4472 and 4440
bytes. Equal sizes are not bytecode identity. Median/p95 of the direct cbuffer
sizing call were 4.0246/4.2985 ms for `-Od` and 3.8063/3.9304 ms for `-O3`;
output-call medians/p95 were 1.0399/1.0788 and 0.8993/0.9603 ms. Each call
performs a full conversion; samples are separate processes in fixed order,
not disjoint phases. Three samples and a trivial shader cannot establish a
meaningful performance advantage; GPU timings, raw samples and the branch
results are in `result-dxil-od/` and `result-dxil-o3/`.

**FAIL observed:** with `-Od -Zi -Qembed_debug`, conversion of the direct
cbuffer input failed before GPU execution. The isolated run returned 6 from
the test (bridge call returned -1 with error code 0) and logged the LLVM
`MetadataLoader.cpp` `PlaceholderQueue` assertion. This occurred in two
separate runs; the log alone does not establish which component owns the root
cause. With the optional MSC ignore-debug setter available and requested, two
processes, the Metal validation run and bad-pixel negative control passed on
both embedded-debug fixtures. This is evidence for the tested input/runtime,
not authorization to enable ignore-debug for all games; other debug shaders
and runtime versions remain unverified. Keep the failure logs at
`result-dxil-debug-final-failure/` and the successful opt-in logs at
`result-dxil-debug-ignore-final/`. Normal D3D12 paths and capability reporting
are unchanged.

Reproduce using the paired isolated stage and fresh output directories:

```sh
meson compile -C build dx12_msc_cbuffer_compare dx12_metal_hlsl_dynamic_main_Od dx12_metal_hlsl_dynamic_branch_main_Od dx12_metal_hlsl_dynamic_main_O3 dx12_metal_hlsl_dynamic_branch_main_O3 dx12_metal_hlsl_dynamic_main_OdDbg dx12_metal_hlsl_dynamic_branch_main_OdDbg
python3 tests/dx12/run_msc_cbuffer_compare.py --stage STAGE --prefix PREFIX --build build --converter /usr/local/lib/libmetalirconverter.dylib --output NEW_OUTPUT --processes 3 --main-fixture metal_hlsl_dynamic_main_Od.ps.cso --branch-fixture metal_hlsl_dynamic_branch_main_Od.ps.cso
# Repeat with the O3 fixtures; for embedded debug, use OdDbg fixtures and add --ignore-debug.
```

## Embedded debug metallib failure isolation (2026-11-02)

**Diagnostic slice:** use the same generated `-Od -Zi -Qembed_debug` fixture,
MSC dylib SHA-256 recorded above, paired isolated Wine stage, and a test-only
forwarding library to save MSC's extracted metallib with exclusive file
creation. Compare a native probe of DXMT's existing
`SM50PatchMetalLibUnsupportedDouble` against an ordinary optimized metallib;
no production flags or patch semantics change. The probe is opt-in and its
failure is a real diagnostic failure, not a passing regression test.

**Evidence:** Apple `metal-shaderconverter` CLI completed the debug DXIL in
both arm64 and x86_64 modes, but it is a separate executable and does not prove
the loaded dylib's identity. With the loaded dylib, the forwarding trace shows
`DEBUG_MSC_COMPILE_END ok`, then `DEBUG_METALLIB_EXTRACTED 5860`, and only
then the LLVM assertion. The extracted metallib SHA-256 is
`e3d6b10745c740e85830a794e73a159911165fb3bfe07a8296cab0f142aa78e1`.
Running the native x86_64 probe directly on those bytes, without Wine or MSC,
reproduces signal 6 / exit 134; an optimized CLI metallib passed. LLDB stopped
at `__assert_rtn`: its stack passes through
`llvm::MetadataLoader::MetadataLoaderImpl::parseMetadata`,
`llvm::parseBitcodeFile` and `SM50PatchMetalLibUnsupportedDouble`. The build's
LLVM headers report 15.0.7. An independently installed LLVM 15 `llvm-dis`
returns `Invalid record` on the extracted AIR bitcode, whereas a newer LLVM
`llvm-dis` reads it and shows debug metadata. This establishes a failure in
DXMT's LLVM 15 AIR parsing path for this particular output; it does not prove
all debug metadata or all LLVM 15 builds are affected.

A separate LLVM 15 prototype using lazy metadata, `StripDebugInfo`, then
`materializeAll` still returned `Invalid record`; it is not a viable fix.
Do not bypass the double-precision lowering for all debug inputs or silently
strip app debug data: either would change semantics without proof. The known
crash is **unfixed**. A follow-up must investigate a compatible metadata
reader/backport or another semantics-preserving approach, then test original
DXIL, normal and double-bearing shaders, preserved debug information, errors,
concurrency and D3D10/11 regressions. Logs, extracted AIR and LLDB backtrace
are in `msc-debug-isolation/` outside the repository.

The probe is built explicitly and is expected to fail on the captured debug
metallib until fixed:

```sh
meson compile -C build msc-debug-metallib-probe
build/src/airconv/darwin/msc-debug-metallib-probe CAPTURED.metallib
```

`tests/airconv/msc_debug_metallib_trace.c` is a test-only MSC forwarding dylib;
set `DXMT_TEST_EXTRACT_METALLIB` to a new path in an isolated test run to
capture the input. Never inject it into a game or share its output path.

## LLVM 15 compatibility candidate (2026-11-02)

The build and native reproducer use the repository's
`toolchains/llvm-darwin` (LLVM 15.0.7); the user's existing toolchain is viable
for a targeted backport. The extracted AIR's `METADATA_DERIVED_TYPE` records
have 15 operands. LLVM 15 accepts at most 14; LLVM 22 accepts 15 and treats the
last as optional pointer-authentication data. In this captured shader the last
operand is zero, and the newer size-as-metadata flag is absent. References:
[LLVM 15 reader](https://github.com/llvm/llvm-project/blob/llvmorg-15.0.7/llvm/lib/Bitcode/Reader/MetadataLoader.cpp)
and [LLVM 22 reader](https://github.com/llvm/llvm-project/blob/llvmorg-22.1.0/llvm/lib/Bitcode/Reader/MetadataLoader.cpp).

`patches/llvm/experimental-llvm15-empty-ptrauth.patch` is a **candidate**, not
applied by the normal build. It accepts a 15-operand record only when the new
pointer-authentication field is zero and the new size flag is absent. A clean
LLVM 15.0.7 source-file copy accepted the patch in dry run. In a separate test
directory, the patched `MetadataLoader.cpp` was compiled against the repo LLVM
headers and linked ahead of the unchanged repo static libraries; the original
`toolchains/llvm-darwin` was not modified. The native AIR postprocessing probe
then passed both the previously crashing 5860-byte debug metallib and the
ordinary optimized metallib. An isolated WineMetal Unix library relinked with
that object loaded the actual MSC dylib and passed both debug-HLSL color paths,
specialization, and the deliberately wrong-pixel negative control with Metal
API/GPU validation. The guarded candidate's WineMetal SHA-256 was
`a98c9cc52686bfea785cdccf9df0349b820a2c19c8ab5ef4d4f5b483ff98b5cf`.
Original logs and artifacts are under `msc-debug-isolation/` and
`stage-msc-debug-backport/`, outside the repository.

**Not production-ready:** LLVM 15 cannot represent nonzero new metadata fields;
the candidate rejects them, but the underlying assertion-on-error problem can
still abort. Other debug record forms, double-lowering with debug metadata,
error containment, original clean build integration, concurrency, other host
architectures and D3D10/11 regressions remain unverified. The experimental
patch is therefore not automatically applied, and this issue remains open.

### Double-lowering diagnostic follow-up

The postprocessing entry point can return success while copying its input after
an AIR parse error. Therefore a successful return alone is not evidence that
lowering occurred. The opt-in probe now has `--expect-lowered`, which parses
and verifies both modules and checks that a double-to-float truncation was
removed and `llvm.dbg.cu` remains present. An LLVM 22-generated opaque-pointer
input was rejected by that check (exit 6), rather than counted as a pass.

Using the isolated guarded LLVM 15 object, `--make-double` parsed the captured
MSC AIR, inserted a fast double multiply and a truncation into a float store,
and serialized a fixture. The subsequent `--expect-lowered` check reported
one truncation before, zero after, and a debug compile unit in both modules.
This proves that this particular fixture was parsed and transformed; it does
not establish complete debug-information equivalence. Serialization by LLVM
15 also means the fixture does not combine the original 15-operand metadata
record with double instructions. Non-fast double cases remain untested.

```sh
probe-verified CAPTURED.metallib FIXTURE.metallib --make-double
probe-verified FIXTURE.metallib LOWERED.metallib --expect-lowered
```

`probe-verified` in this example is the isolated test executable linked with
the candidate object, not the normal build target. The normal LLVM 15 reader
still fails on the captured MSC AIR; no toolchain files were replaced.

On Apple M1 Max, macOS 27.0.1, the transformed fixture was loaded directly
by Metal and rendered into an RGBA8 texture. With Metal API and GPU validation
enabled before device creation, readback was `(255, 0, 0, 255)` and a
deliberately wrong expected-result control exited 8. This is a native Metal
test of the transformed synthetic fixture, not a Wine or game test. The
fixture's SHA-256 was
`b9a395234ef8004b1172fcf42e5e8adf51ecac1ae07a612fc0c9b49983246a6e`.
The test can be reproduced with:

```sh
xcrun clang -fobjc-arc -Wall -Wextra -framework Foundation -framework Metal \
  tests/airconv/msc_debug_metallib_render.m -o /tmp/msc-debug-render
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 \
  /tmp/msc-debug-render LOWERED.metallib correct
```

An additional error-path control passed an opaque-pointer AIR fixture that the
LLVM 15 reader rejects. `SM50PatchMetalLibUnsupportedDouble` returned zero and
its output was byte-identical to the unsupported input. This confirms its
existing fallback does not guarantee that required lowering has happened;
changing production failure policy requires examining caller compatibility and
must not be conflated with the metadata reader candidate.

### Parse-status visibility slice

**Goal and baseline:** expose an uninspected AIR pass-through without changing
the established compatibility fallback. `SM50PatchMetalLibUnsupportedDouble`
previously returned zero for both successfully parsed AIR and parse failures;
WineMetal could not tell these outcomes apart. This is an internal safety and
diagnostics change, not a D3D12 capability promotion.

**Design:** a new extended entry point reports whether LLVM parsed the AIR;
the existing entry point delegates to it with its original return contract.
WineMetal logs an explicit warning for pass-through. Parsing failures continue
to pass through, since rejecting all AIR unsupported by LLVM 15 would be a
separate compatibility policy change. Neither this status nor the warning
protects against assertions inside the LLVM reader. The candidate metadata
backport remains separate and disabled. Rollback removes the extended entry
point, its caller and test, leaving the original processing behavior.

**Validation scope:** the native `airconv msc double parse status` test checks
invalid arguments, a short input, a malformed bitcode payload and a valid
LLVM 15 module. It checks the returned status and that pass-through bytes are
unchanged. This does not prove behavior on every MSC AIR version or that an
unparsed shader is safe to execute. On the current macOS build configuration,
the status test, `airconv lower unsupported double` and
`airconv msc sample-level lod` passed; full `meson compile -C build` and
`meson compile -C build-X86` passed. The WineMetal warning was not separately
exercised through an isolated Wine deployment in this slice.

### LLVM 15 placeholder cleanup candidate

**Goal and scope:** investigate the process abort during metadata parsing;
leave the installed toolchain and production behavior unchanged. LLVM 15's
`PlaceholderQueue` requires an empty queue at destruction, but an early
`parseOneMetadata` error can leave entries in it. LLVM has an independently
reported instance of this error path: [LLVM issue 151250](https://github.com/llvm/llvm-project/issues/151250).

`patches/llvm/experimental-llvm15-placeholder-cleanup.patch` is an isolated
candidate that untracks queued placeholder uses and replaces them with null
on error exits from the metadata and attachment parsers; successful exits
retain the original empty-queue assertion. A separately compiled LLVM 15.0.7 metadata-reader
object with this candidate was linked ahead of the repository's unchanged
static libraries. The original reader aborted on the captured 15-operand
debug AIR; the candidate returned a parse error. A stress program alternated
the failing input with a valid optimized AIR input in the same LLVM context
for 1,000 iterations, verifying the successful module each time; it passed.
The original executable failed with SIGABRT on the first iteration. A second
run passed with AddressSanitizer enabled for the candidate reader object;
the prebuilt LLVM static libraries were not instrumented, so this is limited
memory-safety evidence.

`llvm-metadata-error-stress` is an opt-in test executable taking the failing
and valid raw AIR files in that order. The candidate object and captured inputs
remain in `msc-debug-isolation/` outside the repository. Merely building the
test against the normal toolchain does not apply the candidate and is expected
to abort on the captured failing input. A production fix still needs broader
memory-safety and concurrent malformed-input testing,
coverage of other error paths including LLVM fatal errors, and integration
with a reproducible toolchain build. The candidate is not enabled by default.

### Combined candidate build (2026-11-03)

`tests/airconv/build_llvm15_candidate.py` copies the LLVM 15.0.7 reader into a
new output directory, applies both experimental patches, and links diagnostic
executables against the resulting object and the existing LLVM libraries. It
checks the source tag and reader file status; it does not replace the LLVM
checkout or installed libraries. It does build probe targets in the specified
Meson build directory.

The combined candidate parsed the captured 15-operand debug AIR. A separately
serialized debug fixture with fast double instructions reported one
double-to-float truncation before lowering and zero after, retaining an
`llvm.dbg.cu` node. Metal API and GPU validation on Apple M1 Max returned the
expected red pixel for the lowered fixture. An LLVM 22 opaque-pointer input
was rejected by the parser, and a stress probe alternated this rejected input
with the captured parseable AIR 1,000 times. The opaque-pointer rejection
occurs before metadata parsing, so it does not validate placeholder cleanup in
the combined build. The older cleanup-only test exercises that path.

The combined patch is still experimental. In particular, 15-operand records
with nonzero pointer-authentication data, other malformed metadata records,
concurrent execution and a complete clean LLVM rebuild remain unverified.
No change was made to the production toolchain or capability declarations.

### Metadata rejection and cleanup control (2026-11-03)

`tests/airconv/make_llvm15_ptrauth_rejection.py` makes a synthetic negative
fixture from the captured AIR by changing operand 14 of one uniquely matched
unabbreviated `METADATA_DERIVED_TYPE` record from zero to one. The bitstream
dump confirms the changed record; this deliberately unsupported fixture does
not establish that the operand references valid pointer-authentication data.
The combined candidate rejected it and then parsed the original captured AIR
1,000 times in alternating sequence in the same contexts. An otherwise
identical candidate linked with the format compatibility patch but without
placeholder cleanup aborted at the original queue assertion. Thus this
fixture exercises the combined candidate's metadata rejection and cleanup
path, unlike the opaque-pointer control.

The test only covers one synthetic record and sequential execution. The
candidate remains experimental; concurrent and broader malformed inputs,
complete toolchain rebuilding and actual application behavior are unverified.

An extended stress probe ran eight threads, each with its own LLVM context and
1,000 alternating synthetic rejection and successful captured-AIR parses,
against the combined reader candidate. All 8,000 pairs passed. This covers
concurrency for this one input pair, not arbitrary malformed metadata or
sharing a single LLVM context between threads.

An isolated clone of LLVM `llvmorg-15.0.7` with both candidate patches was
configured, fully built and installed using the x86_64 assertion-enabled
configuration in `docs/DEVELOPMENT.md`. The install is under
`llvm-combined-full-20261103/`, outside the repository. Diagnostic executables
were relinked against that install; the linker map identifies its
`libLLVMBitReader.a(MetadataLoader.cpp.o)`. The eight-thread rejection/valid
parse probe passed again, and the debug double fixture reported one truncation
before and zero after with `llvm.dbg.cu` present. This is a clean LLVM library
rebuild and native diagnostic, not a full DXMT build or Wine runtime test
using the new LLVM installation. The production toolchain was not replaced.

### Isolated DXMT and Wine run with the combined LLVM candidate

A separate `release`, x86_64 Wine cross-build was configured with D3D12 and
tests enabled, using `llvm-combined-full-20261103/install` as its native LLVM
path. The build completed and the WineMetal link command names this install's
library directory. This was not a clean Metal build: the active Xcode reports
the Metal Toolchain missing. In the isolated generated `build.ninja`, seven
AIR generation edges and one metallib edge were changed to copy their existing
outputs from the normal build. No generated build files or artifacts in the
normal build were changed. The copied AIR outputs represent three distinct
shaders; their SHA-256 values are `5a6375b498024ed045f2fdab235026ba2b7c3eabee3dce26bde40ccbc6e81ed6`,
`dbfd94d834433bb550287b28fc9d59a1a6d0d4cbd18c54ab214c0bfe73e39662` and
`ae022464178f5a7fc4646eaf939e1e128e0c73a77d5754ab2cc4364f605402c8`;
the copied `dxmt_command.metallib` SHA-256 is
`a8240aed9ee15666fa85200eec19d71a4448fe7a68b6e273d1c1e04b359f17ec`.

With a separate copied Wine stage and prefix, the `-Od -Zi -Qembed_debug`
main and branch fixtures completed two normal runs, a Metal API/GPU validation
run, and the wrong-pixel negative control, without requesting MSC ignore-debug.
The test runner checked loaded PE/Unix paths, MSC library path, artifact hashes
and pixel results. The isolated `winemetal.so` SHA-256 was
`f548b31f9103f1e0537cb23b5cae162440d4964defc0a72d18093a01adc9c06a`.
Logs and exact results are in `result-llvm-combined-debug-20261103/` outside
the repository. This is evidence for these fixtures, not a clean Metal
toolchain build, comprehensive debug metadata support, or game compatibility.
The candidate remains experimental and the production toolchain is unchanged.

The same isolated DXMT/LLVM stage was subsequently exercised with both `-Od`
and `-O3` variants of the main and branch fixtures. For each configuration,
two normal runs, Metal API/GPU validation and the deliberately wrong-pixel
control passed. The runner verified the same PE/Unix and MSC loaded paths and
artifact identities. Results are in `result-llvm-combined-od-20261103/` and
`result-llvm-combined-o3-20261103/`; this does not cover D3D10/11 or arbitrary
shader operations.

For a D3D11 regression, the candidate LLVM stage also ran
`dx11_shader_backend_boundary.exe bgra-sm5.cso --buffer-view-usage` with
Metal API and GPU validation enabled. All 24 reported BGRA values across three
resource usage modes and two repetitions matched the test's expected values;
the process exited zero. Wine and dyld logs identify the isolated stage's
`d3d11.dll`, `winemetal.dll` and `winemetal.so`. The staged D3D11 DLL SHA-256
was `244728f943d818e2fdf00edb1daa3ae90c6e81546298e7c07c6e50c3dc461c7e`;
the WineMetal Unix library is the candidate identified above. The full log is
`dx11-llvm-combined-20261103.log` outside the repository. This single D3D11
compute/readback case does not establish general D3D10/11 compatibility.

### Clean Metal 27.2 build and debug runtime

After the system upgrade, macOS 27.2 (26B123), Xcode 27.2 (27B52),
`metal` 32023.922 and the Metal Toolchain 27.2 `metallib` were available.
A new isolated x86_64 release DXMT build with the combined candidate LLVM
completed without reusing Metal AIR or metallib outputs. Build log:
`clean-metal272-build.log` outside the repository. The resulting WineMetal
Unix library SHA-256 was
`d2452fd04dc4e88e46defdc05fcdef957a1a14a1d8e1d6b313a0f25ba7b024fa`.

With a separately copied Wine stage and prefix and no ignore-debug option, the
embedded-debug main and branch fixtures passed two normal runs, Metal API/GPU
validation and the deliberately wrong-pixel control. The runner checked the
loaded PE/Unix paths, MSC path and artifact identities. Results are in
`result-clean-metal272-debug-20261103/` outside the repository. The candidate
LLVM patches require the patched CI toolchain or an explicit local candidate
build; this result does not establish compatibility for all shader inputs.

### Reproducible candidate toolchain

`tests/airconv/build_llvm15_candidate_toolchain.sh LLVM_SOURCE NEW_OUTPUT`
builds the experimental x86_64 toolchain in a new directory using the same
LLVM version, patches and CMake settings as the isolated validation. It
rejects an existing output directory and does not alter the source checkout
or the production toolchain. Its complete build was verified in
`llvm-script-20261103/`; a linker map confirmed the stress executable used
that installation's `MetadataLoader.cpp.o`, and eight threads each passed
1,000 synthetic rejection/valid parse pairs. `bash -n` and the existing-output
negative check passed; `shellcheck` was not available.

The script is opt-in. CI now applies both patches when building Darwin LLVM and
keys its x86_64 and arm64 caches by both patch contents, without falling back to
version-only Darwin caches. This CI change has not yet run. Local prebuilt
toolchains are not automatically patched, and unsupported AIR fallback
semantics remain unchanged.

The candidate toolchain script also accepts a third argument `arm64` (the
default remains `x86_64`). On Apple M1 Max, a complete isolated arm64 LLVM
15.0.7 build and install succeeded. A native arm64 stress executable linked
against that installation, confirmed by its linker map, passed eight threads
with 1,000 synthetic metadata rejection and valid captured-AIR parse pairs
each. The build and test artifacts are in `llvm-script-arm64-20261103/`
outside the repository. This verifies the reader in a native diagnostic, not
an arm64 DXMT/Wine runtime or general shader compatibility.

### Derived-type size flag guard

LLVM 22 interprets bit 1 of the derived-type record's first operand as a
size/offset metadata flag even when the record has 14 operands. The original
candidate checked this flag only on 15-operand records. A synthetic 14-operand
fixture with the flag set was incorrectly accepted by that candidate. The
guard now rejects unsupported first-operand bits for every accepted length,
while retaining the separate nonzero pointer-authentication check on length 15.
The fixture generator supports `--size-flag-14`; combined candidates rejected
both negative fixtures and parsed their unmodified counterparts in eight
threads with 1,000 iterations each. The debug double lowering fixture also
passed. The new patch changes the CI cache hash. This guard addresses derived
types only: newer flags and records elsewhere need separate analysis, and
unparsed AIR still follows the existing warning and pass-through policy.

### Basic-type size flag guard

LLVM 22 also uses bit 1 of `METADATA_BASIC_TYPE` operand 0 to indicate a
metadata-valued size. LLVM 15 treats that operand as the distinct flag and
would accept a synthetic record carrying the newer bit with the wrong meaning.
The candidate now rejects values other than 0 or 1 for this operand. The
fixture generator's `--basic-size-flag` produces the synthetic case from the
LLVM 15-serialized debug AIR; the previous candidate accepted it, while the
updated candidate rejected it and parsed the original in eight threads with
1,000 pairs each. The derived-type negative cases and debug double lowering
control also passed. Other metadata record types remain to be audited, and
this does not change the existing unparsed AIR pass-through policy. The
modified patch changes the CI cache identity; CI has not run this revision.

### Composite-type size flag guard

LLVM 15 uses bits 0 and 1 of `METADATA_COMPOSITE_TYPE` operand 0; LLVM 22
adds bit 2 for metadata-valued size and offset. The captured fixture uses the
old bit 1. A synthetic input retaining bit 1 and adding bit 2 was accepted by
the previous candidate; the new guard rejects operand values above 3 while
continuing to parse the original. Eight threads completed 1,000 rejection and
successful parse pairs each. The basic-type and derived-type negative cases
and debug double lowering control also passed. This checks one record shape;
the existing unparsed AIR pass-through policy and other metadata types remain
unchanged. CI will use a new patch hash but has not run this revision.

### String-type size flag guard

LLVM 22 uses bit 1 of `METADATA_STRING_TYPE` operand 0 for metadata-valued
size. LLVM 15 instead treats that operand as a distinct flag. A synthetic
size-flag input, based on LLVM 15's `distringtype-backward.ll` test assembled
with LLVM 15, was accepted by the earlier candidate. The candidate now rejects
operand values above 1 after its existing record-length check. Eight threads
each passed 1,000 rejection/original-parse pairs; the existing composite,
basic, derived and pointer-auth negative fixtures and debug double lowering
control passed. `--string-size-flag` generates this synthetic case. Other
records and unparsed AIR fallback behavior remain unchanged; CI has not run
the updated patch hash.

### Unknown metadata records and patch application

LLVM 15's `parseOneMetadata` ignores unknown record codes. Replacing a known
string-type record code with synthetic code 47 caused the candidate reader to
abort later while resolving a metadata reference. The candidate now returns an
error for unknown codes above LLVM 15's last defined code (`METADATA_ARG_LIST`,
46), retaining the old handling of reserved or known lower codes. Eight threads
each passed 1,000 rejection/original-parse pairs; five existing synthetic
metadata controls and the debug double lowering control also passed. This
tests the eager parsing path, not every LLVM lazy-load fatal-error path, and
does not change the WineMetal pass-through policy for unparsed AIR.

The previous patch files used zero-context hunks. A later patch revision
shifted line positions and demonstrated that applying the cleanup patch after
the compatibility patch could insert code incorrectly and fail compilation.
Both patches now use context hunks and were applied in CI order to a fresh
isolated reader, compiled and tested. `patches/llvm/.gitattributes` disables
the trailing-space check for patch files because blank unified-diff context
lines must contain a leading space. The changed content invalidates the CI
cache; this revision has not run on GitHub Actions.

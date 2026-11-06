# MSC header and runtime version audit

Baseline: DXMT `150f7a81`, local SDK files inspected on 2026-11-02. The
repository's `include/metal_shader_converter/ir_version.h` says 4.0.1; the
installed `/usr/local/include/metal_irconverter/ir_version.h` says 4.0.3. The
installed `metal-shaderconverter --version` reports 4.0.3. The loaded dylib
`/usr/local/lib/libmetalirconverter.dylib` has SHA-256
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`;
`otool -L` reports its Mach-O current and compatibility versions as 0.0.0.
The CLI version and installed header version do not independently establish the
loaded dylib's semantic version. The public installed headers expose no runtime
version query found in this audit. An exported C++ internal symbol with a version
name is not a documented public contract and was not called.

## Differences against the installed folders

- `metal_irconverter`: all paired enum/helper headers are byte-identical except
  `ir_version.h` (patch 1 -> 3). `metal_irconverter.h` has one substantive
  addition, `IRCompatibilityFlagEnsureTGMemoryCoherency = (1 << 10)`; the other
  differences are whitespace. Existing declarations and struct definitions
  match textually, which is useful evidence, not an ABI compatibility guarantee.
- `metal_irconverter_runtime`: `ir_raytracing.h` and
  `ir_tessellator_tables.h` differ in whitespace only (`diff -w`). The runtime
  header removes the `IR_RUNTIME_METAL4` types and four `IRRuntime4Draw*`
  declarations and their inline implementations, plus associated documentation;
  the non-Metal-4 code has no substantive differences in the reviewed diff.
  The current DXMT callers use the non-Metal-4 helpers; no `IRRuntime4` caller
  was found outside the vendored header. A wholesale replacement would still
  remove interfaces present in the repository.
- `metal_irconverter_ext/Metal_HLSL.inc` has no counterpart in the repository;
  no direct source use was found. It is not required merely to fix the log.
- The two installed `LICENSE.txt` files match the repository copy byte for byte.
  The installed folders also contain separate `Acknowledgements.rtf` files not
  present in this vendor directory. Review attribution and redistribution
  requirements when preparing a future header update.

## Upgrade assessment

A focused 4.0.3 compiler-header update is a plausible next slice: the observed
API delta is additive, but compile both architectures and test actual installed
and older supported runtime libraries, MSC reflection, conversion and cache
behavior before accepting it. `ir_version_*` in the bridge represents the
compiled header version, participates in the shader cache key, and its major
value is used by a capability gate; it is not proof of the loaded runtime's
version. Runtime compatibility continues to require symbol and behavior checks.

Do not replace the runtime helper header as part of that small update without
separately evaluating its removed Metal 4 API and geometry/tessellation, mesh,
raytracing, descriptor and residency behavior. Keep 4.0.1 headers for this
version-label fix; no capability, cache or runtime-version policy changes are
made here. Persistent MSC conversion caching is already disabled until the
loaded library identity is validated (`src/d3d12/d3d12_shader_converter.cpp`).

## 4.0.3 header integration (subsequent slice)

The compiler and extension headers were updated from the installed 4.0.3 SDK,
including `Metal_HLSL.inc` and the previously absent
`IRCompatibilityFlagEnsureTGMemoryCoherency`. `ir_raytracing.h` and
`ir_tessellator_tables.h` retain their prior whitespace because their tokens
match the installed files. The 4.0.3 runtime helper is used with the four
previous `IR_RUNTIME_METAL4` blocks preserved byte-for-byte: the installed
helper lacks these interfaces. The two installed acknowledgements files are
retained alongside the headers. The older recommendation above to keep the
4.0.1 headers is superseded by this integration.

The MSC conversion-cache SDK API component is derived from the compiled
`IR_VERSION_MAJOR`, `IR_VERSION_MINOR` and `IR_VERSION_PATCH` macros. That is a
header/API marker, not the dynamically loaded runtime's identity. Persistent
MSC conversion caching remains disabled pending reliable runtime identity.
Both loader and capability logs explicitly distinguish compiled SDK API from
the unknown runtime product version. No converter flags or advertised
capabilities were changed by this update.

## Threadgroup-memory coherency flag assessment

The installed Apple `man metal-shaderconverter` documents
`--ensureTGMemoryCoherency`: it inserts barriers to make threadgroup memory
accesses visible in program order to other lanes in the same SIMD group, for
compute, mesh, amplification and node shaders targeting GPU families older
than Apple9; Apple9 and later provide that ordering in hardware. The matching
API flag is `IRCompatibilityFlagEnsureTGMemoryCoherency`. This suggests a
potential compatibility improvement for affected pre-Apple9 shader workloads,
but does not establish that any particular DXMT workload is fixed, nor replace
HLSL's explicit group synchronization or cross-threadgroup synchronization.
Additional barriers may have a performance cost; none was measured here.

DXMT currently passes the texture-min-LOD compatibility flag via
`compiler_compatibility_flags` and includes those flags in the conversion key
(`src/d3d12/d3d12_msc_capabilities.cpp`,
`src/d3d12/d3d12_shader_converter.cpp:295`,
`src/winemetal/unix/metalirconverter.c:252`). The new bit is not enabled.
Existing `compute_sm6_atomic64.hlsl` uses `groupshared` with an explicit
`GroupMemoryBarrierWithGroupSync`; `compute_sm6_wave_ops.hlsl` exercises wave
operations but does not by itself isolate this flag's memory-order behavior.
Neither fixture proves a flag-on/flag-off difference. Before enabling the flag,
use a valid, reference-checked same-SIMD threadgroup-memory workload, compare
flag-on and flag-off against native Windows, inspect converted AIR, validate on
pre-Apple9 and Apple9-or-later hardware, and measure performance. Check older
MSC runtimes and OS/GPU targeting rather than assuming unknown flags are safe.

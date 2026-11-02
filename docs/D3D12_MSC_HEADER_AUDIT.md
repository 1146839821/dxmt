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

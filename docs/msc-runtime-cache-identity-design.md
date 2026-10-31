# MSC runtime identity for persistent shader cache

## Gate 0: baseline and evidence

At `2dfc8c30`, `DXMTMSCGetCapabilities` reports the `IR_VERSION_*` macros
compiled into WineMetal (`src/winemetal/unix/metalirconverter.c`), while
`MakeMSCConversionCacheKey` hashes those values and the optional-symbol mask
(`src/d3d12/d3d12_shader_converter.cpp`). Two different loaded libraries can
therefore produce the same key. No stale-cache GPU failure has yet been
reproduced. The n84 log identifies the loaded image UUID as
`A5CC3BD5-D1D3-3B9F-A9C4-DF5DF8E0CEBC`; `dwarfdump --uuid` currently reports
the same UUID for the x86_64 slice of the path in that log. This matches an
image build identifier, not a historical full-file hash.

`dxmt_msc_capabilities` is passed directly across the Wine PE/Unix boundary,
including WOW64. Extending it in place makes a newer Unix side potentially
write beyond an older PE caller's allocation. Its current layout and existing
call ordinal must be preserved. PE and Unix builds are expected to be paired,
but the new mechanism must not use an ABI overflow as a compatibility check.

## Gate 1: design card

- **Goal:** Persistent MSC conversions must not be reused across distinct
  loaded converter identities; inability to establish identity must disable
  persistent lookup and insertion without disabling shader conversion.
- **Scope:** MSC conversion cache only, its identity query, serialization key,
  and focused regression tests. In-process caching may remain enabled while a
  process retains one loaded converter.
- **Non-scope:** AIR rewrite integration, other shader caches, capability
  promotion, or changes to D3D10/11 behavior.
- **Boundary:** Preserve the existing capability ABI. Introduce a separately
  versioned, size-checked identity query with an appended Unix-call ordinal,
  or another mechanism proven safe for old PE/Unix pairs and WOW64. An absent,
  unsupported or inconsistent query must produce “identity unavailable”, not
  fabricated zero identity. Resolve optional PE exports safely if mixed versions
  are supported.
- **Identity:** Identify the actually loaded image, including its architecture.
  UUID alone is insufficient to establish unique contents. Hashing only a path
  after `dlopen` is vulnerable to replacement and may describe a different file
  than the mapped image. The implementation must establish a defensible binding
  between the loaded image and any digest, account for fat binaries, and define
  replacement and read-error behavior before turning persistent reuse on.
  If the binding cannot be established, fail closed for persistent caching.
- **Key and migration:** Include an explicitly versioned identity in the
  conversion cache key and bump its namespace/version. Check availability before
  both disk read and disk write. Keep all existing shader, root, compiler,
  device and OS dimensions.
- **Tests:** Two distinct converter images with the same exported symbols and
  header version must not share persistent entries. Same identity must hit;
  missing/invalid identity and mismatched PE/Unix support must never hit or
  insert. Include concurrent/repeated calls, x86_64/arm64 identity and WOW64
  layout checks, cache migration, and normal shader output. Verify actual
  loaded paths, identities and paired WineMetal PE/Unix builds using an
  isolated Wine prefix and cache with Metal validation.
- **Risk:** File hashing races, unsigned or modified binaries, loader aliases,
  per-architecture universal binaries, unavailable old entry points, and
  accidental disabling of valid cache hits. A UUID or file path by itself is
  not a sufficient proof of loaded bytes.
- **Rollback:** Revert the new query and key changes as one focused commit;
  preserve existing persistent cache records and user settings.
- **Done:** Focused unit and isolated runtime tests pass, both full build
  configurations pass, changed bridge ABI is reviewed, and no capability claim
  is promoted.

## Gate 2 status

Implementation is not yet selected: a loaded-image-to-content binding that
survives replacement and works with the available Darwin loader must first be
verified. Proceeding with a path hash or UUID alone would leave the original
identity gap in another form. The next implementation step is a small Darwin
identity probe and adversarial replacement tests, followed by the bridge and
cache change once the probe meets the acceptance conditions above.

## Loaded-image replacement probe

`tests/airconv/test_darwin_loaded_image_identity.py` builds two test-only native
dylibs and a process that reports the mapped image's UUID and calls its
exported function. In an isolated temporary directory it atomically replaces
the loaded pathname, then confirms that the existing process still executes the
original mapping while the path has different bytes. It repeats the replacement
with the second dylib's UUID deliberately changed to match the first; the second
image is ad-hoc signed and independently loaded before replacement. Thus the
control demonstrates two loadable, different local binaries with the same UUID.

This probe passed locally on macOS 27.0.1 under x86_64/Rosetta and native arm64. It demonstrates
why path-only hashing and UUID-only keys are insufficient; it does not establish
a safe way to bind a full file digest to the actual loaded image, and it does
not test a production MSC library or the D3D12 cache. Concurrent replacement and loaded-content hashing remain unverified.

## Mapped `__TEXT` fingerprint control

The probe also computes SHA-256 over the loaded `__TEXT` file-backed bytes.
For the test libraries this fingerprint remained stable across processes and
pathname replacement and distinguished changed executable code. However, a
second control modifies an initialized `__DATA` value, preserves the UUID,
and re-signs the image. Both binaries load and return different values while
their mapped `__TEXT` fingerprints match. This control passed on x86_64/Rosetta
and native arm64. A `__TEXT` digest, even combined with UUID, therefore does
not establish full converter identity; no production key should use it alone.
These are synthetic libraries, not evidence about MSC's specific data layout.

## Temporary fail-closed guard

Until a validated runtime identity exists, the MSC conversion path skips both
persistent cache lookup and insertion while keeping its process-local cache.
This can increase shader conversion work across process launches. The guard is
not a library-identity implementation and does not repair the known ICB
validation issue. Existing persistent records are left untouched.

A local isolated test with paired WineMetal artifacts and Metal API/GPU
validation used a seeded cache containing the earlier experimental ICB rewrite.
The normal fragment case passed; the vertex-ID case produced the previously
observed unpatched GPU validation failure. The seeded MSC table's keys and
value hashes were unchanged in both runs. The latter is a negative control, not
an application pass or by itself proof that the same key would have matched.
`tests/dx12/check_msc_cache_guard.py` checks recorded run identities, expected
outcomes and cache contents. The local logs are in
`dxmt-test/n87-msc-cache-guard/msc-lod-7r6_vwsr` and
`dxmt-test/n87-msc-cache-guard/msc-lod-p6r1ehey`.

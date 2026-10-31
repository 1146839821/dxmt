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

## Mapped code-signature experiment

Apple's [TN3126](https://developer.apple.com/documentation/technotes/tn3126-inside-code-signing-hashes)
describes the Code Directory's page hashes and separate per-architecture
signatures. The test-only `darwin_mapped_signature_probe.c` checks whether the
`LC_CODE_SIGNATURE` range is readable in the loaded `__LINKEDIT` mapping and
hashes that mapped range. It does not parse or verify the signature and is not
a production identity API.

On the current machine `/usr/local/lib/libmetalirconverter.dylib` exposed a
readable embedded-signature range in both architectures. Two independent loads
per architecture yielded the same range hash: x86_64
`e04105fce5839eaad91ed35889e4abdf2e6acc4ad7d1c8c36103d0001c5acb07`;
arm64 `0f24d6ea538604ef61008e429d14cc61ebe73b62f081757a82c7d42cdc20afc1`.
The synthetic test also found distinct mapped signature hashes for the
re-signed initialized-data variants with identical UUID and `__TEXT` hash;
the original mapped signature hash remained stable after pathname replacement.
These are observations, not proof that all signed images are verified, that
all pages are covered, or that every dyld mapping exposes a signature. In
particular, unsupported, unsigned, shared-cache, malformed or unverifiable
images must remain fail-closed until a validated identity scheme is implemented.

## Invalid-signature negative control

Apple's [TN3126 per-page discussion](https://developer.apple.com/documentation/technotes/tn3126-inside-code-signing-hashes)
states that macOS does not always check code as it is paged in. The synthetic
probe now modifies a signed dylib's initialized data without re-signing.
`codesign --verify` rejects the modified file, yet on this local macOS 27.0.1
machine a separate process loaded it and observed the changed data under both
x86_64/Rosetta and native arm64. The loaded UUID, mapped `__TEXT` hash and
mapped signature hash all matched the original. Re-signing the modified image
changed the signature hash as expected.

This is a local observation for the test process and libraries, not a claim
about every entitlement, macOS release or MSC binary. It rules out relying on
mapped signature bytes alone without separately validating their relationship
to the actual loaded code and initialized data. The production persistent-cache
guard remains enabled.

## Mapping vnode identity experiment

On the local macOS 27.0.1 machine, `proc_pidinfo` with
`PROC_PIDREGIONPATHINFO` returned a device and inode for the loaded test
libraries and for the actual MSC image. In the atomic replacement controls,
the mapped region retained the original device/inode while `stat` on the
original path reported the replacement inode. This passed under x86_64/Rosetta
and native arm64. It is a useful replacement check, not a content identity.

A further control modified a loaded test file in place: the on-disk hash
changed while the device/inode remained unchanged; the running test process
continued to return the old mapped value on both local architectures. This
shows that even a matching pathname and mapping inode does not establish that
subsequently read file bytes represent the already loaded contents. The test
does not prove the behavior of all filesystems, shared-cache images, process
security policies, or concurrent modifications. Persistent MSC caching remains
disabled pending a complete, validated binding strategy.

## Pre-opened descriptor experiment

The test-only `darwin_pinned_image_probe.c` opens the candidate before `dlopen`,
checks file metadata before and after hashing, then compares the opened file's
device/inode with the loaded image's mapped vnode. The probe rechecks metadata
and the full-file SHA-256 after loading. In controlled local tests, stable
files and a symlink alias bound successfully; replacement between open and
`dlopen` failed binding; an in-place data edit after loading was detected even
after restoring mtime. These tests passed under x86_64/Rosetta and native
arm64. The current installed MSC dylib also produced `BOUND 1` and `STABLE 1`
for both architectures, with full-file hash
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.

This is evidence for a candidate approach, not a race-free proof. Metadata
semantics and inode reuse, filesystem behavior, modifications during or after
validation, dyld aliases/shared-cache images, unsigned or inaccessible files,
dependencies, and PE/Unix mixed-version compatibility still need explicit
handling. The prototype hashes the entire universal file, not only the active
slice. It does not expose an identity to the production cache and the existing
fail-closed guard remains in effect.

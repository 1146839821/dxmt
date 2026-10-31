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

### Concurrent mutation and unavailable identity controls

The pre-open test has a synchronization point after its first hash read. On the
local APFS test directory, a second process modified the file at that point
and restored its mtime; both x86_64/Rosetta and arm64 probes rejected the
snapshot before emitting an identity. Missing files, non-regular inputs and a
missing symbol also produced no accepted binding. These are controlled cases,
not a proof that filesystem timestamps are a universal concurrency primitive.
The test cannot rule out content changes after the last validation or establish
immutable loaded pages for the converter's entire lifetime. Production
persistent caching remains fail-closed.

## Candidate native component

`src/winemetal/unix/msc_image_identity.{h,c}` extracts the pre-open, full-file
hash, vnode comparison and revalidation logic into a candidate component.
It is not linked to WineMetal or the MSC loader and cannot enable the persistent
cache. Its open operation requires a regular readable file, hashes from a pinned
file descriptor with metadata checks on both sides, and leaves failures
unavailable; its separate mapping and revalidation calls are exercised by
`tests/airconv/test_msc_image_identity_component.py`.

The local component tests cover normal binding, atomic replacement before
loading, in-place mutation after binding (including restored mtime), invalid
paths and repeated close on x86_64/Rosetta and arm64. A manually invoked driver
also bound and revalidated the current installed MSC dylib on both
architectures. These tests do not prove atomicity over a converter's lifetime,
identify transitive dependencies, or validate mixed PE/Unix versions; the
helper must not be interpreted as sufficient to enable cache reuse yet.

### Candidate ownership contract

The candidate object must be initialized using
`DXMT_MSC_IMAGE_IDENTITY_INIT`; it owns one file descriptor and cannot be
copied while open. Calls on the same object, including close, require external
serialization. Opening an already-open object is rejected without losing its
descriptor. The candidate uses nonblocking open and rejects non-regular files,
so a FIFO cannot block the caller before the type check. A failed revalidation
invalidates the binding until close and a fresh open. These are component
contracts, not evidence that a bridged runtime or persistent cache is safe.

## Versioned bridge skeleton

`DXMTMSCGetIdentityV1` is a new, pointer-free, 48-byte PE/Unix request. The
new PE side checks the existing capability response for a dedicated bridge
feature bit before calling the appended Unix ordinal; a Unix bridge without the
bit is treated as unavailable. The Unix side validates size, version and
reserved fields. Both x64 and WOW64 dispatch tables have compile-time ordinal
checks. The current implementation intentionally returns unavailable and a
zero digest: it does not expose the candidate helper or enable disk caching.

In an isolated local Wine 11.0 stage, tests passed for new PE/new Unix, new
PE/old Unix, and the old PE capability query against new Unix. The new x64 and
32-bit PE export bytes were compared against the built files; the WOW64 run
used the x86_64 Unix bridge. These observations cover the tested artifacts,
not every historical or future version combination. WineMetal PE and Unix
libraries should normally be deployed as a matched pair.

## Opt-in runtime diagnostic integration

The native identity component is linked into WineMetal. Only when
`DXMT_TEST_MSC_IDENTITY_SNAPSHOT=1` and an absolute
`DXMT_METALIRCONVERTER_PATH` is supplied does the loader pre-open and hash the
candidate; after `dlopen` it compares the mapping and revalidates. The V1
query serializes diagnostic revalidation through a mutex, closes an invalid
snapshot, and still returns unavailable with a zero digest. Normal loading does
not perform the snapshot. The diagnostic deliberately does not establish a
cache-safe identity or enable persistent caching.

`tests/dx12/run_msc_identity_diagnostic.py` copies a converter to a new isolated
output directory, checks stage/build hashes and loaded export bytes, then tests
stable and changed-metadata cases. A local Wine run passed both, including
repeated queries; a separate normal fragment GPU run passed with diagnostics
disabled. This covers one paired x64 WineMetal build and one local converter,
not concurrent modification across the converter's lifetime or other loaders.

### Diagnostic concurrency and failed-loader controls

The local x64 Wine diagnostic runner starts four threads together before the
first capability query; eight V1 calls returned unavailable, one snapshot was
created, and eight serialized revalidations were observed. Separate runs tested
an invalid selected dylib followed by fallback, and a loadable dylib missing
required MSC symbols. The former retained no candidate identity; the latter
returned unavailable. While the failed-symbol process remained alive, `lsof`
showed no open descriptor for the rejected dylib. A positive control confirmed
that the same descriptor inspection observed the retained snapshot in the
normal case. The tests check loaded bridge export bytes and Unix library path.

These are isolated local observations, not a proof of race freedom, fd handling
on every failure path, or production identity safety. The diagnostic remains
opt-in and the persistent cache remains disabled. The local run's artifacts
are under `dxmt-test/n89-identity-diag/concurrency3`.

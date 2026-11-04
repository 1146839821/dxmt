# MSC runtime identity for persistent shader cache

Current status at `a9cf396efcb591a1ed47e21e88f6219ff27dd60e`: the snapshot
helper and V1 bridge already exist, including opt-in loader diagnostics.
V1 still returns unavailable and the MSC disk-cache guard remains false.
The sections below record successive experiments, not an unimplemented task
list. See [the macOS 27.2 refresh](#macos-272-revalidation--2026-11-04) for
current source evidence and remaining acceptance conditions.

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
It was initially standalone and is now linked into WineMetal for the opt-in
loader diagnostic described below; it cannot enable the persistent cache.
Its open operation requires a regular readable file, hashes from a pinned
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

### Dependency-only replacement control

`test_msc_identity_dependencies.py` builds a primary dylib whose exported
function calls a second dylib, then replaces only that dependency between fresh
processes. On local x86_64/Rosetta and arm64 runs, the function returned 11
then 22 while the primary file SHA-256, UUID, mapped `__TEXT` hash and mapped
signature hash stayed equal. The candidate primary-image component reported
both snapshots bound and stable. Thus a validated primary image alone cannot
identify arbitrary dynamically linked behavior.

`otool -L` on the currently installed MSC dylib lists libbz2, CoreFoundation,
libarchive, libc++ and libSystem for both slices. This is a static dependency
observation, not a complete record of runtime-loaded libraries or evidence
that any of these system dependencies changed. The cache key already includes
OS information, but no test here establishes that OS version alone fully
identifies the dependency closure. The diagnostic V1 response and persistent
cache remain unavailable pending a defensible scope for those dependencies.

### Symbols resolved from a dependency

The dependency regression also loads a root dylib that does not export
`marker` itself. On both local architectures, `dlsym` resolved `marker` from
the dependency and the candidate root identity rejected the provider mapping
(`BOUND 0`, `STABLE 0`). In a separate x64 Wine diagnostic control, an isolated
wrapper linked to the local MSC library supplied the bridge's required symbols
through that dependency. Both images loaded, while the root snapshot and V1
query remained unavailable. The reproducible diagnostic runner now includes
this control. This checks the provider mismatch path, not the identity of all
transitive or late-loaded dependencies.

### Candidate dyld event epoch

`msc_image_epoch.{h,c}` is an unintegrated process-wide change detector. It
registers the remove callback before the add callback, increments a lock-free
atomic counter in callbacks, and makes snapshots unavailable if the counter
saturates. The containing image must remain loaded for the process lifetime:
dyld callback registration has no corresponding unregister API in this design.
Apple's [dyld(3) documentation](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/dyld.3.html)
notes that iterating the image count is not thread-safe; this candidate avoids
using that iteration as its event detector.

Synthetic local tests passed on x86_64/Rosetta and arm64 for concurrent
registration, load, unload, concurrent image activity and saturation. An
epoch is neither a dependency list nor a content identity: it may invalidate
for unrelated images, cannot detect in-place file modifications, and has not
been proven to form an atomic transaction with MSC conversion or cache I/O.
An isolated test also appends a byte to a loaded fixture and observes that the
epoch remains unchanged; it does not establish how mapped pages behave.
It is not linked into WineMetal and does not change the persistent-cache guard.

### Direct system dependency file-identity probe

`test_msc_system_dependencies.py` attempts to load selected system libraries,
then separately applies the candidate file-identity probe. A generated
file-backed dylib is the positive binding and revalidation control. The test
does not require an MSC installation; the optional
`DXMT_TEST_MSC_SYSTEM_DEPENDENCY_ROOT` loads a specified converter first.

On macOS 27.0.1 (26A434), with the installed MSC dylib loaded in the test
process, both native arm64 and x86_64/Rosetta loaded the five direct system
dependencies listed by `otool -L`: libbz2, CoreFoundation, libarchive,
libc++, and libSystem. Their listed paths did not name regular files, and
the candidate helper rejected all five at open. This is a local observation;
the test does not assert that those paths are unavailable on every OS version,
identify the transitive dependency closure, or establish which loaded image
provides each converter symbol. It shows that hashing only openable dylib
paths cannot describe the actual dependency set in this environment. The
shared-cache provenance and lifetime, dynamic loading, and in-place mutation
remain unverified; persistent MSC caching stays disabled.

### Shared-cache observations and architecture control

The system-dependency probe now asks `_dyld_shared_cache_contains_path` about
each listed path and attempts to obtain the actual dlopen image header's
region filename. On this machine both architectures reported all five paths
in the active shared cache, while `proc_regionfilename` returned no filename
for those headers. The path query alone does not prove which image supplies a
particular binding; Apple notes that [cached libraries may be overridden](https://github.com/apple-oss-distributions/dyld/blob/main/include/objc-shared-cache.h).

A separate single-threaded test reads `sharedCacheUUID` from the current
process's `TASK_DYLD_INFO`, checking the reported structure version, size and
detached state. Before and after loading a generated file-backed library, two
fresh runs agreed locally: x86_64/Rosetta
`16939cfbccad3a9b926e5cc5b2d2cb56`; arm64
`c284a291b68b3d0da1a03f866f87efdc`. This is a process metadata observation,
not a guarantee of a full content digest, dependency provenance or stable
identity throughout conversion. Apple describes the shared cache as potentially
multiple files in its [cache layout documentation](https://github.com/apple-oss-distributions/dyld/blob/main/doc/CacheLayout.md).

Earlier native test drivers did not explicitly pass `-arch` to clang, so a
Rosetta-hosted Python process alone was insufficient evidence that its child
executables used x86_64. The identity, pinned-file, dependency and event-epoch
test runners now select the Python process architecture explicitly; all five
were rerun successfully for both architectures on this machine. The broader
historical observations about separately built binaries remain limited to their
recorded artifact identities.

### Shared-cache membership versus actual provider

`test_msc_cache_provider.py` launches a native test executable twice with a
cleaned dyld environment. It resolves `BZ2_bzlibVersion` from the libbz2 system
path, then repeats with a separately built fixture in `DYLD_LIBRARY_PATH`.
It observes the symbol provider with `dladdr` and calls the symbol. On local
macOS 27.0.1, both x86_64/Rosetta and arm64 reported the system path as a
shared-cache member in both runs; the second process instead resolved the
symbol from the fixture and returned its distinct sentinel. The fixture and
environment exist only in that child test process. Systems whose loader policy
ignores the override report SKIP, not PASS for the override case.

This confirms the path-membership API cannot on its own classify the actual
provider, even with a shared-cache UUID. It does not identify all functions
used by MSC, test an MSC conversion with the fixture, or validate cache bytes.
Apple's [dynamic library usage guide](https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/100-Articles/DynamicLibraryUsageGuidelines.html)
describes search-path overrides; actual behavior depends on process policy.
The production persistent-cache guard remains in place.

### Mixed MSC entry-point providers

The opt-in root-image diagnostic previously checked only the provider of
`IRCompilerCreate`. A wrapper defining that function while resolving
`IRCompilerDestroy` and other functions from the real converter was reported
as `bound` and `stable` in an isolated x86_64 Wine test. The diagnostic now
checks each successfully resolved required or optional MSC entry point against
the pinned root mapping. A mismatch or unidentifiable provider closes the
snapshot; converter availability is not changed. The runner tests both the
mixed case and the earlier all-forwarded case, as well as a normal converter,
mutation, concurrency, fallback and descriptor cleanup.

The mixed-provider control failed with the old bridge and passed with the new
one in `dxmt-test/n91-mixed-provider/{before,after}` using a fresh Wine prefix.
The runner verifies the PE export bytes, PE/Unix artifact hashes, and loaded
Unix bridge path. This is a check of exported API entry-point providers, not of
the internal dependencies or late loads of the converter. The V1 identity
query still reports unavailable and MSC persistent caching remains disabled.

### Real conversion event observation

`test_msc_conversion_epoch.py` is an opt-in standalone native probe, enabled
with `DXMT_TEST_MSC_RUNTIME_IMAGE` and `DXMT_TEST_MSC_DXIL`. It explicitly
compiles for the Python process architecture, checks the converter's loaded
path and file hash, requires a nonempty metallib, and rejects an invalid DXIL
control. A test-only wrapper performs a deliberate late `dlopen` during a real
MSC compile; the event counter must increase for that positive control.

On local macOS 27.0.1, using converter SHA-256
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4` and
fragment DXIL SHA-256
`62217eddf6f94fddd38ef8086768dd64ae5b87443d53267f8db9b08320ef7695`,
both x86_64/Rosetta and arm64 produced a 4,828-byte metallib. The observed
epoch did not change between the post-load snapshot and completion in that
sample; the wrapper control increased it during conversion on both. This does
not cover other shader stages, configurations, threads, in-place modifications
or the DXMT/Wine conversion path, and does not identify the dependency closure.
The component remains unintegrated and persistent MSC caching remains disabled.

### DXMT/Wine conversion trace

`run_msc_conversion_trace.py` uses a test-only forwarding dylib to mark calls
to the real MSC compiler within an isolated Wine/DXMT D3D12 fragment test.
It verifies the paired WineMetal artifacts, loaded PE export bytes and Unix
library path, and requires Metal API and shader validation and exact readback.
A deliberately wrong expected-output control must fail as expected. The test
also runs a separate process whose wrapper loads an unrelated test dylib inside
the marked conversion window; the dyld load log must show that image there.
For the native D3D12/DXGI override, the test checks that the staged binaries
differ from the full-build artifacts only in the 16-byte Wine builtin DOS-stub
marker. No built artifact or user Wine installation is modified.

On the local Apple M1 Max, Wine 11.0 x86_64/Rosetta and macOS 27.0.1,
four successful conversions were observed in each of the baseline and
late-load runs; the baseline emitted no dyld load lines between the markers,
and the late-load control emitted the expected image. Both fragment readback
and its negative control passed under validation. Artifacts, hashes and logs
are in `dxmt-test/n92-conversion-trace/results3`. The wrapper means this is
not an uninstrumented converter; the log is process-wide, can interleave
threads, does not cover unloads or in-place changes, and does not establish a
dependency identity or atomic cache transaction. The cache guard is unchanged.

## macOS 27.2 revalidation — 2026-11-04

### Scope and design card

- **Goal:** Resolve the earlier missing-Metal-toolchain build limitation and
  distinguish the existing diagnostic from a usable persistent-cache identity.
- **Baseline:** `feat/d3d12`, `a9cf396efcb591a1ed47e21e88f6219ff27dd60e`,
  46 commits ahead of the locally stored `origin/feat/d3d12` reference. The
  user's pre-existing `.gitignore` edit is excluded; no remote fetch or push
  was performed for this validation slice.
- **Evidence:** Current source paths below, fresh isolated build logs, explicit
  x86_64/arm64 native probes, and paired x64 WineMetal runtime tests. Historical
  macOS 27.0.1 observations above retain their original scope.
- **Scope / boundary:** Documentation and two explanatory helper comments;
  reuse the existing loader, V1 bridge and regression runners. No new identity
  algorithm, bridge ABI, disk-cache access, shader policy or capability change.
- **Plan:** Build current source with the existing candidate LLVM, compile all
  Metal inputs normally, refresh identity controls on both architectures, then
  verify a new Wine stage and prefix with API/shader validation.
- **Risk / rollback:** The tests observe selected inputs and do not prove
  lifetime immutability or all dependency bindings. Reverting this documentation
  slice changes no runtime behavior. Existing user build and Wine installations
  remain intact.
- **Done:** Build and focused controls pass with retained commands, artifact
  hashes and loaded-path evidence; unresolved cache gates stay explicit.

### Current implementation

| Evidence type | Source at the baseline | Confirmed behavior and limit |
| --- | --- | --- |
| SOURCE_AUDIT | `src/winemetal/unix/meson.build:5`; `src/winemetal/unix/metalirconverter.c:598` | The helper is linked and activated only by `DXMT_TEST_MSC_IDENTITY_SNAPSHOT=1`; pre-open requires an explicit absolute converter path. |
| SOURCE_AUDIT | `src/winemetal/unix/msc_image_identity.c:40`, `:63`, `:80` | Pinned-file hash, mapping vnode comparison and revalidation exist. None guarantees unchanged mapped contents for a conversion's lifetime. |
| SOURCE_AUDIT | `src/winemetal/unix/metalirconverter.c:347`, `:359`, `:371` | Every resolved required/optional API entry point is checked against the root mapping in diagnostic mode. Internal bindings and late loads are not enumerated. |
| SOURCE_AUDIT | `src/winemetal/metalirconverter_thunks.h:135`; `src/winemetal/metalirconverter_thunks.c:37`; `src/winemetal/unix/winemetal_unix.c:2589` | V1 is a size/version/reserved-checked, pointer-free 48-byte request with a capability-bit guard. The Unix implementation clears the digest and reports unavailable. |
| SOURCE_AUDIT | `src/d3d12/d3d12_shader_converter.cpp:208`, `:1870`, `:1918` | Persistent read and write are both disabled. The process-local cache is separate. |
| SOURCE_AUDIT | `src/winemetal/unix/msc_image_epoch.c`; `src/winemetal/unix/meson.build:1` | The event counter remains a standalone experiment, not a WineMetal dependency or a cache transaction. |

The installed Xcode SDK's `usr/include/mach-o/dyld.h:68` documents add/remove
callbacks. `usr/include/mach-o/dyld_images.h:52` says the image array may be
unavailable while being changed; `:164` declares `sharedCacheUUID`. These are
image-event and process-metadata interfaces, not a guarantee of an immutable
compiler/dependency closure. This slice changes no Microsoft D3D12 contract;
Mullion and Windows GPU behavior are not substitutes for these Darwin loader
observations and were not retested.

### Fresh build and native results

`LOCAL_PASS`: macOS 27.2 (26B123), Xcode 27.2 (27B52), Apple M1 Max.
`xcodebuild -showComponent MetalToolchain` reports installed build 27B47,
identifier `com.apple.dt.toolchain.Metal.32023.922.4`.
`xcrun metal --version` reports 32023.922; `metallib` resolves within the same
27.2 toolchain. Developer Documentation asset versions were user-reported;
their catalog contents were not independently validated by this run.

Under `/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/`:

- New build: `dxmt-metal272-identity-audit-20261104/`.
- Existing patched LLVM: `llvm-current-patches-20261103/install/`.
- Evidence: `result-msc-identity-refresh-20261104/`.
- New isolated stage/prefix: `stage-msc-identity-refresh-20261104/` and
  `prefix-msc-identity-refresh-20261104/`.

Setup follows `.github/build_dxmt.sh`'s x64 release options, enabling NVAPI,
NVNGX and D3D12, adding tests, and selecting the existing patched LLVM
explicitly. `setup.log`, `build.log` and `build-manifest.json` record the exact
commands and exit statuses. All 388 default build steps completed (exit 0),
including seven real `xcrun metal` compilations and one `xcrun metallib` link.
No AIR/metallib files were copied from another build and no generated Ninja
rules were edited. The log also records 351 warning diagnostics.
`llvm-metadata-selftest` passed
the valid string record and both rejected variants. A final incremental build
after the comment edits also passed. The x86 build was `NOT_RUN` this round.

`native-results.json` contains all commands, architecture, exit status and
output for eight existing runners on each of x86_64 and arm64: loaded-image,
pinned-file, component, dependency, system-dependency, provider-override,
image-epoch and real-conversion tests. All 16 passed; none skipped. Each
launcher uses `/usr/bin/arch -<architecture> /usr/bin/python3`, verifies
`platform.machine()`, and the runners pass that architecture to clang.

Both architectures still load all five listed MSC system dependencies while
their paths are unavailable as regular files. The observed shared-cache UUIDs
were `c1c7d64c41c73219bb29a98228fb1d3e` (x86_64) and
`b411828b445038e3973aafe7b7b5bef5` (arm64), different from the historical
27.0.1 observations. The isolated provider override still changes the resolved
function without changing the queried path's cache membership.

The converter full-file SHA-256 remains
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.
Fragment DXIL SHA-256
`f80ad457898b9997e86325eb8823eb784f66ca851895c62618b8f7adc80d9c0c`
produced 5,244-byte metallibs on both architectures. The baseline event counter
stayed unchanged during that conversion; deliberate late loading advanced it
on both. Invalid DXIL was rejected. This is one fragment fixture, not all
stages/configurations or proof that dependency behavior stayed constant.

### Wine runtime results and remaining gates

`LOCAL_PASS`: Wine 11.0 x64/Rosetta, eight diagnostic groups: stable snapshot,
metadata change, concurrent initialization/queries, invalid-path fallback,
missing symbols, dependency-provided symbols, mixed providers and descriptor
cleanup. `diagnostic/summary.json` records them. The runner checks PE export
bytes, paired PE/Unix hashes and the loaded Unix path. V1 remained unavailable.

`LOCAL_PASS`: `conversion-trace/summary.json` records baseline and deliberate
late-load runs. Each observed two successful conversions in its positive
fragment run; the baseline had no dyld load messages within the marked windows,
and the injected control had the expected image. Exact fragment readback and
the wrong-expected-output control passed with Metal API/shader validation in
both modes. The wrapper is test-only and the dyld log is process-wide; neither
establishes dependency closure or catches all in-place mutations. The paired
WineMetal Unix SHA-256 is
`c9e20da27632144651fd744d01ddb26445800ea4baa1fbd36a0257188f1fbd8c`.

| Persistent-cache acceptance gate | Current status | Required evidence before enabling reuse |
| --- | --- | --- |
| Identify all effective compiler providers | BLOCKED_BY_ARCHITECTURE | Define supported dependency/override policy and attest actual providers, architecture and shared-cache provenance; root hash or path membership alone is insufficient. |
| Bind identity to conversion and cache access | UNVERIFIED | A specified transaction covering lookup, compilation and publication, with file mutation and load/unload controls. Event equality alone does not cover in-place writes. |
| V1 usable digest and namespace migration | NOT_RUN | Only after identity is defensible: same identity hits, different runtime/dependency/configuration misses, old namespace is rejected, unavailable identity never reads or writes. |
| Platform and shader coverage | NOT_RUN | Revalidate WOW64/mixed bridge pairs, other shader stages and concurrent cache operations for the eventual implementation. Current native tests do not establish arm64 Wine support. |

**Gate 0/1:** Existing implementation and remaining gaps verified; no duplicate
identity component is proposed. **Gate 2:** Documentation/comments only.
**Gate 3:** Fresh build, native controls and isolated x64 runtime checks pass.
**Gate 4:** No behavioral or public Feature Level/Shader Model/Tier change;
persistent MSC caching stays disabled. GPU occupancy, bandwidth, execution
overlap, representative performance/memory and RT work remain incomplete.

**Next recommended slice:** Return to the specification's P1 work and establish
a controlled GPU execution-overlap baseline with correct-output controls and
explicitly reported unavailable counters. P2 identity research must not delay
P0/P1 validation or be described as completed persistent caching.

# Typed-origin recipe identity

## Task Analysis

### Current Branch

`feat/d3d12-1`, clean start.

### Baseline

Task `3defaeb`; remote `origin/feat/d3d12`, merge base `85bb2dd`.

### Local Commits Since origin/feat/d3d12

Root-layout audit `dd4caf9`, bounded owned recipe `3defaeb`.

### Current State

Recipe has owned pointers, not a stable identity. Production remains aligned-only.

### Existing Implementation

Production conversion key already includes shader/root/target/compiler versions.
It does not consume the test-side recipe; do not replace its existing dimensions.

### Relevant Files

Tests-side recipe identity header/test header and existing native root probe.

### Existing Tests

38 recipe contracts, 24 native paired layouts and 12 wrong-offset controls.

### D3D12 Contract

Mapping/range flags/root identity affect compilation; live origin/count do not
belong in a shader cache key. Byte offsets still come from internal reflection.

### DXBC / AIRCONV Impact

None.

### DXIL / MSC Impact

Tests-only canonical recipe bytes, not production conversion/cache integration.

### Shared Runtime Impact

None; descriptor lifetime and rejection guards unchanged.

### Missing Pieces

Pointer-free field encoding, ABI/policy version dimensions and identity regressions.

### Risks

Hypothesis: explicit LE semantic fields are invariant to address/padding and
invalidate on ABI/mapping/range/sampler changes. Evidence: current recipe embeds
pointers; production cache hashes supplied shader/root/environment before hits.
Expected effect: deterministic bounded identity prerequisite. Risks: inactive
union bytes included, omitted sampler float bits, ambiguous variable lengths,
partial output on rejection, mistaking model hits for real converter cache proof.

### Minimal Implementation Plan

Bounded atomic serializer; magic/schema/length and policy/ABI fields; typed active
parameter payloads, ranges, mapping and all static sampler fields. Full-byte
model cache comparison, independent golden empty-root wire oracle, mutations.
No decoder/persistent artifact loader or production key/version change.

### Validation Plan

Both builds reconfigured; existing probe plus identity tests/Meson; extra ASan/
UBSan native run. Standards/Spec self-review before local commit, no push.

### Capability Impact

None; host identity is not production/GPU acceptance.

## Wire and identity contract

Tests-only schema 1 is fixed to RS1.1. A complete identity is the exact serialized
byte sequence, not a new hash algorithm. Fixed header is twelve LE uint32 words:
`TOR1`, schema, total byte length, recipe ABI, lowering policy, metadata ABI,
root flags, app cost, internal cost, parameter count, static sampler count,
mapping count. Each mapping has source/app index. Parameters retain original
order, type/visibility and ONLY their active payload. Tables encode range count
and each range's type/count/base register/space/flags/table offset in order.
Constants encode register/space/DWORD width; root descriptors register/space/flags.
All thirteen static sampler fields follow, with MipLODBias/MinLOD/MaxLOD encoded
as exact float32 bits. No floating arithmetic or padding bytes are used.

The independent empty-app fixture is 76 bytes: header plus one Hidden mapping
and CBV(b0/space1/flags0). This is a fixed golden sequence, not observed output
fed back as an oracle. Schema 1 does not deserialize or authorize persisted data.
Future readers must independently reject unknown versions, truncated/overlong
payloads and inconsistent/invalid recipes; schema dispatch cannot be inferred
from the current serializer's success.

Serializer assumes an immutable trusted owned recipe from the producer. It
checks bounded array counts/mapping indices before reads and builds into a local
8192-byte scratch buffer; destination and reported size update only on success.
Capacity is diagnostic policy, not a production artifact size guarantee. It does
not inspect arbitrary pointers or validate all root semantic combinations.

Identity intentionally excludes CPU pointers, inactive union bytes, unused fixed
array capacity, live GPU/table addresses, per-submit origin/count and Metal
reflection offsets. It includes all used mapping/range/sampler state and explicit
lowering/metadata version dimensions. Sampler float bit distinctions can cause
conservative extra misses (for example +0/-0); no broad semantic float
canonicalization is claimed. Reordering application parameters/ranges is NOT
canonicalized away because their order/index/table placement is significant.

This byte identity must eventually be appended to, not substituted for, the
production key's original/transformed shader/root identities and MSC compiler/
target/environment dimensions. The existing `MakeMSCConversionCacheKey` and
its memory/persistent lookup paths are unchanged. Shader CBV claim completeness
and descriptor lifetime are independent prerequisites, not solved by hashing.

## Task Result

### Branch

`feat/d3d12-1`.

### Baseline

`origin/feat/d3d12`; task baseline `3defaeb`, merge base `85bb2dd`.

### Local Commit

Reported at handoff; local only.

### Changed Files

This report, identity header, identity test header and existing native probe.
No production or build-graph changes; the existing optional probe includes tests.

### Implementation

Canonical little-endian semantic-field serializer with length/version framing
and atomic output. Model cache stores a complete byte identity and compares an
independently built recipe; no weak custom digest or struct-memory hash.

### DXBC / AIRCONV Impact

Unchanged; no cross-backend fallback.

### DXIL / MSC Impact

Host identity diagnostic only. Existing reflected-layout probe still runs with
the owned recipe; no shader rewriting, compilation or production cache injection.

### Shared Runtime Impact

Unchanged; aligned-view gates, RS1.0 compatibility, residency/barriers and
production cache versions unchanged. No DLL deployment or prefix mutation.

### Tests Added

38 identity contracts: same semantic recipe at different allocations/inactive
union bytes hits; ABI/lowering/metadata/mapping/constant/root-descriptor changes
miss. All six range and thirteen static sampler fields independently mutate and
must serialize SUCCESSFULLY to distinct bytes. A serialization rejection is not
counted as field-inclusion proof. Restoring mutations hits the original identity;
unused mapping/range/sampler capacity poison remains a hit. Independent empty
root golden bytes match. Undersized output, null recipe, bad mapping count/source/
indices, excessive ranges and unknown parameter failures leave output/size intact.
Field mutation tests are adversarial host identity tests, not acceptance of those
mutated root descriptions by a shader compiler or GPU.

### Tests Run

Both configurations reconfigured before compilation, without compiler warnings:

- Identity 38/38 per build.
- Existing recipe 38/38, native paired layouts 24/24 and twelve old-sampler-offset
  controls detected 12/12 per build.
- Meson 3/3 per build.
- Extra native Clang build with `-Wall -Wextra -Werror`, ASan/UBSan and MSC rpath:
  same 38 identity / 38 recipe / 24 layout / 12 controls pass; exit 0, no detected
  sanitizer diagnostics. Prebuilt MSC library itself is uninstrumented.

Receipts: each build's `recipe-identity-config.log`, `recipe-identity-native.log`,
`meson-logs/testlog.txt`; `build/recipe-identity-sanitized.log`.
Reproduce by reconfiguring BUILD, compiling `msc_root_layout_probe`, running
`BUILD/tests/dx12/msc_root_layout_probe`, then `meson test -C BUILD --print-errorlogs`.
Sanitized command:

```sh
/usr/bin/clang -Wall -Wextra -Werror -I/usr/local/include \
  tests/dx12/msc_root_layout_probe.c -L/usr/local/lib -lmetalirconverter \
  -Wl,-rpath,/usr/local/lib -fsanitize=address,undefined \
  -fno-omit-frame-pointer -g -o build/msc_recipe_identity_sanitized
UBSAN_OPTIONS=halt_on_error=1 build/msc_recipe_identity_sanitized
```

### Runtime Results

Host serialization/model comparison and native root-layout reflection only.
No fresh production cache hit/miss or GPU readback; no game/performance run.
Both builds use the same installed MSC runtime, not independent shader backends.

### Standards

Main-agent self-review: no hard documented-standard violation. Tests stay opt-in,
field encoders use named typed accesses and no object/pointer memcpy for identity.
Repetitive field writes are explicit wire schema, not a public reflection framework.

### Spec

Main-agent self-review: no confirmed bounded-scope defect. Checked field coverage,
active union isolation, LE/float bits, count arithmetic, output atomicity, golden
oracle and success-required mutation tests. Existing production key/environment
dimensions are not removed. Independent review remains NOT COMPLETE because
prior reviewers hit account usage limits; code-review skill's Standards/Spec axes
are main-agent fallback, not independent approval. Summary: Standards 0 hard
findings; Spec 0 confirmed defects. `git diff --check` passes. MSC binding skill
kept actual Metal offsets reflected rather than treating wire bytes as a TLAB.

### Known Limitations

No persisted decoder, root/shader pairing validation, real CBV claim inspector,
production cache hit/miss equivalence, cryptographic artifact integrity or GPU
semantic proof. Metadata lifetime/generation/coherent descriptor updates remain
separate. Trusted producer inputs required; this serializer cannot authorize
untrusted structs/pointers or unknown future schemas. Runtime origin/count is
excluded by API/structure design, not a new production replay test. Leak/OOM
fault injection and cross-platform float representation not validated here.

### Capability Status

PARTIAL host prerequisite; production eligibility UNVERIFIED. No typed-UAV
capability or unaligned-production-view enablement.

### Feature Level Impact

FL11_1 unchanged; FL12_0 unchanged; FL12_1 unchanged; Shader Model unchanged.

### Git Status

Only these four files committed; clean status verified at handoff.

### Push Status

NOT PUSHED.

### Next Recommended Task

Bounded schema decoder and persisted recipe validation: explicit version/length/
truncation/corruption rejection, rebuild through the producer, canonical byte
roundtrip and native reflected-layout equivalence. Keep production cache/views
closed until shader pairing and descriptor metadata lifecycle have their own proof.

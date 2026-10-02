# Bounded internal root recipe

## Task Analysis

### Current Branch

`feat/d3d12-1`, clean task start.

### Baseline

Task `dd4caf9`; remote `origin/feat/d3d12`, merge base `85bb2dd`.

### Local Commits Since origin/feat/d3d12

Offline origin series through `6be6948`; root-layout audit `dd4caf9`.

### Current State

Production remains aligned-only; root-layout proof exposed implicit sampler shift.

### Existing Implementation

Native diagnostic manually appends CBV. No production owned mapping recipe.

### Relevant Files

Tests-side recipe header and native root-layout probe; no production files changed.

### Existing Tests

24 native paired layouts, twelve wrong-offset controls, Meson parser/gate tests.

### D3D12 Contract

App indices/cost unchanged. Internal CBV requires two DWORDs and collision-free
CBV namespace. Constants share CBV namespace; SRV/UAV/sampler do not.

### DXBC / AIRCONV Impact

None; no backend fallback.

### DXIL / MSC Impact

Tests-only immutable owned root recipe; reflection remains source of offsets.

### Shared Runtime Impact

None; production views/rejection guards unchanged.

### Missing Pieces

Owned root clone, explicit app/hidden/static mapping, collision/budget rejection.

### Risks

Hypothesis: bounded recipe can own copied descriptors and separate implicit
sampler mapping while rejecting reserved CBV collisions and budget overflow.
Evidence: prior native reflection moves sampler entry after hidden CBV; existing
production staging still uses app indices. Expected effect: host prerequisite
validated, not production integration. Risks: shallow range pointers, unsigned
interval overflow, ignoring shader-only claims, source mutation, mapping hidden
CBV to app staging or accepting unsupported root flags/visibility.

### Minimal Implementation Plan

Fixed b0/space1, versioned bounded recipe, deep-owned parameters/ranges/samplers,
identity app mapping plus dedicated hidden/static entries. Accept trusted RS1.1
compute roots with ALL visibility, flags NONE, finite tables. Reject incomplete
shader CBV claim input, collisions, unsupported shapes and internal cost >64.
No DXIL parser/reflection replacement or production policy change.

### Validation Plan

Negative host contracts leave output untouched; positive clone mutation tests;
feed generated recipe to existing native MSC reflection and marshaling matrix.
Reconfigure both builds, run probe/Meson, self-review then local commit only.

### Capability Impact

None. Host recipe proof is not production/GPU semantic validation.

## Recipe contract

`msc_typed_origin_root_recipe.h` is deliberately tests-only and is not linked
into production. Its caller supplies a trusted decoded RS1.1 root and a COMPLETE
list of shader CBV register/space/count claims. The complete flag is a caller
contract, not a shader inspection result: this task provides no real DXIL
reflection-to-claims adapter. Existing text lowering still rejects original
CBVs/samplers; accepting a host root recipe does not extend that shader grammar.

Successful recipe owns all parameter, descriptor-range and static-sampler arrays;
its descriptor pointers refer into itself. Caller treats the const result as
immutable, keeps its address stable, and frees it only after borrowed MSC root/
compiler use ends. Do NOT copy or serialize the struct by value: pointers and
padding are not a stable cache identity. Failed construction leaves output
unchanged. ABI version 1 identifies this host prototype, not a production cache
version bump or persisted binary format.

Application parameters remain in original order. Mapping entries explicitly
distinguish `App(original index)`, `Hidden(UINT32_MAX)` and, when present,
`StaticSamplers(UINT32_MAX)`. The static sampler entry is after the INTERNAL
parameter count, never the original app count. Reflection of the generated root
supplies actual byte offsets. Application root bytecode, cost and parameter
indices are not overwritten. Root cost is computed separately from staging
qwords: table 1, root CBV/SRV/UAV 2, inline constants their DWORD count, static
samplers 0. App cost 62 may fit hidden cost 2; 63/64 and malformed huge constants
reject before unsigned addition. No capability follows from that fit.

Fixed reservation is b0/space1. Check both root and supplied shader CBV claims.
Root constants occupy the CBV namespace, finite CBV table ranges cover register
intervals, and SRV/UAV/sampler namespaces are distinct. Inputs with zero/unbounded
or wrapping intervals, missing arrays and excessive counts fail closed.
Root flags other than NONE, non-ALL visibility, local/direct-indexed roots and
RS1.0 inputs are outside THIS prototype; production RS1.0 deserialization and
its volatile-range conversion are unchanged. No alternate reservation search,
silent remap or shader-backend fallback is introduced.

Input caps are 64 app parameters, 64 total descriptor ranges, 64 static samplers
and 64 shader claims, with internal root-cost gate taking precedence over
allocation. Existing valid root flags/ranges are copied without rewriting;
range volatility and APPEND offsets are preserved. This is not a general root
validator: it trusts prior root decoding/validation for application self-overlap,
range offsets and other unrelated semantic constraints. Rejecting a malformed
or unsupported prototype input is not changing public D3D12 acceptance.

## Task Result

### Branch

`feat/d3d12-1`.

### Baseline

`origin/feat/d3d12`; task baseline `dd4caf9`.

### Local Commit

Reported at handoff; only scoped test-side recipe/probe/report changes.

### Changed Files

This report, recipe header, recipe-test header, and existing native root probe.
No build-graph change is needed: the existing optional probe includes both headers.

### Implementation

Owned bounded recipe construction with ABI version/cost/source mapping and atomic
failure. Existing probe now feeds generated roots into MSC rather than manually
appending a borrowed CBV. Original app roots are queried independently.

### DXBC / AIRCONV Impact

Unchanged; no production/compiler/backend fallback.

### DXIL / MSC Impact

Native host recipe and reflected layout only; no shader conversion/rewriting.

### Shared Runtime Impact

Unchanged. Descriptor lifetime, submission resolution, barriers and cache paths
not modified. No DLL deployment, Wine prefix change or production execution here.

### Tests Added

38 host contracts: exact budget boundary/overflow, root CBV/constants and table
collision, shader-only conflict, namespace/space separation, finite interval
overflow, unsupported roots/visibility, null/excessive arrays, complete claims,
empty app root, output preservation and owned-clone lifetime/mapping.
The clone test mutates caller parameter/range/sampler arrays after construction,
then successfully creates an MSC root from the retained recipe. Volatile range
flags and APPEND offset survive cloning. Native matrix verifies all app/hidden/
static source tags against 24 paired app/internal root layouts.

### Tests Run

- Normal/no-private freshly reconfigured before compiling optional probe.
- Each probe: host recipe 38/38, paired root layouts 24/24, old-app-sampler-offset
  negative controls detected 12/12. Each Meson suite 3/3.
- Extra native Clang build with `-Wall -Wextra -Werror`, ASan and UBSan: same
  38/24/12 contracts pass; exit 0, no detected sanitizer diagnostics. MSC itself
  is a prebuilt uninstrumented library. Leak coverage/fault-injected OOM not claimed.
- Initial sanitizer executable failed at dyld before test execution because its
  link omitted MSC rpath. Added `/usr/local/lib` rpath; rerun above passed. This
  was a diagnostic launch configuration failure, not a recipe result.

Receipts: `BUILD/root-recipe-native.log`, `BUILD/root-recipe-config.log`,
`BUILD/meson-logs/testlog.txt`, `build/root-recipe-sanitized.log`.
Reproduction: reconfigure each BUILD, compile `msc_root_layout_probe`, run
`BUILD/tests/dx12/msc_root_layout_probe`, then Meson test. Sanitized command:

```sh
/usr/bin/clang -Wall -Wextra -Werror -I/usr/local/include \
  tests/dx12/msc_root_layout_probe.c -L/usr/local/lib -lmetalirconverter \
  -Wl,-rpath,/usr/local/lib -fsanitize=address,undefined \
  -fno-omit-frame-pointer -g -o build/msc_root_recipe_sanitized
UBSAN_OPTIONS=halt_on_error=1 build/msc_root_recipe_sanitized
```

### Runtime Results

Host construction, memory safety and native MSC root-layout evidence only.
No shader compilation, Metal pipeline or new GPU readback. Both configurations
use the same installed MSC; they are separate builds, not independent backends.

### Standards

Main-agent self-review: no hard documented-standard violation. Tests-only bounded
header and optional probe reuse existing paths. Fixed caps are diagnostic policy,
not a production allocation abstraction. The recipe is not serialized by raw
struct bytes or exposed in public root API.

### Spec

Main-agent self-review: no confirmed bounded-scope defect. Checked output
atomicity, deep ownership, pre-add cost arithmetic, namespace/interval handling,
caller claim completeness boundary and static sampler source mapping. Negative
controls cannot overwrite host memory outside the reflected allocation.
Independent review remains NOT COMPLETE: prior reviewer attempts hit account
usage limits. Code-review skill's Standards/Spec axes are applied as main-agent
fallback, not independent approval. Summary: Standards 0 hard findings; Spec 0
confirmed defects. `git diff --check` passes. MSC binding skill kept all byte
offsets reflected and this proof separate from shader/GPU acceptance.

### Known Limitations

Not production enablement or a general root validator. True shader claim
inspection, embedded-root adaptation, validated transformed shader/root pairing,
metadata generations/residency/replay/barriers, cache-key/persisted recipe and
graphics/indirect/raytracing remain unimplemented/unverified. Const ownership is
a caller contract, not a fully encapsulated production API. No game/performance
acceptance or fresh production baseline run; prior production receipts not refreshed.

### Capability Status

PARTIAL host prerequisite; production eligibility UNVERIFIED. Unaligned
production MSC views remain fail-closed. No TypedUAVLoadAdditionalFormats change.

### Feature Level Impact

FL11_1 unchanged; FL12_0 unchanged; FL12_1 unchanged; Shader Model unchanged.

### Git Status

Only these four files committed; clean worktree verified at handoff.

### Push Status

NOT PUSHED.

### Next Recommended Task

Define a deterministic pointer-free recipe serialization/identity including
root mapping and lowering/metadata ABI, with mutation/cache-hit equivalence
tests. Do not hash runtime origin/count into shader identity or open production
views before coherent descriptor metadata/lifetime has separate proof.

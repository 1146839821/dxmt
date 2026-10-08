# Typed-origin production root ABI integration audit

## Task Analysis

### Current Branch

`feat/d3d12-1`, clean start.

### Baseline

Task `6be6948`; remote `origin/feat/d3d12`, merge base `85bb2dd`.

### Local Commits Since origin/feat/d3d12

Offline scalar/vector origin series ends with FLOAT4 `9e1f3d8` and UNORM4
`6be6948`. Historical production audit is retained as a dated snapshot.

### Current State

Offline parser/native proof does not supply production hidden metadata.

### Existing Implementation

Production roots own app bytecode and query MSC reflection from it; conversion
keys already hash supplied shader/root and compiler environment. Descriptor
snapshots retain coherent resources, static at recording and volatile at submit.

### Relevant Files

Root signature, compute pipeline, shader converter, descriptor heap and command
list in `src/d3d12/`; native bridge in `src/winemetal/unix/metalirconverter.c`.

### Existing Tests

Offline parser/container/native probes; production typed-UAV selected cases.

### D3D12 Contract

App parameter indices/root cost must remain unchanged. Hidden origin/count must
describe the same descriptor snapshot and allocation generation as the view.

### DXBC / AIRCONV Impact

None; do not add backend fallback.

### DXIL / MSC Impact

New opt-in layout-only diagnostic, not production shader conversion.

### Shared Runtime Impact

None; production rejection guards stay closed.

### Missing Pieces

Separate internal root identity/mapping, reservation policy, coherent metadata
generation/lifetime and versioned transformed cache identity.

### Risks

Hypothesis: app layout cannot be reused after root augmentation, particularly
with constants and the implicit static sampler entry. Evidence: production
encoder indexes app staging by reflected parameter index; native bridge assigns
the implicit entry UINT32_MAX. Expected effect: executable finite layout
contracts and an explicit dependency ledger. Risk: confusing host byte-copy
success with GPU execution or production thunk coverage.

### Minimal Implementation Plan

Add optional native MSC root-layout probe with constants/table/app CBV and a
separate hidden CBV; reorder app entries, vary constant widths, add static sampler.
Check count/type/register/space, non-overlap, sizes, sentinel-protected marshaling
and source-descriptor immutability. Do not assume old offsets remain stable.

### Validation Plan

Reconfigure/build both configurations, run existing Meson tests and native probe;
fresh production selected aligned/rejected cases. Self-review before local commit.
MSC binding skill requires reflected offsets and separates layout from GPU proof.

### Capability Impact

No capability, Shader Model or FL change; production eligibility UNVERIFIED.

## Current source integration ledger

This is a fresh source audit at `6be6948`, not a replacement of the historical
`D3D12_TYPED_ORIGIN_PRODUCTION_AUDIT.md` scalar-only snapshot.

| Boundary | Existing production evidence | Required before enablement |
| --- | --- | --- |
| Root ownership | `d3d12_root_signature.cpp:385` owns application blob; `InitializeMSCLayout` queries that blob | Separate immutable internal root/recipe on the MSC PSO; never mutate app indices/staging or charge app root cost |
| Explicit/embedded roots | `d3d12_pipeline_compute.cpp:78` forwards explicit root, otherwise embedded root | Collision scan covering shader metadata AND original root, deterministic reservation and embedded-root policy |
| TLAB mapping | `d3d12_command_list.cpp:2827` uses reflected offsets but maps parameter indices to app staging | Explicit app-to-internal mapping plus hidden address source and separate implicit sampler entry |
| Native bridge | `metalirconverter.c:1587` maps appended static entry to UINT32_MAX | Preserve that semantic sentinel after internal parameter count changes; do not mistake hidden CBV for the old sampler index |
| Descriptor snapshot | `d3d12_descriptor_heap.cpp:425` copies payload, references, current allocation and MSC view under mutex | Capture origin/count/format/allocation identity in the SAME snapshot; copies must carry the entire coherent payload |
| Descriptor observation | `d3d12_command_list.cpp:3151` schedules only volatile/direct-indexed uses for submission reread | Static metadata fixed at recording; volatile metadata resolved with live view at submission, per execution rather than patching in-flight memory |
| Retention | `d3d12_command_list.cpp:3263` retains snapshots under heap lock, emits after unlock | Retain internal root, metadata buffer and native view/backing until completion; independent regions for replay/in-flight submissions |
| Conversion cache | `d3d12_shader_converter.cpp:89` hashes supplied shader/root, environment; memory/persistent early hits at 1666/1673 | Lowering/metadata ABI and mapping recipe version in identity BEFORE lookup; deterministic transformed shader/internal root identity on every hit/miss |
| PSO persistence | Existing application-root/shader-based persistence remains unchanged | Persist and validate the internal mapping recipe too; do not add per-draw origin/count as shader-key dimensions |
| Barriers | `d3d12_command_list.cpp:4337` UAV memory barrier; 4345 aliasing invalidates encoder | Actual D3D12 readback for aliases, transitions, cross-list/queue ordering; prior native Metal barriers do not settle this |

The current recording/submission rejection of missing MSC views and the exact
aligned-only view construction at `d3d12_descriptor_heap.cpp:176` remain intact.
The tests-only text adapter is not linked into production conversion. Its
eight bounded scalar/vector classes do not solve dynamic handle provenance,
CBV reservation, embedded root or cache mapping. App CBVs/samplers remain
outside the adapter grammar; that is not a universal b0/space1 reservation.

Finite experiment: with a one-dword constant, app table and app CBV, static
sampler reflection is index 3/offset 24. Appending hidden CBV makes the CBV
index 3/offset 24 and moves static sampler to index 4/offset 32. Other widths
and ordering exercise the same hazard. This observation is specific to this
MSC layout corpus, not an assumption that app offsets are universally stable.
Always query the internal root independently.

The largest fixture uses 59 constant DWORDs + table cost 1 + app CBV cost 2 +
hidden CBV cost 2 = 64. It does NOT establish that a full 64-DWORD application
root can accept another CBV. A first bounded integration must reject internal
budget overflow; preserving public root cost does not remove the compiler's
internal root constraints. Root collision and full-budget support remain open.

## Task Result

### Branch

`feat/d3d12-1`.

### Baseline

`origin/feat/d3d12`; task baseline `6be6948`.

### Local Commit

Hash reported at handoff; bounded layout diagnostic and audit only.

### Changed Files

This report, `tests/dx12/msc_root_layout_probe.c`, and its optional Meson target.

### Implementation

Native probe independently creates app/internal roots for six constant widths
1/3/4/7/16/59, two table/constant orders and sampler off/on. Checks reflected
count, type, register/space, pointer/constant sizes, alignment/non-overlap,
sentinel-protected host byte writes and immutable app/range/sampler descriptors.
Static samplers reflect as an implicit TABLE in current MSC, not SAMPLER;
the initial probe expectation was corrected against fresh reflection and the
production bridge/encoder's existing implicit-table handling.
Twelve deliberate old-app-sampler-offset writes must mismatch the independently
constructed internal-layout byte image. No fake pointers are submitted to GPU.

### DXBC / AIRCONV Impact

Unchanged. Selected production DXBC regression passes.

### DXIL / MSC Impact

Root-layout diagnostic only; no shader compilation or production rewriting.

### Shared Runtime Impact

Unchanged; no DLL deployment or prefix mutation.

### Tests Added

Opt-in native layout probe; deliberately NOT registered as a D3D12/GPU unit
acceptance test and optional MSC installation remains optional for normal builds.

### Tests Run

Both builds reconfigured before compiling the probe, without compiler warnings.
Each native probe passes 24/24 paired app/internal layout cases and detects
12/12 old-layout reuse controls. Each Meson suite passes 3/3.
Fresh production selected cases pass 3/3 contracts: R8G8B8A8_UNORM DXIL first 1
rejects at recording/Close, aligned DXIL first 4 and DXBC first 1 pass readback.
Receipts: both builds' `root-abi-native.log`, `root-abi-config.log`,
`meson-logs/testlog.txt`; `build/root-abi-production.json` includes commands/logs.

Reproduce: `meson setup --reconfigure BUILD`,
`meson compile -C BUILD msc_root_layout_probe`,
`BUILD/tests/dx12/msc_root_layout_probe`, `meson test -C BUILD --print-errorlogs`.
Run selected production cases with repository Wine, normal prefix, shader cache
disabled and native d3d12/dxgi/winemetal overrides. No production DLL replaced.

### Runtime Results

Native MSC layout reflection and CPU marshal evidence only; no new native GPU
dispatch. Both configurations use the same installed MSC, not two independent
runtime backends. Production aligned/rejected readback is a separate limited
regression, not hidden metadata integration proof.

### Standards

Main-agent self-review: no hard standard violations. Opt-in diagnostic follows
existing tests/native-library layout. No runtime refactor or speculative public
abstraction. Hard-coded finite layout ceiling and test payloads are deliberately
bounded; reflection supplies all offsets, not parameter-index arithmetic.

### Spec

Main-agent self-review: no confirmed defect in bounded diagnostic/audit scope.
Checked count bounds before reflection writes, offset arithmetic before copies,
root/error ownership, app descriptor immutability and explicit negative control.
Independent review remains NOT COMPLETE because prior review workers hit account
usage limits; code-review skill's two axes used as main-agent fallback, not
represented as independent approval. Standards 0 hard findings, Spec 0 confirmed
defects. `git diff --check` passes.

### Known Limitations

No production hidden-CBV ABI, descriptor generation/copy/overwrite/replay proof,
cache-hit equivalence or D3D12 barrier proof added. Root collisions, full-budget
roots, dynamic handles, graphics/indirect/raytracing paths remain unverified.
Layout/CPU checks do not prove shader execution. No game/performance acceptance.

### Capability Status

Production eligibility UNVERIFIED/PARTIAL; unaligned production views remain
fail-closed. MSC binding skill enforced separate internal reflection and explicit
evidence boundaries; no additional-format capability enabled.

### Feature Level Impact

FL11_1 unchanged; FL12_0 unchanged; FL12_1 unchanged. Shader Model unchanged.

### Git Status

Only these three scoped files committed; clean status verified at handoff.

### Push Status

NOT PUSHED.

### Next Recommended Task

Define and test an immutable internal-root/mapping recipe with collision and
internal-budget rejection, independent of app root indices. Start with finite
SM6.0 compute tables only; keep production views closed until descriptor coherent
metadata lifetime and cache recipe identity are also validated.

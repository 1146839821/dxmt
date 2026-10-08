# Typed UAV Mandatory Formats

## Task Analysis

Branch `feat/d3d12-1`; local baseline `5ffc864`; read-only comparison baseline
`origin/feat/d3d12`. Preserve the pre-existing DirectX submodule modifications.
Specification: master prompt section 16.

### Contract

The three baseline formats are R32_FLOAT/UINT/SINT. Additional support requires
the entire fifteen-format all-or-nothing set: RGBA32 FLOAT/UINT/SINT, RGBA16
FLOAT/UINT/SINT, RGBA8 UNORM/UINT/SINT, R16 FLOAT/UINT/SINT and R8 UNORM/UINT/SINT.
Individual UAV load support requires typed store support (not the reverse); optional
formats cannot bypass the complete additional set. Normalized HLSL types must
match normalized resource formats.

Sources:
- [Microsoft typed UAV loads](https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads)
- [DirectX typed-load engineering specification](https://microsoft.github.io/DirectX-Specs/d3d/UAVTypedLoad.html)

### Hypothesis / Evidence / Expected effect / Risk / Validation

- Hypothesis: mandatory formats already have native typed UAV support, but API
  reporting is conservatively disabled and lacks a complete backend GPU matrix.
- Evidence: OPTIONS returns TypedUAVLoadAdditionalFormats=FALSE; per-format
  load/store bits derive independently from the Metal texture-buffer capability
  table, even for formats not eligible for D3D typed UAV loads.
- Expected effect: establish CPU-seeded GPU load/store evidence for all eighteen
  formats through DXBC/AIRCONV and DXIL/MSC. Promote only if the complete matrix
  passes; otherwise retain FALSE and record the exact failing contract.
- Risk: component conversion, normalized types, signed extension, typed-buffer
  view offsets and shader compiler support can differ between the backends.
- Validation: buffer plus non-MS 1D/1D-array/2D/2D-array/3D UAVs, independent
  CPU input and raw destination/readback oracles, API symmetry and negative format
  queries, normal/no-private builds and runtime provenance, gate regressions,
  Standards and Spec reviews before local commit. No push or FL promotion.

## Implementation plan

Use a standalone focused format-matrix fixture and one macro-based shader source;
reuse existing D3D12 resource, descriptor-table, barrier and fence conventions.
Do not merge shader backends or introduce compiler fallback. Do not modify
D3D11's capability table or the Metal/Wine binding ABI.

## Task Result

- Branch: `feat/d3d12-1`.
- Baseline: `5ffc86431a459345e9949e6d6d92173e316db514`.
- LocalCommit: the commit containing this report (`git log -1 -- docs/D3D12_TYPED_UAV_FORMATS.md`).
- ChangedFiles: D3D12 device format reporting and new format-policy header;
  two typed-UAV C++ fixtures, HLSL fixture, Meson shader targets;
  FL12 gate and its unit tests; this report.
- Implementation: restrict typed view/store to the documented format whitelist;
  require native read/write for loads; gate additional/optional loads on the
  complete additional-format capability. Preserve the existing baseline-format
  policy without claiming that its GPU semantics are now complete.
- DXBC/AIRCONVImpact: no compiler changes; audit 18 formats with CPU-seeded
  input and independent decoded-load/raw-store readback checks.
- DXIL/MSCImpact: no compiler changes or fallback. Use explicit SM6.0 shaders
  for exactly the same GPU cases. The nonzero typed-buffer offset remains broken.
- SharedRuntimeImpact: no shared Metal capability-table, D3D11 or binding ABI
  change. Gate provenance now also checks installed PE DLLs in Wine's builtin
  runtime directory and the explicit prefix's system32, before/after execution.
  This checks on-disk provenance, not an independent loaded-module trace.
- TestsAdded: production format-policy fixture; API whitelist/load-store checks;
  144 GPU cases per backend (18 formats times five texture shapes and three
  buffer offsets: 0, 4, 260); gate tests for API/GPU conjunction, installed DLL
  mismatches and preserving execution FAIL when provenance is incomplete.
- TestsRun: normal and no-private release builds; both Meson test suites
  (including 15 Python gate unit tests); full FL12 gate in both variants.
- RuntimeResults: Apple M4/Apple family 9, MSC 4.0.1. Both variants reproduce
  AIRCONV 144 PASS / 0 FAIL; MSC 108 PASS / 36 FAIL. MSC's 90 texture cases and
  18 zero-offset buffers pass; all 18 formats fail at each nonzero buffer offset.
  Policy/API probes and existing shader/minmax contract probes pass. Installed
  runtime/build provenance passes. The typed-UAV GPU gate and FL12 gates FAIL.
- KnownLimitations: all three mandatory R32 baseline formats are also affected.
  Their existing load bits remain advertised: API-policy PASS means reporting
  consistency only, not fulfillment of the baseline GPU contract. This task is
  PARTIAL and must not be used as evidence for FL12 or baseline typed-buffer
  correctness. No game/performance/tessellation acceptance is claimed.
- CapabilityStatus: `TypedUAVLoadAdditionalFormats=FALSE`; no promotion.
- FeatureLevelImpact: none; FL12_0/FL12_1 remain unavailable.
- GitStatus: task files committed separately; pre-existing DirectX submodule
  modifications preserved. Build diagnostics/reports are ignored, not staged.
- PushStatus: NOT PUSHED.
- NextRecommendedTask: fix legal MSC typed-buffer view rebasing/offset semantics
  before promoting typed-UAV support; then resume the DXBC tessellation audit.

### Offset diagnosis

The production descriptor contains the expected byte address, length and texel
offset. CPU prefix poison (`0xa5`) is returned for nonzero views. MSC-generated
AIR, including a shader with an explicit descriptor-table root signature, reads
metadata but uses `tid.x + 0` for typed-buffer texture accesses. This is evidence
of an unresolved native-view/offset integration contract, not proof that changing
the host offset field alone is sufficient or that the vendor compiler is solely
at fault. The companion runtime's offset field is eight bits of alignment padding;
it cannot represent arbitrary D3D buffer FirstElement by itself.

Enabling MSC bounds checking and removing the typed selector bit independently
did not fix the tight buffer probe. Both experiments were withdrawn. Any repair
must honor Metal buffer-view alignment, ownership and the MSC binding ABI; do not
silently route DXIL through AIRCONV or rewrite generated AIR as a fallback.

## Standards review

- [P2] Submitted-resource lifetime on completion failure: fixed by terminating
  the fixture without stack unwinding on signal/event/wait failure, following
  `dx12_reserved_buffer.cpp`; COM owners are also noncopyable.
- [P3] Possible primitive obsession in numeric shader variant IDs: deferred.
  This is test-maintenance debt, not a hard standard violation. IDs currently
  form the explicit HLSL/compiler filename ABI; a future variant extension
  should introduce a shared named mapping rather than silently reorder them.

Two findings; the worst Standards issue (in-flight lifetime) is fixed.

## Spec review

- [P1] Section 16's complete two-backend typed-buffer semantics are not met:
  36 MSC GPU cases still fail. This commit is an audit/reporting correction,
  not completion of section 16.
- [P1] Preserving baseline R32 load bits does not repair their observed nonzero
  offset failure. Do not interpret aggregate FALSE or API-policy PASS as closing
  the mandatory baseline contract.

Two open findings; Spec remains PARTIAL. No scope creep identified.

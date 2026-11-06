# MSC typed-buffer promotion audit

Baseline: `a8a0761062b5d6fbd9fde9db71d49241893c48fb`, `feat/d3d12`.
This is a source and existing-evidence audit; it does not enable the patch,
change capability reporting, or claim native Windows results. The worktree had
an unrelated `.gitignore` change which was preserved.

## Decision

**BLOCKED_BY_ARCHITECTURE:** Do not enable the current rewrite for every MSC
shader. Its experimental compute path does not distinguish a shader that needs
no typed-buffer rewrite from an AIR shape it cannot safely transform; it also
requires an explicit root signature and fails when no operation was rewritten.
A production policy must avoid both rejecting unrelated valid shaders and
silently running known-incorrect typed-buffer operations. This is a promotion
blocker, not proof that every MSC shader currently fails.

## Evidence and gaps

| Area | Current evidence | Promotion condition |
|---|---|---|
| Activation and failure | **SOURCE:** `src/d3d12/d3d12_pipeline_compute.cpp:94-145` converts first, then the opt-in `DXMT_TEST_MSC_TYPED_BUFFER_PATCH` path requires an explicit root signature and treats non-patched results as failure. `src/airconv/msc_typed_buffer_metallib.cpp:21-22,57-58` requires nonempty mappings and reports unsupported if no operation is rewritten. `src/airconv/transforms/msc_typed_buffer_sidecar.cpp:23-46,114-116` returns a single boolean for no operation, indirect calls, and unsupported shapes. | A conservative, testable distinction among **patched**, **proven not applicable**, and **unsupported/unknown**, with errors and allocation failures separate. No automatic fallback to unpatched code when the shader may require the fix. |
| ABI and mapping | **SOURCE:** `src/d3d12/d3d12_descriptor_heap.cpp:512-535,594-615` writes both MSC's 24-byte descriptors and the original heap's packed 32-bit first-element/count metadata. `src/airconv/transforms/msc_typed_buffer_sidecar.cpp:61-112,119-145` recognizes specific AIR GEP/load and call shapes and indexes the original heap with a four-qword stride. `src/d3d12/d3d12_root_signature.cpp:706-725` builds the combined layout from reflected table layouts. | Validate descriptor type, table identity, indexing and bounds across supported MSC versions and shader stages; handle or explicitly reject unknown patterns. Test null descriptors, arrays, mixed tables, table offsets, and resource-heap direct indexing. The current `src/airconv/airconv_public.h:352` comment saying no runtime path enables the rewrite is stale: the test-only compute path does. |
| Scope and stage | **SOURCE:** the test patch is installed only by compute pipeline creation (`src/d3d12/d3d12_pipeline_compute.cpp:104-145`) and binding checks the original root blob (`src/d3d12/d3d12_command_list.cpp:4911-4920,4987-5012`). The experimental LOD and typed patches are mutually rejected (`src/d3d12/d3d12_pipeline_compute.cpp:147-151`). | Design graphics/mesh/ray stages, embedded signatures, direct indexing, combined LOD behavior and stage-specific binding before claiming whole-backend coverage. A compute-only production policy must explicitly guard its scope. |
| Formats and semantics | **LOCAL_PASS:** the opt-in runner exercises scalar R32 FLOAT/UINT/SINT typed UAV loads, stores, dimensions and bounds; `tests/dx12/texture_sampler.hlsl:14-30,34-125` and `tests/dx12/run_r32_typed_uav.py:49-62,100-112`. **SOURCE:** sparse status users are rejected (`src/airconv/transforms/msc_typed_buffer_sidecar.cpp:48-59`); unknown texture-buffer intrinsics are rejected (`:36-47`). | GPU differential evidence for typed SRVs, vector/packed/normalized formats and legal atomics where applicable; null and sparse/status semantics; conversion rounding, alignment and large offsets. A test executable or descriptor metadata alone is not GPU evidence. |
| Residency and lifetime | **SOURCE:** `src/d3d12/d3d12_command_list.cpp:4133-4145` declares both original heap and allocator argument buffer resident for this path; descriptor changes and resource-use collection have separate submission logic. **LOCAL_PASS:** existing isolated results cover one pending submission/reset, re-record after reset with first completion before second submission, a descriptor copy after Close/before submit, and heap switch plus copy; see `docs/D3D12_MSC_TYPED_BUFFER_EXPERIMENT.md:549-596` and n95 `result-indirect-pending-final`, `result-typed-rerecord-final`, `result-indirect-late`, `result-indirect-heap` summaries. | Verify supported descriptor mutation windows and submission-time residency, multiple heaps/tables, multiple queues/submissions and lifetime boundaries under the D3D12 application contract. Do not infer safe post-submission mutation from the pre-submit test. |
| Indirect | **SOURCE:** `src/d3d12/d3d12_command_list.cpp:2020-2031` rejects MSC indirect signatures modifying resource bindings. **LOCAL_PASS:** no-root-update dispatch and bounded count cases are covered by the results in `docs/D3D12_MSC_TYPED_BUFFER_EXPERIMENT.md:533-547`. | Design the MSC root-update ABI and prove per-command extended root/sidecar bindings, residency, reset and lifetime. Current rejection is a general MSC gap, not an implemented path. |
| Cache and persistence | **SOURCE:** the private metallib is patched after conversion (`src/d3d12/d3d12_pipeline_compute.cpp:94-104`); patched PSOs reject `GetCachedBlob` (`:294-302`), and experimental `LoadComputePipeline` is rejected (`src/d3d12/d3d12_pipeline_persistence.cpp:1103-1119`). `StorePipeline` depends on `GetCachedBlob` (`:1047-1055`). | Define versioned keys including transform/ABI and converter identity, preserve coherent root metadata on restore, and test cold/hot cache plus pipeline-library round trips before enabling persistence. |
| Runtime matrix | **LOCAL_PASS:** the runner checks staged artifact bytes, selected PE export bytes, loaded DLL/Unix-library paths, validation-enabled log markers, and exact readback (`tests/dx12/run_r32_typed_uav.py:60-105`). **UNVERIFIED:** patched offline metallib shader-validation instrumentation, 32-bit Wine runtime, multiple MSC versions/devices and Windows differential results. | Exercise supported converter and GPU/OS combinations, verified PE/Unix pairs, 32-bit bridge if shipped, independent Metal validation instrumentation, and native Windows comparisons. |

The [Microsoft typed UAV documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads) governs format support and shader semantics; [root signature 1.1](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1) distinguishes descriptor and data volatility. Support declarations must continue to reflect the full implementation, not the experimental results.

## Recommended next slice: tri-state rewrite analysis

**Goal:** make a conservative distinction between a module with no typed-buffer
operations and a module whose operations cannot be proven safe to rewrite.
Keep all runtime activation and advertised capabilities unchanged.

**Boundary:** `src/airconv/transforms/msc_typed_buffer_sidecar.*` and its
standalone callers/tests; bridge changes only if needed to preserve explicit
status across the existing interface. Preserve the public/bridge numeric values
already used by callers; add a new status rather than redefining existing ones.
Do not implement broad compute/graphics promotion in this slice.

**Approach:** perform non-mutating whole-module analysis first. Return
`NOT_APPLICABLE` only if the analysis can prove no relevant typed-buffer access
or unresolved indirect call can conceal one. Return `UNSUPPORTED` for unknown
calls, unsupported intrinsics or ambiguous provenance; do not equate a parser,
allocation or verifier failure with a clean no-op. Rewrite only after the whole
module passes validation, and preserve the original module on rejection. Review
whether ignored fence/null intrinsics alone require a distinct policy rather
than assuming they are harmless.

**Validation:** synthetic AIR modules covering ordinary unrelated compute,
read/write/width, mixed supported and unsupported operations, indirect calls,
unknown intrinsics, malformed IR, empty mappings, and metadata-only/fence/null
cases; verify rejection is non-mutating and no-op never produces a patched
metallib. Exercise interface status propagation including buffer sizing and
invalid inputs; retain existing GPU positive and negative controls. Record
which converter versions and actual native/Wine runs were used. ABI-breaking
changes, unproven negative classification, or inability to preserve failure
atomicity stop this slice without enabling the runtime path.

**Rollback:** revert this classification slice; the existing test-only patch
and default behavior remain unchanged. **Done:** distinct, conservative status
is observable and covered by negative tests; no unrelated shader behavior or
feature declaration changes.

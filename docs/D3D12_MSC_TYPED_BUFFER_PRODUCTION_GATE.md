# MSC typed-buffer production gate

Audit baseline: DXMT `658aa296` (`feat/d3d12`); Mullion reference checkout
`b580492a557944591bf92f972497417333410925`. This is an evidence and design
review, not an implementation or capability promotion. Previous experiments
and their runtime artifacts are catalogued in
[D3D12_MSC_TYPED_BUFFER_EXPERIMENT.md](D3D12_MSC_TYPED_BUFFER_EXPERIMENT.md).
The working tree had an existing `.gitignore` change, excluded from this slice.

## Required semantics and architecture

A typed buffer SRV specifies a format, first element and number of elements;
see [D3D12_BUFFER_SRV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_buffer_srv).
Null SRV/UAV descriptors must provide zero reads and discard writes where
applicable ([SRV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createshaderresourceview),
[UAV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createunorderedaccessview)).
R32 FLOAT/UINT/SINT typed UAV loads are mandatory; additional formats follow the
specific feature-query rules in [Typed UAV loads](https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads).
Root Signature 1.0 descriptors may change before submission, but not while
submitted work can reference them ([Root Signature 1.1](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1)).
These requirements apply across the applicable shader stages and execution
paths; success for one opt-in compute probe is not complete D3D12 support.

**SOURCE_OBSERVED:** the existing descriptor heap keeps a 32-byte DXMT entry and
a separate 24-byte MSC entry. Typed view first element and count are stored in
the DXMT entry ([heap:107](../src/d3d12/d3d12_descriptor_heap.cpp#L107),
[heap:528](../src/d3d12/d3d12_descriptor_heap.cpp#L528),
[heap:610](../src/d3d12/d3d12_descriptor_heap.cpp#L610)); the experiment passes
the corresponding heap address in an extended root argument
([sidecar writer:11](../src/d3d12/d3d12_msc_resource_sidecar.hpp#L11),
[command list:4048](../src/d3d12/d3d12_command_list.cpp#L4048)). This avoids
assigning new meanings to MSC descriptor bits but depends on DXMT's heap layout
and matching descriptor indices. Mullion's independent texel-buffer descriptor
also carries element count and first element in its own heap
(`/tmp/mullion-audit.Cilay9/src/d3d12/d3d12_descriptor_heap.cpp:276-285`);
that observation does not establish compatibility with MSC's ABI or authorize
replacing DXMT's backend.

## Gate matrix

| Area | Current evidence | Production gate |
| --- | --- | --- |
| Activation and fallback | **SOURCE_OBSERVED:** only compute PSO checks `DXMT_TEST_MSC_TYPED_BUFFER_PATCH`; a missing root/layout or any non-PATCHED status is rejected ([compute:101](../src/d3d12/d3d12_pipeline_compute.cpp#L101)). Default MSC conversion remains unpatched. | Design an explicit per-shader classification and policy. Never treat unsupported or unknown AIR as proof that unpatched output is correct. Preserve normal behavior only when the absence of typed-buffer operations has been established. There is no demonstrated general DXIL-to-AIRCONV fallback. |
| AIR shape and origin | **SOURCE_OBSERVED:** preflight restricts call names, result shapes, descriptor GEP layout and reflected root-table origin; unknown calls reject ([transform:80](../src/airconv/transforms/msc_typed_buffer_sidecar.cpp#L80), [transform:155](../src/airconv/transforms/msc_typed_buffer_sidecar.cpp#L155)). `NOT_APPLICABLE` has a deliberately narrow classifier ([metallib:52](../src/airconv/msc_typed_buffer_metallib.cpp#L52)). | Broaden only with proven origin/alias analysis, fail-atomic checks and tests for each compiler form. Establish safe behavior for zero mappings, helpers, changed AIR versions and mixed shaders. A name/signature match alone does not prove pointer provenance. |
| Formats, bounds and status | **LOCAL_PASS:** isolated Wine R32 FLOAT/UINT/SINT scalar UAV/SRV fixtures and controls; selected native Metal R16 experiments are not full DXMT coverage. **SOURCE_OBSERVED:** sparse-status consumers are rejected ([transform:138](../src/airconv/transforms/msc_typed_buffer_sidecar.cpp#L138)); null access is guarded ([transform:209](../src/airconv/transforms/msc_typed_buffer_sidecar.cpp#L209)). | Differential tests for all actually reported formats, component fill and conversions, null, OOB, zero/large views, sparse status and atomics as applicable. `MakeBufferSlice` currently bounds byte offset and length to `UINT32_MAX` ([buffer:48](../src/d3d12/d3d12_buffer.cpp#L48)); do not claim arbitrary buffer capacities from small-view tests. |
| Binding and stage coverage | **SOURCE_OBSERVED:** resource-sidecar layout rejects local roots and directly indexed heaps ([root:659](../src/d3d12/d3d12_root_signature.cpp#L659)); compute experiment requires an explicit root and cannot combine the sampler-LOD patch ([compute:107](../src/d3d12/d3d12_pipeline_compute.cpp#L107), [compute:147](../src/d3d12/d3d12_pipeline_compute.cpp#L147)). Graphics/mesh/ray paths lack this integration. | Resolve embedded/global/local roots, heap direct indexing, shader-visible range indexing, all relevant stages, stage-specific argument slots, combined LOD and typed transforms, and root-signature compatibility. Validate per-encoder rebind and error paths. |
| Indirect and lifetime | **LOCAL_PASS:** dispatch-only indirect, count, pre-submit descriptor changes, heap switches, encoder break, pending reset/rerecord and descriptor-only resource tests documented in the experiment record. **SOURCE_OBSERVED:** submission resolves volatile descriptors ([command list:4418](../src/d3d12/d3d12_command_list.cpp#L4418)). Root-updating indirect signatures are rejected in the experiment. | Test root-updating indirect arguments, signatures and state reset, stage-specific resource uses, independent heap/allocator lifetime, pending and resubmitted work. Existing resource-only GPU reads are consistent with, but do not prove, a particular Metal residency declaration. Respect application-owned D3D12 lifetime and forbid in-flight descriptor mutation where required. |
| Pipeline/cache | **SOURCE_OBSERVED:** patched compute PSOs reject `GetCachedBlob`; pipeline-library load is disabled for the experiment ([compute:295](../src/d3d12/d3d12_pipeline_compute.cpp#L295), [persistence:1112](../src/d3d12/d3d12_pipeline_persistence.cpp#L1112)). MSC conversion disk cache is disabled pending trustworthy runtime identity ([converter:201](../src/d3d12/d3d12_shader_converter.cpp#L201)). | Establish transformed/untransformed variant identity, extended-root ABI and transform-version keys, runtime identity, cache invalidation and pipeline library round trips before lifting any restriction. Test disk and process caches separately. |
| Validation and policy | **LOCAL_PASS:** existing tests verify exact output, loaded artifact identity, and enabled Metal validation. **UNVERIFIED:** shader-validation instrumentation of rewritten metallibs; native Windows and broader GPU/OS/MSC versions. Typed additional formats are currently false ([device:819](../src/d3d12/d3d12_device.cpp#L819)); per-format queries are independent ([device:1027](../src/d3d12/d3d12_device.cpp#L1027)). | Establish an instrumented positive/negative shader-validation control, compare against Windows/native or independent conformance evidence, retain accurate feature/format reporting, and automate the covered tests. Do not raise Feature Level, Shader Model, Tier or format claims based on experimental success. |

## Recommended implementation sequence

1. **Classification boundary:** handle the existing proven `NOT_APPLICABLE`
   status separately from `UNSUPPORTED`, first in the opt-in compute slice.
   Verify a typed-free shader remains usable and unknown/mixed AIR still fails
   closed. Do not lift the environment guard or alter public claims.
2. **Origin and operation completeness:** build a shader/intrinsic and root
   mapping corpus from actual MSC conversions and establish safe provenance;
   incrementally implement missing shapes and sparse/atomic semantics with
   negative controls and Windows reference results where available.
3. **Descriptor and stage lifecycle:** design direct-indexed/local/embedded
   roots and graphics stage bindings, together with descriptor copies,
   residency and asynchronous submission evidence; preserve sampler LOD and
   existing AIRCONV behavior.
4. **Promotion gate:** cover reported formats and resource limits, indirect
   root updates, combined transforms, PSO/pipeline libraries and safe cache
   identity. Integrate automatic tests and validate across supported runtime,
   OS and GPU configurations before proposing any capability change.

## Design card for slice 1: no-op versus unsupported

- **Goal:** let the opt-in compute experiment retain the original converted
  metallib only when the existing classifier returns `NOT_APPLICABLE`; continue
  rejecting `UNSUPPORTED`, invalid inputs and failed patching.
- **Baseline/evidence:** current `SM50PatchMetalLibTypedBufferSidecar` exposes
  distinct statuses; current compute PSO treats all non-PATCHED statuses as
  `E_NOTIMPL`. The classifier scans module definitions and is conservative;
  it is not a general DXIL reflection proof.
- **Scope:** explicit root signature and layouts that the existing experiment
  already accepts; preserve non-experimental behavior. If no-op, do not mark
  the PSO as sidecar-consuming, extend its command binding or require a
  patched-root identity solely for that result. Keep original conversion bytes.
- **Non-scope:** enabling the feature by default, widening the classifier,
  supporting unknown AIR, direct-indexed/local roots, additional formats or
  shader stages, or changing advertised capabilities.
- **Module boundary:** carry the bridge status from the existing two-phase
  patch call into compute PSO selection; command recording consumes the
  resulting ordinary or patched variant. No change to MSC descriptor layout.
- **Tests:** synthetic no-op, malformed and mixed/unknown modules; an isolated
  Wine compute PSO using a genuinely no-op-classified MSC result; unknown AIR
  rejection; exact GPU readback, artifact identity and validation controls;
  both complete builds. If MSC emits AIR calls outside the classifier for the
  chosen shader, record that as a limitation rather than weakening it without
  evidence.
- **Risks:** status ambiguity, false no-op classification, roots with zero
  mappings, pipeline cache or root-identity contamination, and test fixtures
  that are optimized away. Verify the exact transformed/untransformed bytes.
- **Rollback/DoD:** revert the compute status branch and targeted regression;
  completion requires no-op pass, negative rejections, no behavior change with
  the flag absent, clean build, and delivery evidence. This slice is not itself
  approval to enable the sidecar in production.

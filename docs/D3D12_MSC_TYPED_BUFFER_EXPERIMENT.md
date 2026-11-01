# MSC typed-buffer experiment

## Goal and baseline

At baseline `9ed1ed4c`, MSC typed-buffer views do not consistently honor
`FirstElement`, view bounds, or `GetDimensions`. On the local M1 Max, a
four-element R32 view returned the 64-element backing-buffer width, and a
write/read at view index 4 accessed the backing resource. The existing
AIRCONV path is independent. These are local observations, not a statement
about all converter versions or hardware.

## Scope and design

`msc-typed-buffer-experiment` is a **standalone research tool**; no production
pipeline calls it. It matches a direct texture-handle load from field 1 of a
three-qword descriptor GEP, including a dynamic element index, and uses the
same GEP to load field 2. It recognizes typed-buffer read, write and width
operations, rejects unknown texture-buffer intrinsics and indirect calls,
and verifies the rewritten module before writing a metallib. The tool assumes
an experimental texel-size byte in metadata bits 40–47; production descriptors
**do not provide this field**, and no claim is made that MSC reserves those
bits for this purpose. Do not use its output as a production shader.

The optional repeatable `--sidecar <table-offset> <sidecar-offset>` experiment
instead matches the root argument and each specified descriptor-table offset, then
loads `(FirstElement, NumElements)` from the second qword of a 32-byte-stride
sidecar using the same dynamic index. The caller is responsible for extending
and binding the root argument and retaining/residenting the sidecar. A table
offset mismatch, duplicate or overlapping mappings, unaligned sidecar offset,
unknown buffer instruction or unrecognized handle form is rejected. This matcher does not prove
root argument identity against reflection and cannot handle all AIR forms.

A native Metal experiment with two UAV descriptor tables, independently
selected dynamic descriptors and different sidecar ranges passed GPU
read/write and dimensions checks; the unpatched AIR failed the same oracle.
The offline metallib has not been shown to contain shader-validation
instrumentation, and this experiment does not exercise D3D12 root binding.

The tool maps out-of-view indices to the maximum unsigned index and uses the
experimental texel size and byte length to compute dimensions. A zero size
produces a zero count. The approach is not approved for production: descriptor
origin, all shader shapes, conversion versions, atomics, null handling,
residency, volatile updates, bounds and status semantics remain unverified.
In particular, changing MSC's descriptor ABI without a compatibility guarantee
is not an acceptable production design. A separate sidecar and its copy,
update, lifetime, synchronization and direct-indexing behavior require study.

## Evidence and tests

`meson test -C build 'airconv msc typed buffer experiment'` runs synthetic
positive and rejection controls. Local native Metal and isolated Wine GPU
experiments have exercised dynamic R16/R32 descriptors and R32 texture-buffer
loads, stores, bounds and dimensions. The GPU fixtures use experimental
metadata and injection; they do not validate the existing production path.
See `dxmt-test/n95-capability-gate/msc-buffer-air/evidence.md` for the machine,
artifacts, validation limits and logs. Offline metallib shader-validation
instrumentation has not been established.

Rollback is limited to the optional Meson target and files in `tests/airconv`.
No feature level, shader model or tier is promoted by this experiment.

## Sidecar design candidate

**Goal.** Preserve the full D3D12 typed-buffer view offset and element count
without changing the MSC descriptor ABI. This remains a design candidate; it
is not wired into production.

**Entry.** A separate eight-byte entry per CBV/SRV/UAV heap slot can hold
`FirstElement` in the low 32 bits and `NumElements` in the high 32 bits. Null
and non-typed slots use zero. This avoids deriving element size from reserved
MSC metadata bits and represents offsets beyond the MSC eight-bit padding
field. The existing checked `BufferSlice` bounds remain the authority for
whether an entry is valid. Indices and offset arithmetic must be checked before
addition, including the largest representable index.

**Data flow.** The heap must update each sidecar entry together with its
descriptor while holding the same lock, and copy it for every supported
CPU-to-CPU and CPU-to-shader-visible descriptor copy. Heap binding must make
the backing allocation resident and keep it alive through GPU completion.
Each root descriptor table needs a sidecar pointer adjusted to the same heap
slot as its MSC table pointer; dynamic descriptor indexing must use the same
index for both tables. Directly indexed heaps require a separate complete
binding strategy. Shader transforms must fail closed for a resource origin or
operation they cannot prove, rather than silently emitting uncorrected AIR.

The existing resource descriptor copy in `d3d12_descriptor_heap.cpp` and MSC
table-address translation in that file are starting points. The sampler LOD
sidecar demonstrates extra root-argument layout work but is experimental and
is not proof that a resource sidecar has correct lifetimes or all binding
semantics. `d3d12_command_list.cpp` binds descriptor-heap buffers and handles
root argument uploads; graphics, compute, indirect execution and all active
shader stages need corresponding validation.

**Prototype evidence.** An isolated native Metal experiment with two dynamic
descriptors used separate sidecar entries `(260, 4)` and `(261, 4)` and left
the MSC descriptor layout unchanged. R16_SINT and R32_SINT returned and wrote
the expected data; a zero-count control returned zero without modifying the
buffers. This validates only that experimental data flow on the local machine,
not production descriptor copying, GPU completion lifetime, Metal shader
validation instrumentation, or Windows behavior. Details and artifact hashes
are in `dxmt-test/n95-capability-gate/msc-buffer-air/evidence.md`.

### Existing AIRCONV heap as candidate sidecar

The existing 32-byte `ShaderVisibleDescriptorGPUStorage` entries already
store typed buffer `FirstElement` and `NumElements` in their second qword.
Both typed SRV and UAV writes populate that field, and `CopyDescriptors`
copies the entire entry under the descriptor heap locks. For a shader-visible
resource heap, its GPU descriptor handle is already an address into this
allocation. A candidate implementation could pass the original table handle
as the sidecar table pointer and index its second qword with a 32-byte stride.
This avoids allocating a second descriptor mirror and leaves MSC's 24-byte
descriptor table unchanged.

An isolated native Metal prototype using this 32-byte layout passed dynamic
R16_SINT and R32_SINT accesses at element offsets 260 and 261. This does not
establish that the existing allocation is retained or resident at every MSC
graphics, compute, indirect or direct-indexed use. In particular, MSC currently
binds the separate MSC heap buffer; any use of the AIRCONV heap allocation
must explicitly address residency and in-flight lifetime. Root-signature table
offsets, heap switching, null and overwrite handling, descriptor-copy timing,
validation instrumentation, and concurrent submission are also unverified.
The production path remains unchanged.

An additional fixed-shape Wine diagnostic supplied the real D3D12 heap's
descriptor-table address in an extended root argument, and declared that heap
allocation resident. For R32_SINT, tests passed when a descriptor was changed
after `Close()` and before submission, both by direct rewrite and CPU-heap
copy. The unmodified runtime failed the late-copy control. The injected offline
metallib has not been shown to contain shader-validation instrumentation;
the diagnostic does not cover resource replacement, update after submission,
heap switches, multiple tables or generic root layout. Logs are in
`dxmt-test/n95-capability-gate/run-generic-sidecar-late-final` and
`dxmt-test/n95-capability-gate/run-sidecar-late-baseline`.

`dx12_descriptor_ownership --typed-buffer-sidecar` verifies the real mapped
shader-visible descriptor allocation on the local Wine/DXMT build. It checks
direct typed SRV/UAV writes, CPU-heap-to-visible copies, copying updated
entries, independent slots, explicit clearing and copying a cleared slot.
Large `FirstElement` values are included. The isolated run with module-identity
checks and Metal validation enabled passed; see
`dxmt-test/n95-capability-gate/run-typed-sidecar-heap-final`. This is a CPU
read of the GPU allocation, not a GPU execution, heap-switch or in-flight
lifetime test.

**Acceptance before integration.** Verify descriptor copies and overwrites,
heap switching and table offsets, nulls and zero lengths, dynamic and direct
indexing, in-flight submission lifetime, resource residency, overflow and
out-of-bounds loads/stores, format conversion, `GetDimensions`, atomics,
unsupported AIR shapes and both shader backends. The runtime path must remain
unchanged until these contracts are met. Rollback removes the isolated
sidecar work without changing the public feature declaration.

### Resource-table reflection layout slice

**Goal and boundary.** Derive a deterministic candidate sidecar slot for each
root descriptor table containing an SRV or UAV from MSC's actual root-signature
reflection. CBV-only and sampler tables need no typed-buffer metadata. This
is an explicitly requested, lazy layout calculation; it does not bind the
sidecar, rewrite AIR, or change advertised capabilities. Local roots and
resource-heap direct indexing return `E_NOTIMPL` because their mappings require
separate designs.

**Evidence and design.** Existing DXMT root metadata associates ranges with
root-parameter identities; MSC reflection supplies each table's TLAB offset.
The builder requires exactly one 8-byte reflected resource-table entry per
candidate, validates bounds, alignment and uniqueness, and orders extension
slots by reflected offset. It publishes output only on success. A shared
lock makes lazy initialization idempotent. The individual sampler and resource layouts both start after the reflected
TLAB and overlap. The combined-layout builder preserves sampler slots and
appends resource slots, checking reflected-table collisions and overflow.
Neither the combined layout nor the standalone AIR experiment is wired into
production binding.

**Validation and risks.** The `dx12_sampler_lod_metadata
--resource-sidecar-layout` probe constructs mixed SRV, UAV, sampler and
CBV-only tables, compares against actual reflection, checks concurrent calls
and malformed layout inputs, and was run locally with an isolated Wine prefix
and verified module paths. It does not execute a shader. Residency, descriptor
lifetime, dynamic descriptor selection, direct indexing and runtime use of simultaneous
sampler sidecars remain integration blockers. Rollback removes the independent
layout method, helper and probe; no capability declaration needs reverting.

### Combined sidecar layout slice

The pure combined builder consumes both table lists from one reflected root
signature. It revalidates each list, rejects a reflected entry claimed by both
classes, preserves the existing sampler ABI and allocates resource slots after
the sampler extension. An error leaves the output unchanged. The mixed-table
reflection probe checks those properties, empty input, collision and overflow;
the existing static-sampler probe checks sampler-only compatibility. This is
layout evidence, not GPU evidence. It does not bind a combined allocation,
select a combined pipeline variant, or establish residency or lifetime.
Rollback removes the helper and its tests without changing runtime behavior.

### Root-owned combined layout

The root signature lazily owns a combined layout. Initialization serializes
publication, initializes both existing reflected layouts, and publishes only a
complete successful result. Repeated calls return the same root-owned object;
the individual sampler layout is unchanged. A local root or direct-indexed
resource or sampler heap remains unsupported by the corresponding component.
The mixed-table probe interleaves combined and resource initialization across
threads and compares the root-owned result with an independently built layout.
This does not allocate or bind a combined GPU argument buffer. Runtime usage,
residency, lifetime and GPU correctness remain unverified. Rollback removes the
root-owned combined state and its test while preserving the existing layouts.

### Resource address write experiment

An isolated helper validates the combined layout, staging bounds and membership
of nonzero GPU descriptor handles in the bound shader-visible resource heap,
then writes the original GPU handle into each resource sidecar slot. Validation
is completed before writes so invalid inputs leave the allocation unchanged.
A mixed-root test uses an actual command allocator GPU-heap allocation and
resource descriptor heap, checking distinct table addresses, null handles,
invalid or unaligned handles, short staging/allocation and untouched sampler
slots and guard bytes. This is CPU-side inspection of mapped allocation memory,
not GPU readback. It does not validate table range lengths, descriptor updates,
heap residency, submission lifetime, GPU execution or root-argument rebinding.
The helper is not called by the production command encoder. Rollback removes the
helper and probe without changing the existing command path.

### Sidecar residency and lifetime integration audit

**SPEC_REQUIRED.** [Microsoft descriptor-table guidance](https://learn.microsoft.com/en-us/windows/win32/direct3d12/advanced-use-of-descriptor-tables)
requires applications to avoid modifying GPU-referenced descriptor entries
while they are in use. [Apple's argument-buffer residency guidance](https://developer.apple.com/documentation/metal/tracking-the-resource-residency-of-argument-buffers)
requires declaring indirectly accessed resources to the relevant encoder.

**SOURCE_OBSERVED at 98757cb7.** `GetDescriptorHeapBuffer()` returns the
32-byte AIRCONV allocation while `GetMSCDescriptorHeapBuffer()` returns the
separate MSC allocation (`src/d3d12/d3d12_descriptor_heap.cpp:361-378`). MSC
graphics and compute bind the latter (`d3d12_command_list.cpp:3409-3421`,
`4934-4952`); this cannot make the former resident. Resource-use encoding
merges usage/stages per encoder and retains the Metal resource in its encoder
(`d3d12_command_list.cpp:2059-2069`, `2255-2304`;
`d3d12_command_encoder.hpp:75`). Submission retains command allocators until
completion (`d3d12_command_queue.cpp:916-929`, `480-491`), and allocator
cleanup destroys encoder-owned references (`d3d12_command_allocator.cpp:217`).
Pending descriptor uses also retain resource-heap COM objects for submission
resolution (`d3d12_command_list.cpp:1022-1028`, `4320-4331`). These facts do
not establish GPU correctness for the new sidecar: its helper currently has no
production caller and does not itself declare the 32-byte allocation resident.

**LOCAL_PASS, AIRCONV only.** On the current local build, the existing
`dx12_airconv_compute_residency.exe --clear-residency-override` passed GPU
readback, filtered and conservative descriptor-footprint cases, and UAV
counter copy/mutation/fenced reuse with Metal API and shader validation enabled.
The executable matched the build by SHA-256; loader logs identify the isolated
D3D12 and WineMetal paths. Log:
`dxmt-test/n95-capability-gate/run-sidecar-write/airconv-residency.log`.
This does not exercise MSC sidecar residency, heap switching, or early object
release. The earlier fixed-shape patched MSC diagnostic explicitly declared
the 32-byte buffer resident, but is not a production-path lifetime test.

**Next implementation gate.** When a shader variant actually consumes resource
sidecars, declare the bound 32-byte resource heap allocation as read on every
relevant compute or render encoder and stage, retain it through encoder lifetime,
and regenerate the declaration after heap switches and encoder boundaries.
Keep both descriptor heap objects and allocator-backed argument storage valid
through the applicable submission lifetime. Test two heaps and multiple tables,
pre-submit descriptor replacement, encoder breaks, multiple submissions and
fenced reuse using exact GPU readback and negative controls. Test with Metal
validation and verify loaded artifacts. Do not infer in-flight mutation safety
or support for direct heap indexing from this result.

### AIR entrypoint and TLAB identity gate

The sidecar mode of the offline rewrite now requires exactly one compute or
fragment AIR entrypoint and identifies the TLAB argument from its
`air.indirect_buffer` binding at location 2. Every rewritten texture-buffer
operation must derive its descriptor table from that exact argument; metadata
that is absent, ambiguous, malformed, attached to another argument, or uses a
different binding is rejected before output. Vertex, mesh and object stages
remain unsupported by this experimental gate. Legacy metadata-bit mode is
unchanged and remains unsuitable for production.

The synthetic suite checks missing metadata, duplicate binding, wrong binding,
wrong scalar or pointer argument and multiple stages. A real MSC-generated
multi-table compute AIR module also passed the gate; its rewritten offline
metallib produced the expected two-descriptor GPU readback with Metal API and
shader validation enabled. The offline metallib's shader-validation
instrumentation is unverified. This confirms identification for that module,
not arbitrary MSC versions, helper-function forms, descriptor provenance,
null table semantics, or production integration. Rollback removes the
experimental metadata gate and fixtures without changing runtime behavior.

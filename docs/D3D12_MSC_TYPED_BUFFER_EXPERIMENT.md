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

### Descriptor and intrinsic shape gate

The offline matcher now validates the actual 24-byte descriptor structure
layout and offsets, checks the supported read/write argument counts and the
fixed index position and type, and rejects volatile or atomic descriptor and
table loads. Synthetic tests cover a 32-byte descriptor, volatile loads and an
unexpected index type, as well as prior invalid provenance cases. A real MSC
multi-table module still rewrote and passed the existing native Metal GPU
readback (`out=51,71 values=11,21,32,42`). Validation layers were enabled;
instrumentation of the offline metallib remains unverified.

This gate is experimental and does not establish complete intrinsic overload,
metadata, descriptor provenance, bounds, null, helper-call or status semantics.
In particular, its resource index and supported return forms still need a
complete ABI audit before production use. Rollback removes the added offline
checks and their tests without changing production behavior.

### Sparse-status gate

[Microsoft's CheckAccessFullyMapped contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/checkaccessfullymapped)
defines its status in terms of tiled-resource mapping. The existing rewrite's
out-of-range sentinel has not been shown to preserve that status. It must not
be assumed that a zero data result implies a correct status result.

Sidecar-mode preflight now requires a two-component read result with an i8
status and rejects uses of that status or of the complete aggregate. A
synthetic negative case checks rejection before output; the existing value-only
real MSC multi-table shader still passes the offline native Metal GPU readback.
This is a conservative experimental restriction, not an implementation of
sparse-resource status or proof of all out-of-bounds semantics. Shader
validation was enabled for the native run but offline metallib instrumentation
is unverified. Production conversion and capability declarations are unchanged.

### Reproducible R32_SINT out-of-bounds GPU control

`tests/airconv/run_msc_typed_buffer_gpu_oob.py --rewriter
build/src/airconv/darwin/msc-typed-buffer-experiment` compiles a native Metal
probe, rewrites the checked-in MSC 4.0.1 compute AIR fixture and compares exact
GPU results. Two descriptor tables and four textures are used; one invocation
reads and writes in range, and another indexes element 1 of a one-element
view. On the local M1 Max the patched result was `out=51,2
values=11,20,32,40`: the out-of-range reads contributed zero and the two
out-of-range writes preserved their sentinel values. The wrong-expected control
failed as intended; an unmodified AIR control also failed (`out=1024,1024`).
Metal API and shader validation were enabled; offline metallib shader
instrumentation is not established. This covers scalar R32_SINT values only,
not missing format components, other formats, sparse status, Wine/D3D12
submission, or production sidecar binding. The test is opt-in because it
requires a local Metal device. Its HLSL input is retained beside the captured
AIR; recompilation with other MSC versions may produce different AIR shapes.

### R32_SINT missing-component observation

The GPU bounds runner now instruments the checked-in MSC AIR to store all four
components of its first texture-buffer read. This instrumentation is added to
AIR after compilation; the original HLSL uses only the scalar x component. On
the local M1 Max, the patched shader returned `(10,0,0,1)` for an in-range
R32_SINT read and `(0,0,0,1)` for an out-of-range read. A deliberately wrong
alpha expectation fails, as do the previous scalar and unpatched controls.
[Direct3D 11.3 functional specification section 19.1.3.3](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
describes zero for missing components except integer alpha one. This result is
an observed Metal AIR experiment, not a Windows-native or production D3D12
conformance result. Other formats, sparse status and validation instrumentation
of the offline metallib remain unverified.

### Reusable sidecar transformation

The sidecar-only AIR pass now lives in `src/airconv/transforms` and is linked
into AIRCONV, but no production conversion or pipeline calls it. The offline
experiment invokes the same pass; the obsolete metadata-bit mode stays in the
test executable only. The pass validates table mappings and collects all
supported operations before mutating the module. Existing synthetic rejection
cases and the opt-in Metal GPU bounds/default-component regression pass using
this extracted pass. This is not production enablement: descriptor provenance,
additional AIR overloads, sparse status, root upload, resource residency and
in-flight lifetimes remain integration gates. The sample and typed-buffer passes share the AIR entrypoint and root-argument
metadata scanner; sample-specific rejection remains in the sample path. Rollback removes the new transform
and restores the experiment-local implementation.


### Shared MSC root argument recognition

The sampler patch and experimental typed-buffer pass now share one AIR
entrypoint metadata parser. It identifies the argument bound to the MSC TLAB
slot, rejects malformed or ambiguous metadata and accepts only the existing
compute and fragment forms. The sampler path retains its separate rejection of
sample instructions in other functions. Both existing synthetic test suites
and the typed-buffer native Metal readback with negative controls pass. A full
Wine sampler GPU run was not performed in this slice. The typed-buffer pass
remains unconnected to production conversion; no capability is promoted.
Rollback restores the two prior parsers and removes the shared header.

### Experimental metallib interface

`SM50PatchMetalLibTypedBufferSidecar` accepts a metallib and reflected original
and extended TLAB sizes, validates up to 64 mappings and their alignment,
bounds and uniqueness, parses and verifies bitcode, applies the sidecar pass,
and returns a newly owned metallib only on success. Invalid input and
unsupported AIR return distinct codes with a null output; the caller decides
whether to use the original. AIRCONV is built with C++ exceptions disabled, so
allocation exhaustion is not guaranteed to produce a recoverable error.

A native test checks successful output by parsing and verifying the resulting
bitcode and finding a sidecar load. It checks short and corrupt inputs,
misaligned, overlapping, duplicate and out-of-bounds mappings, excessive table
count, and an unsupported table association. The existing sampler and
experimental transform suites pass. This is not a WineMetal thunk or pipeline
integration; no runtime path invokes the new interface. The following offline GPU test
uses its generated metallib. Capability declarations are unchanged.
Rollback removes the interface, implementation and test.


### Metallib interface GPU check

The opt-in `run_msc_typed_buffer_gpu_oob.py` now produces its patched metallib
through `SM50PatchMetalLibTypedBufferSidecar`, rather than invoking the AIR
pass directly. The R32_SINT multi-table in-bounds, out-of-bounds and default
component checks pass on the local Metal device. Wrong-value, wrong-component
and unpatched controls fail as intended; invalid interface input does not
produce an output file. Metal validation was enabled, but instrumentation of
the offline metallib is unverified. This does not exercise WineMetal thunks,
D3D12 command binding, resource residency or submission lifetime. No production
path has been enabled. Rollback restores the direct-pass test invocation.

### Experimental WineMetal typed-buffer bridge

`DXMTMSCPatchTypedBuffer` adds Unix-call ordinal 199 after the existing
ordinals, plus a WOW64 structure and conversion thunk. It mirrors the sampler
bridge's size-query and caller-owned output convention. Invalid parameters
return an error with zero output size; unsupported AIR returns success with an
explicit unsupported patch status; an undersized output buffer is not written.
The bridge is not called by D3D12 pipeline creation.

On local 64-bit Wine, the targeted thunk test passed size query, undersized
buffer and sentinel preservation, successful output, invalid mapping,
unsupported mapping and excessive table count. It verified the loaded export
bytes and Unix library path against the task-specific staged build. The
resulting metallib also passed the existing native Metal multi-table GPU probe
(`out=51,71 values=11,21,32,42`), with API and GPU validation enabled; offline
metallib shader-validation instrumentation remains unverified. The original
prefix and deployment were preserved. The first test attempt loaded an older
WineMetal PE from the copied stage's Wine library directory and failed with an
unimplemented export; the final isolated run used matching PE and Unix builds.
The WOW64 structure has a compile-time size/offset check and the x86 PE build
passes, but a 32-bit Wine execution was not performed. Pipeline integration,
GPU sidecar binding, residency and submission lifetimes remain unverified.

### Opt-in compute integration experiment

With `DXMT_TEST_MSC_TYPED_BUFFER_PATCH=1`, compute pipeline creation uses the
WineMetal patch bridge and the reflected combined root layout. Command recording
uploads sidecar table addresses and declares the original 32-byte resource heap
buffer resident for the compute encoder. Pipeline root mismatch and unsupported
AIR fail explicitly; experimental PSOs do not expose a cached blob and library
loading is disabled with the flag. The default production path is unchanged.
The currently validated path is compute only and cannot be combined with the
sampler LOD test patch.

In isolated 64-bit Wine with MSC 4.0.1 and Metal API/GPU validation enabled,
R32_SINT direct and CPU-heap-copy descriptor updates after Close and before
submission passed GPU readback, dimensions and guard checks. R32_FLOAT and
R32_UINT late-copy checks passed after explicitly rebuilding their opt-in HLSL
fixtures; the first runs used stale fixtures and failed the dimensions check.
The no-patch R32_SINT control failed as expected. The targeted test source, HLSL and runner are retained as an opt-in
regression; fixtures must be explicitly built before staging. The patched metallib's shader validation
instrumentation is unverified. Graphics, multiple resource heaps, in-flight
lifetimes, sparse status, indirect commands and broader formats remain
unverified; no public capability was promoted. Logs reside under
`dxmt-test/n95-capability-gate/run-typed-pipeline`.


### Reproducible compute integration regression

Build `dx12_typed_buffer_scalar_float_fixture`,
`dx12_typed_buffer_scalar_uint_fixture`, `dx12_typed_buffer_scalar_sint_fixture`
and `dx12_texture_sampler` explicitly, then use the staged and matching runtime
with `run_r32_typed_uav.py --buffer --scalar --experimental-patch --late-copy`.
On the local M1 Max / Wine 11 environment, all three formats passed readback,
late descriptor copy, bounds guards, loader identity and enabled validation.
The same command without `--experimental-patch` failed readback for all three,
as expected; the ordinary R32 texture tests passed. The offline patched shader
instrumentation is unverified and this does not prove graphics, multiple heaps
or in-flight submission lifetime. Experimental compute is still opt-in.

### Compute view-offset boundary checks

The committed opt-in R32 regression was rerun against the current build with
`--buffer --scalar --experimental-patch --unaligned` and separately with
`--zero-offset`. FLOAT, UINT and SINT all passed GPU readback, dimensions,
view guards, loaded-module identity and enabled validation checks. Results are
in `dxmt-test/n95-capability-gate/run-typed-auto-unaligned2` and
`run-typed-auto-zero2`. The first attempted run correctly rejected a stale
staged D3D12 DLL; a fresh isolated stage was prepared before testing. Offline
metallib shader-validation instrumentation is unverified.

These tests use one resource descriptor heap and do not validate heap switching
or in-flight lifetime. A meaningful switch test must use different view
metadata and distinct readback expectations for each heap; merely copying the
same descriptor to another heap cannot detect a stale sidecar pointer.

### Compute heap-switch regression

The opt-in R32 runner's `--heap-switch` mode records two extra compute
invocations in the same command list. Heap A's typed view has four elements;
heap B has two. After changing heaps, the test rebinds both descriptor tables
and requires the GPU to report lengths 4 and 2. This distinguishes a stale
sidecar from the new heap. FLOAT, UINT and SINT passed on local 64-bit Wine
with API and GPU validation enabled and verified module identities. A SINT
negative control expecting the stale length 4 failed and reported `4,2`.
The opt-in shader fixtures must be explicitly rebuilt before running.

This validates a recorded heap switch and the resulting two readback values,
not heap destruction, concurrent submissions, indirect execution, graphics,
or all encoder-transition and in-flight lifetime cases. Offline metallib
shader-validation instrumentation is unverified. Production enablement and
capability declarations are unchanged.

### Compute to blit to compute boundary

The heap-switch runner also accepts `--encoder-break`, inserting a buffer copy
between the two dispatches. It checks the copy sentinel as well as the two
heap-specific view lengths. FLOAT, UINT and SINT passed on the local isolated
Wine/Metal setup, with module identity and validation checks; logs are in
`dxmt-test/n95-capability-gate/run-typed-encoder-final`. This covers one
recorded compute-to-blit-to-compute sequence, not concurrent submission,
resource destruction or all encoder transitions. Shader-validation
instrumentation of the offline metallib remains unverified.

### Pending submission and command-list reset

The R32 runner's `--pending-reset` mode queues a wait on an unsignaled gate,
submits the workload and a completion fence, checks that the completion fence
is still zero, and resets the command list using a different allocator before
releasing the gate. Descriptor heaps and resources remain alive throughout, as
required by the application contract. All three R32 formats passed GPU readback
and guard checks on local 64-bit Wine with the experimental patch and validation
enabled; the same cases without the patch failed as expected. Logs are in
`run-typed-pending` and `run-typed-pending-control` under the n95 stage.

This checks one pending submission and list reset, not application-initiated
resource destruction, multiple simultaneous queues or submissions, or all
Metal resource lifetime cases. Offline metallib shader-validation
instrumentation remains unverified. No production capability is promoted.

### Experimental compute sidecar and ExecuteIndirect

Dispatch-only indirect signatures inherit the parent compute encoder's buffers,
including the MSC argument buffer and experimental sidecar. The runner's
`--buffer --scalar --experimental-patch --indirect` mode performs two indirect
dispatches with a root constant changed between them and checks GPU readback.
The `--indirect-count` mode also checks two-command calls with count-buffer
values 0, 1, 2 and 3 (clamped to the maximum of 2), and a two-command call
without a count buffer. Each execution atomically adds the typed view's element
count to a separate result; the expected sums are 0, 4, 8, 8 and 8. On local
64-bit Wine, R32 FLOAT, UINT and SINT passed; a deliberately wrong expected sum
and a run without the experimental patch failed. Logs and runtime identity are
in `result-indirect-count`, `result-indirect-count-wrong`, and
`result-indirect-count-no-patch` under the n95 stage. This does not cover every
indirect signature or submission lifetime combination.

The `--indirect-pending` mode combines that multi-command/count workload with a
queue wait and resets the submitted command list using a different allocator
while the completion fence is still zero. The application holds the command
signature, original allocator, heaps, and resources through completion. This
tests the permission in [ID3D12GraphicsCommandList::Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-reset)
to reset a list while it executes; it does not reset its in-flight allocator.
Local three-format results and runtime identities are recorded in
`result-indirect-pending-final`; the wrong-expectation and no-patch controls are
in `result-indirect-pending-wrong-final` and `result-indirect-pending-no-patch-final`.
This does not test simultaneous submissions, re-recorded work, resource release
before completion, or native Windows behavior.

The `--rerecord` mode records a second workload on the reset list while the first
submission is blocked. It uses a separate descriptor heap with a two-element
view and different indirect argument and count offsets. It closes the new list
while the first completion fence remains zero, releases the gate, waits for the
first fence, and only then submits the re-recorded list. This respects the
[ExecuteCommandLists requirement](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists)
that the GPU no longer reads the previously submitted list. The first readback
must remain `0, 4, 8, 8, 8`; the second must read view dimension 2. A sentinel
initializes the second readback location, and zero-count and wrong-expectation
controls must fail. Local three-format evidence is in `result-typed-rerecord-final`,
`result-typed-rerecord-zero-final`, `result-typed-rerecord-wrong-final`, and
`result-typed-rerecord-no-patch-final`.
This does not establish simultaneous submissions or native Windows behavior.

The `--indirect-late` mode records indirect typed reads, writes, and dimensions
queries against a two-element buffer UAV, then uses `CopyDescriptorsSimple`
after `Close` and before submission to replace it with a four-element view at a
different first element. The root signature is version 1.0. In local 64-bit
Wine, R32 FLOAT, UINT, and SINT passed typed readback, guards, bounds and the
indirect count/dimension sums; skipping the descriptor copy and disabling the
patch each failed. Evidence is under n95 in `result-indirect-late`,
`result-indirect-late-skip`, and `result-indirect-late-no-patch`. The shader
reads the sidecar table at execution; no descriptor is mutated while submitted
work is in flight. This does not cover root signature 1.1 static descriptors,
post-submission mutations, or all descriptor/residency lifetime combinations.

The `--indirect-heap` mode uses indirect dispatch for both typed UAV access and
the dimensions queries, with a buffer copy between the latter two dispatches
and a switch to a second descriptor heap with a two-element view. The local
R32 FLOAT, UINT, and SINT runs read dimensions 4 then 2 and checked the copy
sentinel. Incorrect expected dimensions and disabling the experimental patch
both failed. Evidence and runtime identity are in `result-indirect-heap`,
`result-indirect-heap-wrong`, and `result-indirect-heap-no-patch` under n95.
The command recording path switches to a copy encoder and back; this test does
not independently inspect a GPU capture of encoder boundaries, nor cover every
heap or indirect-signature combination.

When a signature changes root arguments, `PreDispatch` skips normal binding and
indirect encoding uses the ordinary root-argument layout without the sidecar.
The existing MSC pipeline validation rejects such signatures and `Close` returns
`E_FAIL`. The `--indirect-reject` mode checks that rejection for R32 FLOAT, UINT,
and SINT without submitting GPU work. This slice does not change advertised
capability. Metal validation being enabled does not establish that the rewritten
metallib was shader-validation instrumented.

Design references: [Microsoft ExecuteIndirect](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect),
`EncodeIndirectComputeCommand`'s `inherit_buffers` decision, and the independently
implemented Mullion `ExecuteIndirect` path. The last is a reference, not the
binding ABI used here. Local results and loaded-runtime identity are recorded in
the n95 `result-indirect-dispatch-final2` and `result-indirect-reject-final2`
directories. The no-patch GPU control is in `result-indirect-control-final`.

### Conservative no-op classification

The experimental metallib API now distinguishes `NOT_APPLICABLE` (numeric 3)
from `UNSUPPORTED` (2), retaining all existing numeric status values. It emits no
metallib for the new status. To prove a no-op, the module must contain a
function definition, no indirect or external call, and no AIR intrinsic call;
`llvm.bswap` is the only permitted external intrinsic. All functions are
scanned, including helpers. Unknown calls and fence/null-only AIR remain
unsupported. For modules with candidate typed-buffer operations the preflight
also rejects unknown external calls; the current narrow allowlist for unrelated
intrinsics is deliberately incomplete and can reject previously accepted
experimental shaders. No-op does not bypass input/mapping validation, and the
opt-in compute path still rejects non-patched results. Default runtime behavior
and advertised capabilities are unchanged.

Synthetic AIR and metallib tests cover no-op, internal calls, the intrinsic
allowlist, unknown external/AIR calls, indirect calls, fence/null-only calls,
mixed known and unknown operations, malformed mappings and failure atomicity.
The isolated 64-bit Wine thunk verifies status and untouched output on a no-op
(`typed-classify-thunk-final3.log`). R32 GPU regression and no-patch controls
are in `result-typed-classify-positive-final3` and
`result-typed-classify-control-final3`. This is not a proof for every MSC AIR
version, shader stage, or intrinsic; the restricted allowlist and the missing
production fallback policy remain promotion blockers. Shader-validation
instrumentation of the rewritten metallib and 32-bit Wine runtime remain
unverified.

### `air.popcount.i32` classification

A DXIL `countbits` fixture compiled with the locally installed MSC CLI 4.0.3
produced a direct declaration and call `i32 @air.popcount.i32(i32)`; the
extracted AIR and metallib are in `msc-countbits/float.ll` and
`msc-countbits/float.metallib` under n95. The CLI's SHA-256 was
`f05e0ebefc08a79fa339335310be678df39ed9e2c55c02c31ea0263b135c8f2c`;
the generated metallib's was
`0be5801956d9a3518bed159108b1c4aee5032cf9745c0204353b1b7904f68650`.
Microsoft specifies [countbits](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/countbits---sm5---asm-)
as counting set bits. The no-op analysis and typed-buffer preflight accept only
this exact declaration and direct i32-to-i32 call, not variants with similar
names or incompatible signatures. Other unclassified AIR remains rejected.

Synthetic tests check this operation both alone and combined with a typed-buffer
rewrite, and reject wrong signatures and names. The isolated Wine runtime
compiled `typed_buffer_math_{float,uint,sint}.cs.cso`, then passed GPU readback
of countbits, typed buffer bounds and 4-to-2 heap transitions; wrong expectations
and no-patch controls failed. Evidence and runtime artifact hashes are in
`result-typed-popcount`, `result-typed-popcount-wrong`, and
`result-typed-popcount-no-patch` under n95. The CLI AIR capture is separate from
the runtime converter run; its exact AIR was not captured from that run. Metal
validation was enabled but instrumentation of rewritten metallib remains
unverified. This does not enable the experiment by default or promote capability.

### `air.reverse_bits.i32` classification

MSC CLI 4.0.3 compiled the `reversebits` DXIL fixture into a direct
`i32 @air.reverse_bits.i32(i32)` call. The extracted AIR and metallib are in
`msc-reversebits/float.ll` and `msc-reversebits/float.metallib` under n95;
the metallib SHA-256 is
`07c19848da0d5bfe278c03ca02ba462180625be69d429cef84e6770b308cf965`.
[Microsoft's reversebits specification](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/reversebits)
defines bit reversal per component. The experimental analysis accepts only a
direct call to that exact external declaration with one i32 argument and an
i32 result. Similar names, other widths and incompatible signatures remain
unsupported.

Synthetic no-op and mixed typed-buffer AIR tests cover the accepted signature
and rejection cases. Isolated 64-bit Wine GPU tests combine this operation with
indirect typed-buffer accesses, `countbits`, heap switches and a copy; the
expected reversed results for 4 and 2 are `0x20000000` and `0x40000000`.
R32 FLOAT/UINT/SINT pass; wrong-expectation and no-patch controls fail. Logs
and artifact identity are in `result-typed-reversebits`,
`result-typed-reversebits-wrong`, and `result-typed-reversebits-no-patch` under
n95. The CLI capture is distinct from runtime conversion, whose AIR was not
captured. Metal validation was enabled but rewritten-metallib shader-validation
instrumentation is unverified. No default behavior or capability is promoted.

### Typed-buffer preflight: 2D sampling signature

The previous preflight accepted any declaration whose name started with
`air.sample_texture_2d.`. This did not check whether its call shape matched the
captured 2D sampler operation. It now accepts only a direct non-variadic
`air.sample_texture_2d.v4f32` declaration and the observed nine-argument shape:
address-space 1 texture pointer, address-space 2 sampler pointer, float2, i1,
int2, i1, float, float, i32, returning an unpacked `{float4, i8}`. Unknown
suffixes and other shapes remain unsupported. The check validates pointer
address spaces but does not prove the provenance or texture type of each pointer;
this is not a general AIR verifier or promotion of other sample overloads.
Sample-only modules still follow the conservative no-op policy.

The signature was observed in the previously captured MSC runtime AIR
`msc-buffer-air/runtime15.ll` and the separate MSC CLI 4.0.3 capture
`msc-reversebits/float.ll` under n95. Synthetic mixed typed-buffer/sampler tests
accept the matching shape and reject unknown suffixes, wrong pointer address
space, wrong return component type and packed return structure without producing
output. Isolated Wine GPU tests using R32 FLOAT/UINT/SINT plus sample and bit
operations are recorded in `result-typed-sample-guard-final`; wrong-result and
no-patch controls are in `result-typed-sample-guard-wrong-final` and
`result-typed-sample-guard-no-patch-final`. This check does not yet constrain
other previously allowed calls such as global atomic add or get-read-sampler.
Metal validation was enabled; rewritten-metallib instrumentation is unverified.
Default behavior and public capabilities are unchanged.

### Typed-buffer preflight: global atomic add signature

The prior experimental preflight accepted `air.atomic.global.add.s.i32` solely
by name. It now requires a direct, non-variadic declaration returning i32 with
an address-space 1 pointer, three i32 operands, and either an i1 operand or an
additional i32 followed by i1. The five-argument form was observed in the MSC
CLI 4.0.3 capture `msc-reversebits/float.ll`; a temporary diagnostic of the Wine
runtime conversion observed the six-argument form in
`result-typed-atomic-diagnostic/float.log` under n95. The temporary logging was
removed. The evidence does not establish why the converter forms differ.

Synthetic mixed typed-buffer AIR tests cover both forms and reject other names,
address spaces, flag/result types, and an i64 extra operand. The isolated Wine
GPU test executes atomic additions while checking indirect command counts and
R32 FLOAT/UINT/SINT typed-buffer results; results are in
`result-typed-atomic-guard-final`, with wrong-expectation and no-patch controls
in `result-typed-atomic-guard-wrong-final` and
`result-typed-atomic-guard-no-patch-final`. This checks the call shape, not the
provenance of the pointer or the semantics of all order/flag values. Other
atomic intrinsics remain unsupported. Metal validation was enabled but rewritten
metallib shader-validation instrumentation remains unverified; default runtime
behavior and advertised capability are unchanged.

### Typed-buffer preflight: read sampler call shape

The previously captured Wine runtime AIR `msc-buffer-air/runtime15.ll` under
n95 contains `air.get_read_sampler()` returning an address-space 2 sampler
pointer, passed to `air.read_texture_buffer_1d`. The preflight now uses one
check both for independent calls and for that read operand: it must be a direct,
non-variadic call to the exact external declaration with zero arguments and a
pointer result in address space 2. An internal definition with the same name
is rejected. The check does not prove the opaque pointer's resource provenance.

Synthetic AIR tests cover an independent call, the typed-buffer read operand,
wrong address space, wrong return type, arguments, a function definition, and a
variadic declaration. The existing experiment remains fail-closed for other
shapes; no-op classification is unchanged. Isolated Wine GPU R32 FLOAT/UINT/SINT
indirect read/write/count tests passed in `result-typed-read-sampler`, while
wrong-expectation and no-patch controls are recorded in
`result-typed-read-sampler-wrong` and `result-typed-read-sampler-no-patch`.
The runtime AIR capture is earlier evidence; this run did not independently dump
its AIR. Metal validation was enabled, but rewritten-metallib shader-validation
instrumentation remains unverified. Default behavior and capability reporting
are unchanged.

### Typed-buffer access intrinsic preflight

The experimental transformer formerly accepted any AIR read/write name with a
matching prefix, and skipped fence/null calls without checking their shapes. It
now permits only the observed `s.v4i32`, `u.v4i32`, and `v4f32` read/write
names. Read results must be an unpacked matching four-element vector plus i8;
write values must be the corresponding vector. Texture arguments must be
address-space 1 pointers, and calls must target non-variadic declarations.
Width, fence, and null calls require their observed argument and return shapes.
Unsupported overloads fail before mutation; this narrows experimental coverage
and does not establish that every format represented by an accepted AIR name is
supported. Pointer provenance, null-resource behavior and sparse status remain
separate unverified requirements.

The name and call shapes were compared against captured CLI and Wine AIR in
`msc-buffer-air/*.ll` and `msc-reversebits/float.ll` under n95. Synthetic AIR
tests cover supported calls, unknown suffixes, incorrect width and read/write
vector returns, packed read returns, and malformed fence/null calls, with
failure atomicity checks. Local 64-bit Wine GPU R32 FLOAT/UINT/SINT regression
results are in `result-typed-access-guard`; wrong-result and no-patch controls
are in `result-typed-access-guard-wrong` and
`result-typed-access-guard-no-patch`. This run did not independently dump its
runtime AIR. Metal validation was enabled; instrumentation of the rewritten
metallib is unverified. Default behavior and capability reporting are unchanged.

### Null typed UAV: validation failure

Microsoft specifies zero reads and discarded writes for a null UAV created with
an appropriate view description ([CreateUnorderedAccessView](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createunorderedaccessview)).
The isolated 64-bit Wine experiment creates a valid R32 buffer UAV description
with a null resource, binds it through a descriptor table, and reads, writes,
queries dimensions, and checks a separate sentinel buffer. With the experimental
sidecar enabled, FLOAT, UINT, and SINT all returned zero for both reads and
GetDimensions, and left the sentinel unchanged, but **all three failed Metal
validation with `Invalid Metal usage: Null texture access`**. Zero dimensions is
only a local observation here. The sentinel is not bound by the null descriptor,
so its preservation alone does not prove that arbitrary memory was untouched.

Results and loaded-runtime identity checks are in `result-typed-null` under n95.
A live-UAV control (`result-typed-null-live`) failed the null-result oracle for
all three formats as expected. The existing indirect-count typed-buffer
regression passed in `result-typed-null-regression`. Metal validation was enabled;
instrumentation of the rewritten metallib is not independently verified. The
validation diagnostic lacks line information, so the specific AIR instruction
responsible is unconfirmed. The current sidecar transform remaps access indices
but does not guard null texture reads, writes, or fences; addressing those paths
requires a separate change and validation. Null typed UAV support is not established;
default behavior and capability reporting are unchanged.

### Experimental null texture guard

The experimental sidecar rewrite now checks the texture handle with the observed
`air.is_null_texture_buffer_1d` intrinsic before each rewritten texture-buffer
read or write and each texture-buffer fence. Null reads produce a zero aggregate;
null writes and fences are skipped. Non-null calls retain their existing index
remapping. Preflight rejects incompatible existing null-intrinsic declarations,
name collisions, or inconsistent handle types before changing the module. Sparse
status uses remain rejected by the existing preflight, so the zero aggregate is
only relied on for value-only reads.

The isolated 64-bit Wine tests in `result-null-guard-null` under n95 now pass
readback, runtime identity, and the enabled Metal validation checks for R32
FLOAT, UINT, and SINT. `result-null-guard-live` is a deliberately failing
live-UAV control; `result-null-guard-regression` passes non-null indirect-count
coverage. The separate sentinel caveat above still applies, and shader validation
instrumentation of the rewritten metallib is not independently established.
This is experimental coverage, not a capability promotion: SRVs, other formats,
Windows native comparison, and null descriptors changed after recording remain
unverified. Default behavior and capability reporting are unchanged.

### Null typed SRV

[CreateShaderResourceView](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createshaderresourceview)
requires zero reads from a null descriptor created with a valid view description.
A descriptor-table compute probe covers R32 FLOAT, UINT and SINT with a buffer
view at element 4 of a 16-element sentinel buffer. For the null resource it
checks a read at zero, an out-of-bounds read, and GetDimensions; for a live
resource it expects the sentinel, zero out-of-bounds, and a view length of four.
The zero dimension of the null view is recorded as a local observation.

With the experimental sidecar enabled, the isolated Wine GPU runs in
`result-typed-srv-final-null` and `result-typed-srv-final-live` under n95 passed
readback, runtime identity and enabled Metal validation checks. The live-view
control with the patch disabled (`result-typed-srv-live-no-patch`) failed all
three formats: the observed dimensions were 64 rather than 4. The preceding
null-UAV regression passed in `result-typed-srv-uav-regression`. Rewritten
metallib shader-validation instrumentation is not independently verified.
Windows native behavior, other typed formats, indirect SRV indexing, and
post-recording descriptor changes remain unverified. No default behavior or
capability declaration changed.

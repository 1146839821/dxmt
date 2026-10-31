# MSC SampleLevel minimum LOD: runtime sidecar design

Status: experimental compute-only test path, 2026-10-31. Original design
baseline `d8eb1bbd` on `feat/d3d12`. The AIR transform is used only when the
explicit test switch is set; ordinary runtime compilation remains unchanged.
This does not claim that ResourceMinLODClamp is implemented.

The subsequent sampler metadata slice preserves the original static and dynamic
sampler LOD ranges and copies dynamic ranges with descriptors. Normal runtime
execution does not bind a GPU sidecar. Relaxed Metal sampler states
are created lazily and retained by the owning descriptor heap or root signature;
descriptor copies retain the state when already created. A shader-visible
sampler heap can now allocate a separate, stable-address sidecar buffer on
request and keep its entries synchronized with descriptor writes and copies.
The heap owns the buffer and relaxed states. A recording retains each bound
sampler heap, and each queue submission retains those heaps until its GPU
command buffer completes, including after command-list reset. Static sampler
uploads and sidecar binding are implemented only in the test compute path;
broader shader access and residency remain unverified. An internal recording
hook records sidecar addresses for submission-time checks.
Recordings also retain root signatures used to encode static samplers, and
submissions retain them through GPU completion. This preserves both ordinary
static sampler states and any relaxed states owned by those roots, but does not
itself upload or bind a static-sampler LOD sidecar.

Preparing an already valid sidecar does not rewrite its GPU-visible entries.
When preparation recovers from an update failure, it rewrites only failed
slots, avoiding writes to unrelated slots that may be in use by the GPU.
An update that cannot create a relaxed sampler clears that sidecar entry and
marks the heap sidecar invalid; preparation must be repeated successfully before
using it. Since an already encoded GPU address cannot report such a later
failure on its own, future integration must invoke the recording hook when it
binds the sidecar. The queue validates recorded addresses before submission;
lists without recorded sidecars are unaffected. Do not bind this buffer yet.
The current validity flag covers the entire heap, so failure of an unrelated
entry can conservatively reject a recorded table. Submission failure follows
the queue's existing device-removal policy. Bundle replay must record uses on
the parent; the hook itself rejects bundle command lists.

The root signature can derive an experimental sidecar extension from reflected
MSC top-level offsets and its parsed sampler-table ranges. The shared layout
type is also accepted by the AIR transform. The extension sorts offsets and
appends aligned pointer slots; local roots, directly indexed sampler heaps and
ambiguous mappings are rejected. Only the test compute path consumes this
layout; no production pipeline cache version has changed.
An experimental AIRCONV C entry point now accepts table offsets and applies the
strict transform to an MSC metallib, distinguishing unsupported, unchanged,
and invalid input. It is not called by the WineMetal compilation path. The
native AIRCONV build disables C++ exceptions; allocation-failure behavior of
the LLVM transformation and metallib writer remains unverified and must be
addressed before a runtime caller relies on this path.
An explicit WineMetal thunk now transports the offsets and copies the patch
result using a size-query protocol; it does not use the shader cache or alter
normal compilation. Both 64-bit and WOW64 thunk paths have been exercised with
the same real MSC metallib; their resulting bytecode hashes match. This does
not validate DXMT pipeline cache integration or end-to-end shader binding.

## Goal and evidence

The test-only compute integration on 2026-10-31 patched a real
MSC shader after conversion-cache lookup and bound the sidecar through DXMT.
On the local Apple M1 Max, the ordinary SampleLevel clamp matrix and the
ordinary/reserved empty-view matrix matched their expected values with Metal
validation enabled. The two-mip reserved case returned zero for clamp 1.5;
the previous test expected 3.0, but the D3D functional specification defines
an empty view when the resource clamp exceeds the final view mip. The test
oracle was corrected accordingly. With the patch disabled, the ordinary clamp
matrix failed. This is evidence for the tested paths only, not a production
feature. See [D3D11.3 functional specification, empty-set cases](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#58-per-resource-mipmap-clamping).
The test-only patched compute PSO now snapshots its serialized root signature
and rejects dispatch with a different or missing signature; switching between
patched and unpatched compute PSOs dirties root arguments. This conservative
identity check is not a substitute for a production cache and ABI design.
Experimental patched compute PSOs reject cached-blob export and pipeline
library storage; while the patch test switch is enabled, compute pipeline
library loading is rejected to avoid reusing an unpatched in-memory PSO. The
normal conversion cache holds only the original MSC output. This is a test
guard, not a versioned production cache key or a serialized patched pipeline.
Local compute tests also passed with separately created root-signature objects
containing the same serialized data, and after switching from an unpatched PSO
to a patched PSO without resetting the root arguments. These cases exercised
ordinary and reserved textures; a reverse switch and bundle replay remain
unverified.
The test-only compute path also passed with a dynamic sampler table at a
nonzero heap offset after replacing its descriptor between Close and queue
submission. Omitting the replacement changed the readback and failed the same
oracle. This does not cover multiple tables, concurrent submissions, or all
descriptor mutation and failure cases.
An additional compute test uses two independent sampler root tables at
nonzero offsets in the same heap, with different LOD ranges. Ordinary and
reserved texture readbacks matched; swapping the table handles failed the
oracle. A separate test switched between two sampler heaps within one command
list, rebound the descriptor tables, and verified both dispatch outputs;
using identical ranges in both heaps failed the distinguishing oracle.
Concurrent submission semantics remain unverified.
Another compute test submitted the same closed list twice, waited for the
first GPU completion, changed a dynamic sampler, and verified both readbacks.
Omitting the change failed the oracle. This does not exercise simultaneous
execution or mutation while a submission is in flight.

The opt-in regression matrix can be run with
`python3 tests/dx12/run_msc_lod_experiment.py --stage <stage> --prefix <prefix> --build <build> --logs <logs>`.
The caller must prepare an isolated Wine stage and prefix using
`docs/DEVELOPMENT.md`, populate it with the matching build's executable,
D3D12/DXGI and WineMetal PE/Unix pair, and provide `sample.cso`, `multi.cso`,
and `grad.cso` (or override their names). D3D12 and DXGI must be native PE
images rather than Wine-builtin-marked images. The runner checks hashes,
loaded paths, and the first 16 loaded bytes of one D3D12 and one WineMetal
export against their staged PE images, along with validation activation,
expected output, and negative controls;
it writes logs to a new directory and does not install binaries. A skipped
reserved-texture case is reported as a failure, not as a pass.

The multi-queue experiment records two independent direct lists with separate
UAVs and readbacks, sharing a read-only texture and sampler heap. A CPU fence
holds the second queue while the first completes; both lists are reset with
independent allocators and the application retains the sampler heap until
both submissions complete. Both readbacks are checked. This demonstrates overlapping outstanding work,
not simultaneous GPU execution. The control deliberately corrupts the second
expected output. The runner accepts repeated `--case` options to select cases.
The test does not mutate descriptors while either queue may use them and does
not submit a command list again while it is in flight, consistent with the
[D3D12 command-list execution rules](https://learn.microsoft.com/en-us/windows/win32/direct3d12/design-philosophy-of-command-queues-and-command-lists)
and [descriptor-table lifetime rules](https://learn.microsoft.com/en-us/windows/win32/direct3d12/advanced-use-of-descriptor-tables).
Native Windows behavior and simultaneous GPU execution are not verified here.

Multi-queue test design card:

- Goal: validate experimental sidecar use by two outstanding queue submissions.
- Baseline: `3609008e`; the previous probe covered only one queue at a time.
- Evidence: the Microsoft command-list and descriptor rules linked above;
  Mullion `b580492a` has independent D3D12 queue tests, but was not used as a
  normative oracle for this experiment.
- Scope and boundary: test-only compute probe and runner; independent lists,
  allocators, output buffers, and readbacks; one shared read-only texture and
  sampler heap. Production shader and queue behavior are unchanged.
- Non-scope: parallel GPU execution, descriptor mutation in flight, Windows
  native behavior, graphics stages, and public capability promotion.
- Validation: exact ordinary/reserved outputs and a deliberately wrong oracle,
  matching runtime identities, Metal API and shader validation, and builds.
- Risk: a blocked submission proves overlapping outstanding work, not actual
  concurrent hardware scheduling; a passing test covers this shader shape only.
- Rollback: revert the test, runner, and this documentation together.
- Done: both output sets match, the negative control fails, validation is active,
  and the test and existing relevant regressions build and run successfully.

Fragment conversion probe design card:

- Goal: determine whether a real MSC fragment `SampleLevel` metallib can pass
  the experimental transform before planning graphics-stage integration.
- Baseline: `294b3aae`; the auto root argument discovery accepts only one
  `air.kernel`, and the opt-in patch and sidecar upload are compute-only.
- Scope: compile `msc_lod_fragment.hlsl` as `ps_6_0`, serialize a two-table
  root signature, obtain MSC's reflected offsets, and invoke the existing
  WineMetal transform through `dx12_msc_lod_fragment`. No graphics PSO or
  production path is modified.
- Evidence: on MSC API 4.0.1 the fragment was rejected by auto-discovery;
  exactly one explicit index in 0..31 passed the strict transform and an
  overlapping sidecar layout was rejected. A compute shader supplied as a
  fragment was rejected by MSC. These are local conversion observations, not
  proof of ABI correctness or rendered pixel behavior.
- Risk: a structurally accepted explicit index is not a safe substitute for
  stage-aware metadata validation. Root identity, stage visibility, residency,
  dirty-state transitions, and cache compatibility still need design and GPU
  verification. Native Windows and other MSC versions were not tested.
- Rollback: remove the probe, fixture, build target, and this card together.
- Done: real fragment conversion, strict auto rejection, explicit transform,
  invalid-layout control, matching runtime identity, and build checks complete.

Fragment metadata discovery design card:

- Goal: identify a fragment shader's root argument from its AIR entry metadata
  without guessing an argument index.
- Baseline: `7ffc4fe0`. On MSC API 4.0.1, the fragment has one `air.fragment`
  entry whose input metadata identifies an indirect buffer at binding 2.
- Scope: extend the existing strict single-entry discovery to accept either one
  kernel or one fragment. Reject mixed stages, multiple entries, duplicate
  binding locations, missing location, non-constant-address-space pointers,
  and sampling hidden in another function. Preserve the compute checks.
- Non-scope: graphics PSO integration, vertex/mesh stages, actual rendering,
  descriptor binding, and public capability claims.
- Validation: synthetic malformed metadata and compute regressions; an MSC
  `ps_6_0` fixture must produce identical automatic and explicit patched
  metallibs. Run the existing compute experiment with matching runtime images.
- Risk: MSC metadata varies across versions; unknown forms fail closed. A
  successful transform does not verify rendering or sidecar lifetime.
- Rollback: revert discovery and associated tests together.
- Done: the real fragment fixture and relevant negative/compute controls pass,
  along with the build and runtime identity checks.
With the test-only patched path, injecting relaxed-sampler creation failure
after Close invalidated the recorded sidecar and queue submission rejected the
list with E_FAIL. An unpatched control did not register a sidecar and failed
the rejection oracle, as expected.
A retry of that descriptor update before submission restored the sidecar and
both ordinary and reserved compute readbacks passed. The paired no-retry case
was rejected at submission.
With GPU execution gated, another test submitted a patched compute list,
reset and released the list, and released the application's sampler heap
reference. A private-data marker remained alive until completion and was then
released; ordinary and reserved texture readbacks matched. This tests one
in-flight submission, not concurrent submissions on multiple queues.

Enable correct sampler and SRV minimum-LOD ordering for the explicitly
validated 2D SampleLevel AIR form, while preserving descriptor mutation before
submission and sampler lifetime through execution. Other AIR forms and
Texture.Load need separate solutions. The measured behavior, hardware and
converter identity, and native prototype limits are in
`D3D12_MSC_MIN_LOD_AUDIT.md`.

**SPEC_REQUIRED:** Root Signature 1.0 descriptor tables and 1.1 volatile
descriptor ranges permit descriptor replacement after recording and before
submission; they cannot be modified while a referencing execution is in flight.
See [Microsoft Root Signature 1.1](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1).
Static descriptor ranges have a longer application promise. The implementation
must also obey the SRV and sampler clamp ordering in the functional spec cited
by the audit. Native prototype observations do not generalize to all formats,
stages or devices.

**SOURCE_OBSERVED:** `src/d3d12/d3d12_descriptor_heap.cpp:840-1032` stores
dynamic sampler objects and separately mapped AIRCONV/MSC tables; creation
and `CopyDescriptors` update the tables. `src/d3d12/d3d12_root_signature.cpp:488-501`
encodes static sampler handles and bias but does not retain their D3D MinLOD
and MaxLOD in that encoded array. `src/d3d12/d3d12_command_list.cpp:3961-4037`
uploads a per-use MSC top-level argument buffer and separately encodes static
samplers. A later sidecar cannot infer the D3D clamp range from those encoded
handles. Resource descriptor submission resolution at
`src/d3d12/d3d12_command_list.cpp:4244-4410` does not by itself establish
sampler-sidecar ownership or lifetime.

## Proposed boundary

The experimental sidecar is separate from the MSC-owned descriptor encoding.
Each sampler table entry needs a retained relaxed sampler, its GPU resource
ID, and the original D3D sampler minimum and maximum LOD. Its entry stride,
alignment and field offsets must be verified on both sides of the PE/Unix
boundary and against the AIR transform. Preserve the original sampler for
operations that are not transformed. Static samplers need the same data,
owned by the root signature or by another owner with an equivalent lifetime.

Dynamic descriptor writes and copies must update the corresponding sidecar
entry, including CPU-only source heaps, nonzero table bases, and multiple
tables. Handle invalid descriptors and allocation failures without publishing a
partially initialized entry. A shader-visible sidecar must be resident whenever
the shader dereferences it. Retain the heap, sidecar allocation, and relaxed
sampler objects through GPU completion; establish this for each heap actually
referenced by recorded commands, including heap changes and bundles. Existing
resource-descriptor retention is not evidence that this sampler lifetime is
already satisfied.

Append sidecar pointers outside the MSC-reflected top-level argument buffer,
with checked arithmetic and a deterministic mapping from each actual sampler
table layout to one appended slot. Use reflected MSC offsets for existing
entries. The compiler and command-list encoder must consume the same immutable,
versioned mapping; never interpret an unrecognized AIR form as successfully
covered. Reconcile direct indexing and static-sampler layouts before enabling
them. `src/d3d12/d3d12_pipeline_persistence.cpp:28,692,1176-1195` includes a
binding-layout version in persistent cache validation; any changed runtime ABI
must invalidate incompatible cached pipelines and distinguish patched from
unpatched shader binaries.

The current transform recognizes only a narrow 2D explicit-level instruction.
`Unchanged` and `Patched` do not certify other instructions. Runtime integration
must identify all relevant sampling operations and fail safely where their
semantics cannot be established; do not turn on the pass as a global fix based
on the current return value. Empty views, swizzles, status results, null
descriptors, sampler bias and LOD edge cases need independent validation.

The transform also provides an opt-in conservative preflight that rejects
indirect calls, non-call invocations, implicit-level calls, and external calls
other than the recognized sample and LLVM intrinsics before modifying a module.
Defined helpers are scanned too. This deliberately rejects other AIR intrinsics
until their behavior is established. It does not prove converter-version
compatibility or the semantics of null descriptors, status values, or numeric
edge cases. Only the test compute path requests this preflight. A compute
pipeline containing SampleGrad was rejected with E_NOTIMPL under the test
switch; an unpatched SampleGrad pipeline and a patchable SampleLevel pipeline
both failed the rejection oracle, as expected.

The experimental patch API also accepts an auto-detection sentinel for a
single compute entry point. It identifies the AIR argument tagged as an
indirect buffer at the MSC argument-buffer binding point; missing, malformed,
or ambiguous metadata is rejected. The argument ordinal is not a fixed ABI:
it varies with the shader. This discovery is currently limited to compute and
does not establish compatibility with arbitrary MSC versions or helper calls.
It is not connected to production compilation, cache keys, or argument-buffer
uploads; unsupported shaders must not silently use a mismatched layout.

## Rejected shortcuts

- A record-time snapshot of dynamic descriptors misses legal pre-submission
  descriptor replacement.
- Reusing the original sampler for the transformed operation cannot enforce
  resource clamping above the sampler maximum in the tested Metal behavior.
- Expanding only the command-list upload without adding retained static and
  dynamic sampler metadata leaves missing inputs and unsafe resource IDs.
- Treating unsupported AIR as an unmodified successful shader produces silent
  wrong results; promoting capability claims would be unsupported.

## Implementation and verification gates

1. Define a private versioned sidecar ABI and implement sampler creation,
   copying, ownership, static sampler metadata and allocation failure paths.
   Verify CPU-only to shader-visible copies, replacement, nonzero bases,
   multiple heaps/tables and destruction with focused tests. Do not enable the
   shader transform in this step.
2. Connect layout generation to reflection and top-level uploads, with explicit
   residency and submission lifetime for every referenced heap and sampler.
   Validate rebinding, repeated submissions, bundles, pre-submit mutation and
   allocation failures. Invalidate incompatible persistent caches.
3. Add fail-closed conversion coverage and enable only after the entire path is
   validated with real DXMT compute and graphics output readbacks, native
   Windows comparisons where available, Metal API/shader validation and
   matched WineMetal binaries. Cover empty views, fractional clamps, sampler
   MinLOD/MaxLOD conflicts, copies, static and dynamic samplers, and negative
   controls. Texture.Load and other unsupported operations remain separately
   tracked; capability claims remain unchanged until all obligations pass.

Risks include Metal sampler restrictions, sampler IDs outliving their objects,
shader cache collisions, and unrecognized converter output. Roll back an
integration by disabling the transform and reverting the versioned ABI and its
binding changes together; never leave a patched shader with an old TLAB layout.

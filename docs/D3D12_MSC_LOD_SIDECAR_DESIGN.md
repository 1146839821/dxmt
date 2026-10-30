# MSC SampleLevel minimum LOD: runtime sidecar design

Status: design only, 2026-10-30. Baseline `d8eb1bbd` on `feat/d3d12`.
The experimental AIR transform is not called by runtime compilation. This
design does not claim that ResourceMinLODClamp is implemented.

The subsequent sampler metadata slice preserves the original static and dynamic
sampler LOD ranges and copies dynamic ranges with descriptors. It does not yet
bind a GPU sidecar or prove submission lifetime. Relaxed Metal sampler states
are created lazily and retained by the owning descriptor heap or root signature;
descriptor copies retain the state when already created. A shader-visible
sampler heap can now allocate a separate, stable-address sidecar buffer on
request and keep its entries synchronized with descriptor writes and copies.
The heap owns the buffer and relaxed states. A recording retains each bound
sampler heap, and each queue submission retains those heaps until its GPU
command buffer completes, including after command-list reset. Static sampler
uploads, sidecar binding, and submission-time sidecar validation remain
unimplemented; actual shader access and residency are not yet verified.

An update that cannot create a relaxed sampler clears that sidecar entry and
marks the heap sidecar invalid; preparation must be repeated successfully before
using it. Since an already encoded GPU address cannot report such a later
failure on its own, future integration must validate again at submission and
must retain the heap through GPU completion. Do not bind this buffer yet.

## Goal and evidence

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

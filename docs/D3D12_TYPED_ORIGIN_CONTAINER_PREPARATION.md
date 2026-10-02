# Production typed-origin container preparation

## Task Analysis

Baseline `a0bcced`, clean worktree. Continue implementation gaps first. The
Wine boundary now exists, but container regeneration lives only in an offline
tool. Hypothesis: a D3D12-owned preparation artifact provides the required
validated shader and binding order for compiler-root/cache/binding integration.
Evidence: current converter forwards original bytes; native lowering emits IR,
not a full validated container. Expected effect: a callable component built into
d3d12.dll, with immutable owned bytecode and ordered binding records. Risk:
DXC module lifetime, auxiliary container semantics, partial publication and
loading a stale Unix library. Validation: both production builds and focused
real-container calls through matching PE/Unix binaries, not a new full matrix.

## Contract

`PrepareD3D12TypedOriginShader` selects compiler and validator DLLs from an
explicit absolute Windows drive directory. It copies the input into a DXC blob,
fully validates it, inspects the supported container envelope, extracts bitcode,
calls the production Wine lowering boundary, assembles the returned IR and
fully validates/inspects the regenerated container. DLLs outlive their COM
objects. Owned bytecode and binding order publish together only on success.
The artifact carries lowering schema version 1 for the future cache identity.

The current envelope is SM6.0 compute with DXIL, signatures, reflection, hash,
PSV and feature parts only. Embedded roots, libraries, debug and unknown parts
reject rather than losing their semantics through assembly. The assembler
regenerates auxiliary shader metadata; no byte-identical-container claim is
made. This is not a replacement for later stage/handle/array implementation.

The ordinary converter/PSO path is intentionally unchanged: passing the
transformed shader without augmented compiler root and matching origin records
would be incorrect. Root reservation/remapping, transform-aware cache lookup,
descriptor selection and recording/submission metadata ownership are still
required. No environment switch silently enables the incomplete ABI, and no
DXIL-to-AIRCONV fallback is added.

## Task Result

Both production DLLs and the focused executable built in normal/no-private
variants. Existing Meson regressions passed 3/3 in each. The focused fixture
compiles the same production preparation source; it is not a PSO/DLL entry-point
test. Real Wine PE/Unix calls passed FLOAT (3168 -> 4008 bytes) and CFG/loop/atomic
(3600 -> 4652 bytes) in both variants, including exact binding identity/order,
malformed-container rejection and unchanged complete artifacts on failure.
Native direct-helper/boundary equivalence also passed in both variants.

Initial staging mixed the old installed PE export table with the new Unix
library. Explicit export lookup now rejects old runtimes without invoking Wine's
missing-import stub. Successful runs use a full isolated runtime layout, with
copied ntdll/loader, linked unchanged Wine support files and the selected new
PE/Unix libraries. PE staging applies the repository's `winebuild --builtin`
postprocessing; this intentionally changes the copied PE image from raw build
bytes. Loader logs and matching final Unix/executable SHA-256 pairs are in
`BUILD/typed-origin-prepare-{cfg,float}-final-overlay.log` and
`BUILD/typed-origin-prepare-provenance.log`. Temporary roots are under
`/Users/zhangbo/.cache/dxmt-origin-boundary.UEy0AS/`, including `no-private/`.
No existing game DLL or installed Wine Unix library was replaced.

The loader issue is consistent with upstream Wine's installed DLL directory
preceding WINEDLLPATH and its runtime root being derived from ntdll's canonical
location; the actual loaded paths, not that inference, establish run provenance.
[Wine loader source](https://github.com/wine-mirror/wine/blob/master/dlls/ntdll/unix/loader.c).

## Standards

Independent review found allocation exceptions escaping the PE HRESULT boundary
and inconsistent native error translation. Both were corrected. Main review
also added a native local allocation-exception catch: PE cannot catch host C++
exceptions across Wine. Only this helper enables exceptions in an isolated
static library; AIRCONV and unrelated runtime flags remain unchanged. Final
targeted review found no remaining findings. No allocation-failure injection or
arbitrary-malformed-bitcode security guarantee is claimed.

## Spec

Independent review found no scoped implementation bug, but identified weak
binding-order and unchanged-binding checks in the probe. The probe now checks
both binding identities and complete contents on failure. Final targeted review
found no new findings. MSC skills kept root-layout changes and GPU acceptance
separate from container validity.

Review summary: Standards two findings resolved plus main boundary fix; Spec
zero implementation findings, two probe gaps strengthened. No game deployment,
MSC compilation, GPU dispatch, game acceptance or feature-level promotion.
Next: compiler-root reservation/reflected mapping and transform-aware cache
identity, followed by the production binding and readback closure.

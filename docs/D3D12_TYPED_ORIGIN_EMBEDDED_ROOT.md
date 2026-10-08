# Typed origin embedded compute roots

## Task Analysis

- Hypothesis: retaining a parsed embedded root and its original bytes permits
  existing augmented-root preparation without silently discarding root semantics.
- Evidence: compute conversion reads RTS0 but only retains explicit root objects;
  preparation rejects RTS0 before lowering.
- Expected effect: embedded typed compute PSOs feed the same augmented-root,
  cache and static/live dispatch contract as explicit-root PSOs.
- Risk: mismatched bound/selected roots or carrying the old root into a shader
  that now needs a private CBV. Preserve application identity separately;
  transformed shader must not embed the old root. Explicit PSO roots override.
- Validation: both builds, actual embedded PSO static/volatile readback, explicit
  regression and mismatch rejection. No capability promotion.

Review correction before finalization: explicit PSO roots override embedded roots
([Microsoft root creation rules](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-root-signature)).
Only implicitly selected roots need embedded identity matching. The mismatch
negative must bind a different command-list root to an implicit-root PSO, not
reject a compatible explicit override solely for different serialized bytes.

## Task Result

Compute PSOs without explicit roots now retain a root object created from the
original shader container. Typed preparation accepts one validated input RTS0
and retains its raw payload in the artifact, while final output inspection
continues to reject embedded roots: the transformed shader is paired with an
external augmented compiler root, not the incompatible original root. Cache
version advances to 6. MinMax envelope admission is unchanged.

Implicit-root variants check their preserved embedded identity; explicit roots
remain authoritative and may override a different embedded serialization.
Existing dispatch recording checks the selected/bound augmented roots. Root
creation uses the public device method, preserving the test-linked factory's
dependency boundary; the initially unresolved internal factory link was fixed.

Evidence: `/Users/zhangbo/.cache/dxmt-typed-embedded.xbZ1SA`.

- Both reconfigured full builds and final reviewed rebuilds succeed. Host suites
  pass 5/5 in each variant; diff whitespace passes.
- Final libraries pass eight actual complete-buffer GPU readbacks: two builds
  each with implicit embedded volatile/static roots, a distinct explicit root
  overriding embedded static flags, and an existing explicit no-RTS0 regression.
- Both builds reject a different command-list root for an implicit-root PSO at
  RecordD3D12TypedOriginDispatch with E_INVALIDARG. No submission follows.
- Embedded fixtures are SM6.6 divergent two-lane arrays. Runtime preparation
  validates original and regenerated containers through selected DXC before MSC.
  Initial load logs identify task-owned D3D12 and deployed DXC paths.
- Task-owned runtime/application copies only; no game/prefix DLL changes.

Standards self-review: resource ownership, original identity and cache separation,
reused guarded binding and unchanged MinMax envelope checked. Spec self-review
found and corrected an overly strict explicit/embedded byte-match condition using
the Microsoft rule cited above; explicit override now has a GPU success oracle.
No remaining finding in this scope. Main-agent reviews, not independent reviews.
MSC integration guidance kept application and augmented roots distinct.

Graphics, root-updating indirect, complete format/lifetime qualification and
approved unconditional compiler distribution remain open. No FL promotion,
game benchmark or tessellation acceptance.

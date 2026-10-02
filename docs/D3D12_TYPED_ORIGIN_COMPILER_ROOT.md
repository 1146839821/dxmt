# Typed-origin compiler root preparation

## Task Analysis

- Hypothesis: appending a private CBV keeps application parameter indices stable,
  but requires independent MSC reflection, including the implicit static sampler entry.
- Evidence: the existing root object owns only application blob, staging and MSC layout;
  the validated origin shader requires b0/space1 and cannot use that unchanged layout.
- Expected effect: a root-owned immutable compiler artifact can be reused by later PSO
  integration without rewriting application binding state.
- Risk: CBV namespace collision, exceeding 64 DWORDs, static sampler relocation,
  retained decoded-root storage and mismatched reflection.
- Validation: both build variants, actual DLL root-object probe, focused regression tests
  and independent source reviews. Full GPU matrices follow implementation closure.

## Scope

The actual root object lazily prepares and caches owned serialized RS1.1 bytes and
MSC-reflected locations under a mutex. The application must retain the root while
using the borrowed const artifact. Application bytes, layout and staging are separate.
RS1.1 flags are copied; RS1.0 normalization retains the existing volatile conversion.
The hidden ALL-visible root CBV is DATA_VOLATILE. Future origin storage still needs
immutable per-submission lifetime through GPU completion.

Preparation rejects b0/space1 CBV collisions, local roots and application cost above
62 DWORDs. It consumes trusted decoded descriptors, not arbitrary root-signature
bytes; this does not harden the existing raw deserializer against malformed offsets.
PSO/cache identity, encoder binding and GPU dispatch integration remain pending.
There is no default enablement or FL/SM capability promotion.

## Task Result

Normal and no-private production DLLs and the root-object fixture built successfully.
Both final Wine runs returned exit 0 and `typed-origin production root PASS`.
The fixture calls the actual DLL's CreateRootSignature and cached preparation method,
not a separately compiled mock builder. Loader logs select the staged native d3d12.dll
and the matching variant's isolated winemetal.so; built/staged d3d12 SHA256 pairs match.
Installed Wine/game DLL files were not replaced.

Focused cases: RS1.1 DATA_STATIC range and application root flags, RS1.0 volatile
range conversion, unbounded UAV range, exact 64-DWORD compiler root, application
63-DWORD rejection, constants b0/space1 collision rejection, sequential cache reuse,
and unchanged application blob/layout/staging. These are six cases per variant,
not a full GPU matrix. Representative reflection: constants=3 places hidden CBV
at byte 24 and static-sampler entry at byte 32; constants=61 yields 256 and 264.
Production code uses reflection rather than these observed offsets.

Meson focused regressions passed 3/3 in each variant; `git diff --check` passed.
Independent Standards and Spec reviews found no confirmed scoped production defect.
Named private binding constants, compile-time type checks and failure diagnostics
address the Standards observations. Remaining probe coverage includes root-descriptor
flags, alternate collision forms and concurrent initialization; these have not been
claimed tested. Static-sampler reflection is not an independent second oracle.

Final runtime receipts (ignored build outputs):
`build/typed-origin-root-runtime-final.log` and
`build-no-private/typed-origin-root-runtime-final.log`.
No MSC shader compilation, PSO dispatch, GPU readback, tessellation/game acceptance,
performance claim or capability promotion occurred in this increment. No push.

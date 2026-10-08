# Typed-origin compute PSO integration

## Task Analysis

- Hypothesis: retaining application CS/root on the real compute PSO permits lazy
  preparation of a distinct Metal PSO without modifying application cache identity.
- Evidence: production compute creation currently forwards only original CS/root;
  container/root preparation does not create a native pipeline.
- Expected effect: a reusable PSO-owned variant carrying compiler root, ordered
  resource identities and application table locations for encoder integration.
- Risk: stale bytecode pointers, ambiguous table mappings, cache identity collisions,
  RS1.0/1.1 flag loss and premature dispatch without coherent submission metadata.
- Validation: both builds, real DLL variant creation, focused regressions and review.

The converter key includes the transform/binding versions and ordered resource
identities, as well as the existing transformed shader/root/MSC environment inputs.
Ordinary cache keys remain unchanged. Binding locations are freshly resolved from
the owned compiler root; they are not reconstructed from cached metallib reflection.
The application PSO retains original CS and an explicit root; embedded-root and
unsupported lowering paths remain rejected by this private preparation method.
Successful variants are immutable and tied to one explicitly selected DXC directory.
No directory is discovered globally and no DXIL/AIRCONV fallback is introduced.
Retaining original CS and root adds per-MSC-compute-PSO memory/lifetime overhead;
this increment does not include a memory/performance measurement.

Encoder selection, per-submission origin records and private descriptor tables are
still required before dispatch can use this variant. Existing default dispatch and
static/volatile descriptor residency paths are unchanged. This is an incremental
implementation step, not closure of the typed-UAV or FL12_0 goal.

## Task Result

Both production DLLs and the actual-DLL probe built successfully after Meson
reconfiguration. Final Wine runs returned exit 0 in normal and no-private variants.
Each run created three application PSOs and three distinct private Metal pipelines
using the existing validated scalar-FLOAT SM6.0 container corpus.

The probe overwrites caller CS bytes and releases its root reference before private
preparation; the retained inputs remain usable. It verifies ordered u0/u1 identities,
application table offsets 5/6, reflected threadgroup size 4/1/1, unchanged original
PSO, same-PSO variant reuse, invalid relative DXC directory rejection and rejection
of changing the selected directory after success. RS1.1 uses explicit offset 5 and
DATA_STATIC; RS1.0 uses APPEND following a five-entry SRV range and preserves the
existing DESCRIPTORS_VOLATILE/DATA_VOLATILE conversion. The leading range's space7
does not establish nonzero-space coverage for the actual target UAVs in space0.

With `DXMT_SHADER_CACHE=0`, both final logs show two typed-origin conversion cache
misses (different compiler roots) followed by a typed-origin memory cache hit on
the third PSO. This proves observed miss/hit behavior, not every key-mutation or
persistent-cache invalidation dimension. Those checks remain deferred.

Loader logs select staged native d3d12.dll and the matching isolated winemetal.so;
each built/staged d3d12 SHA256 pair matches. Installed Wine/game DLL files were not
replaced. Ignored receipts: `build/typed-origin-pso-runtime-final.log` and
`build-no-private/typed-origin-pso-runtime-final.log`.

Meson regressions passed 3/3 in each variant; `git diff --check` passed. Independent
Standards and Spec reviews found no confirmed scoped correctness defect. Shared
native pipeline construction addresses the duplication observation; added APPEND,
RS1.0 and cache logging address part of the focused coverage observations. Ambiguous
mapping rejection, target nonzero spaces, visibility negatives, concurrent preparation
and complete cache-key mutation coverage are not claimed tested. Main self-review
checked ownership/publication, root/parameter mapping and unchanged default routing.

No dispatch/readback, game acceptance, full typed-UAV matrix, performance improvement
or FL/SM promotion is claimed. MSC compilation/binding skills guided separate compiler
root/cache identity and deferred submission-safe encoder selection. Local commit only;
no push. The next implementation gap is immutable per-submission origin records and
private descriptor-table generations before encoder selection.

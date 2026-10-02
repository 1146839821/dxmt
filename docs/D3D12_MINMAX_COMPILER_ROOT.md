# MinMax compiler root and binding locations

## Task Analysis

- Hypothesis: reuse serialized/reflected compiler-root preparation, append three
  private parameters and resolve pair identities to preserved application slots.
- Evidence: validated MinMax artifacts require b0/space2, N point SRVs and 2N
  samplers; offline linear binding is not an application root integration.
- Expected effect: owned compiler root with unchanged application parameter
  indices/flags and explicit static versus heap sampler locations for replay.
- Risk: DWORD budget, namespace collisions, APPEND/unbounded offsets, static
  sampler relocation, ambiguous bindings and shared typed-origin regressions.
- Validation: both builds, isolated Wine root reflection/location probes and
  existing typed-origin root regression. No PSO/submission admission claim.

## Implementation

The actual RootSignature object now owns immutable MinMax compiler roots keyed
by pair count (1..64). A mutex protects publication; std::map keeps borrowed
artifact addresses stable when another count is prepared. The caller must keep
the application root alive. Application blob, staging indices and ordinary MSC
layout are not replaced.

The shared compiler-root preparation engine owns serialized RS1.1 bytes and
checks MSC reflection for every location's index/type/size, bounds and overlap.
Typed-origin retains its b0/space1 two-DWORD contract. MinMax reserves all space2
bindings and four DWORDs, appending these parameters after application indices:

1. b0/space2 state CBV (DATA_VOLATILE).
2. t[0,N)/space2 point SRV table (DESCRIPTORS_VOLATILE | DATA_VOLATILE).
3. s[0,2N)/space2 private sampler table (DESCRIPTORS_VOLATILE).

The reflected static-sampler entry, if present, remains last. The helper's private
input contract is explicitly restricted to b0 followed only by descriptor tables.

Pair location resolution matches compute-visible application SRV/sampler tables,
including explicit offsets, APPEND and bounded/unbounded ranges. It retains exact
range flags. Existing RS1.0 deserialization remains responsible for converting
legacy ranges to volatile. Static samplers carry their original index separately;
heap handles are not resolved here. Missing, ambiguous, graphics-only and
overflowing slots reject without publishing a location vector.

## Validation (2026-10-03)

Normal and no-private production DLLs and focused probes build successfully.
Both Meson host suites pass 4/4. Isolated Wine runs load the newly staged native
D3D12/DXGI DLLs and matching Winemetal runtime; installed/game DLLs are unchanged.

Both variants pass five focused root/location scenarios: RS1.1 and converted
RS1.0 heap samplers, RS1.1 and converted RS1.0 ordinary static samplers, and a
legal unbounded trailing SRV range. Application cost60 + private4 reaches the
64-DWORD boundary. Checks cover actual RootSignature cache pointer stability
across pair counts, original blob/upload/layout preservation, reflected private
locations and static-table ordering. The resolver/negative helper probes compile
the same production source directly, since internal helpers are not DLL exports.

Deserialization independently checks private b0/N/2N, range type/space/register,
offset, visibility and flags. Negatives cover missing/duplicate/graphics-only
bindings, static/table ambiguity, APPEND after unbounded overflow, N=0/65,
private-space constants/root CBV/SRV/UAV/table/static sampler collisions, and
insufficient DWORD budget. Failure checks compare all compiler-root fields and
the complete previous location vector. These are focused contracts, not a full
binding or GPU matrix.

Existing typed-origin production root regression passes in both variants,
including cached identity, RS1.0/1.1 flags, unbounded range, static-sampler
relocation, maximum valid cost and private-CBV/budget rejection.

Receipts: `/Users/zhangbo/.cache/dxmt-minmax-root.vBmrvA/{normal,no-private}/`
`dx12_minmax_root.final.log` and `dx12_typed_origin_root.final.log`, plus matched
build/staged hashes in `provenance.sha256`.

## Standards

Independent source review: no hard violations, three advisories (duplicated table
walk, generic private-layout input and sentinel location alternatives). The private
layout contract is now explicit and validated. Lookup consolidation and stronger
location alternatives are deferred; static and table results currently initialize
disjoint sentinel states and are tested separately.

## Spec

Independent source review: no confirmed implementation defect; two focused
coverage gaps (rejection paths and private-range/failure-preservation oracles).
The additional deserialized-range and negative checks above address those gaps.
Fresh runtime results are main-agent evidence, not subagent execution. Final
self-review and diff whitespace checks passed.

## Remaining production closure

PSO selection and recording/submission binding still need integration. Static
application slots must snapshot once, with no PendingDescriptorUse/live reread;
volatile slots resolve and retain uniquely under the heap lock, then encode
outside it. Ordinary/reduction private samplers and point views must retain
native ownership through GPU completion. Existing static reduction root creation
admission is not changed here; static-sampler probes use ordinary linear filters.
This does not prove an augmented-root shader compile, D3D12 MinMax GPU dispatch,
full MinMax/FL12_0 support, performance or game/tessellation acceptance. No push.

# N15 — checked cross-resource texture mapping copies

2026-11-04. Evidence E: /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-texture-copy-20261104.

## Gate 0 / Gate 1 — Design Card

- Goal/current path: CopyTileMappings → texture owner → checked WineMetal copy → destination CPU publication. Prevent synchronous failures from being ignored or published.
- Baseline: feat/d3d12 at 07dc291bf5833fed4bc2518fe748cdb48524ae81; only original .gitignore dirty, hash764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759. Local origin left/right0/8. Live origin d1e83d41db5abb86d03bdbde4b7254c54debac6f and Mullion93de32b9145885ceb2e45be7d445e56d62499b3c rechecked; no fetch/push.
- Evidence: SOURCE_OBSERVED, src/d3d12/d3d12_texture.cpp:2076–2204 snapshots source mappings, publishes destination at2172–2175, then calls unchecked copyTextureMappings at2198. Whole-method preparation bad_alloc is not contained. Standard tiles and full contiguous packed tail share one native array/batch. Existing queue CopyTileMappings already closes handoff and marks OOM/removal terminal.
- SPEC_REQUIRED: Microsoft CopyTileMappings API returns void and copies reserved mappings; overlapping results use an original source snapshot. Regions must fit. https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-copytilemappings . Installed Metal MTL4CommandQueue.h:371–393 defines void texture copies, equal sparse page size and nonconcurrent alias use. Keep barriers/handoff and snapshot semantics.
- Reference: pinned Mullion src/d3d12/d3d12_command_queue.cpp:1356–1405 snapshots mapped flags, orders overlaps and uses its own Map/Settle owner with retained source/destination. Source reference only; no code transplanted. Windows native / Mullion runtime NOT_RUN.
- Scope/boundary: add checked texture-copy export/wrapper using existing packet/Unix ID/dispatch entries; catch preparation/snapshot bad_alloc; cross-resource checked native return before publication. Retain self-copy per-operation update path and publication behavior for a separate partial-batch slice. No new D3D12 source/header, scheduler or state table.
- Selection: reuse existing snapshots, operation array, locks and queue removal boundary. Avoid transaction/rollback framework and Mullion scheduler replacement. False opaque status is generic DXGI_ERROR_DEVICE_REMOVED, not an invented OOM cause. C++ bad_alloc is E_OUTOFMEMORY.
- Changes: d3d12_texture.cpp; winemetal.h, Metal.hpp, winemetal_thunks.c and unix/winemetal_unix.c; extend existing packed fixture's shared failure helper and add optional copy timing. Keep update failure CLI and existing GPU tests intact.
- Proof: prepare/snapshot/allocation/submitted ×actual standard/packed/mixed layouts ×mapped/NULL source ×first/second copy; invalid flags/empty preflight, exact reason/fence/MAX/Signal. Old-DLL ignore-result controls, debug thunk, actual normal GPU mapping/alias/NULL/source-remap and overlap/cross-queue oracles, adjacent buffer/update/barrier/D3D11. Isolated matched PE/Unix identities with validation; injected native errors drain real queue work before cleanup.
- Cost: no new default allocation/lock/scan/native batch/encoder/GPU wait; add hook environment checks and one checked return. Time repeated full mixed mapping copies with identical fixture/shaders/paired bridge and old/new DLL in alternating processes; warm batch median/P95/range, with API backpressure and post-timing GPU verification made explicit.
- Risks/capability: synchronous issuance is not async completion; terminal removal is not rollback; CPU table exclusion is source proof. Full async lifetime and real allocation pressure UNVERIFIED. Public FL/SM/Tier, Metal3 shadow path, D3D11 and Airconv/MSC unchanged. No Apple7 hardware blocker is inferred.
- Rollback/DoD: revert focused commit only. Gate0/1 before writes; validate build/static/positive/negative/cost and runtime identity, audit scope, preserve .gitignore; commit in repository style without push. User continuation authorizes this slice; stop after delivery, recommend only one next slice.

## Gate 2–4 — implementation and delivery evidence

**Task summary / findings:** cross-resource texture CopyTileMappings now issues its checked native copy before destination CPU-table publication. Synchronous false status returns generic DXGI_ERROR_DEVICE_REMOVED; preparation/snapshot bad_alloc is contained as E_OUTOFMEMORY. Existing queue EndSparseMapping executes before terminal removal. Source snapshots, standard operations and the collapsed complete packed-tail operation retain their existing construction and acceptance rules.

**Design decisions:** add one checked entry while retaining the void export, existing packet, Unix IDs and both dispatch arrays. Reuse the original source snapshot, source/destination locks, complete operation array and queue error owner. No transaction framework or Mullion scheduler is introduced. The existing same-resource update loop and its early CPU-publication order are source-identical; its native per-operation failures remain a separate known gap. Whole-method C++ OOM containment also covers its preparation, without claiming its native failures are fixed.

| Changed file / current anchor | Purpose / evidence type |
|---|---|
| src/d3d12/d3d12_texture.cpp:56,2094,2155,2186–2206 | Opt-in prepare/snapshot injection, function try/catch and checked cross-resource copy before CPU publication. SOURCE_OBSERVED. |
| src/winemetal/winemetal.h:2516 | Add checked export; existing void export and packets preserved. |
| src/winemetal/Metal.hpp:986 | Bool checked wrapper; old void wrapper preserved. |
| src/winemetal/winemetal_thunks.c:1702 | Return existing raw Unix call status, bypassing the debug-asserting void macro. |
| src/winemetal/unix/winemetal_unix.c:4394–4473 | Independent opt-in allocation/submitted copy failure counter, actual operation counts and cross-resource identity. Private completion event/wait only on explicitly injected native failures. |
| tests/dx12/dx12_reserved_texture_packed.cpp:128,726,877 | Shared update/copy failure fixture and copy timing CLI; retain existing update modes and GPU oracles. Distinct source/destination, mapped/NULL source and initially mapped destination, safe old-DLL drains, exact removal/fence/MAX/Signal and loaded export identity checks. |
| docs/sparse-texture-copy-status.md | Baseline, specification/reference comparison, design, validation, costs and delivery limits. |

**Actual layouts / native operations:** standard total4, standard_mips1, packed_mips0, testing2 tiles; packed total1, standard_mips0, packed_mips4, packed_tiles1; mixed total2, standard_mips1, packed_mips3, packed_tiles1. Native operations per call are2/1/2 and same_resource=0. The complete packed tail is one native operation. No fabricated capability result or assumed family exclusion.

| Check | Result / evidence |
|---|---|
| Full x64 Release default build | PASS, E/build.log. Existing .github/build_dxmt.sh feature/toolchain configuration plus tests; isolated paired deployment follows docs/DEVELOPMENT.md. No deployment into user installs. Existing unchanged shader fixtures reused by exact hash. |
| Release cross-copy failures | LOCAL_PASS:48 processes =prepare/snapshot/allocation/submitted ×3 actual layouts ×mapped/NULL source ×first/second nonempty copy. E/faults.json and candidate-{phase}-{layout}-{mapped,null}-{1,2}.log. |
| C++ OOM |24 prepare/snapshot cases; exact E_OUTOFMEMORY removal reason, pending fence wake/MAX and later Signal rejection. Source catch includes earlier BuildRegionTiles allocations as well. |
| Native failure |24 allocation/submitted cases; generic removal reason without guessing OOM. Injected allocation failure drains prior real queue work; submitted failure performs the real copy and waits on a private completion event before returning simulated false. |
| Preflight | Invalid flags and an empty copy remain nonterminal and do not consume the copy failure counter. Two expected E_INVALIDARG warnings per copy probe. |
| Debug-asserting thunk | LOCAL_PASS:12 processes =4 phases ×3 layouts at second copy; debug PE thunk only uses -UNDEBUG, other objects remain release. Actual new checked entry used in runtime. |
| Old-DLL controls | Six expected exit1 cases, allocation/submitted ×3 layouts at second copy. Exact retained07dc291b D3D12 DLL with the same newly built PE/Unix pair and executable; old code ignores failure. Controls drain and never shader-read inconsistent mappings. |
| Shared helper update regression | LOCAL_PASS:36 processes repeat the full prior update failure matrix after helper reuse, including remap/NULL, first/second call and invalid/SKIP preflight. E/updates.json and adj-update-* logs. Update production/native owner bytes unchanged. |
| Normal GPU readback | LOCAL_PASS:19 validated processes, E/positives.json. Mixed and pure-tail mapping copy/alias inheritance, full mip chains, NULL inheritance and independent source remap; existing standard/mip-array/view/overlap/cross-resource/cross-queue/R8 full edge-and-padding tests; normal buffer/cross-copy/self-copy; debug packed run. |
| Wrong-oracle controls | Two expected exit1 processes, packed alias expected value and R8 edge expected value. Retain actual error outputs in positive-alias-negative.log / positive-bad-edge.log. |
| Adjacent failure boundaries | Three LOCAL_PASS:buffer update submitted, buffer self-copy allocation and barrier encoder failure. |
| D3D11 boundary | LOCAL_PASS:legacy SHEX/SHDR shader creation through Airconv and DXIL/empty/hybrid rejection. No D3D11 draw/readback claim. |
| Timing / post-timing correctness | Eight processes with API/GPU validation deliberately disabled for timing. All finish standard CopyTiles and packed mapping/alias/NULL/source-remap GPU checks. E/performance-runs.json. |
| Identity / delivery audit | PASS, E/artifacts.json and audit.json: build/stage bytes, actual loaded PE paths/export prefixes for fault probes, matched PE/Unix SHA and actual dyld path/UUID, MSC SHA/path; packets/dispatch tables/capability/buffer/update/queue/shader owners unchanged; original .gitignore hash preserved. |
| Static / scope | PASS, scoped clang-format before build and git diff --check. No new D3D12 file, helper scripts or generated binaries in repository. |
| Other environments / broader semantics | NOT_RUN:Windows native, Mullion runtime, x86/WOW64, actual no-placement target, physical memory exhaustion and async-error recovery. UNVERIFIED:full async resource/allocator lifetime and partial transaction recovery. |

Final validated batch =127 processes (119 exit0, eight expected negatives); timing adds eight exit0 GPU processes. Exclude the initial three layout-sanity processes from these totals. C++/native failures are injected rather than physical OOM or GPU failures. CPU publication exclusion is SOURCE_OBSERVED, while public device/fence/Signal contracts are LOCAL_PASS. Packed shader checks first/last values, not every packed-tail byte; existing standard/overlap oracles provide their own full-page/edge checks. Do not claim public readback of the private CPU mapping table after removal.

The first staging attempt used an accidentally self-referential prior-stage path in the external runner and stopped with FileNotFoundError before deployment. The path was corrected to the retained update-candidate stage; no source or GPU test repair was required. Diagnostic scripts/logs stay outside the repository. No production/test source edits occurred after the successful build or these final runtime batches.

### Cost evidence

Only D3D12 DLL differs in baseline/candidate timing. Identical new executable, DXIL, WineMetal PE/Unix, Wine and MSC on this M1 Max. ABBA/BAAB order, four processes per build, nine64-call batches per process; sample0 warmup excluded, leaving32 warm batch samples each. Repeated source→destination copies cover the full mixed resource (one standard tile plus packed tail). Source data is stable; no concurrent alias shader access. QPC timer covers CopyTileMappings API wall time including queue backpressure. Signal/wait is outside timer using a separate fence; normal GPU alias verification follows the benchmark.

| Warm API microseconds/call | Median | P95 | Range |
|---|---:|---:|---:|
| Baseline07dc291b |240.280 |266.533 |230.450–269.656 |
| Candidate |245.287 |277.363 |236.480–287.811 |

Observed median +2.08%, P95 also higher, overlapping ranges. **INCONCLUSIVE**: no performance improvement or absence of regression is established. Do not infer pure CPU cost, GPU timeline overlap or broad device behavior. Default path retains the same vectors/snapshot, locks, native batch, queue handoff, encoders and barriers; adds prepare/snapshot/native hook environment checks and checked return branching. No new default allocation, lock, scan or GPU wait. Private completion allocation/waits are injection-only. Keep measured overhead as an unresolved cost risk.

### Capability, risks, Git and next slice

Real environment: Apple M1 Max/Apple7, macOS27.2 26B123, Xcode27.2 27B52, Metal32023.922, Wine11.0. Compiled MSC SDK API4.0.3; runtime product version unknown. Converter /usr/local/lib/libmetalirconverter.dylib SHA256 cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4. Final actual Unix UUID53225D50-EE0F-3F14-89C0-9E102074C9D9, SHA25691c0904a909cdbe4c7ea588ddf242665debe68ac82a386be82ca33271737b6d0.

Public Feature Level/SM/Tier unchanged; Metal3 shadow, D3D11, dual Airconv/MSC and MSC compatibility policy preserved. Cross-resource synchronous copy status is closed for this slice. Same-resource native update status/publication is BACKEND_NOT_IMPLEMENTED. Broader async failure/lifetime and rollback guarantees remain UNVERIFIED backend issues, not an Apple7 PlacementSparse hardware blocker. Success here proves synchronous issuance plus the tested fenced GPU paths, not async recovery.

Isolated stages/prefixes use sparse-texture-copy-{candidate,baseline,debug}-20261104 under external capability-gate, with per-case shader caches in E. E/run.py retains commands, env, staging, controls and cost collection; E/audit.py verifies source/identity/status invariants. Choose fresh labels/roots for future reruns to preserve evidence. Existing user Wine/CrossOver, build/cache data and .gitignore edit are preserved.

Gate0/1 preceded writes; Gate2 implementation, Gate3 validation and Gate4 delivery audit pass for this slice. Focused repository-style commit without push; exact final SHA, source/evidence hashes and final Git state are recorded in E/delivery-manifest.json. Build version metadata predates the final commit; committed source bytes are compared with tested source, not claimed embedded-new-SHA binaries. Final index/untracked are empty; only original .gitignore remains dirty.

**Next single recommended slice:** same-resource texture CopyTileMappings checked per-operation update, stop at first failure and defer CPU publication until every native operation succeeds. Preserve the saved overlap snapshot, standard/packed-tail mapping layout and multi-heap handling; validate partial-batch failures without claiming rollback.

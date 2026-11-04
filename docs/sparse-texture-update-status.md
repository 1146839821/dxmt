# Reserved texture update status — Design Card, 2026-11-04

## Gate 0 / Gate 1

- Goal: contain preparation OOM and propagate synchronous native texture mapping failure before publishing the CPU mapping table.
- Baseline: feat/d3d12 at 7164d9923bcb9e13a5b1b3b00f76aee8fb2fa97c; only the user's .gitignore edit is dirty. Live target reference d1e83d41db5abb86d03bdbde4b7254c54debac6f; Mullion reference 93de32b9145885ceb2e45be7d445e56d62499b3c. No fetch/reset/deploy into user installations.
- Evidence (SOURCE_OBSERVED): src/d3d12/d3d12_texture.cpp:1906–2048 prepares both standard tiles and a collapsed packed tail in one vector, calls a void bridge, then publishes at one table-write point. Publication is already after issuance, but native failure is ignored. Preparation bad_alloc can escape. src/d3d12/d3d12_command_queue.cpp:714–742 already completes handoff and marks OOM/removal terminal.
- SPEC_REQUIRED: Microsoft ID3D12CommandQueue::UpdateTileMappings documentation requires NULL/SKIP and ordered region semantics; public API returns void. https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-updatetilemappings . Installed Metal MTL4CommandQueue.h:351–374 defines a void texture update, compatible placement page size and ResourceState synchronization. Keep current barriers/handoff.
- MULLION_OBSERVED (source only): pinned src/d3d12/d3d12_command_queue.cpp:1291–1353 builds texture operations, captures resource/heap and uses its own scheduler/queue. Retained source: external result-metal4-sparse-reclassification-20261104/mullion. Its scheduler is not transplanted. Windows native and Mullion runtime are NOT_RUN.
- Scope / owner: texture UpdateTileMappings only, including standard and full contiguous packed-tail preparation. Add a checked WineMetal export/wrapper using the existing packet, Unix call ID and dispatch entries; preserve the void entry. Catch bad_alloc as E_OUTOFMEMORY. Opaque native failure returns DXGI_ERROR_DEVICE_REMOVED, without guessing OOM or rolling back potentially issued operations.
- Non-scope: texture CopyTileMappings, async error/lifetime architecture, partial rollback, capability promotion, residency/backend replacement, shader conversion, D3D11 behavior.
- Proposed changes: src/d3d12/d3d12_texture.cpp, src/winemetal/{winemetal.h,Metal.hpp,winemetal_thunks.c}, src/winemetal/unix/winemetal_unix.c and existing tests/dx12/dx12_reserved_texture_packed.cpp. No new D3D12 source/header, packet or dispatch-table change.
- Test plan: existing fixture gains queried standard/packed/mixed fault modes and optional update timing. Exact opt-in prepare/allocation/submitted failure hooks, first/second nonempty update, remap/NULL, invalid flags/SKIP preflight, pending-fence wake/MAX and rejected later Signal. Native submitted tests drain a private event before reporting simulated failure; default path adds no wait. Ignore-result and debug-thunk controls; actual GPU normal readback, full edge/padding, alias/cross-queue and adjacent buffer/D3D11 checks. Use isolated matched PE/Unix stages, cache/prefix and validation.
- Cost model: existing vectors, locks, single native batch and handoff remain. Added default cost: environment checks and checked return branch; no additional allocation/lock/encoder/GPU wait. Compare identical fixture/runtime bytes with retained baseline D3D12 DLL, alternating processes, warm batch median/P95/range; API wall time includes queue backpressure, not pure CPU or GPU overlap.
- Risks: a successful synchronous return proves issuance, not GPU completion. Terminal removal is not rollback. CPU publication ordering is source evidence, not direct public readback of private state. Actual memory pressure and async resource lifetime remain UNVERIFIED. Device is real M1 Max with Metal4/PlacementSparse; these are backend gaps, not an Apple7 hardware blocker.
- Rollback: revert this focused commit; preserved void exports and unchanged packets keep existing callers intact. Do not reset the user's working tree.
- Definition of Done: targeted build/static checks and real positive/negative tests pass with runtime identity; honest costs/NOT_RUN results documented; no capability changes; commit in repository style without push. Gate 0/1 complete; implementation is authorized by the user's continuation.

Evidence root E: /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-texture-update-20261104.

## Gate 2–4 — implementation, validation and delivery

**Task summary / findings:** N15 texture UpdateTileMappings now catches preparation bad_alloc as E_OUTOFMEMORY and forwards a false synchronous WineMetal status as DXGI_ERROR_DEVICE_REMOVED. The existing queue closes its handoff before marking either failure terminal. Both standard tiles and a complete contiguous packed tail remain in the original single native batch; CPU mappings publish only after the checked call succeeds. Native void Metal APIs expose issuance status here, not GPU completion. The existing packed-tail acceptance rules are unchanged.

**Design decisions:** reuse the existing resource owner, complete operation vector and queue removal boundary. Add one checked export while keeping the void export, packet, Unix IDs and both dispatch tables unchanged. Do not introduce a parallel scheduler, table, residency owner, per-tile transaction or rollback mechanism. No Mullion code is copied. Existing old callers can retain the void ABI; the newly built D3D12 DLL requires its matching updated WineMetal pair.

| Changed file / current source anchor | Purpose / evidence |
|---|---|
| src/d3d12/d3d12_texture.cpp:38,1926,2036–2073 | Opt-in preparation failure; whole-method OOM containment; checked native call before the sole CPU-table publication. SOURCE_OBSERVED. |
| src/winemetal/winemetal.h:2495 | Add checked export; retain the existing void export and packet. |
| src/winemetal/Metal.hpp:958 | Bool wrapper, with existing void wrapper retained. |
| src/winemetal/winemetal_thunks.c:1646 | Return raw existing WINE_UNIX_CALL status as bool; do not use the debug-asserting void macro. |
| src/winemetal/unix/winemetal_unix.c:4252–4332 | Independent opt-in texture allocation/submitted fault counter and operation counts; actual private completion wait only for injected failures. Default conversion/status behavior otherwise retained. |
| tests/dx12/dx12_reserved_texture_packed.cpp:128,499,805 | Extend existing fixture with standard/packed/mixed failure contracts and update timing. Actual tiling queries, loaded export code/path identity, invalid/SKIP preflight and safe ignore-result controls. Existing GPU sample oracles retained. |
| docs/sparse-texture-update-status.md | Baseline, design, reference comparison, results, costs and remaining limitations. |

**Real queried layouts on this M1 Max:** standard = total4, standard_mips1, packed_mips0, testing2 standard tiles; packed = total1, standard_mips0, packed_mips4, packed_tiles1; mixed = total2, standard_mips1, packed_mips3, packed_tiles1. Actual native operation counts are 2 / 1 / 2. No fabricated family or page-size result. The full packed-tail CPU range becomes one native operation; these probes do not assume one Metal operation per D3D tile.

| Validation | Result / retained evidence |
|---|---|
| Full x64 Release default build | PASS, E/build.log; configuration follows .github/build_dxmt.sh plus existing tests, paired-runtime deployment follows docs/DEVELOPMENT.md. Original deploy script is not executed. |
| Explicit texture DXIL fixtures | PASS, E/fixture-build.log. These seven targets are not build-by-default. |
| Release failure matrix | LOCAL_PASS:36 processes =3 phases ×3 queried layouts ×remap/NULL ×first/second nonempty update. E/faults.json and candidate-{phase}-{layout}-{map,unmap}-{1,2}.log. |
| Preparation OOM | Exact E_OUTOFMEMORY reason, pending fence wakes/MAX, later Signal rejects. Twelve prepare cases; whole-method bad_alloc containment is also source-audited. |
| Native failures | Generic DXGI_ERROR_DEVICE_REMOVED without inventing a native OOM cause. Twelve allocation and twelve submitted cases. All injected native failures wait for a private real event before terminal resource cleanup. |
| Empty/invalid preflight | Invalid flags remain nonterminal; all-SKIP does not consume the nonempty failure counter. Verified before every fault. |
| Debug-asserting thunk | LOCAL_PASS:6 cases, allocation/submitted ×3 layouts, second call; plus one real packed shader/readback run. Only thunk object uses -UNDEBUG; not a complete debug build. |
| Exact old-DLL negative controls | Six expected exit1 cases: allocation/submitted ×3 layouts on second mapping. Same current PE/Unix bridge and new fixture; old D3D12 DLL bytes from delivery7164d992 ignore the status. Healthy controls drain without reading inconsistent mappings. |
| Normal GPU correctness | LOCAL_PASS:17 validated processes, E/positives.json: packed UAV/copy-upload/full-chain/alias inheritance, standard texture, mip/array, view ranges, overlap/cross-resource/cross-queue, R8 full edge/padding; adjacent normal buffer/cross-copy/self-copy; debug packed run. Packed shader oracle checks first/last values, not every tail byte; standard/overlap tests supply their existing stronger checks. |
| Wrong-data controls | Two expected exit1 processes: packed alias expected value and R8 edge expected value. E/positive-alias-negative.log, positive-bad-edge.log. |
| Adjacent failure contracts | Three LOCAL_PASS:buffer update submitted, buffer self-copy allocation, barrier encoder failure. |
| D3D11 boundary | LOCAL_PASS:legacy SHEX/SHDR creation through Airconv and DXIL/empty/hybrid rejection. E/adjacent-d3d11.log. No D3D11 draw/readback claim. |
| Timing and post-timing GPU checks | Eight processes, API/GPU validation deliberately off; every process completes existing packed shader/readback phases and standard CopyTiles readback. E/performance-runs.json, performance-summary.json. |
| Runtime identity / delivery audit | PASS, E/artifacts.json and audit.json: stage/build hashes, paired WineMetal PE/Unix, actual dyld Unix path/UUID, loaded fault-probe PE export prefixes, converter path/SHA, unchanged packets/dispatch tables, buffer/copy/queue/capability/shader owners and preserved .gitignore hash. |
| Static checks | PASS:scoped clang-format before building, git diff --check; no unrelated formatting. |
| Other targets / unclosed semantics | NOT_RUN:Windows native, Mullion runtime, x86/WOW64, an actual no-placement device, physical memory exhaustion and async error recovery. UNVERIFIED:full asynchronous allocator/resource lifetime and atomic partial-submission rollback. |

Delivery counts exclude the initial three layout-sanity processes and one repeated negative-oracle diagnostic. Final validated batch =71 processes (63 exit0, eight expected negatives); timing adds eight exit0 GPU processes. Preparation/native failures are injected, not physical GPU failures. CPU publication exclusion is SOURCE_OBSERVED; device/fence/Signal behavior is LOCAL_PASS. Do not interpret removal as rollback or read back the private table through a dead device.

The first staging attempt found the explicit packed shader fixtures absent; they were then built with their existing Meson targets. The negative-alias test returned its expected exit1, but the collector first expected a different error string; its marker was corrected and that case rerun while retaining previous successful runs. These are fixture/collector errors, not hidden GPU failures or production changes. Diagnostic logs remain separately named.

### Performance and default-path cost

Identical executable, shaders, WineMetal PE/Unix and MSC bytes for baseline/candidate; only D3D12 DLL differs. ABBA/BAAB order, four processes per build, nine64-call batches per process; sample0 is warmup, leaving32 warm samples per build. Each call maps both the queried standard tile and packed tail, alternating two heaps. QPC covers UpdateTileMappings API wall time and queue backpressure; Signal/wait is outside the timer on a separate timing fence. Normal shader phases continue afterward. No claim of pure CPU recording cost or GPU overlap.

| Warm API microseconds/call | Median | P95 | Range |
|---|---:|---:|---:|
| Baseline7164d992 |245.103 |278.695 |236.494–280.430 |
| Candidate |247.951 |278.648 |233.661–295.283 |

Observed median +1.16%, P95 -0.017%; distributions overlap. **INCONCLUSIVE**: no established performance improvement or absence of regression. The default path adds preparation-hook environment checks, native-hook environment checks and a checked-result branch, but no new allocation, lock, heap scan, native mapping batch, encoder, barrier or GPU wait. Private completion objects/waits exist only on explicitly injected native failures.

### Capability, risks and Git state

Environment: Apple M1 Max/Apple7; macOS27.2 26B123, Xcode27.2 27B52, Metal32023.922, Wine11.0. Compiled MSC SDK API4.0.3; runtime product version remains unknown. Actual /usr/local/lib/libmetalirconverter.dylib SHA256 cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4. Final Unix UUID D0C0B32E-83B6-3511-A0B3-F44F55BB12C9 and SHA256 d20ce85bbdb6e04df309e2a0b83f6e4b69c6eb1bcc0d4d710f65a2a2dac7a156 are checked against actual dyld load.

Public FL/SM/Tier unchanged. Metal3 shadow path, D3D11 behavior, Airconv/MSC and MSC policy preserved. Synchronous texture update status is closed for this slice; texture mapping copy status remains BACKEND_NOT_IMPLEMENTED. Full async lifetime/error proof and transaction recovery remain UNVERIFIED backend issues, not BLOCKED_BY_HARDWARE on Apple7. No capability promotion is justified by these tests alone.

Isolated stage/prefix names: sparse-texture-update-{candidate,baseline,debug}-20261104 under the external capability-gate directory, per-case caches in E. E/run.py retains commands/env and staging; E/audit.py verifies data/identity/source invariants. Choose fresh labels/roots for future runs to preserve evidence. No helper scripts, generated shader bytes or logs enter the repository. Existing user Wine/CrossOver and the original .gitignore modification are preserved.

Gate0/1 preceded production writes; Gate2 implementation, Gate3 validation and Gate4 delivery audit pass for synchronous texture update status and publication. A focused repository-style commit is made without push; final commit SHA, index/untracked state, source and evidence hashes are recorded in E/delivery-manifest.json. Generated build Git version metadata predates that commit; delivery verifies tested source bytes against committed source rather than claiming newly embedded version metadata.

**Next single recommended slice:** cross-resource reserved texture CopyTileMappings synchronous checked native status and destination CPU publication. Audit standard tiles and packed tail under the existing owner; keep same-resource overlap/partial batches and broader async lifetime separate.

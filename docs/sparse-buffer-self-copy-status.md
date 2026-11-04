# N15 — checked same-resource buffer mapping copies

2026-11-04. Gate0 baseline feat/d3d12 `64cccc6def3d75ff5fc7d2ca59d24f38e1b8f1cd`; only the user's existing .gitignore edit. Live origin `d1e83d41db5abb86d03bdbde4b7254c54debac6f`, Mullion HEAD `93de32b9145885ceb2e45be7d445e56d62499b3c`; no fetch or push. M1 Max, macOS27.2 26B123, Xcode27.2 27B52, Metal32023.922. No Apple7 placement-sparse hardware blocker.

## Gate0 evidence

`SOURCE_OBSERVED`: buffer CopyTileMappingsFrom at703–749 snapshots source mappings, but publishes the destination table before its same-resource per-tile void update calls. A native conversion/status failure is ignored (or asserts through a debug void thunk), and later tiles continue. The prior slice already added checked cross-resource status and queue terminal-error handling; reuse those owners.

`SPEC_REQUIRED`: [Microsoft CopyTileMappings](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-copytilemappings), remarks74, defines overlapping copies as if the source were saved before destination writes. The API returns void. Apple installed SDK MTL4CommandQueue.h:397–410 describes void per-heap updateBufferMappings, nil heap for unmap. Synchronous success is issuance, not asynchronous completion. Public Apple page requires JavaScript and its Markdown fetch failed; installed SDK is used for the exact declaration, not inferred page contents.

Mullion pinned source `result-metal4-sparse-reclassification-20261104/mullion/src/d3d12/d3d12_command_queue.cpp:1356–1405` reverses overlapping operations like memmove and uses its own Map/Settle scheduler. This is source comparison, not MULLION_OBSERVED runtime proof. No code is transplanted. Windows native and Mullion runtime NOT_RUN.

## Gate1 Design Card

- **Goal/current path:** CopyTileMappings → buffer saved snapshot → checked per-tile WineMetal updates → destination publication; prevent ignored/continued partial failures.
- **Invariant/evidence:** preserve D3D12 source-snapshot overlap semantics; stop at the first false native status; CPU table stays unpublished on failure (source proof).
- **Scope/non-scope:** same-resource buffer loop and existing probes only; no texture, new backend/scheduler/table, capability promotion, real async completion recovery or rollback of issued mappings.
- **Plan A/B/selection:** keep saved heap mappings and check each existing update, versus replace with ordered native copies/grouped multi-heap batching. Select A: no new ordering, batching or heap-lifetime model.
- **Owner/boundary:** buffer owns snapshot/publication; existing checked bridge owns issuance status; existing queue closes handoff then marks removal. Do not add a second state machine.
- **Changes/files:** move the common destination table write after native work; replace self-copy void calls with tryUpdateBufferMappings and early removal HRESULT. Existing buffer failure/overlap fixtures gain partial-position, NULL/mixed/multi-heap, full-word and timing cases.
- **Performance:** same vector, locks, native operation count and queue events; one status branch per tile, no default-path GPU wait. Compare same-executable API batch time baseline/candidate separately from validated correctness.
- **Fault safety:** reuse native update allocation/submitted hooks; allocation injection must drain earlier test-queue work before terminal cleanup, as submitted injection already does. This test-only drain is not production asynchronous-lifetime proof.
- **Proof/test plan:** fail first/second self tile with mapped, NULL, mixed and two-heap source; exact reason/fence/MAX/Signal checks; old release baseline must fail observability and safely drain; debug-asserting checked thunk; full-page forward/reverse/NULL/multi-heap GPU readback and wrong-oracle; adjacent cross-copy/buffer/barrier/D3D11 smoke.
- **Risks:** issued earlier tiles cannot be rolled back; failure is terminal with old CPU table retained. Prior global allocator/submission lifetime and async-error gaps remain open; do not claim atomic GPU rollback. No public table readback after removal.
- **Rollback/DoD:** revert this focused commit only; retain evidence/prefixes externally. Build, identities, positive/negative/fault checks and cost evidence required; commit in repository style without push; original .gitignore preserved.

Continuation of the user-approved recommended route authorizes this slice. Gate0/1 complete before production writes. Results follow after validation.

## Gate2–4 results and delivery

**Task summary / findings:** synchronous same-resource buffer mapping failures now stop the per-tile loop and return DXGI_ERROR_DEVICE_REMOVED to the existing queue error boundary. The saved overlap snapshot remains authoritative; the destination CPU table is written only after all native calls return success. Earlier issued tiles are not rolled back. This closes ignored synchronous self-copy status, not GPU transaction atomicity or asynchronous recovery.

**Changed files / source evidence:**

| File | Change / evidence |
|---|---|
| src/d3d12/d3d12_buffer.cpp:729–751 | Reuse tryUpdateBufferMappings, early terminal HRESULT, common publication after self/cross native work; remove redundant nested condition/early success. Source snapshot and per-tile mode/range/heap-offset calculations retained. |
| src/winemetal/unix/winemetal_unix.c:4185,4204–4219 | Opt-in update-hook call log proves whether later tiles were invoked. Injected allocation failure drains prior test-queue work before terminal cleanup; submitted injection already does. Normal conversion failure/default path gains no wait. |
| tests/dx12/dx12_reserved_buffer.cpp:175–398 | Existing failure probe adds mapped, NULL, mixed and two-heap self-copy modes. Separate setup native calls from first/second failing self tile; invalid flag preflight stays healthy; exact removal/fence/MAX/Signal checks retained. |
| tests/dx12/dx12_reserved_buffer_overlap.cpp:109,141–174,207–266 | Existing overlap oracle checks all three pages with nonuniform per-word data, adds second heap, and provides explicit 64-call batch timing mode. Default, cross-queue, reverse-queue and early-work behavior retained. |
| docs/sparse-buffer-self-copy-status.md | Design, ownership, specifications, source/reference comparison, tests, timing, risks and Git delivery. |

No new src/d3d12 file, mapping table, scheduler, bridge export, packet or Unix dispatch id. No queue, texture or shader-converter production edit.

**Validation evidence:** E = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-buffer-self-copy-20261104. E/run.py reuses the prior isolated runner/staging patterns and retains exact argv/environment; results.json, performance-runs.json, audit.json and artifacts.json identify final runs. The initial one-case log is preparation evidence and is not counted twice.

| Check | Result | Evidence |
|---|---|---|
| Release failure matrix | LOCAL_PASS: allocation/submitted ×mapped/NULL/mixed/two-heap ×tile1/tile2 =16 processes | candidate-{allocation,submitted}-buffer-self*.log |
| Stop at first native failure | LOCAL_PASS: observed call sequence ends exactly at setup_calls + failing_tile | Native opt-in observed_call log and results.json; no further update is issued after the candidate failure |
| Terminal semantics | LOCAL_PASS: generic removal reason, pending fence wake/MAX, later Signal rejected; invalid flags remain nonterminal | All 20 release/debug fault cases; this does not invent a public CopyTileMappings HRESULT |
| Debug checked thunk | LOCAL_PASS: both phases, second tile, single/two-heap source =4 processes | debug-*.log; only PE winemetal_thunks.c is compiled with -UNDEBUG, not a complete debug build |
| Baseline negative controls | Expected exit1 in8 processes; old loop continues remaining tile after first-tile failure and does not remove device | baseline-{allocation,submitted}-buffer-self*.log; healthy control signals/waits before throwing |
| Full-page normal overlap | LOCAL_PASS:6 candidate modes (default, multiheap, cross-resource, cross-queue, reverse-queue, early-work), plus2 baseline modes;2,162,688 DWORD comparisons | *-overlap-*.log, positive_words in results.json |
| Wrong expected-data controls | Expected exit1 in4 existing bad-oracle modes | candidate-overlap-bad*.log; failure not misreported as successful data proof |
| Adjacent shader data | LOCAL_PASS:two existing normal buffer rounds/replays (622,592 words) and five-phase cross-copy shader round (163,840 words) | candidate-adjacent-buffer-normal/cross-copy.log |
| Adjacent failures | LOCAL_PASS:buffer update allocation, sparse barrier encoder, cross-copy allocation/submitted | candidate-adjacent-*.log; original failure modes remain functional |
| D3D11 boundary | LOCAL_PASS:legacy SHEX/SHDR creation; DXIL, empty, hybrid rejection | candidate-d3d11.log; no rendering/readback claim |
| Build/static | PASS:full x64 release default target, external debug-thunk build, scoped clang-format, git diff --check | build.log, debug-build.json, formatting.json; three pre-existing macOS27 Managed/didModifyRange deprecation warnings |
| Runtime identity | PASS:actual loaded PE paths/in-memory probe exports, native dyld path/UUID, per-file SHA/build pairing and MSC dylib identity | artifacts.json, audit.json and per-process logs |
| Other targets / broader semantics | NOT_RUN/UNVERIFIED:Windows native, Mullion runtime, x86/WOW64, real no-placement target, actual memory pressure, async GPU error recovery and submission/allocator lifetime proof | No synthetic capability results or hardware-blocker claim |

There are 47 validated processes, including 12 expected-negative exits. Their complete data oracles compare 2,949,120 DWORDs (2,555,904 candidate and 393,216 baseline). All 47 enable Metal API and shader validation; no assertion, shader-validation error, GPU address fault, command-buffer abort or completion timeout was found. The separate eight timing processes disable validation and add 393,216 successful readback comparisons. Do not merge timing validation status with correctness validation status.

CPU publication prevention is SOURCE_OBSERVED: the only table lock/write follows both successful native branches. The failure tests observe device/fence/Signal behavior and native call count; they cannot publicly inspect the private table after removal. Native returned success means preparation and issuance, not completion. Failure after an earlier tile was issued remains terminal with no attempted rollback. The opt-in allocation hook drains earlier test work; it is not a new production lifetime guarantee for genuine allocation failure.

The baseline control uses the exact retained D3D12 DLL bytes from delivery 64cccc6d, with the new test executable and current matched WineMetal PE/Unix pair. The bridge ABI and dispatch tables are unchanged and audited. Sharing the new native test hook gives both versions safe draining and observable call counts; this is an isolated source comparison, not a reproduction of every historical runtime component. In normal and timing cases the hook environment is absent. Controls never shader-read deliberately inconsistent mappings and explicitly drain healthy queues before releasing resources.

The first scoped test-edit script rejected an ambiguous replacement before writing; it was narrowed to the failure function. A later external audit transport command had a quoting syntax error before execution; the corrected verifier ran successfully. These are tooling errors, not GPU test failures; production/test binaries did not require a subsequent semantic repair.

## Performance / cost model

Same new overlap executable, current paired WineMetal bytes, Wine and MSC on the same device. ABBA/BAAB order, four processes per build, nine batches of64 two-tile same-resource calls per process; sample0 is warmup, samples1–8 yield 32 warm batch samples per build. Timer includes CopyTileMappings queue backpressure; completion Signal/wait and full readback are outside the timer. This is API wall cost, not pure CPU recording cost or GPU timeline overlap.

| Warm API microseconds per call | Baseline median / P95 / range | Candidate median / P95 / range |
|---|---|---|
|64-call two-tile batches |238.987 /280.216 /230.852–329.569 |248.688 /275.497 /231.900–293.270 |

Observed median +4.06%, P95 -1.68%, overlapping distributions. INCONCLUSIVE: no proven speedup and no proven absence of a cost regression; retain the result for future mapping-cost work. Default path retains one source vector, two resource-table lock acquisitions, the same per-tile native conversions and queue handoff/barriers. Added cost is checked status/early branch per tile; publication moves later. No new allocation, lock, heap scan, encoder or default-path GPU wait. No general performance conclusion is drawn from one M1 Max.

## Capability, risks and delivery

Public Feature Level/SM/Tier and MSC compile/runtime compatibility policy are unchanged. Existing Metal3 shadow path, D3D11 and texture implementations are preserved. Actual runtime MSC product version remains unknown; compiled SDK API 4.0.3 and selected dylib SHA256 cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4 are verified. Wine 11.0 and the external patched LLVM15 installation are retained; compiler/build environment follows .github/build_dxmt.sh configuration plus tests and docs/DEVELOPMENT.md paired-runtime requirements.

Stages/prefixes are isolated under the external capability-gate directory with sparse-buffer-self-{candidate,baseline,debug} names. The user's Wine/CrossOver installs and original .gitignore edit are preserved. E/run.py first/main/perf and E/audit.py retain reproducible commands, hashes and source invariants; choose fresh evidence labels/root before a future run to preserve logs. No Python/Bash/generated fixtures enter the repository.

Gate0/1 preceded production writes. Gate2/3 and delivery audit pass for synchronous same-resource buffer copy status and publication; performance remains INCONCLUSIVE. A focused repository-style commit is made without push; exact final SHA, clean index/untracked state and preserved original .gitignore hash are recorded in E/delivery-manifest.json. Generated binary Git version metadata predates that commit; manifest compares tested source bytes with committed source rather than claiming a new embedded SHA.

Remaining risks: texture mapping update/copy still has unchecked native failures and early publication; real asynchronous completion/removal/resource-lifetime proof is not closed; no atomic rollback is provided for partial native submission. These are BACKEND_NOT_IMPLEMENTED / UNVERIFIED issues, not evidence of unsupported Apple7 placement sparse. Capability promotion remains gated.

**Next single recommended slice:** reserved texture UpdateTileMappings synchronous checked submission and CPU-table publication order. Audit its standard-tile and packed-tail branches under the same owner; keep texture copy and broader lifetime changes separate.

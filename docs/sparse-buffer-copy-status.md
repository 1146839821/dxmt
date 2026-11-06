# N15 — cross-resource buffer CopyTileMappings publication

2026-11-04. Baseline feat/d3d12 `2ff81f5d5ef2ebe9eb576dd797343898f16f27f2`; only the user's existing .gitignore edit. One cross-resource batch slice; self-copy partial batches are separate.

## Gate0 / evidence

`SOURCE_OBSERVED`: buffer CopyTileMappingsFrom snapshots source mappings, writes the destination table, then allocates/submits native copy operations through an unchecked void bridge. C++ allocation failure can escape after publication; native conversion failure is discarded. Native copy conversion already allocates/converts the entire array before a single Metal call. Intrusive private-reference table assignment does not allocate a new container.

`SPEC_REQUIRED`: [Microsoft CopyTileMappings](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-copytilemappings) is void, copies mappings rather than contents, and defines overlapping source snapshot semantics. [Apple buffer mapping copy](https://developer.apple.com/documentation/metal/mtl4commandqueue/copymappings%28sourcebuffer%3Adestinationbuffer%3Aoperations%3A%29) requires matching sparse page sizes and prevents simultaneous use of aliased tiles. Tests serialize source upload and destination access; they do not use both aliased buffers concurrently. Metal's void batch call does not prove asynchronous completion.

Mullion reference pinned `93de32b9145885ceb2e45be7d445e56d62499b3c`, command_queue.cpp:1356–1405: its CopyTileMappings uses a distinct Map/Settle scheduler and handles overlap in its own model. No code is copied. Windows/Mullion runtime are NOT_RUN. M1 Max with macOS27.2/Xcode27.2 can execute actual 64 KiB placement-sparse buffers; no hardware blocker inferred.

## Gate1 / Design Card

- **Goal:** cross-resource buffer copy must prepare/submit successfully before publishing the destination CPU table; synchronously lost work must be observable.
- **Scope:** additive checked copy PE/WMT entry reusing Unix packet/id; cross-resource C++ preparation/OOM and checked submission before table copy; queue closes handoff before terminal failure; normal/fault regressions in existing fixtures.
- **Non-Scope:** same-resource native per-tile loop/order, texture paths, asynchronous failures, rollback after possible submission, allocator lifetime, public FL/SM/Tier.
- **Owner:** source owns locked snapshot, destination owns publication, bridge owns conversion/issuance status, queue owns EndSparseMapping/removal. No scheduler or mapping-table duplication.
- **Changes:** build cross-resource operation batch before destination lock/write; false→DXGI_ERROR_DEVICE_REMOVED; bad_alloc→E_OUTOFMEMORY. Catch contains preparation failures; self-copy's existing snapshot/publication/native order remains. Copy queue handles fatal resource HRESULT after closing event pair; validation stays nonterminal. Preserve old void export and Unix layouts/tables.
- **Tests:** CPP snapshot/operation-vector, native conversion allocation, and submitted-status fault phases; first/second cross-resource copies of mapped/NULL source; invalid flags preflight healthy. Native submitted fault drains a private completion event before return to retain resources safely. Check reason/fence/MAX/Signal refusal. Extend existing copy fixture to full two-tile seeded readback, mixed/NULL/reversed mappings, destination stability after source unmap, cleared output per phase, wrong-oracle control. Normal previous buffer/admission/barrier and D3D11 checks retained. Controls ignore checked result but never shader-read inconsistent mappings and drain before cleanup. CPU publication prevention is a source control-flow conclusion, not a public post-removal readback.
- **Risks:** terminal OOM handling is shared at CopyTileMappings queue boundary; no claim texture/self-copy errors are fixed. The same-resource snapshot may still fail preparation, now contained; its native calls remain unchecked. Submitted faults are synthetic, not async stress. New import needs paired PE deployment. No performance claim.
- **Rollback:** revert this focused commit, preserve external stage/prefix/cache/logs. No new Unix entry/packet.
- **DoD:** matrix, full-page data, expected-negative controls, full build and identity audit pass; unchanged self-copy algorithm verified; commit without push; remaining gaps explicit.

User “继续” authorizes this slice. Gate0/1 precede production edits. Results follow after validation.

## Gate2–4 / implementation and delivery

**Task summary:** completed the synchronous cross-resource buffer-copy status and publication slice. Cross-resource operations now prepare and pass the checked native call before the destination CPU mapping table is changed. Known C++ preparation OOM is contained; a lost native operation is terminal. This does not complete all sparse transactions.

**Baseline evidence:** branch feat/d3d12 at `2ff81f5d5ef2ebe9eb576dd797343898f16f27f2`. Live origin branch was `d1e83d41db5abb86d03bdbde4b7254c54debac6f`, Mullion HEAD `93de32b9145885ceb2e45be7d445e56d62499b3c`; no fetch, remote write or reference implementation transplant. Environment: Apple M1 Max/Apple7, macOS27.2 `26B123`, Xcode27.2 `27B52`, Metal32023.922 and Wine11.0. Real placement sparse support with 65536-byte pages was established by the preceding device probes. No test overrides a capability result. Compiled MSC SDK API4.0.3; loaded runtime product version remains unknown. The selected dylib SHA256 is `cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.

**Findings and design decisions:**

- `SOURCE_OBSERVED`: `d3d12_buffer.cpp:703–731` takes the locked source snapshot, prepares all cross-resource copy operations and checks submission before locking/writing the destination table. A false result returns DXGI_ERROR_DEVICE_REMOVED. The function catch at753 contains bad_alloc as E_OUTOFMEMORY. The intrusive heap-reference assignments do not allocate a container. This is control-flow evidence, not GPU inspection of the private table after device removal.
- `LOCAL_PASS`: `d3d12_command_queue.cpp:763–774` always closes the sparse handoff before recording either terminal HRESULT. Fault probes observe the exact known OOM/generic removal reason, pending fence wake with MAX, and refusal of subsequent Signal. Invalid flags remain nonterminal and do not consume the first valid copy injection.
- `SOURCE_OBSERVED`: `winemetal_thunks.c:1660–1673` returns Unix status through the additive checked entry without invoking the asserting void-call macro. Existing void exports, Unix packet definitions, ids and both dispatch tables stay unchanged. This requires deployment of the matching PE library with the new importing D3D12 DLL; it does not establish compatibility with arbitrarily old WineMetal PE builds.
- `SOURCE_OBSERVED`: native conversion prepares the complete operation array before one Metal copy call (`winemetal_unix.c:4278–4320`). Its success reports synchronous preparation/issuance only. It cannot prove GPU acceptance/completion or diagnose a specific allocation failure from the generic native status. After a possibly issued operation fails, terminal removal is used rather than speculative rollback or device reuse.
- The same-resource saved-mapping loop at `d3d12_buffer.cpp:735–749` is byte-for-byte identical to the baseline. Its earlier table publication and unchecked per-tile updates are deliberately still open. Both native texture mapping functions are unchanged. No scheduler, second table or resource-backend replacement was introduced.

**Changed files:**

| File | Purpose |
|---|---|
| `src/d3d12/d3d12_buffer.cpp:36,671` | Opt-in preparation faults; contain allocation failure; move checked cross-resource submission before table publication |
| `src/d3d12/d3d12_command_queue.cpp:771` | Propagate fatal copy HRESULT after closing the existing handoff |
| `src/winemetal/winemetal.h:2499` | Add uint32 checked copy export; preserve void API |
| `src/winemetal/winemetal_thunks.c:1660` | Forward existing Unix call result without debug assertion |
| `src/winemetal/Metal.hpp:965` | Typed bool wrapper |
| `src/winemetal/unix/winemetal_unix.c:4260` | Exact opt-in native allocation/submitted-status faults; drain private test completion before simulated submitted failure |
| `tests/dx12/dx12_reserved_buffer.cpp:175` | First/second mapped/NULL copy failure probes, healthy invalid-flag preflight and runtime identity |
| `tests/dx12/dx12_reserved_buffer_copy_mapping.cpp:104,272` | Seeded two-page full readback, per-phase output sentinel, stable VA/descriptor and independent destination mapping checks |
| `tests/dx12/reserved_buffer_copy_mapping.hlsl:4` | 64-thread whole-buffer shader copy; existing Meson fixture/target retained |
| `docs/sparse-buffer-copy-status.md` | Design, source evidence, results and remaining scope |

**Validation results:** external evidence root E is `/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-buffer-copy-20261104`. The final run JSON and logs at E are delivery evidence. `preformat-validation/` retains the earlier run and is not added to final counts.

| Check | Result | Evidence |
|---|---|---|
| Release copy faults | LOCAL_PASS:4 phases ×mapped/NULL source ×first/second copy =16 processes | `results.json`, `dxmt-final-{snapshot,prepare,allocation,submitted}-{mapped,null}-{1,2}.log` |
| Debug thunk faults | LOCAL_PASS:4 phases at second copy | `variants.json`, `final-debug-*.log`; only winemetal_thunks.c is compiled with -UNDEBUG, not a complete debug DXMT build |
| Ignore-result controls | Expected exit1 in8 processes; missing device removal detected | `final-control-{allocation,submitted}-{copy,copy-null}-{1,2}.log`; only the checked cross-resource result gate is removed |
| Submitted-fault safety | LOCAL_PASS:private completion signal/wait succeeds before each simulated error, including controls | Required `submitted_completed=1`; completion event is independent of removal cancellation |
| Copy data/NULL semantics | LOCAL_PASS:49 release rounds plus1 debug round; five phases per round, 8,192,000 DWORD comparisons | `copy-results.json`, `extra-debug-copy.log`; mapped, mixed NULL, all NULL, reversed pages, destination preserved after source unmap |
| Normal prior buffer coverage | LOCAL_PASS:52 release rounds/replays plus1 debug round; 16,498,688 DWORD comparisons | `results.json`, `variants.json`; existing remap/NULL/cross-queue/replay oracle |
| Existing overlap/handoff | LOCAL_PASS:default, cross-resource, cross-queue, reverse-queue, early-work; four expected wrong-oracle exits1 | `extra-results.json`, `extra-overlap-*.log`; sampled overlap oracle, not full-page shader coverage |
| Prior error/admission regressions | LOCAL_PASS:update allocation and barrier-copy encoder failure;36 queue initialization failure/recovery checks | `dxmt-final-prior-*.log`, `dxmt-final-queue-admission.log` |
| D3D11 boundary | LOCAL_PASS:legacy SHEX/SHDR creation and DXIL/empty/hybrid rejection | `extra-d3d11.log`; no D3D11 draw/readback claim |
| Oracle negatives | Expected exit1 for normal-buffer and full-copy wrong data, plus four overlap cases | Corresponding wrong/bad logs; controls never shader-read an intentionally inconsistent mapping |
| Build/static checks | PASS:full x64 release default target, control and debug-thunk links, scoped clang-format and git diff --check | `delivery-build.log`, `control-build.json`, `debug-build.json`, `formatting.json`; earlier native build has three existing macOS27 Managed/didModifyRange deprecation warnings |
| Actual runtime identity | PASS:build/stage bytes paired, actual PE module paths and in-memory export prefixes, native Unix UUID/hash, selected MSC path/hash | `dxmt-artifacts.json`, `audit.json`; Unix UUID `002F5FB8-B07B-3ACE-8A39-EED12D32E598` |
| Broader targets/semantics | NOT_RUN/UNVERIFIED:Windows native, Mullion runtime, x86/WOW64, real non-placement target, genuine memory pressure, async GPU faults; performance NOT_MEASURED | No hardware support or conformance inferred from these omissions |

Final matrix totals **54 processes**, including **14 expected-negative exits**. All final GPU cases enable Metal API and shader validation; no assertion, shader-validation error, GPU address fault, command-buffer abort or test completion timeout was found. The two full-word oracles account for **24,690,688 successful DWORD comparisons**; overlap sampling is reported separately. Test hooks simulate failure and do not establish physical OOM or real asynchronous failure behavior.

The ignored-result controls retain valid native barriers and all other candidate logic. An allocation control omits the failed batch; a submitted control completes its real batch before reporting the simulated error. If removal is missing, the probe signals/waits to drain before throwing and releasing objects. No CPU table is publicly read after removal. The normal-copy shader output is cleared to 0xcdcdcdcd before each phase, so stale prior results cannot mask an omitted dispatch.

Validation was repeated after scoped test-source formatting and the final rebuild. The external audit initially assumed literal paths for a symlinked control Wine tree and case-sensitive Windows DLL spelling. These verifier assumptions were corrected against actual dyld/loader paths and UUIDs; they were not GPU failures. The final audit checks both the resolved native identity and the actual PE module identity.

**Capability impact and regression risks:** public Feature Level, Shader Model, Tiled Resources Tier and MSC compilation policy are unchanged. Metal3/shadow implementation, D3D11 behavior and texture paths are retained; the real non-placement target remains NOT_RUN. The shared CopyTileMappings queue boundary now makes existing E_OUTOFMEMORY/DXGI_ERROR_DEVICE_REMOVED resource results terminal, including any texture result with those HRESULTs; that is not evidence that texture publication is repaired. Preparation catches also contain same-resource snapshot OOM, but do not fix its partial native submission semantics. The fault-only completion wait does not add a default-path GPU wait. The default mapping path has opt-in hook environment checks; no performance claim is made.

**Reproduction and Git state:** `run.py` stages the matched final build and runs the main matrix; `copy_readback.py` runs full copy data; `build_variants.py`/`run_variants.py` retain exact external control/debug build commands; `extra_regressions.py` covers debug copy/overlap/D3D11; `audit.py` checks identities, counts and source invariants. Use a fresh evidence label/root on a new run to retain prior logs. Builds follow `.github/build_dxmt.sh` configuration with tests enabled; staging, prefixes and shader caches are isolated under the external capability-gate directory. No deployment to user Wine/CrossOver. A focused repository-style commit is made without push; final SHA and status are recorded in `delivery-manifest.json`. The user's original .gitignore hash remains `764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759` and the file is excluded from the commit.

**Gate conclusions:** Gate0/1 completed before implementation. Gate2, Gate3 and Gate4 pass for this synchronous cross-resource buffer-copy slice. Remaining sparse gaps are BACKEND_NOT_IMPLEMENTED / UNVERIFIED, not Apple7 PlacementSparse hardware blockers. In particular, same-resource partial copy batches, texture update/copy status/publication, asynchronous error reporting and allocator lifetime proof remain open. Broad capability promotion is still gated.

**Next recommended single slice:** same-resource buffer CopyTileMappings checked per-tile submission and partial-submission failure handling, preserving the saved overlap snapshot and using terminal failure rather than rollback after possible issuance. Keep texture work and capability changes separate.

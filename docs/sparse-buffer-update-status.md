# N15 — checked buffer UpdateTileMappings publication

2026-11-04. Baseline feat/d3d12 `8986ba764397a36102b322c4489cabf0f0564668`; worktree initially only the user's .gitignore edit. This is one buffer update slice, not texture/copy transaction completion.

## Gate0 / evidence

`SOURCE_OBSERVED`: buffer UpdateTileMappings builds tile/update/operation vectors, issues a void WineMetal update, then publishes tile_mappings_. Native copy_sparse_buffer_mapping_operations allocates and converts the complete array before one Metal call. Native conversion failure currently returns STATUS_UNSUCCESSFUL, which the old PE void entry discards (or debug asserts). C++ vector bad_alloc can escape without closing the queue handoff.

`SPEC_REQUIRED`: [Microsoft UpdateTileMappings](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-updatetilemappings) is void; SKIP retains mappings. [Apple buffer mapping](https://developer.apple.com/documentation/metal/mtl4commandqueue/updatemappings%28buffer%3Aheap%3Aoperations%3A%29) takes a placement heap or nil for unmap. SDK MTL4CommandQueue.h:397–410 confirms a void batch call. CPU-side success therefore means preparation and issuance, not asynchronous GPU completion. Using terminal device removal for lost ordered work is the existing DXMT policy, not a new Microsoft error-return contract.

Mullion reference `93de32b9145885ceb2e45be7d445e56d62499b3c`: Map/UpdateTileMappings (command_queue.cpp:587–655,1291–1353) captures resources/heaps and uses its own scheduler/status metadata. It does not provide a compatible WineMetal HRESULT/CPU-table transaction implementation; no code is copied. Windows/Mullion runtime are NOT_RUN. Current M1 Max/macOS27.2/Xcode27.2 can execute the existing real sparse paths; not a hardware blocker.

## Gate1 / Design Card

- **Goal:** prevent buffer CPU-table publication when synchronous native update preparation fails, contain C++ preparation OOM, and report the lost void queue operation observably.
- **Scope:** additive checked buffer-update PE/WMT entry; original Unix id/packet/table and void API retained; buffer update checks result before table lock/write; function-level bad_alloc→E_OUTOFMEMORY; queue closes the handoff before terminal error for DXGI_ERROR_DEVICE_REMOVED or E_OUTOFMEMORY. Invalid-argument warnings remain nonterminal.
- **Non-Scope:** texture update, CopyTileMappings (including self-copy per-tile batches), asynchronous errors, rollback after possible submission, allocator lifetime, Feature Level/SM/Tier.
- **Owner:** resource owns validation/preparation/table; WineMetal owns native conversion/status; queue owns EndSparseMapping and removal. No global mapping table or scheduler.
- **Changes:** tryUpdateBufferMappings returns bool via additive uint32 PE export; false returns DXGI_ERROR_DEVICE_REMOVED before CPU table commit. C++ preparation allocation exceptions return E_OUTOFMEMORY. Existing table assignment uses intrusive private heap references and does not allocate. Native conversion remains all-before-one-call; a false result after possible issuance is terminal rather than attempting rollback or reuse.
- **Tests:** exact opt-in phase DXMT_TEST_SPARSE_BUFFER_UPDATE_FAILURE=prepare|allocation|submitted, FAIL_AT=1|2. prepare throws bad_alloc before the final operation vector; native allocation simulates NULL conversion, submitted returns false only after the actual Metal call. Counters only count nonempty real updates. Exercise two-tile remap/reuse and NULL updates, each as first update or after a completed successful mapping. Preflight locally rejected flags and valid SKIP must stay healthy and not consume injection. Assert reason (known C++ OOM versus opaque native failure), pending fence wake/MAX, Signal rejection and bounded teardown. Normal full-page GPU/replay, earlier barrier/admission and D3D11 tests remain. Controls ignore the native checked result, with valid barriers and a drain before releasing resources; no shader reads from an intentionally inconsistent control mapping.
- **Risks:** E_OUTOFMEMORY handling at the shared UpdateTileMappings queue boundary also makes an existing resource OOM terminal; copy path is unchanged. Nil/native status does not establish a precise OOM cause. No table readback is possible through public D3D12 after terminal removal; publication prevention is a source control-flow conclusion. Performance is unmeasured; fault flags add checks per mapping.
- **Rollback:** revert only this focused commit; keep external artifacts/prefixes. Pair caller and PE/Unix libraries. Old exports/Unix packet layouts stay intact.
- **DoD:** checked native and C++ OOM cases observable with no stranded handoff, SKIP/validation preserved, old ignore-result control rejected, full normal regression/build/identity audit pass, no capability promotion, focused commit without push.

User “继续下一项” authorizes this slice. Gate0/1 completed before production edits. Results follow after Gate2/3.

## Gate2–4 / implementation and delivery

Changed files and source evidence:

- `src/d3d12/d3d12_buffer.cpp`: catches std::bad_alloc across preparation, and checks the native bool before tile_mapping_mutex_ and tile_mappings_ assignment. Private heap-reference assignment is intrusive (com_pointer.hpp:73–85) and does not allocate a new container. OOM injection is limited to nonempty operation preparation; valid SKIP does not consume it.
- `src/d3d12/d3d12_command_queue.cpp`: after EndSparseMapping, recognizes the resource's E_OUTOFMEMORY or DXGI_ERROR_DEVICE_REMOVED and calls the existing removal policy. The shared UpdateTileMappings boundary applies this OOM handling to any resource returning that HRESULT; this is not a claim that texture update publication is fixed.
- `src/winemetal/winemetal.h`, `winemetal_thunks.c`, `Metal.hpp`: additive checked uint32/boolean buffer-update entry; old void entry, packet layout, call id and both dispatch tables retained. The checked entry uses WINE_UNIX_CALL directly, so debug assertions do not replace its failure return.
- `src/winemetal/unix/winemetal_unix.c`: simulated conversion allocation failure or status failure after a real submission. Default conversion/Metal call semantics remain. For the **submitted injection only**, a separate native shared event is created before issuance, signaled after the mapping and waited on before returning the injected failure; a timeout aborts without unwinding GPU-owned resources. This test-only drain prevents fault-test cleanup from releasing resources still needed by mapping. It is absent from normal execution and does not validate asynchronous failure handling.
- `tests/dx12/dx12_reserved_buffer.cpp`: reuse the fault fixture for two-tile REUSE remap/NULL, first/second updates, preflight invalid flags/SKIP, expected removal reason, pending fence/MAX and rejected later Signal. Add checked export runtime identity. Preserve barrier/admission/default/wrong-oracle modes.
- This report: design, evidence and limits. No shader, cache ABI, advertised capability or resource backend replacement.

Evidence root (**E**): `/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-buffer-update-20261104`. Only logs with final labels and their current results/variants/audit JSON are delivery evidence. Earlier batches and pre-lifetime JSON/logs are retained but not counted as final passes.

| Check | Result | Evidence |
|---|---|---|
| Release buffer failures | LOCAL_PASS:3 phases ×2 operations ×2 positions =12 processes | E/dxmt-final-{prepare,allocation,submitted}-{update,unmap}-{1,2}.log |
| C++ preparation OOM | E_OUTOFMEMORY reason, pending fence wake/MAX, later Signal rejected | Four prepare cases; source function try block catches injected bad_alloc |
| Native failures | Generic DXGI_ERROR_DEVICE_REMOVED reason; no OOM cause guessed from opaque native status | Four allocation cases, four completed-submission status cases |
| Preflight | Invalid flags remain nonterminal; SKIP remains healthy and does not consume the nonempty-update counter | All 12 release fault cases and all three debug fault cases |
| Debug-asserting thunk | LOCAL_PASS:prepare/allocation/submitted at second update; one normal full-page round/replay | E/final-debug-*.log, variants.json |
| Ignore-result controls | Expected exit1 in8 processes; regression detects missing removal | E/final-control-{allocation,submitted}-{update,unmap}-{1,2}.log |
| Submitted-test lifetime | Actual native completion marker required before every injected submitted return, including controls | `submitted_completed=1`; separate event is not part of device-removal cancellation |
| Normal MSC values | LOCAL_PASS:3 processes ×16 rounds, two admission-regression rounds, default and ignored-hook rounds =52 rounds/replays, 16,187,392 word comparisons | E/dxmt-final-normal{1,2,3}.log and final admission/default/invalid-hook logs |
| Additional debug GPU round | LOCAL_PASS:one round/replay, 311,296 comparisons | E/final-debug-None-None-None.log |
| Prior regressions | Two barrier failure cases pass; 36 queue-admission failures/retries pass; wrong expected-data control exits1 | E/results.json and final logs |
| D3D11 boundary | LOCAL_PASS:legacy SHEX/SHDR shader creation and DXIL/empty/hybrid rejection; no draw/readback claim | E/final-d3d11.log |
| Runtime identity | Build/stage PE pairing, actual module paths, in-memory export prefixes including new checked entry, Unix UUID/SHA and MSC path checked | E/dxmt-artifacts.json, audit.json |
| Build / static | Full x64 release default target and external control/debug variants PASS; scoped formatting/diff checks PASS | E/delivery-build-final.log, control/debug-build.json, delivery-manifest.json |
| Other targets / broader semantics | Windows native, Mullion runtime, x86/WOW64, genuine no-placement target, async failure/partial transactions/performance NOT_RUN or NOT_MEASURED | No fabricated device capabilities |

Total positive buffer comparisons are **16,498,688** across 53 rounds/replays. Debug coverage compiles only winemetal_thunks.c with -UNDEBUG; other objects/Unix library remain release. It is not a complete debug DXMT build. Test failures are injected, not physical memory exhaustion or hardware failures.

The instrumented controls remove only the buffer's checked-result gate, leaving C++ OOM containment, valid native barriers and all other candidate code. Allocation controls issue no failed batch; submitted controls finish real mapping work before returning the simulated error. The healthy control then signals/waits to drain before throwing/releasing resources. No shader reads an intentionally inconsistent control mapping. These controls validate failure observability, not public readback of the private CPU table.

CPU publication prevention is `SOURCE_OBSERVED`: the failed return precedes the table lock/assignment, while the public device/fence/Signal contract is `LOCAL_PASS`. A real native failure before issuance does not change the table. A status failure after possible issuance is terminal: this code does not roll back potentially changed GPU mappings or reuse the device. Asynchronous GPU acceptance/completion and genuine allocation pressure remain unverified.

The initial submitted-failure batches passed data/status checks but did not independently ensure mapping completion before resource cleanup on device removal. During delivery review the test-only native completion wait was added, and all final batches/controls/debug and D3D11 checks were rerun with the final Unix image. Earlier logs are historical preparation evidence, not lifetime proof. No wait was added to the production default path.

Environment: real Apple M1 Max/Apple7, macOS27.2 `26B123`, Xcode27.2 `27B52`, Wine11.0; Metal4/PlacementSparse true and page64=65536. MSC compiled SDK API4.0.3, runtime product version unknown; actual converter SHA remains `cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`. Current native image identity is in audit.json. The existing external patched LLVM release build follows .github/build_dxmt.sh feature configuration plus tests and docs/DEVELOPMENT.md matched-runtime deployment rules.

Reproduction: E/run.py stages the candidate and runs final matrix/regressions; E/run_variants.py uses the retained externally built control/debug artifacts. Their JSON files retain compile/link commands and hashes. Select new evidence labels/directories before rerunning to preserve logs. Native flags/counters are process-local; fault CLI modes require rounds=1. Isolated stage/prefix/cache names include sparse-buffer-update, sparse-buffer-control and sparse-buffer-debug under the external capability-gate directory. No installation into user Wine/CrossOver.

Gate0/1 preceded implementation; Gate2/3 and delivery audit pass for this synchronous buffer-update slice. A focused repository-style commit is made without push; final SHA/state recorded in E/delivery-manifest.json. Only the user's pre-existing .gitignore edit remains unstaged. Public FL/SM/Tier and MSC compilation policy are unchanged.

Remaining: texture update and buffer/texture CopyTileMappings still have unchecked failures; same-resource multi-heap copies and partially issued batches need distinct handling. Native async error reporting and prior allocator lifetime proof remain open. **Next single slice:** cross-resource buffer CopyTileMappings checked submission and CPU publication order, leaving self-copy partial batches explicitly separate.

Delivery source anchors: [src/d3d12/d3d12_buffer.cpp:547](/Users/zhangbo/Documents/Vibe-Codeding/dxmt/src/d3d12/d3d12_buffer.cpp:547), [src/d3d12/d3d12_buffer.cpp:641](/Users/zhangbo/Documents/Vibe-Codeding/dxmt/src/d3d12/d3d12_buffer.cpp:641), [src/winemetal/winemetal_thunks.c:1618](/Users/zhangbo/Documents/Vibe-Codeding/dxmt/src/winemetal/winemetal_thunks.c:1618), [src/winemetal/unix/winemetal_unix.c:4192](/Users/zhangbo/Documents/Vibe-Codeding/dxmt/src/winemetal/unix/winemetal_unix.c:4192), [tests/dx12/dx12_reserved_buffer.cpp:173](/Users/zhangbo/Documents/Vibe-Codeding/dxmt/tests/dx12/dx12_reserved_buffer.cpp:173).

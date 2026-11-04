# N13 — sparse handoff lifetime audit and fenced retirement oracle

2026-11-05. Evidence E: /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-retirement-audit-20261105.

## Gate 0 / Gate 1 — Design Card

- GOAL / DoD: distinguish application lifetime duties from backend ownership; prove a valid gated sparse mapping → readback → unmap → fence → heap-release sequence, including command-list Reset during flight.
- BASELINE: feat/d3d12 at c193dffcc643892e94bb0132d4ff7948f84d27ed; only original .gitignore dirty, SHA764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759; local origin0/10. Live target d1e83d41db5abb86d03bdbde4b7254c54debac6f and Mullion93de32b9145885ceb2e45be7d445e56d62499b3c rechecked without fetch/push.
- CURRENT PATH / OWNER: queue BeginSparseMapping → resource mapping snapshot → WineMetal native mapping → EndSparseMapping; subsequent main work and Signal wait for sparse_event_value_; Submission completion owns submitted recording/allocators. Existing owners stay authoritative.
- INVARIANTS / SPEC: INV-03/04/08/11/12/14; Microsoft Important Changes explicit synchronization, GraphicsCommandList::Reset remarks, CommandAllocator::Reset remarks, UpdateTileMappings remarks; installed MTL4CommandQueue.h event/mapping contracts. Caller keeps resource memory alive until GPU use ends; Reset(list) during execution is permitted, Reset(allocator) is not.
- EVIDENCE / HYPOTHESIS: source mapping table holds private heap references while mapped, but publication is not GPU completion. Heap destruction immediately unregisters residency. No verified valid-input early-retirement failure; do not label absence of a queue mapping-retention vector as BACKEND_NOT_IMPLEMENTED by itself.
- PLAN A/B / SELECTION: add indefinite/duplicated queue pinning versus audit existing lifetime contract and add bounded legal oracle. Choose the latter; no production change without a proven valid counterexample. Mullion Map captures resource/heap and waits its own mapping completion; its scheduler is not directly portable.
- FILES / SCOPE: extend existing dx12_reserved_texture_overlap.cpp helper and CLI; this audit. Test one existing three-page heap, standard R32 texture, original full-word and edge/padding verifier. No new D3D12 file, bridge API, scheduler, shader or runner in repository.
- CHANGES: GPU gate; map/copy then submit readback; reset/release list using fresh allocator while keeping original allocator/resources alive; queue NULL mapping and completion Signal; verify both fences pending; release gate, wait unmap completion, verify earlier snapshot; release app heap, verify NULL CopyTiles and shader Load readbacks using the existing SM6 fixture; shader output is initialized with two nonzero upload canaries and checked both while mapped and after retirement. Bounded watchdog and drain on failure; no intentional GPU UAF.
- PROOF: real validated positive, wrong-last-word and wrong-shader-value negatives, baseline/candidate production source identity; nearest normal overlap/cross-queue/packed smoke only. Record loaded paths/PE/Unix hashes and actual Unix UUID/converter identity; no fake capability results.
- PERF / NON-SCOPE: production code unchanged, so no runtime overhead delta to measure; test gate wall durations are diagnostics, not performance benchmarks. Async native GPU errors, physical OOM, all formats/heaps, packed-tail retirement and no-placement fallback stay outside this proof.
- RISKS / ROLLBACK: GPU gate must always be opened/drained before fixture cleanup; watchdog release is FAIL, not PASS. Revert this focused test/doc commit. No FL/SM/Tier promotion; preserve Metal3/D3D11/Airconv/MSC. User goal continuation authorizes this slice and repository-style commit, no push.

Gate 1 refinement: the native texture CopyTiles branch always issues the sparse blit after zero-filling padding (d3d12_command_queue.cpp:1046–1122), so a suspected CPU mapping-table shortcut does not apply to this branch. Add an independent mapped/nonmapped shader Load oracle for the shader path; do not misclassify the existing CopyTiles proof. Existing cs_6_0 fixture is reused with normal D3D12 root/descriptor bindings, no native ABI changes.

## Gate 2 / Gate 3 / Gate 4 — Delivery report

### 1. Task Summary

**LOCAL_PASS** for this bounded N13 slice. Added a real GPU regression for standard R32 placement-sparse texture mapping handoff, submitted-recording replacement, fenced unmap and application heap release. No production implementation was changed. This is not a proof of every reserved-resource lifetime, physical deallocation or general Tiled Resources conformance.

### 2. Baseline Evidence

- Source baseline: `feat/d3d12`, `c193dffcc643892e94bb0132d4ff7948f84d27ed`. Local tracking reference started 0 behind / 10 ahead. Previously recorded live remote target `d1e83d41db5abb86d03bdbde4b7254c54debac6f`, Mullion `93de32b9145885ceb2e45be7d445e56d62499b3c`; these are observations, not an assertion that the target baseline is latest. No fetch, branch change or push.
- Device/runtime: Apple M1 Max / Apple7, macOS 27.2 `26B123`, Xcode 27.2 `27B52`, Metal toolchain `32023.922`, Wine 11.0, compiled MSC SDK API 4.0.3. Runtime MSC product version remains unknown; the loaded dylib bytes and x86_64 UUID were checked.
- Build B: `/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/dxmt-metal272-identity-audit-20261104`; patched LLVM from `llvm-current-patches-20261103/install`. Release x64 cross configuration follows `.github/build_dxmt.sh`, with tests enabled and isolated artifacts. The script's sudo, patch application and user-environment deployment were not run.
- E contains `baseline.json`, `specifications.json`, `sdk-evidence.json`, `artifacts.json`, `runs-final.json`, `audit-result.json` and final `delivery-manifest.json`. Retained `pre-shader-*` and nine exploratory process logs are distinct from the final ten-process matrix.
- Candidate and historical baseline use the same final test executable and fixtures. WineMetal PE/Unix bytes match each other and the current build. Candidate D3D12 was rebuilt with current generated version metadata; do not claim it is byte-identical to the retained historical DLL. The audited production sources are byte-identical to the source baseline.

### 3. Findings and source evidence

All repository paths below are relative to `/Users/zhangbo/Documents/Vibe-Codeding/dxmt`. References describe this slice's baseline; no production source lines were changed.

| Finding | Source / line | Evidence type / boundary |
| --- | --- | --- |
| Caller keeps memory available through GPU use; COM release is not a substitute for synchronization. | E/d3d12-lifetimes.txt:43–56; official source and HTML hash in E/specifications.json | SPEC_REQUIRED. A premature final heap release is not a valid conformance oracle. |
| Command-list Reset is allowed while execution is pending; it differs from allocator Reset. | E/list-reset.txt:71–80 | SPEC_REQUIRED. This test uses a fresh allocator, retaining the original allocator. |
| Mapping tables keep private backing heap identity. | src/d3d12/d3d12_texture.cpp:528–541; src/d3d12/d3d12_buffer.cpp:118–132 | SOURCE_AUDIT. CPU publication is not GPU completion. |
| Heap destruction unregisters residency. | src/d3d12/d3d12_heap.cpp:60–65 | SOURCE_AUDIT. Does not prove application Release immediately destroys the underlying Metal allocation. |
| Queue completion owns submitted recordings/allocators and publishes successful public fence completion after command-buffer completion. | src/d3d12/d3d12_command_queue.cpp:86–110, 474–503, 831–833, 937–949 | SOURCE_AUDIT plus LOCAL_PASS for this recording Reset/release sequence. |
| Existing sparse handoff orders main work → ResourceState barrier → mapping → subsequent main work / Signal. | src/d3d12/d3d12_command_queue.cpp:585–623, 930–931, 1650–1651 | SOURCE_AUDIT plus LOCAL_PASS for queued old-data readback followed by NULL mapping. |
| Native texture CopyTiles zero-fills the destination then issues the sparse blit; this branch has no CPU mapped/unmapped skip. | src/d3d12/d3d12_command_queue.cpp:1046–1122 | SOURCE_AUDIT. Independent shader Load additionally exercises normal MSC descriptor access. |
| R32 SRV creation uses the texture view, with no mapped-tile table test or NULL substitution. | src/d3d12/d3d12_texture.cpp:1246–1274, 1425–1450 | SOURCE_AUDIT. Independent shader result is not a CPU all-NULL shortcut. |
| Native ResourceState barrier commits and releases its command buffer and allocator immediately. | src/winemetal/unix/winemetal_unix.c:4131–4175 | SOURCE_AUDIT; allocator release safety remains SPEC_UNVERIFIED. Installed MTL4CommandAllocator.h:43–50 constrains reset; this alone does not settle destruction/internal ownership. |
| Barrier commit does not install asynchronous feedback. | src/winemetal/unix/winemetal_unix.c:4161–4166; E/MTL4CommandQueue.h:55–68 | BACKEND_NOT_IMPLEMENTED for this feedback/error-reporting route, not proof of a failed mapping in this run. |
| Mullion holds its own mapping callback captures until its mapping event completes. | E's pinned Mullion reference: src/d3d12/d3d12_command_queue.cpp:593–625, 1338, 1392; actual retained file under result-metal4-sparse-reclassification-20261104/mullion | SOURCE_AUDIT, not MULLION_OBSERVED runtime behavior. Different scheduler/ownership model; no transplant. |

Mullion also contains a `Settle` workaround for delayed unmapping (reference queue:629–657), including spare-page map/unmap. That source comment records its author's observation, not a reproduced fact on this M1 Max / macOS 27.2 run; the bounded all-NULL blit and shader checks here pass without adding that workaround. This does not settle every buffer/writable-texture combination.

The absence of an additional queue mapping-retention vector is **not** independently a defect. No valid-input early-retirement bug was reproduced. No blocker in this slice is attributed to Apple7 lacking Placement Sparse.

### 4. Design Decisions

Use existing production owners and add one bounded test; reject speculative duplicate pinning and scheduler changes. Queue a GPU gate, overlapping same-resource mapping copy and old-data readback; Reset/close/release the command list using a fresh allocator while its original submitted allocator remains alive. Queue all-NULL mapping and a second fence, verify both fences are pending behind the gate, then open it and check the old-data readback. Only after unmap completion does the application release its heap reference.

Both full CopyTiles and actual shader Load then verify zero from the unmapped texture. Existing `reserved_texture.read.cs.cso` reads texels (0,0) and (128,0); its mapped check returns two distinct known values. Shader output is initialized from two nonzero upload words before dispatch, so a skipped dispatch fails. Both shader checks use ordinary D3D12 root signature, SRV/UAV table, PSO and MSC execution. No native binding constants or ABI changes.

The watchdog opens a stuck gate after 15 seconds and records failure. Failure cleanup opens/drains queued work before fixture cleanup. No deliberate GPU use-after-free, allocator Reset in flight or physical allocation retirement assertion.

### 5. Changed Files

- `tests/dx12/dx12_reserved_texture_overlap.cpp:99–247`: existing-fixture shader readback and bounded watchdog helpers.
- Same file:382–418: in-flight list Reset/release and one-shot pre-completion callback, with drain on failure.
- Same file:427–437, 486–561, 689–723: wrong-last-word control, fenced retirement case, actual module/export identity, positive/negative CLI modes. Existing normal/multiheap/cross-queue behavior remains available.
- `docs/sparse-handoff-retirement-audit.md`: design, source/specification evidence, validation boundaries and next slice.
- No production source, vendored header, new shader, repository runner or build registration change. The pre-existing `.gitignore` edit remains outside the commit.

### 6. Validation Results

Final build: **PASS** (`build-shader-final.log`, full default x64 release target). Scoped clang-format, `git diff --check`, production-source hashes, original `.gitignore` hash, stage/build pairing, actual loaded PE paths/export bytes and Unix/MSC dyld UUIDs: **PASS** (`audit.py`, `audit-result.json`). Candidate and baseline each use an isolated Wine prefix; each process uses an isolated shader-cache path.

All ten final processes enable Metal API and GPU Shader Validation. The audit found none of its checked assertion, shader validation, GPU address fault, aborted-buffer, recording-failure, watchdog or completion-timeout diagnostics. This observation is limited to the tested paths and checked diagnostics.

| Case | Processes | Result / evidence |
| --- | ---: | --- |
| Candidate fenced handoff/retirement: first + two repeats + native-operation observation | 4 | LOCAL_PASS: pending fences, list replacement, old-data snapshot, all-NULL CopyTiles and shader zeros. |
| Historical baseline with identical final fixture | 1 | LOCAL_PASS. Confirms no production fix is demonstrated by this test addition. |
| Wrong last word of queued tile | 1 | Expected exit 1: tile 0, word 16383. No final success marker. |
| Wrong retired shader expected value | 1 | Expected exit 1: actual 0,0 versus expected 1,0. No final success marker. |
| Ordinary overlap, multiheap/cross-queue, packed-tail smoke | 3 | LOCAL_PASS. Packed-tail smoke is not packed-tail retirement coverage. |

Final total: 8 successful exits + 2 expected negative exits; 23 successful 49,152-DWORD full CopyTiles checks (1,130,496 words, excluding packed helper checks); 12 successful two-value shader comparisons plus one deliberately rejected comparison. The observed native mapping calls are 1–5, operation counts [1,3,1,1,3], four mappings with the same nonzero native heap followed by a three-operation NULL unmap. The existing hook's failure index is UINT64_MAX, never reached; no failure or capability is fabricated.

Commands (from the repository; absolute paths are retained in run records):

```sh
PATH="$PWD/toolchains/llvm-mingw-20260908-ucrt-macos-universal/bin:$PATH" \
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/prefix-dxc-build-cache-gpu-20261104 \
meson compile -C /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/dxmt-metal272-identity-audit-20261104
python3 /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-retirement-audit-20261105/run.py final-first
python3 /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-retirement-audit-20261105/run.py final-rest
python3 /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-retirement-audit-20261105/audit.py
```

The retained runner stages the executable/fixtures separately from Wine and does not deploy to the user's existing environment. To repeat the matrix, use new output labels/cache paths and preserve existing evidence files.

### 7. Capability Impact

None. Feature Level, Shader Model, Tiled Resources Tier and other public capability declarations are unchanged. Metal3/shadow backing, D3D11 and MSC compatibility code are unchanged. No performance improvement is claimed: production code did not change; gate/wall times are not a benchmark.

### 8. Regression Risks and remaining blockers

| Item | Classification | Remaining validation |
| --- | --- | --- |
| ResourceState barrier asynchronous completion/error feedback route | BACKEND_NOT_IMPLEMENTED | Design feedback ownership and terminal error propagation using existing queue/device owners; do not infer sparse API completion feedback exists. |
| Lifetime of a committed native barrier's allocator after application release | SPEC_UNVERIFIED | Establish exact release/internal-retention contract. The reset prohibition alone is insufficient evidence. |
| Packed-tail retirement, all texture formats, buffers, multiple heaps under retirement, OOM/device-loss races, Metal3 fallback/D3D11 runtime | UNVERIFIED / NOT_RUN in this slice | Existing smoke/source preservation does not replace these targeted executions. |
| Windows-native reference, additional GPUs/OS versions, Mullion runtime | TARGET_DEVICE_NOT_AVAILABLE / NOT_RUN | No native or Mullion runtime result claimed. |
| MSC semantic failure in the bounded R32 Load case | No new MSC_SEMANTIC_GAP established | Two-point readback is not blanket shader/residency conformance. |

Tests introduce a dependency on the existing read fixture only for the new modes, and a watchdog thread only for the gated case. Existing command-line modes do not load the new fixture. Full physical heap destruction is not instrumented; releasing the application's reference is the exact tested operation.

### 9. Git State

Focused commit subject: `test(d3d12): validate sparse handoff retirement`. Final commit ID, final working-tree/tracking state and artifact hashes are recorded after commit in E/delivery-manifest.json. Only this test and report are included; the user's original `.gitignore` change is preserved. No push or PR. Build artifacts were produced before the test/document commit, with that source baseline identity retained explicitly.

Gate conclusions: Gate 0 evidence sufficient for the bounded test, Gate 1 approved continuation within existing architecture, Gate 2 test-only implementation complete, Gate 3 LOCAL_PASS as tabulated, Gate 4 source/artifact/scope audit PASS. The broader roadmap remains incomplete.

### 10. Next Recommended Slice

One N13 Design Card: establish the native sparse ResourceState barrier's asynchronous feedback and allocator/command-buffer completion ownership contract, then design a bounded failure-propagation oracle through existing D3D12 queue/device owners. First verify SDK contracts and Mullion differences; retain current waits and public capabilities. Do not widen into resource-backend replacement or automatically implement a new bridge ABI. This recommendation is not an implemented fix.

## N13 follow-up — ResourceState barrier feedback and completion Design Card

2026-11-05. **Gate 0 complete; Gate 1 PROPOSED; Gate 2 not started.** This follow-up is an audit/design deliverable, not a production fix or a new GPU result. The earlier fenced-retirement results above remain historical evidence at their stated source/artifact identity.

### Task Summary / Goal / Baseline

Goal: connect asynchronous native ResourceState-barrier completion/error evidence to DXMT's existing submission completion owner, without confusing API admission, GPU event ordering, CPU feedback delivery or safe resource retirement. The first recommended implementation prerequisite is a native feedback-timing prototype, not a broad sparse backend replacement.

Target baseline: `feat/d3d12`, `d21b4ace8faed35a42c1e4949ad58b0c1c14345b`; initial worktree contains only the user's original `.gitignore` modification, SHA256 `764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759`. Local tracking ref is 0 behind / 11 ahead. Fresh `ls-remote` observations: target `d1e83d41db5abb86d03bdbde4b7254c54debac6f`; Mullion `93de32b9145885ceb2e45be7d445e56d62499b3c`. No fetch or history changes.

New evidence E2: `/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-barrier-feedback-design-20261105`. It contains current source hashes/excerpts, installed SDK headers, freshly fetched pinned Mullion source, official documentation snapshots, remote refs and the explicitly non-GPU ordering model. `sources.json` records URLs and hashes. Environment rechecked: macOS 27.2 `26B123`, Xcode 27.2 `27B52`; M1 Max/Apple7 GPU evidence is inherited from the preceding real tests, not rerun here. No capability emulation.

### Gate 0 Findings / specification and reference evidence

Paths below are relative to `/Users/zhangbo/Documents/Vibe-Codeding/dxmt` unless prefixed E2. New production conclusions are source-level observations, not reproduced native GPU failures.

| ID | Conclusion | Source and evidence type |
| --- | --- | --- |
| F1 | Checked barrier entry returns synchronous Unix-call status only. The native commit installs no feedback handler. | `src/winemetal/winemetal_thunks.c:1597–1604`; `src/winemetal/unix/winemetal_unix.c:4131–4175`; `src/winemetal/Metal.hpp:926–934`. SOURCE_AUDIT; BACKEND_NOT_IMPLEMENTED for asynchronous barrier feedback transport. |
| F2 | Main queue completion waits its classic command buffer; it has no auxiliary barrier-completion result to consume before retiring its FIFO record or publishing a fence. | `src/d3d12/d3d12_command_queue.cpp:86–110,456–515`. SOURCE_AUDIT. Absence of a consumer is confirmed; loss of an actual GPU error was not reproduced. |
| F3 | Installed API provides completion/error feedback for committed command buffers. Mapping methods are separate void queue operations with no corresponding feedback parameter. Barrier success therefore cannot certify later mapping success. | E2/MTL4CommitFeedback.h:19–40; E2/MTL4CommandQueue.h:236–259,353–435. SDK_CONTRACT. |
| F4 | GPU event completion is not a documented CPU-feedback-delivery guarantee. Mullion explicitly retains feedback work after GPU events/fences and settles it separately. | E2/MTL4CommandQueue.h:261–275; E2/mullion-queue.cpp:529–562; E2/mullion-bridge.c:5656–5681,5721–5737. SOURCE_AUDIT / DESIGN_INFERENCE, not MULLION_OBSERVED on this device. |
| F5 | Existing device-removal listeners already cancel pending CPU/GPU waits; fence state rejects later signals once removed. Native callbacks cannot directly call these PE C++ objects. | `src/d3d12/d3d12_command_queue.cpp:124–142`; `src/d3d12/d3d12_device.cpp:1794–1821`; `src/d3d12/d3d12_fence.cpp:131–141,179–210`. SOURCE_AUDIT. Retain this authority instead of another removal registry. |
| F6 | Native barrier currently releases its allocator and command buffer after commit. Header text constrains allocator Reset, while Apple's WWDC explanation discusses release when encoding will not continue. These do not precisely settle every committed-object internal-retention detail. | `src/winemetal/unix/winemetal_unix.c:4161–4166`; E2/MTL4CommandAllocator.h:43–50; E2/apple-core.json; E2/apple-wwdc.txt:254–273. SPEC_UNVERIFIED for the exact release contract; not a proven UAF or a hardware blocker. |
| F7 | Existing barrier fault cases cover native factory failures and a synchronously reported post-commit injection. They do not execute a delayed native feedback-error path. | `src/winemetal/unix/winemetal_unix.c:4110–4128,4166–4168`; `tests/dx12/dx12_reserved_buffer.cpp:177–219,320–399`. TEST_SOURCE_AUDIT. |

Official references: [Apple Metal 4 core API](https://developer.apple.com/documentation/metal/understanding-the-metal-4-core-api), [commit feedback](https://developer.apple.com/documentation/metal/mtl4commitfeedback), [WWDC25 Explore Metal 4 games](https://developer.apple.com/videos/play/wwdc2025/254/), and [Microsoft GetCompletedValue](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue). Microsoft specifies UINT64_MAX for a removed device; this must remain distinct from a successful requested fence value. Full URLs and retrieved bytes are retained in E2, rather than treating search snippets as evidence.

### Mullion comparison / shareable semantics

Mullion's native wrapper holds per-command-buffer feedback, a completion group, allocator and other objects (`E2/mullion-bridge.c:5121–5155,5235–5259`). Its callback stores error/timestamps and completes the group (`5665–5673`). Its wait handles error feedback even if a GPU event cannot signal (`5721–5737`). Its queue additionally keeps an `unsettled_` collection after GPU completion (`E2/mullion-queue.cpp:529–562`).

Share the distinction between GPU progress and CPU feedback, retain callback payloads, and handle errors when GPU-event progress is missing. Do not transplant Mullion's full native command-buffer wrapper, held-work scheduler, allocator pool or unsettled submission registry. No reference source code is copied by this documentation change; any later code port still requires a license/source review.

### Design alternatives / decision

1. **Poll a queue-local sticky error at the next API call or just before fence publication. Reject as sufficient closure.** There may be no later API call, feedback may arrive after that poll, and a main command buffer may remain blocked on a sparse event.
2. **Wait for every barrier on the application's mapping call. Reject as the default.** This adds CPU/GPU synchronization to a currently asynchronous API and can prevent the application from reaching the work that opens an existing queue gate.
3. **Native callback calls PE device/queue methods, or a new error-monitor thread. Reject.** This introduces cross-ABI execution/lifetime hazards or another scheduling owner.
4. **Proposed: native completion credential owned by the existing bounded Submission record.** Prepare the credential before publishing its owning record; have the existing FIFO completion thread consume it. The Unix callback updates native completion evidence only. This is the preferred design to prototype, not an approved bridge implementation.

A credential contains completion/error evidence for one auxiliary native workload. It is not another D3D12 resource-state map, capability snapshot, scheduler or device-removal authority. Per-credential completion avoids assuming callback delivery order; taking a maximum completed sequence is insufficient if a later callback arrives first.

### Architecture / module boundary / proposed transitions

Affected invariants: INV-03, INV-04, INV-08, INV-11, INV-12, INV-14. One selected node: N13. Existing N14 device/fence contracts are dependencies, not a second development task.

Proposed sequence, subject to the native prototype and Gate approval:

1. Under current queue admission/commit ownership, prepare a native barrier credential and native objects **without submitting GPU work**. Attach it to the existing main-queue handoff `Submission` before `CommitSubmissionLocked` publishes that record. Creation failure is handled before a record can wait on a missing credential.
2. Submit the current main signal; keep existing sparse-queue wait, ResourceState visibility barrier, mappings and mapping signal. Commit the prepared barrier with `MTL4CommitOptions`. The native callback captures native payload/event references, never a PE `this` pointer.
3. Track separately: native submit admission, feedback terminal state/error, and safe native retirement. A real callback is completion evidence. Aborted-before-submit can retire without a callback. A synchronous test hook reporting failure after a real commit **cannot** grant early retirement; the real workload still needs completion evidence.
4. If feedback reports a terminal error, retain the first native diagnostic, publish the result, then release the internal sparse-event wait through the existing cancellation convention. This signal is an error-unblock mechanism, never successful mapping/fence evidence. Do not hold a native result lock while calling Metal or destructing captured objects.
5. The existing PE completion thread waits for the corresponding classic main command buffer and consumes that record's native feedback, outside `commit_mutex_` and `submission_state_->mutex`. It invokes the existing `MarkDeviceRemoved` path on a real native execution failure before later FIFO records can publish affected success. Healthy records submitted before the failing barrier retain their ordering.
6. Resources of later, unblocked main command buffers remain alive until their actual terminal completion. Device removal and callback delivery are not permission to release in-flight GPU allocations. Only after both required completions can the record/credential payload retire.

The callback may run early, late or in a different order from another callback. Its result must remain available if delivered before the PE waiter starts. A per-record condition/group avoids polling; use no additional scheduler or permanent monitor thread. The existing 32-entry admission ring bounds published credentials, plus at most one under preparation; do not add an unbounded pending-feedback vector. FIFO publication closes the late-error window even if the mapping GPU event and subsequent classic command buffer finish before CPU feedback arrives.

No normal path adds a GPU fence/event or a GPU wait beyond the current handoff. The cancellation convention needs explicit lost-event/late-callback tests; missing feedback must not be silently turned into success or a fabricated GPU completion. An ordinary diagnostic timeout does not establish device removal. Exact safe teardown when the driver supplies no terminal evidence remains a risk to resolve before implementation acceptance.

### Proposed Changes / ABI inventory / Scope and Non-Scope

Possible future additive interfaces, names not frozen: prepare a barrier credential; submit it; wait/read its terminal feedback. Keep existing `SparseMappingQueue_tryBarrierBeforeResourceState` behavior and old exports available. Do not reinterpret its synchronous boolean as asynchronous completion. Native payload ownership must have an exact retain/release ledger, including aborted-before-submit, submitted host-error injection, callback-before-wait and queue release; callback/options/payload references must not form a cycle.

Any later bridge slice is limited to existing `src/winemetal/{winemetal.h,winemetal_thunks.h,winemetal_thunks.c,winemetal.def,Metal.hpp}`, `src/winemetal/unix/winemetal_unix.c`, existing `src/d3d12/d3d12_command_queue.cpp` and the selected existing test. Add Unix-call identifiers at the end of both tables; verify PE/Unix pairs, native/WoW64 structure layout, optional API availability and null factories. No new `src/d3d12` file. The current commit changes **only this report**.

Non-scope: sparse mapping-operation error coverage beyond the barrier commit, new descriptor/root ABI, native backend migration, Metal3 replacement, D3D11 changes, MSC compilation/runtime changes, FL/SM/Tier promotion, and intentional GPU faults. In particular, adding a post-mapping dummy commit does not prove its feedback reports a previous void mapping operation's failure; that claim remains SPEC_UNVERIFIED.

### Test Plan / minimum M1 Max verification matrix

These are acceptance cases for future work. **All GPU rows are NOT_RUN in this design round.** Use real supportsFamily/supportsPlacementSparse/page-size queries and retain their output; do not override them. Reuse existing R32/64-KiB fixture and the previous retirement probe wherever possible.

| Stage | Case / oracle | Failure boundary |
| --- | --- | --- |
| First native prototype | Correct ResourceState barrier and real mapping on M1 Max; nonuniform mapped data → shader/CopyTiles readback, then NULL unmap → zero. Record native feedback error=nil, callback count and actual GPU event progress. | Baseline capability must be queried, and a missing callback/readback must fail rather than skip. |
| First native prototype | Hold a **test-controlled report gate** after receiving actual native feedback; let the real GPU event/readback complete first. Verify the consumer remains pending until report release. Also deliver before the waiter starts. | Simulated report latency is not a native GPU failure or proof that a particular driver naturally delays feedback. |
| First native prototype | Two credentials, deliberately release reports in reverse order; drop the caller's queue handle while an explicit native fixture owner remains through callback/drain; check exactly-once cleanup. | No maximum-sequence shortcut, callback/owner cycle or resource release before actual completion. |
| Bridge after separate approval | At the real callback adapter, independently exercise success and a clearly labelled **synthetic error result** after real benign GPU work. Existing pending fence wakes with UINT64_MAX, later Signal is rejected, no affected success is published. | This proves propagation only, not native GPU-fault generation or recovery. Never issue unsafe work to manufacture an error. |
| Bridge after separate approval | Error notification while the main queue waits on the sparse event; never-submitted prepare failure; existing synchronous submitted-failure hook. | Gate cancellation must unblock; safe retirement still waits for real native/main completion where work was submitted. |
| Nearest regression | Existing `dx12_reserved_buffer` barrier update/copy fault cases, `dx12_reserved_texture_overlap --handoff-retirement` and both negative controls, multiheap/cross-queue and packed smoke. | Preserve current HRESULT contracts, complete-word/edge checks and healthy prefix fence completion. |
| Compatibility gate | Relevant D3D11/Airconv smoke, Metal3/no-placement path on an available real target; x64 and x86 builds with compatible bridge pairs. | NOT_RUN when the necessary target is absent, never fake a capability to create a passing result. |
| Later differential | Windows native device-removal behavior and actual naturally captured native error feedback, when available. | TARGET_DEVICE_NOT_AVAILABLE / NOT_RUN until genuine evidence exists. |

For new bridge tests extend the existing `dx12_reserved_buffer` failure entry instead of adding another repository runner. External prototype/scripts/results remain under `dxmt-test`. Use isolated Wine prefixes, per-case shader caches, Metal API/Shader Validation, actual module/export bytes, Unix/MSC loaded paths/UUIDs and artifact hashes. Separate native evidence, injected transport results and static models in the manifest.

### Performance / Risks / Rollback / Definition of Done

Cost hypothesis: one native result payload/options/condition and callback per existing sparse barrier; no added draw/dispatch work or descriptor scan. Waiting for CPU feedback can delay FIFO retirement, ring capacity and public CPU fence notification even after GPU completion. **Performance is UNMEASURED**, not zero overhead. Next prototype must measure callback-delivery lag and admission/backpressure separately from GPU time, with warm/cold samples; a later bridge change needs equivalent-build A/B comparison. This is a correctness trade-off to measure, not a claimed optimization.

Risks: callback late/missing delivery, failure without an event signal, accidental lock inversion, callback/native-reference cycle, preparation/record-publication race, suppression of healthy prior fence values, premature cleanup after an injected submitted error, and PE/Unix ABI mismatch. Preserve original waits and use existing first-error device authority. Rollback is the focused future commit; this documentation commit has no runtime effect.

Prototype DoD: actual queried device identity; real mapped/unmapped full-data oracle; actual native callback observed; early/late/reversed report ordering and exactly-once ownership verified; explicit separation of synthetic/native failures; no validation diagnostics; retained commands/artifact identities; measured callback lag. This round does not meet that GPU DoD. Bridge DoD additionally requires device/fence propagation, admission/cancellation/teardown regressions and measured cost. Do not mark the asynchronous gap closed when only the prototype or synchronous fault tests pass.

### Validation Results / Capability Impact / Git State / Next Recommended Slice

- **PASS — SOURCE/SPEC AUDIT:** installed headers and current target/pinned Mullion source inspected; fresh Mullion bytes match the retained pinned reference. Official documentation snapshots and source hashes retained in E2. No production source or test changed.
- **STATIC_MODEL_PASS_NOT_GPU_EVIDENCE:** E2/check_order_model.py enumerates four schedules for a single post-GPU error poll; one permits publication before feedback. Requiring both main completion and the record's feedback admits three schedules with no premature publication under the listed assumptions. It also checks reverse report order and separation of submitted-error status from safe native retirement. This is a design counterexample/proof boundary, **not LOCAL_FAIL/LOCAL_PASS for Metal**. An unused-loop error in the initial external model script was corrected and recorded in E2/model-initial-failure.txt; it was not a GPU/build failure.
- **NOT_RUN:** production rebuild and GPU/runtime tests; this is a documentation-only design round and no bridge implementation was made. Earlier real-test results are not re-labelled as this round's executions.
- **CAPABILITY IMPACT:** none. No new MSC_SEMANTIC_GAP or BLOCKED_BY_HARDWARE conclusion. The M1 Max target is available; missing asynchronous feedback is BACKEND_NOT_IMPLEMENTED, while exact release/void-mapping error contracts remain SPEC_UNVERIFIED. Other GPUs/Windows-native differential remain TARGET_DEVICE_NOT_AVAILABLE / NOT_RUN.
- **GIT:** baseline above; only this report is committed with repository style, no push. The original `.gitignore` edit stays untouched. Final commit ID, status and evidence hashes are in E2/delivery-manifest.json.
- **NEXT:** execute one external native feedback-timing/ownership prototype on M1 Max with real R32 sparse readback and controlled report delay, before approving any additive bridge ABI. Stop before production integration and review its results against this Design Card. The broad development goal remains active.

## N13 native feedback timing / ownership prototype — execution report

2026-11-05. **Gate 0 / 1 / 2 / 3 / 4 complete for the external prototype only.** The default-feedback-queue prototype passes. A separate custom-feedback-queue teardown configuration fails locally and is not accepted. No production bridge implementation or capability promotion is included.

### Task Summary / Baseline Evidence / Changed Files

Selected node: N13. Execute the prerequisite in the preceding Design Card, distinguishing native workload completion, GPU-event progress, CPU report delivery, consumer completion and safe retirement. Baseline `feat/d3d12`, `2a618d8e91400540eb9f562c84a74d3a087bc4a4`; local tracking ref 0 behind / 12 ahead at start. Only the original `.gitignore` edit was present, SHA256 `764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759`. No remote refs are refreshed in this round. Mullion remains the pinned source reference `93de32b9145885ceb2e45be7d445e56d62499b3c` retained in E2, not a new Mullion runtime result.

New evidence E3: `/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-feedback-native-prototype-20261105`. All prototype sources, runners, binaries, SDK snapshots, logs, crash reports and manifests are outside the repository. The sole repository change is this report append; its preceding bytes are preserved exactly. Production sources, repository tests, build settings and deployment are unchanged.

Fresh environment: Apple M1 Max, macOS 27.2 `26B123`, Xcode 27.2 `27B52`, Metal compiler `32023.922`, native arm64. Every final process queries the real device: Apple7=true, Metal4=true, PlacementSparse=true, Apple8/Apple9=false; R32Uint 64-KiB pages return a 128×128×1 tile. The fixture creates a 256×128 placement-sparse texture and a 131072-byte placement heap. Its reported **Metal** sparse texture tier is 2; this is not a D3D12 Tiled Resources Tier declaration.

`E3/baseline.json` records starting Git state, source/header hashes and environment. `E3/runs.json` pairs every final process with exact source/binary/log hashes and loaded Metal/AGX/MetalTools image paths and UUIDs. Metal UUID is `4309245A-A57C-32FE-A6F9-54918647F25D`; AGXMetalG13X UUID is `79D56015-6C6E-3021-B27B-3D0D4EF1472E`. Release prototype SHA256: `0836803fd3938cb7bb99ecdbe4305d3f830354c9a004488d2b820e94f7b29dda`; source SHA256: `736ed4d0961d9973e32eb3d09a5e2059af42c644461d2c10753f1ccba59cf877`. The pipeline is native MSL on a classic compute encoder; no Wine or MSC is loaded. This does not validate PE/Unix pairing or MSC shader semantics.

### Source / SDK / Mullion evidence and design decisions

| Conclusion | Source and evidence type |
| --- | --- |
| Existing DXMT barrier still returns synchronous admission status and installs no completion feedback. Existing FIFO retirement still waits only the classic main command buffer. | `src/winemetal/unix/winemetal_unix.c:4131–4167`; `src/d3d12/d3d12_command_queue.cpp:456–515,585–623`. SOURCE_AUDIT, refreshed through CodeGraph; BACKEND_NOT_IMPLEMENTED remains open. |
| Actual Metal feedback supplies native error and host-time GPU timestamps for its committed workload. Mapping methods remain separate void operations. | E3/MTL4CommitFeedback.h:19–40; E3/MTL4CommandQueue.h:237–275,353–371. SDK_CONTRACT. Successful barrier feedback is not mapping-error coverage. |
| The prototype keeps native command buffers, allocators, options, resources and explicit queue ownership through real feedback and main completion. It resets each allocator only after the callback application body has drained. | E3/sparse_feedback.mm:50–124,250–393. SOURCE_AUDIT / LOCAL_PASS. Reset and release of fixture-owned references are counted; internal Metal object deallocation or global leak freedom is not inferred. |
| A real native result and its test-controlled report are separate per-ticket states. A report received before its waiter remains available; releasing ticket 22's report does not complete ticket 21. | E3/sparse_feedback.mm:50–105,395–468; final REPORT_GATE / EARLY_REPORT / REVERSE_REPORTS records. LOCAL_PASS for this prototype, not production behavior. |
| Caller queue-handle release is safe in the tested sequence because an explicit fixture owner remains through callbacks and drain. The callback captures only its result payload and completion group, not the queue, work/options owner or any PE object. | E3/sparse_feedback.mm:274–299,360–393,449–468. SOURCE_AUDIT / LOCAL_PASS. This does not test destruction of the last in-flight queue/resource reference. |
| Mullion's distinction between GPU progress and unsettled CPU feedback remains useful; its held-work scheduler is not transplanted. | E2/mullion-queue.cpp:529–562; E2/mullion-bridge.c:5656–5681,5721–5737. Retained SOURCE_AUDIT, no new MULLION_OBSERVED result. |

The native sequence mirrors the current hybrid handoff: classic main signal → Metal4 queue wait → ResourceState visibility barrier committed with options/feedback → real map/unmap → Metal4 signal → classic wait/upload/compute readback. Both texture and backing heap, plus upload/output buffers, are explicitly resident. Each mapped upload uses nonuniform per-tile/per-word data with changing seeds; every unmapped texel must read zero. CPU output is prefilled with nonzero canaries, and 32 trailing guard words must remain unchanged. The external Python checker independently recomputes the full-output hash. No native `CopyTiles` API is claimed; this slice uses full-data shader readback, with the existing D3D12 CopyTiles regressions left intact.

No callback queue is blocked to create a passing schedule. The test holds a separate report gate **after actual native callback receipt** while real event/readback completion is already observed. This is SIMULATED_REPORT_DELAY, not naturally delayed driver feedback or a synthetic GPU error. A native completion group covers the callback's application body; captures and options contain no ownership cycle. Exactly-once counts cover per-ticket result destruction and clearing fixture-owned native handles after actual completion, not every internal driver allocation.

### Validation Results / test matrix

Final batch: 15 processes, all independently audited. Four validated positive processes include one ASan build; eight additional positive processes run with API/Shader Validation explicitly disabled for timing. Three validated negative controls exit 2 as expected. Every process uses the same final source; ASan uses a separately hashed binary.

| Case | Result / evidence |
| --- | --- |
| Actual capabilities, 64-KiB map/upload/read → NULL unmap/zero | **LOCAL_PASS:** 12 positive processes × 22 full readbacks = 264 readbacks / 8,650,752 checked DWORDs; 88 readbacks have API+Shader Validation enabled. Every readback includes 32 unchanged guards. E3/runs.json and per-process logs. |
| Native feedback and timestamps | **LOCAL_PASS:** all 330 final-batch callbacks, including benign GPU work underlying the negative controls, report error=nil exactly once and have ordered host-time timestamps. This is success evidence only; no native GPU fault was induced. |
| Held reports, early reports, reverse reports | **LOCAL_PASS:** 48 positive pending checks, 24 early-report cases and 12 reverse pairs. Held map/unmap reports remain withheld for 51.61–55.99 ms after actual receipt; consumers complete only after release. This is controlled transport evidence. |
| Caller handle drop / allocator and result ownership | **LOCAL_PASS:** 12 positive caller-handle-drop cases retain the explicit fixture queue owner. Across all 15 final processes, 330 result payloads are created/destroyed, 330 owned native handle sets retire and 330 allocators reset, each once after actual completion. Weak payload references expire after full fixture drain. |
| Wrong expected word | **EXPECTED_REJECTION:** exactly one mismatch in each of 11 mapped readbacks; unmapped outputs remain correct. The independently computed true-data hashes still match. |
| Omitted compute dispatch | **EXPECTED_REJECTION:** all 22 outputs retain the nonzero canary and are rejected. An unexecuted shader cannot pass the unmapped-zero oracle. |
| Consumer ignores report gate | **EXPECTED_REJECTION:** four premature-consumption checks fail; actual GPU work and native feedback still complete normally. No fake native error is reported. |
| API/Shader Validation and ASan | **PASS for final batch:** expected validation-enable messages and actual debug device/PSO classes checked; no API/Shader error diagnostics. The full default-queue ASan positive run passes with leak detection disabled; this is CPU memory-safety evidence, not a global leak audit. |
| Production build, Wine/MSC, D3D11, Metal3 target, x86, Windows-native, Mullion runtime | **NOT_RUN:** production code is unchanged and the selected slice is an external native prototype. These are not replaced by the native positive result. |
| Real asynchronous error, missing GPU event, missing callback, PE device/fence propagation, 32-entry FIFO backpressure | **NOT_RUN:** required before production integration acceptance. A timeout would terminate the probe as failure, never fabricate completion or device removal. |

### Retained failures / runtime qualification

The initial external compile failed because a C++ member named `id` hid the Objective-C type; it was renamed to `serial`. `compile-initial-failure.log` is retained. This was a probe build error, not a repository build regression.

The first GPU process completed all 22 readbacks and received feedback but then crashed during cleanup of a **custom** descriptor `feedbackQueue`. This whole process is **LOCAL_FAIL**, excluded from final PASS/timing totals. A reduced ARC reproducer creates no GPU work (`E3/queue_teardown.mm:5–31`): explicit strong dispatch-queue and Metal-queue owners still reproduce a post-Metal-queue-release dispatch crash both with and without validation. Removing that post-release dispatch instead exposes `API MISUSE: Over-release of an object` in the fixture destructor. An ASan run also retains the libdispatch crash. Using the default Metal feedback queue is the passing control. Logs, binaries, crash reports and source/flag combinations are retained in E3.

This local configuration failure is observed, but the exact runtime/SDK ownership-contract cause is **SPEC_UNVERIFIED**; do not label it a proved DXMT defect, a native GPU fault, or an Apple7 hardware limit. The accepted prototype uses the documented default feedback queue plus an independent completion group. Do not add an undocumented extra-retain workaround or introduce a custom feedback queue in the bridge based on these results.

An additional diagnostic run with `OBJC_DEBUG_MISSING_POOLS=YES` exits successfully but emits 371 autorelease warnings. The captured first warning stack is MetalTools `_decodeReportLogState` / `preCompletionHandlers` on `com.Metal.CompletionQueueDispatch`, before the user completion handler (`E3/missing-pool-backtrace.log`); the debugger session was deliberately stopped/killed and is not a completed GPU acceptance run. Explicit pools remain around the prototype feedback body and main scope. Do not call this supplemental diagnostic clean, assume every warning has the same origin, or claim leak freedom. Final standard validation runs omit this optional ObjC diagnostic flag while retaining both Metal validation modes and stderr reporting.

### Timing / remaining blockers / capability impact

Eight unvalidated positive processes provide 16 process-first-cycle samples and 112 warm barrier samples. “First cycle” does not mean a cleared driver/shader cache. GPU timestamps are documented host seconds; each is checked against the probe's converted mach-absolute host clock. Event/readback observation times are CPU upper bounds sampled after main completion, not exact GPU event timestamps.

| Metric, microseconds | First-cycle median / P95 / range, n=16 | Warm median / P95 / range, n=112 |
| --- | --- | --- |
| Actual callback receipt minus barrier GPU end | 120.04 / 371.42 / 88.17–371.42 | 124.79 / 238.04 / 63.96–743.79 |
| Native commit call CPU duration | 10.48 / 56.21 / 6.42–56.21 | 5.06 / 8.54 / 3.25–35.92 |
| Mapping plus signal call CPU duration | 84.10 / 348.71 / 13.21–348.71 | 7.52 / 18.87 / 4.83–93.08 |

P95 is nearest rank. Controlled held/early/reverse cases, validation/ASan processes, failures and debugger runs are excluded. No natural callback arrived after the CPU's event/readback observation in the 12 positive processes; the sampling point does **not** prove callbacks always precede actual GPU event signals. Controlled report delay validates the proposed consumer invariant only. No A/B production overhead, admission-ring saturation, speedup or “no regression” conclusion follows from these native timings.

- **BACKEND_NOT_IMPLEMENTED:** production WineMetal feedback transport, per-Submission consumption, asynchronous error cancellation and FIFO fence-publication closure remain open. The prototype does not close them.
- **SPEC_UNVERIFIED:** custom-feedback-queue teardown ownership cause; exact immediate allocator/command-buffer release contract; error observability of separate void mapping operations; safe teardown without any terminal evidence. The prototype deliberately retains allocations through actual completion.
- **MSC_SEMANTIC_GAP:** none newly established. MSC, its counter/descriptor ABI and compatibility behavior are outside this native-MSL slice and remain unvalidated here.
- **TARGET_DEVICE_NOT_AVAILABLE / NOT_RUN:** Windows-native and other GPU differential targets. The real M1 Max target is available and passes the accepted placement-sparse path; no BLOCKED_BY_HARDWARE conclusion is justified for it.

Public Feature Level, Shader Model and Tiled Resources Tier are unchanged. Metal3 shadow backing, existing Reserved Resource tests, D3D11 and MSC runtime compatibility are untouched. Rollback of this round is the focused report append; external evidence is retained, not deployed.

### Git State / Next Recommended Slice

Only this report is committed with repository history style; no push. The original `.gitignore` remains untouched and unstaged; no other repository work is introduced. E3/delivery-manifest.json records the resulting commit, final Git state, file identities and audit outcome.

Recommend one next N13 slice: add a native barrier-completion credential through the **existing WineMetal bridge**, using the default feedback queue and a native-owned result/group, with prepare/submit/wait/release and benign-work delayed/early/reverse/synthetic-error transport checks. Keep old entry points and ABI-table ordering; validate x64/x86 paired artifacts and exact ownership. Do not yet attach it to D3D12 FIFO retirement or claim the asynchronous error gap closed. This narrows the next gate to transport and lifetime before the separate existing-Submission integration. Stop here for review/authorization; no production implementation begins in this prototype round. The broader development goal remains active.

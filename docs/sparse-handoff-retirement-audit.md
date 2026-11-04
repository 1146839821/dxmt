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

### Next bridge slice Design Card — 2026-11-05

- Goal/baseline: additive native barrier feedback transport at e9152dcc; original .gitignore only. Gate 0 source/SDK and E2/E3 evidence refreshed. Continuation authorizes this bounded N13 implementation.
- Evidence: old checked barrier has synchronous status only. E3 default feedback queue passes real sparse readbacks; custom feedback-queue teardown remains unverified. Mullion per-work result semantics are a reference; no scheduler/source is copied.
- Scope/invariants: INV-03/04/08/11/12/14; existing WineMetal native/C/thunk/WMT layers and existing reserved-buffer test. No second resource-state owner or scheduler.
- Proposal: prepare owns encoded command objects without submitting; submit admits once; timed wait returns prepared/pending/terminal result. Callback captures only native payload, preserves native error/timestamps and wakes per-credential waiters. Existing NSObject ownership handles release.
- ABI: append native/WoW64 call identifiers after ordinal 199; fixed-width embedded requests/results and asserted sizes/offsets. Old exports/indices unchanged; no callback to PE pointers.
- Tests: bounded per-prepare report delay and separately labelled synthetic error after real feedback; no blocked Metal callback queue or unsafe GPU work. Verify early/delayed/reverse/timeout/duplicate-submit/prepared-release and caller-release cases.
- Non-scope: D3D12 FIFO/device-removal integration, mapping-error certification, backend replacement and capability changes.
- Cost: one native object/condition and retained command objects per opted-in barrier, one options/callback per submission; no new normal GPU waits/draw work. Existing production callers do not opt in this round.
- Validation/DoD: x64/x86 builds, exact ABI layout audit, paired x64 Wine transport, real reserved-resource readback and nearest fault/retirement regression with validation and artifact identity. Measure native transport and distinguish injected delay.
- Risks/rollback: callback cycles, publication races, missing terminal evidence and WoW64 drift. Timeout never invents success/removal. Revert the focused additive commit; retain native-fault/missing-callback and unavailable-target limitations explicitly.

### Git State / Next Recommended Slice

Only this report is committed with repository history style; no push. The original `.gitignore` remains untouched and unstaged; no other repository work is introduced. E3/delivery-manifest.json records the resulting commit, final Git state, file identities and audit outcome.

Recommend one next N13 slice: add a native barrier-completion credential through the **existing WineMetal bridge**, using the default feedback queue and a native-owned result/group, with prepare/submit/wait/release and benign-work delayed/early/reverse/synthetic-error transport checks. Keep old entry points and ABI-table ordering; validate x64/x86 paired artifacts and exact ownership. Do not yet attach it to D3D12 FIFO retirement or claim the asynchronous error gap closed. This narrows the next gate to transport and lifetime before the separate existing-Submission integration. Stop here for review/authorization; no production implementation begins in this prototype round. The broader development goal remains active.

## WineMetal sparse completion bridge — delivery, 2026-11-05

### Task Summary / Baseline Evidence

**Gate 0–4 complete for additive native completion transport only.** D3D12 FIFO consumption is not implemented in this round. Broad sparse asynchronous error handling remains open.

Baseline: feat/d3d12 at e9152dccbe9180e59cdb0d177dc0caef525cdf24, 13 ahead / 0 behind local origin/feat/d3d12 (not refreshed). Only original .gitignore dirty; SHA256 764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 preserved. M1 Max / Apple7, macOS27.2 26B123, Xcode27.2 27B52, Metal32023.922, Wine11.0, compiled MSC SDK API4.0.3; runtime MSC product version is not queried.

E4: /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-feedback-bridge-20261105. baseline.json, before-* source copies, SDK headers, sources.json/Apple documentation JSON, build/process logs, loaded export bytes, Unix UUID/path, artifact hashes and ownership traces are retained. Earlier E2 Mullion93de32b9145885ceb2e45be7d445e56d62499b3c remains the pinned source reference; no Mullion code copied. Microsoft lifetime/synchronization and Apple commit-feedback contracts recorded above remain applicable. Default feedback queue only.

### Findings / Design Decisions

- **SPEC_REQUIRED / LOCAL_PASS:** separate commit admission, actual native feedback receipt and report publication. Prepared never submits. Pending persists through bounded test-report delay after actual feedback; timeout is transport success with nonterminal state, not completion. Submitted-host-failure injection returns false after commit but cannot permit early retirement.
- **LOCAL_PASS:** native payload retains queue/allocator/command buffer/NSError. Metal's copied block owns the payload; payload does not own commit options, avoiding a cycle. Delayed dispatch owns it until report publication. Actual creation/native/report/destruction traces cover prepared release, caller release, last-caller release while GPU-gated and one callback. This is not general leak-freedom or missing-callback teardown proof.
- **LOCAL_PASS:** append ordinals200–202; both 203-entry tables preserve their exact original 200-entry prefixes. Result56 bytes embedded in request72; no retained PE pointer/callback. Error handle is borrowed while credential lives. Compile-time sizes/offsets and actual WoW64 execution checked.
- **LOCAL_PASS:** feedback modes resolve exports dynamically. Final original probe import table has no WineMetal dependency, preserving native-Windows import behavior; native Windows execution remains NOT_RUN.
- Reuse existing NSObject/Reference owners and native condition, one credential per opted-in barrier. No scheduler, second resource-state map or extra normal draw/GPU wait. Existing callers and d3d12_command_queue.cpp are byte-identical to baseline. Reject FIFO expansion/custom-feedback-queue workaround in this slice.

### Changed Files / Source Evidence

| File | Purpose / evidence |
| --- | --- |
| src/winemetal/winemetal.h:2478–2505 | States, fixed-width result, owned prepare/one-shot submit/wait contracts. SOURCE. |
| src/winemetal/winemetal_thunks.h:55–66 | Additive ordinals and embedded request. SOURCE / ABI audit. |
| src/winemetal/winemetal_thunks.c:1603–1628 | Explicit status checks, copyout and null-output rejection. SOURCE / real transport. |
| src/winemetal/Metal.hpp:909–927 | Existing ownership wrapper exposes credential. SOURCE / build. |
| src/winemetal/unix/winemetal_unix.c:4173–4385 | Native owner/encoding/default callback/publication/wait/cleanup and bounded opt-in controls. SOURCE / ownership traces. Tables near5063/5274. |
| tests/dx12/dx12_reserved_buffer.cpp:36–336 | Actual capability queries, dynamic exports, GPU gates, ABI/null/duplicate/delay/reverse/error/release checks and exceptional-exit gate cleanup. CLI near980. SOURCE / GPU. |
| docs/sparse-handoff-retirement-audit.md:305 onward | Design Card, evidence, delivery and limits. DESIGN. |

### Validation Results

| Check | Result / boundary |
| --- | --- |
| x64 release complete default-target build, D3D12/tests/NVAPI/NVNGX enabled | **PASS**, build-final.log and earlier full logs. Existing Managed-storage deprecation warnings retained. |
| x86 release script-profile default-target build, D3D12/tests disabled | **PASS**, build-x86-script-profile.log and build-x86-final.log. Extra x86 D3D12-enabled build **FAIL** in unchanged d3d12_raytracing.cpp:210: uint64 offset narrows into 32-bit D3D12_RANGE/SIZE_T. build-x86.log retained; outside scope. Script-profile PASS is not x86 D3D12 PASS. |
| Final x64 nine feedback modes | **LOCAL_PASS**, normal/delay/reverse/synthetic/submitted-host-failure/release/three preparation failures. Each also passes existing MSC GPU buffer readback311296 DWORDs. Both Metal validation modes enabled. |
| Nearest regressions | **LOCAL_PASS**, ordinary buffer, submitted-barrier removal behavior, fenced texture handoff retirement, packed-tail readback. Two wrong-oracle processes reject with expected exit1. |
| Final x64 aggregate | **13 successful exits + 2 expected negative exits**; ten full buffer checks =3112960 DWORDs. Additional retirement/packed checks not included in buffer total. acceptance-audit.json / acceptance.log. |
| Actual WoW64 transport | **LOCAL_PASS**, PE32 pointer4/result56/request72, normal/delay/release/synthetic through paired x86 PE/x64 Unix. Exact export bytes/loaded Unix UUID checked. External fixture, not x86 D3D12 execution. |
| D3D11 nearest smoke | **LOCAL_PASS**, external hardware-device default-to-staging CopyResource/Map compares256 DWORDs; actual native DXMT module path and Metal validation checked. Not full rendering coverage. |
| Static delivery | **PASS**, scoped clang-format/diff check/table prefixes, no WineMetal static import, final build-stage pairing, original .gitignore and unchanged D3D12 production queue. |
| Actual native GPU error, missing callback, physical OOM | **NOT_RUN**. Synthetic error is separately labelled after actual benign-work feedback, not NSError/device-loss reproduction. |
| Native Windows/other GPUs/Metal3 fallback execution | **NOT_RUN**; unavailable differential targets classified TARGET_DEVICE_NOT_AVAILABLE. No hardware block for available M1 Max. |

Reproduction: python3 E4/final_runs.py, followed by python3 E4/audit.py (expand E4 to full path above). Isolated Wine prefix/stage/cache, paired PE/Unix modules and exact source/artifact identities are retained. Earlier transport-*, delivery-* and final-* runs are supplemental, excluded from acceptance totals.

Retained setup/audit failures: first WoW64 launch selected the parent x64 DLL and failed LoadLibrary; separate executable directory with x86 DLL corrected selection. Next process passed GPU/ABI but export parser only accepted x64; PE32 parser validated the retained result without rerunning it. First release-only external process exited before its deliberately delayed report; trace audit rejected it. A bounded drain and mandatory native/report/destruction evidence closed that fixture gap. Repository release mode also drains and opens GPU gates on exceptions. These are not native GPU faults or accepted ownership results.

Six final validation-enabled process-first samples: prepare median709.20µs/range654.00–880.80µs; submit median219.35µs/range197.50–435.30µs. Includes validation/instrumentation, not warm steady state or A/B production costs. Report delay excluded from these call durations. No speedup/no-regression claim. Production callers do not use new allocations/conditions/callbacks yet.

### Capability Impact / Regression Risks / Remaining Blockers

No FL/SM/Tiled Tier promotion. Metal3 shadow backing, D3D11 implementation, Airconv/MSC and existing Reserved Resource semantics unchanged. New exports require paired PE/Unix artifacts.

- **BACKEND_NOT_IMPLEMENTED:** existing Submission/FIFO integration, error-before-success/fence publication, cancellation/unblocking and safe main-work retirement. Transport alone does not close production semantics.
- **SPEC_UNVERIFIED:** attribution for separate void mapping failures, custom feedback-queue teardown, safe reclamation without terminal evidence. Immediate allocator release is avoided, not certified.
- **MSC_SEMANTIC_GAP:** none newly established; broader shader/counter/descriptor/sparse conformance stays separate.
- **TARGET_DEVICE_NOT_AVAILABLE / NOT_RUN:** unavailable Windows/other-GPU differentials. Actual M1 Max Apple7/Metal4/PlacementSparse queries succeed; no Apple7 PlacementSparse hardware block.

Rollback: focused additive commit. Test controls opt-in/bounded/separately labelled. No PE callback/custom feedback queue retained. Missing callback can retain submitted payload indefinitely; timeout does not invent terminal success or allow early retirement.

### Git State / Next Recommended Slice

Commit seven source/test/report files with repository style, no push; original .gitignore unstaged, no staged/untracked residue. Resulting HEAD/tracking/hashes recorded post-commit in E4/delivery-manifest.json.

Next one N13 slice: **attach credentials to the existing Submission/FIFO completion owner**, gate success/fence publication on required native terminal evidence; test benign delay/reverse/synthetic errors, submitted-host-failure cancellation and safe retirement. Preserve existing owners/waits, no second scheduler/state truth or capability promotion. Stop after this transport delivery; no automatic FIFO implementation in this round.

## N13 sparse feedback → existing FIFO — Design Card, 2026-11-05

- GOAL / BASELINE: close auxiliary barrier feedback-before-success publication at eee6030e; original .gitignore only. Prior turn is progress (committed bridge and real validation), not goal completion.
- CURRENT PATH / EVIDENCE: BeginSparseMapping publishes classic handoff Submission, submits native barrier with only synchronous status; CompletionThread waits classic completion and can advance without native feedback. Existing ring32 and removal listeners remain authoritative.
- INVARIANTS / SPEC: INV-03/04/08/11/12/14; Microsoft removed-fence UINT64_MAX and explicit queue synchronization; Apple per-commit feedback and retained allocator obligations; pinned Mullion per-work result/reference semantics only, no code copy.
- PLAN A/B / SELECTION: poll a sticky queue error at future API calls versus bind one credential to its existing handoff Submission; choose the latter to cover late reports and no future API calls without monitor thread/second scheduler.
- PROPOSED CHANGES: prepare before main admission; existing CommitSubmissionLocked commits main and submits attached credential before publishing/notifying the record. Existing completion thread waits main then that exact credential outside both queue locks, marks removal before advancing/publishing success, and retires only after native/main terminal evidence. Never expose Prepared as submitted or use a maximum callback sequence.
- ERROR BOUNDARY: prepare failure removes before main admission; post-commit synchronous failure preserves the published record and real native lifetime. Existing removal listener releases sparse-event waits. Later submitted main work still waits actual terminal completion; timeout never grants success or retirement.
- FILES / SCOPE: existing command_queue.cpp, existing reserved-buffer probe, existing bridge test controls only if required for deterministic cancellation, this report. No new src/d3d12 file, resource-state truth, callback into PE or ABI change.
- PROOF / DoD: real delayed-report success fence stays pending; reverse reports do not skip lower work; synthetic error wakes registered fence as UINT64_MAX and rejects later Signal, with healthy prefix retained; submitted/prepared failure and safe retirement; wrong oracle and nearest buffer/texture/packed/D3D11 regressions, exact module identities.
- COST: one existing credential per native mapping handoff, bounded by ring plus preparation; one native condition wait per handoff, unchanged GPU work/waits. Measure same-device baseline/candidate CPU mapping cost; distinguish deliberate latency and validation overhead.
- NON-SCOPE / RISKS: mapping-operation error attribution, missing terminal callback reclamation, native GPU fault generation, cross-device full conformance. No Metal3/D3D11/backend/capability changes; missing callback remains explicit risk, never fabricated completion.
- ROLLBACK: one focused FIFO/test/doc commit; preserve old bridge exports and current waits. Continuation authorizes this N13 slice, repository-style commit and no push; stop after delivery, broad goal remains active.

## N13 sparse feedback FIFO — Delivery, 2026-11-05

### 1. Task Summary / Gate outcome

**Gate 0–4 complete for the per-handoff native barrier credential → existing Submission/FIFO path.** Success publication now waits the exact barrier's terminal feedback. Synthetic error propagation/cancellation and retained native/main lifetime are locally verified. This does not complete the broad sparse/MSC development goal or certify error reporting for separate native mapping operations.

### 2. Baseline Evidence

Baseline eee6030e7dd6347c690e8d877d33918872a206c5 on feat/d3d12; local origin reference 0 behind /14 ahead, not refreshed. Original .gitignore SHA256 764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 preserved. Actual environment rechecked: M1 Max, macOS27.2/26B123, Xcode27.2/27B52, Metal32023.922; isolated staged Wine11.0 runtime. Repository toolchains/wine SDK executable reports8.17; do not conflate SDK tool version with tested runtime. Compiled MSC API4.0.3, runtime product version unknown.

E5: /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-feedback-fifo-20261105. Contains baseline.json/before-* copies, sources.json, pinned Mullion93de32b9145885ceb2e45be7d445e56d62499b3c source, Apple documentation JSON, freshly fetched Microsoft GetCompletedValue contract, exact build/link commands, paired stage files, loaded code/UUID evidence, every process environment and stdout/stderr, performance and ownership audits. Isolated prefix/stage/cache only; no shared Wine deployment, sudo, patch reapplication or push.

### 3. Findings / Source Evidence

| Finding | Source / evidence |
| --- | --- |
| Baseline handoff committed a native barrier without attaching its feedback to FIFO, allowing later public fence completion before report delivery. | Baseline queue:585–615/456–515; previous bridge commit supplies per-work credential. SOURCE_AUDIT. Independent differential probe with 1500ms report delay: baseline early=1/value1/exit1, candidate early=0/value1/exit0. LOCAL_PASS of distinguishing oracle; deliberately delayed report, not a naturally captured native error. |
| Each published handoff now has a submitted or submit-failed credential; completion waits classic main then that exact native report outside both queue locks. | src/d3d12/d3d12_command_queue.cpp:86–90,460–543,560–589,626–667. SOURCE / GPU traces. |
| Device-removal listener remains the cancellation authority. UINT64_MAX cancellation unblocks later sparse-event-dependent work; those submissions still wait real classic terminal state before retirement. | Existing SubmissionState:131–141 and completion:474–530. New cancellation test suppresses exactly one mapping signal only with explicit synthetic-error hook. LOCAL_PASS: serial3/4 retire main_status4 after error on serial2. No success is inferred from cancellation. |
| Reverse native reports cannot complete an earlier handoff. | Two real credentials publish in reverse order; FIFO still waits lower report before later Signal. verified-reverse.log. LOCAL_PASS. |
| Queue admission ring bounds held credentials. | 128 mapping calls with 200ms report delay, final real 16384-DWORD readback; peak33 native credentials (ring32 plus preparation), all destroyed. verified-ring-audit.json. LOCAL_PASS, not proof against missing callbacks. |
| Mullion distinguishes GPU event completion from later CPU feedback using unsettled_ / SettleFeedback. | E5/mullion-queue.cpp:532–562; bridge feedback at5665–5673/wait5721–5737. SOURCE_AUDIT, not Mullion runtime observation. Share per-work semantics; do not copy its scheduler/unbounded unsettled registry. |

### 4. Design Decisions

Keep the existing queue admission/commit owner and bounded Submission. Prepare native objects before main admission. Queue existing sparse wait, commit classic handoff, submit its attached credential, then publish/notify the record. A failed submit still publishes the already committed main record so its real payload can drain. A prepare failure removes before admitting main work. Existing synchronous post-commit fault behavior is retained; no false early native retirement.

The existing completion thread consumes exact per-work feedback after classic completion. Error invokes existing MarkDeviceRemoved before advancing FIFO; later fence Signal cannot succeed on the removed device. Healthy prefix is verified independently before the failing operation. Native callbacks never call PE; no new thread, scheduler, resource-state map, bridge ABI/export or GPU synchronization is added.

Reject sticky future-API polling because there may be no next API and callback delivery may be late. Reject synchronous wait in mapping API because it can prevent the application from submitting the gate-opening work. Existing ring pressure may naturally block admission; that is measured and bounded, not a new application-side per-mapping wait.

Two opt-in queue test controls: cached completion trace with process-specific observation event; hold one mapping signal only when explicitly paired with synthetic-error mode, exercising cancellation without unsafe GPU work. Neither changes advertised capabilities. The cancellation probe is process-bounded; absent native feedback can still hang drain, an explicit remaining limitation.

### 5. Changed Files

- src/d3d12/d3d12_command_queue.cpp:89 — own credential in existing Submission; completion460–543 waits/report/removal before FIFO advance; admission560–589 submits before record publication; Begin/End626–683 preserve event ordering and tested failure handling. Cached opt-in tracing/cancellation controls near700. No new src/d3d12 file.
- tests/dx12/dx12_reserved_buffer.cpp:702–870 — extend existing probe with healthy prefix, delayed/reverse/error/cancel/prepared/submitted failure contracts, real nonuniform16384-DWORD readback and wrong-last-word control. fifo-perf repeats128 mappings and records CPU duration. Queue drains before fixture resource destruction, including exceptional exits; unique named event prevents cross-process collision. Existing test paths remain available.
- docs/sparse-handoff-retirement-audit.md:392 onward — Design Card and this delivery. WineMetal source/ABI, shaders, resource backends and feature declarations unchanged.

### 6. Validation Results

| Check | Result and scope |
| --- | --- |
| x64 release complete default-target build with D3D12/tests/NVAPI/NVNGX | **PASS**, build-verified.log. Compiles actual production change and existing tests. |
| x86 release script-default profile, D3D12/tests disabled | **PASS**, build-x86.log. Not x86 D3D12 execution; earlier unchanged raytracing SIZE_T narrowing remains documented. |
| Final FIFO matrix | **8 success +1 expected negative**, verified-fifo.log: delay/reverse/error/cancel/submitted/three preparation failures and wrong readback. Metal API/shader validation enabled, actual paired modules. |
| Adjacent regressions | **12 success +3 expected negatives**, verified-regressions.log: MSC reserved-buffer311296-word readback; wrong oracle; barrier factory/encoder/submitted faults at first and second call; fenced handoff retirement, wrong-tile/wrong-shader controls; packed-tail; D3D11 256-word hardware buffer copy. |
| Ring saturation | **PASS**,128 delayed mappings and16384-word readback, peak33 credentials, all retired. Both validation modes on. |
| Independent old/new fence oracle | **PASS of differential**, baseline expected exit1 early success; candidate exit0 waits report. Both validation modes on; identical external executable/dependencies. |
| A/B cost fixture | **8 success**,4 baseline+4 candidate in ABBA repeated order,128 mappings/process and full16384-word readback. Validation disabled for timing only. Same test/Wine/MSC/Unix dependencies, exact per-run DLL hashes. |
| Static/delivery audit | **PASS**, scoped clang-format, diff check, original .gitignore, exact code export bytes/path/Unix UUID, build-stage pairing, no WineMetal source changes, no new modules. E5/verified-audit.json and audit-verified.log. |
| Native GPU failure generation, missing callback, physical OOM, all independent queues/device races | **NOT_RUN**; benign synthetic result proves transport/consumer behavior only, not NSError generation/recovery. |
| Windows native / other GPU / Metal3 fallback runtime / full D3D11 rendering | **NOT_RUN**. Unavailable differential targets TARGET_DEVICE_NOT_AVAILABLE; do not label real M1 Max unavailable or Apple7 PlacementSparse unsupported. |

Final acceptance aggregate:35 processes =30 successful exits and5 expected negative exits (includes A/B and differential; excludes earlier exploratory runs). Nine ordinary FIFO/performance readbacks plus ring etc. are recorded individually; do not combine heterogeneous checks into a fictitious conformance coverage percentage. Correctness runs use API+shader validation, timing runs deliberately do not.

Reproduction, expand E5 to full path: python3 E5/fifo_runs.py; python3 E5/regressions.py; python3 E5/performance.py; python3 E5/audit.py. Ring and independent differential commands/source are retained externally with their process manifests. Existing stage/prefix remain isolated. Do not overwrite retained labels during a new run; use fresh output paths.

Retained failed attempts: initial PE SetEnvironmentVariable did not update native Unix getenv(), so no delay was injected and pending oracle failed; controls moved to process-launch environment. Baseline compiler probe initially chose host GCC rather than LLVM-MinGW (-fblocks rejected), then passed with configured compiler PATH. One audit initially counted the additional ring process as a ninth-case batch member; label filter corrected without rerunning GPU work. Named-event initialization and failure cleanup were tightened during review, followed by final formatted-artifact matrix rerun. These are not native GPU faults, and earlier successful runs are supplemental rather than substituted for final acceptance.

### 7. Performance / Capability Impact

Final four-vs-four process CPU mapping medians: baseline275.985547µs, candidate290.699610µs (about+5.33%); ranges269.657031–293.285937 and260.965625–302.066406. Earlier same-method batches showed approximately+4.33% and+4.73% medians. Distributions overlap;128 calls include ring admission pressure, process-first initialization, native encoding and mapping, not isolated callback cost. No warm-driver-cache guarantee, no speedup/no-regression claim. GPU timeline cost and broader scene impact remain unmeasured.

One credential reference per Submission adds8 bytes before alignment (32-slot ring plus one completion local), one condition wait per sparse handoff and existing bridge object/condition/retained command objects per active handoff. Classic nonsparse submissions add a nullable-field check, no new allocation/GPU wait. Cost is accepted for closing feedback-before-success semantics; optimization needs independent evidence and must preserve lifetime/order.

Public FL/SM/Tiers unchanged. Metal3 shadow backing, Airconv/MSC, D3D11 implementation, WineMetal ABI and prior waits retained. Real M1 Max supports Apple7/Metal4/PlacementSparse; no hardware-block classification is introduced.

### 8. Regression Risks / Remaining Blockers

- **BACKEND_NOT_IMPLEMENTED:** attribution/propagation of asynchronous failures from separate void sparse mapping operations remains open; this credential describes the preceding ResourceState barrier workload only. Existing checked mapping APIs still provide synchronous status. Do not claim all sparse GPU errors are now covered.
- **SPEC_UNVERIFIED:** missing native terminal callback teardown and safe reclamation; custom feedback-queue ownership cause; independent mapping-operation error observability. Indefinite wait preserves safety rather than inventing success, but can strand completion/admission/queue destruction.
- **MSC_SEMANTIC_GAP:** no new gap established; shader/counter/descriptor/format and sparse conformance still require their own evidence.
- **TARGET_DEVICE_NOT_AVAILABLE / NOT_RUN:** native Windows/other-GPU differential; broad Metal3/no-placement and D3D11 rendering coverage absent. Actual Apple7 target available.
- Only owning queue FIFO gates its public completion. No new global scheduler/device-wide success guarantee is asserted; cross-queue progress depends on existing public fence synchronization. Controlled synthetic error is not natural device-loss reproduction.

### 9. Git State / Rollback

Focused commit includes only the existing queue, existing test and report. Preserve original unstaged .gitignore; no staged/untracked residue and no push. Resulting exact HEAD/status/tracking/source hashes recorded post-commit in E5/delivery-manifest.json. Rollback is this single focused commit; prior bridge and earlier evidence remain available.

### 10. Next Recommended Slice

One next N13 design/audit slice: **establish the native sparse mapping-operation completion/error contract** for update/copy/unmap independently of the preceding barrier. Check Apple SDK/documented guarantees and pinned Mullion ordering; design a benign per-operation proof using existing completion owner. Do not assume a later dummy command-buffer callback certifies prior void mapping errors, introduce a second scheduler, generate unsafe GPU faults or promote capabilities. Broad user goal stays active; stop this implementation round after commit.

## N13 sparse mapping-operation contract — Audit scope, 2026-11-05

- GOAL: distinguish native mapping admission, ordered completion, cancellation and error attribution; read-only source/specification audit plus next Design Card.
- BASELINE: feat/d3d12 at 85a44a0c6555398c278fc1dc48a78f4cfea7a295; original .gitignore only; local origin reference 0 behind /15 ahead, not refreshed.
- OWNER: existing command_queue.cpp admission/Submission/completion and resource logical mappings; WineMetal owns native translation. No replacement scheduler or second mapping truth.
- INITIAL EVIDENCE: four native mapping methods return void; checked thunks report Unix-call status. MTL4CommandQueue signalEvent orders prior GPU work; MTL4CommitOptions feedback belongs to a commit.
- BOUNDARY: preceding ResourceState barrier credential certifies that workload, not later mapping operations; a later benign commit must not be assigned undocumented error coverage.
- SCOPE: current DXMT, pinned Mullion, installed SDK27.2 and official Apple/Microsoft contracts; retain source identities externally.
- NON-SCOPE: production/test edits, capability promotion, backend migration, unsafe GPU faults and repeated previous acceptance batches.
- VALIDATION: source/contract evidence and consistency checks only this round; new GPU completion/error oracle remains NOT_RUN until its own implementation slice.
- COST / ROLLBACK: documentation only, no runtime cost; revert this focused documentation commit.
- DoD: sourced findings, blocker classification, minimal Design Card/test matrix, clean scope audit, repository-style commit without push.

## N13 sparse mapping-operation contract — Delivery, 2026-11-05

### 1. Task Summary / Gate outcome

Gate 0/source audit complete; Gate 1 below is proposed for the next minimal native validation slice. No production, bridge, test, shader or capability code changes. This round establishes what existing completion evidence means; it does not implement mapping-error reporting. Broad sparse/MSC goal remains active. Stop after this focused documentation delivery.

### 2. Baseline Evidence

DXMT feat/d3d12 at 85a44a0c6555398c278fc1dc48a78f4cfea7a295; local origin reference 0 behind /15 ahead, not refreshed. Only original unstaged .gitignore at start, SHA256 764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759. Rechecked macOS27.2/26B123 and Xcode27.2/27B52; installed SDK Metal headers retained. Previous real M1 Max Apple7/Metal4/PlacementSparse observations remain valid prior-round evidence, not a new hardware query this round. Wine11.0/MSC API4.0.3 identities are prior E5 runtime evidence, not freshly executed binaries.

E6 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-mapping-contract-audit-20261105. baseline.json, before-report.md, source-evidence.json and line-numbered excerpts lock current code. retrieval.json records freshly retrieved Apple/Microsoft pages and SDK bytes. operation-docs.json records six specific Apple mapping/event pages. reference-verification.json verifies pinned Mullion93de32b9145885ceb2e45be7d445e56d62499b3c queue/bridge bytes against their pinned raw URLs. No local Mullion checkout was found at the expected sibling path; pinned source is the reference, not a claim about current remote HEAD or a running Mullion build.

### 3. Findings / contract and source evidence

| ID / evidence type | Finding and boundary | Exact source |
| --- | --- | --- |
| M1 SDK_CONTRACT / SOURCE_AUDIT | Four placement mapping methods return void and accept no NSError output or commit options. Checked DXMT thunks mean the Unix function returned success after native invocation, not successful GPU mapping execution. Unmap is an update operation with Unmap mode; zero-operation calls return synchronously without issuing native work. | E6/MTL4CommandQueue.h:351–431; src/winemetal/winemetal_thunks.c:1644–1656,1673–1685,1700–1712,1728–1740; native:4405–4463,4483–4545,4564–4605,4625–4687. |
| M2 SDK_CONTRACT | Queue signalEvent schedules its signal after all preceding GPU work is complete. This supplies an ordered progress boundary for preceding mapping operations when the value is actually GPU-produced. It carries neither an operation result nor NSError. | E6/MTL4CommandQueue.h:261–276; apple-op-2.json abstract and apple-op-5.json. |
| M3 SDK_CONTRACT / SPEC_UNVERIFIED | Commit feedback describes errors while running the committed command buffers. Neither the retrieved mapping docs nor feedback docs promise attribution of previous separate void mapping errors to a later benign commit. A successful preceding barrier or subsequent dummy commit therefore cannot certify mapping success. This is missing documented coverage, not proof that the driver can never report such an error. | E6/MTL4CommitFeedback.h:19–40; MTL4CommandQueue.h:236–259; apple-feedback.json and six apple-op files. |
| M4 SOURCE_AUDIT | Existing sparse_event is also a cancellation channel: removal writes UINT64_MAX. Its observed value may therefore mean cancellation rather than native mapping completion. Existing EndSparseMapping queues the mapping signal, while attached Submission owns feedback for the preceding barrier. | src/d3d12/d3d12_command_queue.cpp:125–141,626–683; native:4223–4227,4270–4285. |
| M5 SOURCE_AUDIT | Logical mapping tables publish after all synchronous checked invocations return success. They describe accepted logical mappings needed by later queue work, not a GPU completion ledger. Overlapping self-copy applies saved heap mappings; a later synchronous failure may follow earlier issued native updates and remains terminal. Do not add a second table or attempt asynchronous rollback. | buffer.cpp:638–667,705–753; texture.cpp:2050–2090,2180–2233. |
| M6 SOURCE_AUDIT / UNVERIFIED lifetime | TileMapping/TileUpdate privately retain heap identity; BeginSparseMapping's Submission contains command buffer/barrier but no mapping-specific resource/heap payload. This is a concrete ownership boundary to test, not a proven UAF: legal application lifetime, native internal retention, residency and GPU retirement must be evaluated separately. Merely releasing native objects early is not an authorized D3D12 test. | buffer.cpp:118–133; texture.cpp:528–542; queue.cpp:86–110,650–656. |
| M7 SOURCE_AUDIT | Synthetic submitted-failure hooks explicitly signal a fresh shared event and drain before returning failure; allocation-failure hooks sometimes drain earlier submitted updates. These protect those probes. Production has no equivalent per-operation error credential; hook outcomes are not naturally observed native GPU faults. | native:4417–4461,4499–4543,4579–4603,4641–4685. |
| M8 SPEC_REQUIRED | D3D12 update/copy APIs return void. Copy overlap must behave as copying through a temporary source; out-of-resource source/destination regions are undefined and debug-layer errors. Invalid arguments are not a safe native asynchronous error-generation technique. | E6/ms-update.html, ms-copy.html; readable ms-update.txt/ms-copy.txt Remarks. |

Residency remains separate: DXMT attaches the global residency set to both queues (queue.cpp:723,735); retaining a heap reference does not establish its GPU residency or completion. This audit does not re-certify the global residency implementation. Existing preceding ResourceAlias/ResourceState barrier is retained; no absence of an explicit repeated barrier here is declared a new bug without a complete stage/synchronization proof.

Official references: [Apple signalEvent](https://developer.apple.com/documentation/metal/mtl4commandqueue/signalevent(_:value:)), [Apple commit feedback](https://developer.apple.com/documentation/metal/mtl4commitfeedback), [Apple queue](https://developer.apple.com/documentation/metal/mtl4commandqueue), [Microsoft UpdateTileMappings](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-updatetilemappings), [Microsoft CopyTileMappings](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-copytilemappings). Retrieved full bytes, hashes and URLs are retained in E6. Attempted Microsoft direct3d12/mapping-and-unmapping-operations page returned404; it supports no conclusion. SDK source and exact operation pages carry the conclusions instead.

### 4. Mullion comparison / Design Decisions

Pinned Mullion queue:593–625 captures ordered mapping work, settles prior workload, commits residency, invokes update, then signals and CPU-waits its mapping event before starting subsequent classic/Metal4 work and encoding an event wait. Update capture:1338–1353 retains resource/heap; copy capture:1392–1404 retains source/destination. Their lifetime extends through that wait. Native bridge:4129–4164 invokes the same four void methods and returns STATUS_SUCCESS; no mapping-specific NSError or callback is supplied. This is SOURCE_AUDIT, not MULLION_OBSERVED GPU behavior or proof that its wait is safe after all faults.

Mullion's committed-buffer feedback settlement at queue:532–562,787–815 remains distinct from mapping event progress. Its CPU wait/held-work scheduler and resource capture pattern cannot be copied wholesale into DXMT's asynchronous mapping entry point: an application may need to submit work that opens an earlier wait. Share only ordered-event, retained-input and independent-feedback concepts. No source code copied; no licensing change.

Choose an external, native-only completion contract probe first. Reject broad production feedback ABI changes until there is a documented result to transport. Reject reinterpreting synchronous bool, nil NSError on a sentinel commit, or cancellation event advancement as mapping success. Also reject using the classic resource-state encoder as a drop-in replacement: installed MTLResourceStateCommandEncoder.h:55–114 describes sparse texture operations without a placement heap/offset interface and no buffer equivalent; the existing placement buffer/heap model cannot be preserved by simple renaming.

### 5. Next Design Card — N13 native mapping completion boundary

- GOAL / BASELINE: prove ordered buffer map/copy/unmap completion on actual M1 Max using baseline85a44a0c and current SDK; explicitly leave asynchronous mapping-error attribution unresolved unless authoritative evidence is obtained.
- EVIDENCE / INVARIANTS: M1–M8, INV-03/08/11/12/14/15. Real capability and64-KiB page queries; no overrides. Earlier page compatibility evidence is a prerequisite to recheck, not an assumed enum value.
- SCOPE / NON-SCOPE: external Objective-C++ probe under dxmt-test; buffer two-page fixture first. No production/bridge/test repository edits, capability change, shader compiler migration, global scheduler, unsafe native parameters or natural GPU fault generation. Texture integration follows separately.
- ARCHITECTURE: use existing queue/event semantics as reference; distinct user-controlled gate and one private, GPU-only completion event not published to device-removal listeners. Original application ownership and residency stay valid through confirmed completion. No second resource-state authority.
- PROPOSED FLOW: allocate real placement sparse source/destination buffers and compatible resident heaps; initialize two distinct patterns, order classic producer to native mapping; map/copy/remap/unmap; enqueue exact private event value; later classic readback waits that value. For unmap, remap valid backing before reading so the probe does not depend on undefined unmapped buffer loads.
- ORDER ORACLE: put a fresh GPU wait before the mapping. CPU confirms private completion remains pending and downstream fence/readback cannot complete while gate is closed; open gate, require completion and full nonuniform readback. Separate completion result from data correctness, NSError from a sentinel commit, and timeout/unknown.
- ERROR / CANCELLATION: never write the private completion event from CPU. A test-labelled unrelated cancellation event may advance while gate remains closed; private event must remain pending. Release gate and drain actual work before fixture destruction. Timeout must not imply terminal native work or permit early reclamation; external watchdog terminates a stuck probe process, preserving logs.
- OWNERSHIP: hold source/destination resources, both old/new heaps, residency set, queues/events and any encoded command allocator through real completion. Releasing only a redundant reference is safe; physical last-reference teardown is a later separately designed test. No claim about production internal retention follows from conservative probe ownership.
- TEST PLAN / COST: matrix below, API+shader validation, independent wrong-word and wrong-event controls, exact binary/SDK/device identity. Measure native event enqueue/host wait/readback separately if timed; no optimization claim. One private event per external fixture, no production cost.
- RISKS: event nonarrival is not a diagnosed GPU error; a benign sentinel callback may arrive with nil error without mapping-error coverage. Resource/page-size API rejection ends test as actual unsupported/incompatible result, not fabricated success. Cross-device differences remain unverified.
- ROLLBACK / DoD: external probe removable without touching runtime; all primary positive/negative oracles and drain/identity evidence retained. Results use LOCAL_PASS/FAIL for exact observations; no implementation gate is passed solely by the design. Stop after this one validation slice.

### 6. Test Matrix / Validation Results

All proposed GPU rows below are **NOT_RUN in this audit round**. Previous E5 tests remain historical evidence and are not counted again.

| Case | Required observation | Interpretation |
| --- | --- | --- |
| Actual device/page-size | GPU/family/Metal4/PlacementSparse and returned compatible64-KiB page size, real creation success | Actual prerequisite, no family-based fallback fiction. |
| Buffer map → classic readback | Distinct full-page patterns after exact completion event | Ordered map + data path only. |
| Cross-resource copy and remap | Destination observes source heap identity; remap changes selected page only | Mapping-copy/remap semantics, not atomicity on driver error. |
| Unmap → valid remap | Private event completes unmap boundary; valid remap restores fresh expected data | No reads from unmapped pages. |
| Closed gate / unrelated cancel | Private event and downstream readback remain pending despite unrelated cancellation marker | Detect false completion from CPU cancellation. |
| Open gate / final drain | Actual private event completes; full readback matches; fixture lives through drain | Completion/ownership under benign execution. |
| Wrong last word / wrong event | Oracle rejects altered expectation and unrelated advanced event | Detect permissive/fictitious PASS. |
| Optional sentinel feedback | Log callback NSError solely as that commit's result | Never infer previous mapping error coverage. |
| Native mapping failure / no callback | NOT_RUN; no invalid input, deliberate OOM or hang | No claim of automatic error recovery or safe missing-event reclamation. |
| Texture/packed-tail, D3D11, Metal3, Windows/other GPU | Later existing regression slices, NOT_RUN here | No broad compatibility promotion. |

Executed this round: current Git/SDK/environment checks, CodeGraph-first source audit, fresh Apple/Microsoft retrieval and pinned Mullion hash verification **PASS**. Documentation scope/diff/hash audit and committed state retained in E6/delivery-manifest.json. Full build, Wine execution, GPU readback and performance **NOT_RUN** because runtime sources/tests are unchanged; repeating previous35 processes would not resolve this missing API contract. Retrieval404 and an initial evidence-helper quoting SyntaxError are retained/identified as research tooling failures, not GPU failures; corrected helper completed, baseline JSON parses.

### 7. Capability Impact

FL, SM, Tiled Tier and all options unchanged. Metal3 shadow backing, D3D11, Airconv/MSC and existing reserved tests untouched. Actual M1 Max availability is established by prior execution; no BLOCKED_BY_HARDWARE classification is permitted based on Apple7. This documentation patch has zero runtime/per-frame allocation, lock or synchronization cost; no runtime performance measurement or improvement claimed.

### 8. Remaining blockers / reclassification

| Item | Current classification | Resolution required |
| --- | --- | --- |
| Mapping-specific native asynchronous error observability/attribution | **SPEC_UNVERIFIED** first; cannot label a missing documented mapping callback simply BACKEND_NOT_IMPLEMENTED as if such API exists | Authoritative Apple contract or safely reproducible native evidence. Sentinel success is insufficient. |
| Independent mapping terminal-completion credential in existing DXMT owner | **BACKEND_NOT_IMPLEMENTED**, while ordered-event primitive exists | First validate distinct GPU-only event semantics; later design exact bounded lifetime/bridge integration. Current cancellation event alone is insufficient evidence. |
| Missing-event/driver-error teardown and safe release | **SPEC_UNVERIFIED** | Terminal-work guarantee or documented cancellation/teardown contract; timeout is not that guarantee. |
| Production mapping inputs retained through actual completion | **UNVERIFIED ownership boundary**, not a proven UAF | Separate legal application-lifetime/native-retention test; compare M6 and Mullion captures. |
| MSC sparse shader/descriptor/format conformance | No new **MSC_SEMANTIC_GAP** proved here; previous independent blockers retained | Its own end-to-end compiler/binding/readback tests. |
| Native Windows/other GPU differential | **TARGET_DEVICE_NOT_AVAILABLE / NOT_RUN** in this workspace | Obtain actual target. M1 Max itself is available. |

### 9. Changed Files / Git State / Rollback

Only docs/sparse-handoff-retirement-audit.md appended. No source/test/ABI/config modification and no new src/d3d12 files. Preserve original .gitignore bytes. Commit in current docs(d3d12) style, no push, no remote refresh claim. Exact final SHA, diff scope, staged/unstaged/untracked state and local tracking count recorded after commit in E6/delivery-manifest.json. Revert only this focused documentation commit if the decision changes; retain historical evidence.

### 10. Next Recommended Slice

Execute the Design Card's **external native buffer map/copy/unmap completion-boundary probe**, with distinct GPU-only completion and benign gate/cancellation controls on M1 Max. Do not yet integrate a mapping-error bridge, change production FIFO/backend or raise capabilities. This is one N13 validation slice; shader/texture expansion and runtime integration remain subsequent decisions.

## N13 native buffer mapping boundary — Execution card, 2026-11-05

- GOAL / BASELINE: execute prior section5 Design Card at 3d917452; one external native buffer validation slice, original .gitignore preserved.
- CURRENT PATH / CONTRACT: native producer → sparse queue gate/barrier/map or copy/unmap → private GPU completion event → classic readback. Prior E6 official source contracts remain pinned.
- SCOPE: external Objective-C++ fixture/commands/logs under dxmt-test, real64-KiB page query, map/copy/remap/unmap, closed-gate/unrelated-cancel and wrong-oracle controls.
- OWNERSHIP: retain both heaps, source/destination/placed initialization buffers, upload/readback buffers, residency set, queues/events and barrier allocators until exact native and classic completion; no production lifetime assertion.
- METHOD: distinct GPU-only completion event never CPU-signaled; CPU opens only the separate gate. Unmap has its own completion checkpoint and is followed by valid remap before reading.
- NON-SCOPE: production/bridge/test source edits, mapping NSError coverage, unsafe GPU faults, capability promotion, backend replacement or repeated full Wine regression batches.
- VALIDATION / COST: Metal API+shader validation environments at process launch, full nonuniform word/guard checks, changing seeds, independent negative controls, actual binary/SDK/image identities. Timing diagnostic only, no optimization claim.
- ERROR / ROLLBACK: timeout means unknown and no safe in-process resource teardown; terminate isolated probe preserving logs. Revert focused report commit; external artifacts do not alter runtime.
- DoD: compile probe, real positive/negative GPU evidence, cleanup after confirmed completion, source-scope audit, documented remaining blockers, repository-style commit/no push; overall goal active.

## N13 native buffer mapping boundary — Delivery, 2026-11-05

### 1. Task Summary / Gate outcome

Gate0/1 baseline and prior Design Card verified; Gate2 external native probe implemented; Gate3 real GPU matrix and Gate4 identity/scope audit PASS for this slice. Five distinct map/copy/remap/unmap checkpoints use a private GPU-only completion event. Broad goal remains active; production mapping completion/error integration is not complete. No runtime, bridge, repository test or capability code modified.

### 2. Baseline Evidence

Baseline feat/d3d12 at3d9174526d05299ed43204d2c393516f44fb8970; local origin reference0 behind/1 ahead at start, not refreshed by this agent. Original unstaged .gitignore SHA256764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 preserved. Rechecked actual macOS27.2/26B123, Xcode27.2/27B52, Apple clang21.0.0/clang-2100.4.1.1 and Metal32023.922/metalfe32023.922.4. Native executable uses system Metal/Foundation, neither Wine nor MSC loaded; no claim about a new DXMT/Wine build.

E7 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-mapping-native-boundary-20261105. Retains mapping_boundary.mm, release/ASan binaries, exact compile commands in compile.json/builds.json, run_matrix.py, each log/process.json, matrix.json, audit.json, environment.txt, SDK headers and prior E6 official source/reference bytes. Pinned Mullion93de32b9145885ceb2e45be7d445e56d62499b3c queue593–625/1338–1353/1392–1404 and bridge4129–4164 remain source references, not a new Mullion runtime observation.

### 3. Findings / real evidence

| Finding | Evidence / exact boundary |
| --- | --- |
| Actual M1 Max is Apple7=true, Apple8/9=false, Metal4=true, PlacementSparse=true. Returned64-KiB page bytes65536. Two real131072-byte compatible placement heaps and two131072-byte sparse buffers created; reported buffer sparse tier1. | E7 mapping_boundary.mm:71–148; every final DEVICE/RESOURCE record. NATIVE_OBSERVED / LOCAL_PASS for these resources, not D3D12 Tier1 promotion. Device/pipeline classes are MTLDebugDevice/MTLDebugComputePipelineState. |
| Closed GPU gate prevents native mapping completion and dependent classic execution despite a separate event advanced to UINT64_MAX. | Probe:220–281,303–334; each PENDING record has private_value0/read_done0/canary_bad0, unrelated cancel_valueUINT64_MAX.40ms observation per checkpoint. LOCAL_PASS of the exact benign ordering oracle, not device-removal recovery. |
| Opening gate yields native completion value1 and classic statusCompleted4, with no natural NSError from preceding barrier. | Probe:334–348 and COMPLETE records. Callback is labelled preceding-barrier coverage only; it is not assigned mapping-error coverage. |
| Map source to heapA; copy mappings to destination; remap destination page1 to heapB; source data stays heapA; unmap destination; map both destination pages to heapB. | Probe:248–280,351–384,386–391. Seven full32768-DWORD readbacks per process, distinct per-heap/per-page/per-word/seed data, guard words checked. All real data matches except one deliberately changed expected word in negative control. |
| Unmap is independently followed by private event/classic empty checkpoint before valid remap/read. No shader reads an unmapped buffer. | Probe:270–275,287–302,386–391. Completion/order verified. No unmapped-load, shader-status, physical-page-release or post-error unmap claim. |
| Resources/residency and barrier allocator remain owned through exact completion. | Probe fields60–69, initial residency145–158, allocator reset384, residency detach/end392–394. ASan positive run passed. This conservative native fixture does not prove production COM lifetime or last-reference reclamation. |

Producer initializes distinct data through ordinary placed buffers at heap offset0, completes classic producer work and orders sparse queue behind its event (probe:203–217). A ResourceAlias barrier into ResourceState matches existing DXMT native handoff (production native:4223–4227). Classic compute uses both heaps and a buffer barrier between disjoint output dispatches; no concurrent alias writes. Each source/destination read covers both pages. Private completion events are never CPU-signaled; CPU modifies only gate and unrelated cancellation marker.

Specification: E7/MTL4CommandQueue.h:261–276 gives prior-GPU-work event ordering; mapping methods:395–431 and operation structs:170–207 define buffer page mapping/copy. MTL4CommitFeedback.h:19–40 scopes errors to committed command buffers. Retained E6 apple-op-0/2/3/5.json and operation-docs.json provide exact official URLs/hashes. Fresh Apple residency documentation retained in apple-residency.json/new-sources.json. Attempted buffer-create documentation URL404 is recorded and supports no claim; installed MTLDevice.h:1493–1508 supplies exact API. No driver guarantee extrapolated from benign execution.

### 4. Design Decisions

Use an external Objective-C++ executable with ARC-owned fixture fields, explicit resident heaps/resources and retained barrier allocator. Distinct private completion, application gate, classic read-done and unrelated cancellation events prevent shared-value conflation. The gate tests observability before work can run. Each negative control still opens gate and drains actual GPU work before exiting1. Timeout uses process termination rather than unwinding uncertain in-flight resource ownership; the external runner has45s watchdog, actual per-event waits10s.

Three changed seeds prevent stale all-zero/readback acceptance. Wrong-word toggles exactly the last source word's expectation and must produce exactly one mismatch; wrong-event substitutes the already advanced unrelated cancellation event into the pending oracle and must reject exactly one checkpoint. Both controls otherwise run the full real mapping sequence with valid inputs. These are oracle-negative controls, not synthetic Metal capabilities, driver errors or invalid GPU work.

Mullion shares event ordering/resident input ownership concepts; its CPU mapping wait/held-work scheduler is not copied. Existing DXMT production FIFO, Metal3 fallback, D3D11, MSC/Airconv and mapping tables remain untouched. No source borrowed from reference implementation, no new licensing obligation introduced.

### 5. Changed Files / artifacts

Only repository change: docs/sparse-handoff-retirement-audit.md execution card and delivery. All probe source, temporary runner, logs and generated binaries reside in E7 outside repository; no new src/d3d12 file or repository Python/gate runner. Release source429 lines, exact final source SHA256768b57fd3904c3df6abd25cab86e49527fe9159295a6895629d1bd66c6f3cc23. Release binary SHA256eeb023d5e44d0764a36c376708122cd662082d175c2f4a6e20d60c1f2e1c7099; ASan binarya37a1597714efa257e26141d968d522da0a5108ba4185cc6d8b6fce19c5e8147. Loaded path and LC_UUID match each actual executable, system Metal/AGX/IOGPU image UUIDs retained; no WineMetal PE/Unix pairing is involved in this native-only slice.

### 6. Validation Results

| Check | Result |
| --- | --- |
| Release and ASan native builds | PASS; Xcode clang, C++20, ARC/blocks, Foundation/Metal, deployment target27.2. Exact commands/logs retained. No compile warnings. |
| Three release processes with different seeds | LOCAL_PASS;15 mapping checkpoints,21 full readbacks and688128 DWORD comparisons, no mismatches/guard corruption. |
| ASan positive process | LOCAL_PASS;5 checkpoints,7 readbacks and229376 DWORD comparisons. ASAN_OPTIONS detect_leaks=0:halt_on_error=1; no claim of leak testing. |
| Wrong-word control | Expected exit1; one deliberate data-oracle mismatch, remaining readbacks/checkpoints and final drain complete. |
| Wrong-event control | Expected exit1; one deliberately rejected completion-oracle substitution; actual private completion, data and drain complete. |
| All final processes |6 total =4 positive +2 expected negative;30 mapping checkpoints,42 readbacks,1376256 compared DWORDs including negative-process checks. All six process launches have API+GPU validation enabled and stderr reporting; logs contain only enabled notices plus JSON evidence, no validation diagnostics. |
| Identity / source scope / formatting | PASS E7/audit.json: binary SHA/path/UUID, exact final source, no src/tests/include/build/config diff, original .gitignore preserved, no staged/untracked residue before delivery. |
| Complete DXMT build / Wine regression / D3D11 / Metal3 runtime | NOT_RUN: no production/test changes, native probe validates only the stated boundary. Prior E5 results remain historical, not rerun or recounted. |
| Native mapping failure / driver hang / missing event / Windows/other GPU | NOT_RUN; no unsafe fault induced. This run does not demonstrate mapping NSError attribution or cancellation teardown. |

Initial unformatted native run also passed5 checkpoints/7 readbacks; supplemental only, excluded from six-process final totals. Before final acceptance, an explicit classic compute memory barrier was added between output dispatches, source formatted and rebuilt. Final release/ASan matrix uses the recorded final bytes. No failed GPU run was hidden. Actual command: python3 E7/run_matrix.py (expand E7 to absolute path); use a fresh evidence destination/labels when repeating to preserve historical outputs.

### 7. Capability / performance impact

Public FL/SM/Options/Tiers unchanged. Real native sparse tier1 is a Metal buffer property, not evidence for any complete D3D12 tiled tier. No Apple7 hardware-block assertion. Native probe is not an MSC compiler/binding test. Zero production runtime cost because only documentation changed; no CPU/GPU optimization or no-regression claim. Deliberate5x40ms pending windows and compilation/process startup make elapsed process time unsuitable as a mapping performance benchmark.

### 8. Risks / remaining blockers

- SPEC_UNVERIFIED: asynchronous errors from the separate void mapping operations; no official evidence that preceding barrier or later sentinel commit reports them. All benign callbacks succeeded; that does not close the contract gap.
- BACKEND_NOT_IMPLEMENTED: DXMT still lacks an independent mapping-terminal credential bound to existing Submission ownership. This probe establishes usable benign ordering with a private event; production integration remains separate.
- SPEC_UNVERIFIED: no-event/driver-error terminal teardown and safe last-reference release. Device-removal cancellation event remains unsuitable as sole completion proof. A timeout never authorizes resource retirement.
- UNVERIFIED: production mapping input lifetime/residency through failure and partial self-copy, texture/packed-tail variants, and broader shader-status/format conformance. This conservative fixture does not expose an actual production UAF or prove its absence.
- TARGET_DEVICE_NOT_AVAILABLE: Windows/other-GPU differential; actual M1 Max target remains available. No new MSC_SEMANTIC_GAP established here.

### 9. Git State / Rollback

Focused docs(d3d12) commit, no push or shared Wine modification. Final SHA/status/tracking and artifact index retained post-commit in E7/delivery-manifest.json. Original .gitignore bytes preserved. Reverting this one report commit rolls back repository changes; external native evidence remains reproducible and does not change runtime. No goal completion claim.

### 10. Next Recommended Slice

One N13 Design Card for an **independent native mapping-completion credential through the existing WineMetal owner**, then bounded integration into existing Submission after its ownership/failure protocol is settled. Use this probe's private GPU-only terminal event, keep it separate from cancellation and committed-barrier NSError, and retain named mapping inputs through actual terminal evidence. First specify prepare-before-issue, partial-issue failure, no-event/removal and release contracts; do not add a scheduler, second logical mapping table or publish an undocumented mapping-error success result. Runtime implementation requires that concrete design; this round stops after native validation/report commit.


## N13 independent mapping completion credential — Design Card, 2026-11-05

### 1. Task Summary / Goal and gate

One audit/design slice at7dfb8fa4: define ownership and completion evidence for placement-sparse mapping batches through existing WineMetal and Submission owners. Production integration remains gated on the no-event/teardown contract. This section is a proposed design, not implemented runtime behavior. No source/test edits or new GPU runs in this round.

Gate0 evidence/source audit PASS; Gate1 Design Card complete for review. Gate2 production implementation NOT_STARTED, Gate3 new runtime validation NOT_RUN, Gate4 documentation scope/identity/diff audit PASS. The next controlled prototype is explicitly narrower than production integration.

### 2. Baseline Evidence

Branch feat/d3d12, full baseline7dfb8fa40984d06724e0e9d6c555d7281f7da087. Only pre-existing .gitignore dirty; local origin reference0 behind/2 ahead, not refreshed. E8 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-mapping-credential-design-20261105 retains baseline, source excerpts, contracts and review. E7 native results above are historical evidence, not new executions.

### 3. Findings / source evidence

| Evidence type | Confirmed finding and current anchors |
| --- | --- |
| SOURCE_AUDIT | Submission owns exact barrier feedback but no mapping resource/heap payload: src/d3d12/d3d12_command_queue.cpp:86–110. Commit publishes that record before Update/Copy issues mappings:560–589,626–681,786–847. Appending ownership to a stack record after Begin would race CompletionThread:460–543. |
| SOURCE_AUDIT | Removal CPU-signals the shared sparse event to UINT64_MAX:command_queue.cpp:125–141. It therefore cannot also be the mapping retirement credential. Stopping/joining the completion thread:615–623,688–693 does not manufacture GPU completion. |
| SOURCE_AUDIT | Heap destructor unregisters both backing buffer and native heap:src/d3d12/d3d12_heap.cpp:60–65; buffer/texture destructors unregister resource residency:buffer.cpp:239–242,texture.cpp:1054–1057. Native retain alone prevents object destruction but does not prevent these COM owners removing residency. Device unregister immediately removes allocation and commits the global set:device.cpp:2115–2120,2135–2148. |
| SOURCE_AUDIT | Private Com references preserve the actual C++ owner:src/util/com/com_pointer.hpp:48–50 and com_object.hpp:49–69. Every device child already holds a device reference:src/d3d12/d3d12_device_child.hpp:64–70. Submission must own private resource/heap references; native credentials must never retain/call back into the PE queue or device. |
| SOURCE_AUDIT | Buffer update builds inputs before issue, then replaces old mapping refs after synchronous acceptance:buffer.cpp:565–667. Copy snapshots source, but must additionally retain overwritten destination heaps; self-copy issues one native call per tile and may fail after a prefix:671–753. Texture equivalents1944–2090/2094–2233 include packed-tail operations. Existing vectors are temporary, not completion ownership. |
| SOURCE_AUDIT | Unix buffer thunk copies/frees operation arrays around a void native call; test failure paths explicitly drain a private event:src/winemetal/unix/winemetal_unix.c:4405–4470. Those test-only drains are not a production guarantee. Existing barrier credential owns a committed barrier only:4179–4336. |
| SDK_CONTRACT | Retained E8/MTL4CommandQueue.h:261–276 specifies queue signal after preceding GPU work;395–431 defines separate void map/copy operations. E8/MTL4CommitFeedback.h:19–40 describes committed-command-buffer error. Neither gives an NSError terminal channel for the separate mapping operations. Fresh official signal/feedback JSON hashes equal E6. |
| SPEC_REQUIRED | Microsoft UpdateTileMappings is void, queue-ordered mapping work; CopyTileMappings overlap must behave as temporary snapshot. Retained E8/ms-update.txt and ms-copy.txt, plus fresh official Update HTML; no safe invalid-GPU-input experiment follows from this contract. |
| REFERENCE_SOURCE | Fresh pinned Mullion93de32b9145885ceb2e45be7d445e56d62499b3c queue bytes match E6 SHA256. Queue593–625 commits residency, issues mappings and CPU-waits a dedicated mapped event;1338–1353/1392–1404 retain resource/heap inputs in update closure. Bridge4129–4164 uses the same void native methods. This is source observation, not runtime evidence or a fault-terminal guarantee. |
| HISTORICAL_LOCAL_PASS | E7 final audit/source hash rechecked this round. Its6 real processes,30 checkpoints,42 readbacks prove the stated benign native boundary on M1 Max; no new GPU execution or failure/release guarantee is inferred. |

### 4. Design Decisions / proposed implementation contract

**Boundary and single authority.** Resource tile_mappings_ remains the only logical mapping truth. Existing resource helpers prepare immutable operation inputs and ownership snapshots; existing queue owns admission and Submission FIFO; WineMetal owns one native completion credential. No second logical mapping table, scheduler, global unsettled registry or queue retained by its payload. Snapshot records are transient inputs/retains, never queried as a competing resource-state database. Other queues must obey existing D3D12 synchronization; snapshots do not legalize concurrent unsynchronized remaps.

**Prepare before any queue issue.** Under existing commit owner, wait for bounded admission first, then fully validate and prepare all resource operations, affected source/destination private Com references, overwritten destination heap refs, source snapshot heap refs, all new heap refs, native operation storage and the private event. For duplicate destination tiles retain the original backing plus all intermediate/new heaps, not just final values. All-NULL/unmap still needs resource and old-heap ownership. Packed-tail native coverage determines affected old refs. Preserve current input validation and Metal3 behavior; this design does not silently change invalid-argument policy. Any preparation failure has zero mapping side effects and no logical publication.

Resource-local preparation must finish before BeginSparseMapping's handoff publication. An optional internal payload parameter alone is insufficient if old heaps are collected after issue. A future scoped change may split the existing helpers into prepare / issue / publish phases within their current .cpp owners and adjust internal declarations in device.hpp:327–343; no public D3D12 COM ABI change or new src/d3d12 file. The prepare snapshot needs the existing tile mutex; release it before queue/native waits. Do not hold tile/residency/submission locks across completion.

**Payload and bounded admission.** Proposed Submission owns one shared payload containing private Com<MTLD3D12Resource,false> source/destination refs, private Com<MTLD3D12Heap,false> refs and the native credential. Native credential owns the sparse queue and GPU-only event, copied native inputs/native refs as necessary; no PE callback. Complete all retain/vector allocations before issue. Prepare only after FIFO space is available; existing commit serialization permits at most32 admitted records including the one moved to the completion worker, plus one preparation under commit ownership. This bounds record count, not bytes: input size remains O(affected mapping operations), with checked counts/overflow and pre-issue allocation failure. No whole-heap scan or hash deduplication required; duplicate heap refs are acceptable initially.

**Two evidence dimensions, not a single success enum.** Native lifecycle is Prepared → Issuing → SealedPending → GPUComplete; zero-issued Prepared can become NotIssued. Issued prefix remains owned until GPUComplete, regardless of separate host outcome (Accepted / RejectedBeforeIssue / FailedOrUnknownAfterIssue). Prepared/Issuing waits cannot return successful retirement. A queue record may be published with the same payload already attached before issue; completion waits for sealing and then the private event without holding queue locks. PE must never mutate a moved Submission or locate it again by ring slot. Host errors, device cancellation, barrier feedback, timeout and native GPU completion stay distinct. No mapping NSError or generic MappingSucceeded result is invented.

The native issuer records an operation as possibly issued before invoking its void Metal method; any ambiguous transport failure is conservatively treated as possibly issued. Event completion reports prior queue work finished, not correctness of every mapping or successful D3D12 submission. Successful FIFO publication additionally requires existing classic handoff and exact barrier feedback to settle and no latched removal/host failure; completion must not publish a later application fence as success after an earlier terminal failure.

**Ordered execution and partial failure.** Proposed timeline:

1. Prepare complete payload/native storage/event; bind payload to the existing handoff Submission before its first classic commit/publication.
2. Queue existing main→sparse wait and preceding ResourceAlias→ResourceState barrier through the current owner; preserve barrier's own feedback credential.
3. Issue prepared mapping calls. Native issuer owns an allocation-free close path; no per-tile array copy/allocation after the first call. Non-self copy remains one native copy where valid; self-overlap uses the existing immutable source snapshot.
4. On success or partial/ambiguous issue, seal exactly once by enqueuing the credential's private event after all possibly issued calls, then existing sparse→main progression signal. No CPU writer can access that private event. For genuine zero-issue rejection, use NotIssued; no GPU retirement assertion needed.
5. Publish logical mappings only after synchronous acceptance of the whole batch, retaining old/new refs independently until GPU completion. Prefix failure leaves CPU mappings unpublished, marks terminal failure under existing policy, never rolls back already issued GPU work. A successfully sealed prefix still must drain even if removal follows.
6. Completion worker settles main/barrier, payload seal and private GPU event, then releases snapshot/native payload outside queue/resource/residency locks. Healthy completion permits normal FIFO progress. A transport failure while sealing is Unknown, not NotIssued.

Close must run on every post-issue exit (including exceptions), independently of whether device removal has already been latched. The existing cancellation event can unblock dependency waits; it never seals/completes the private credential. If a later classic buffer fails or a preceding barrier reports error, keep the mapping payload until its own actual completion proof. Native preparation must make seal allocation-free; a void signal/transport still cannot prove that a lost event will arrive.

**No-event / teardown gate.** If the private event never signals, there is currently no verified alternate proof that mapping GPU access has stopped. Do not release resources, unregister residency, advance successful FIFO retirement or convert a timeout/removal to completion. An indefinite bounded FIFO stall also stalls queue destruction/join; it is a known availability/deadlock risk, not an acceptable finished production recovery policy. Unbounded quarantine, native self-retain cycles and process-wide leaks are rejected. Abort/_Exit from the isolated probe must not be copied into DXMT. Therefore automatic production integration remains **SPEC_UNVERIFIED** until a documented alternate terminal boundary or a separately approved bounded device/queue teardown ownership protocol is established and tested. A successful later sentinel callback alone is not assumed to cover mapping errors. This gate does not block a controlled native credential prototype whose harness always opens/drains valid GPU work or terminates its isolated process externally.

**Bridge / ABI proposal.** Keep SparseBarrierCompletion and its56-byte result unchanged, preserve existing ordinal0–202 (current prepare/submit/wait200–202:src/winemetal/winemetal_thunks.h:55–58). Future mapping credential operations are additive at203+ after rechecking both dispatch tables; fixed-width result must separately encode lifecycle, possibly-issued count, seal state and event observation, with explicit size/alignment assertions on PE/native and32/64-bit builds. No NSError field without actual error provenance. Prepare/issue/seal/wait transport failures must not be conflated with result state. Prefer normal Object release only for NotIssued or GPUComplete; invalid premature release must be prevented by owner protocol, not hidden self-retention. No new production export or exact struct layout is committed by this Design Card. New PE with old Unix library is not supported merely because old ordinals remain stable; matched PE/Unix artifacts and existing compatibility policy are mandatory. No MSC API/SDK/runtime ABI change.

**Alternatives / cost.** Keep current shared sparse_event as ordering/cancellation channel, reject using it as retirement proof. Reject preceding-barrier NSError as mapping status, synchronous per-operation CPU drain, whole-Mullion scheduler, native-only heap retention and second mapping registry. Proposed cost per logical batch: one event, one shared payload, affected input/ref storage and event poll/wait via existing completion thread; no new worker, draw hot-path change or extra mapping barrier intended. Queue completion can stall behind a long mapping batch; ring backpressure is intentional. Native object sizes, allocation count, wall time, CPU submit and GPU timeline remain unmeasured. Do not claim optimization/no regression.

### 5. Changed Files / future scope

This round changes only docs/sparse-handoff-retirement-audit.md. E8 contains external evidence, no repository runner/source/test changes. Future bridge prototype belongs to existing winemetal.h/thunks.h/thunks.c/Metal.hpp/unix/winemetal_unix.c; later separate resource/Submission integration belongs to existing d3d12_buffer.cpp/texture.cpp/device.hpp/command_queue.cpp. Neither later slice is implemented by this report. No reference code copied, no license change.

### 6. Validation Results / proposed test matrix

Current checks: baseline/source anchors and private-ref/residency owners audited;14 source snapshots hashed; fresh official signal/feedback/Microsoft Update contracts fetched; pinned Mullion queue hash matched; E7 source/final audit rechecked. LOCAL_PASS of these read-only/document checks only. Full build, new native GPU run, Wine, D3D11/Metal3, MSC and32-bit ABI tests are **NOT_RUN** this round. Historical E7 remains limited to its observed boundary.

| Future case | Oracle / expected owner behavior | Gate |
| --- | --- | --- |
| Prepare failure, invalid input, zero issue | No native mapping invocation or logical publication; NotIssued permits release. No removal fabricated for mere invalid input. | Native prototype; integration policy separately audited |
| Closed gate, unrelated cancellation MAX | Private event0, data canary, ownership alive despite cancelled shared progress; opening gate yields exact readback and completion. | Reuse E7 real GPU setup |
| Map/copy/remap/unmap with changing data | Full per-word readback, source unchanged, guards intact; never read unmapped pages. | Native prototype |
| Partial self-copy issue plus host failure | Fail after a known valid prefix; always seal/drain; retain source snapshots and overwritten/new heaps; no logical publish or success fence. Separate host failure from completion. | Native then resource integration |
| Allocation failure at every preparation point | No first GPU issue until all arrays/event/refs available; verify no allocation required by seal. | Native/host injection, no unsafe GPU fault |
| Early public resource/heap release | Private COM counts keep destructors/residency membership alive until exact completion, then release once. Native-only retain is a negative control. | Requires PE integration; NOT proved by E7 |
| Adjacent batches, repeated calls, replay-like reuse attempt | Each batch distinct immutable event/value; previous event cannot complete another; seal once; credential reuse rejected. Other queues require legal fence synchronization. | Native then FIFO integration |
| All-null, old/new multiheap, overlapping self-copy, packed tail | Preserve all overwritten backings; exact operation semantics match existing reserved tests; no rollback after prefix. | Integration regression |
| Main/barrier failure while mapping gate held | No success fence/allocator retirement; payload remains until private completion; gate reopened by controlled harness and drain confirmed. | FIFO integration |
| Teardown, full FIFO, absent private event | No false release/completion; detect stalled join with external watchdog. Timeout is UNVERIFIED completion, not PASS of recovery. | Production promotion blocked pending terminal protocol |
| Natural driver/mapping fault; Windows/other GPU | No deliberate invalid GPU memory access; actual results required. | NOT_RUN; no fake fault/capability |

Prototype acceptance must include real API+shader validation, a wrong-event negative oracle, seeded full readbacks, source/binary/loaded-image identities and actual completion before controlled cleanup. Resource integration additionally runs existing Reserved Resource cases, partial-failure tests without their protective drain masking production logic, D3D11/Metal3 adjacent regression and matched WineMetal PE/Unix identities. Independent builds32/64 assert layout; inability to run32-bit runtime is NOT_RUN. No change to Tier/FL acceptance.

### 7. Capability Impact

No public FL/SM/Tier, Metal3 path, D3D11 behavior, MSC compatibility or current Fence policy changes. M1 Max Metal4/PlacementSparse is available; no Apple7 hardware block. Snapshot lifetime is BACKEND_NOT_IMPLEMENTED, missing mapping error/teardown guarantee SPEC_UNVERIFIED. No new MSC_SEMANTIC_GAP found. Windows/other-device differential remains TARGET_DEVICE_NOT_AVAILABLE for those targets only.

### 8. Regression Risks / Definition of Done

DoD for this design: concrete single owners, prepare-before-publication timeline, partial/zero/ambiguous issue distinction, private completion separate from cancellation/feedback, residency retention, bounded record admission, proposed ABI/test/cost/rollback and honest no-event gate. All specified here; runtime correctness is not established. Major remaining risk is loss of private event after actual issue: neither timeout nor cancellation supplies a safe in-process release boundary. The proposed native prototype must expose this limitation rather than turn it into a success state. Production resource helper split must also review lock order and cross-queue legal synchronization before implementation.

### 9. Git State / Rollback

One focused docs(d3d12) commit, no push, no fetch or shared Wine modifications; .gitignore original bytes preserved. E8 post-commit delivery-manifest.json records final SHA/status/tracking, exact evidence hashes and doc-only diff. Revert this focused report commit to remove the design; existing runtime and E7 evidence are unchanged. No overall-goal completion claim.

### 10. Next Recommended Slice

Implement and execute one **controlled native mapping-completion credential prototype**, using existing WineMetal native owner conventions and E7's real sparse buffers. Prove allocation-before-issue, exact event separation, partial-issued-prefix sealing, single close and retained-input release under healthy completion with valid GPU work. Keep production queue/resource callers disconnected; keep ABI design additive and paired. Gate its GPU evidence and ownership audit before separate Submission integration. No-event production recovery stays explicitly unresolved; do not silently broaden this next slice to a scheduler or device teardown redesign.


## N13 native mapping credential prototype — Execution card, 2026-11-05

- GOAL / BASELINE: at9cd2713b execute prior Design Card's controlled native prototype. Gate0 source/owner baseline verified; Gate1 scope fixed below.
- SCOPE: external MRC NSObject credential plus ARC real sparse-buffer harness, prepared native storage/retains/private event, healthy and host-failed partial-prefix issue, single sealing, gate/cancel/timeout observations, explicit release gate. Existing WineMetal native-owner conventions, no production caller or bridge ABI edits.
- OWNERSHIP: credential retains queue, native input copies and named owner lease through actual GPU completion; fixture retains old/new heaps/residency and classic readbacks. Lease destruction is measured separately from final native/COM destruction; do not infer production residency correctness.
- PROOF: actual64-KiB mapping/copy/remap/unmap/full-word readback, pending private event under closed gate/unrelated cancellation, prepared/issuing/zero/partial state checks, duplicate-close/reuse rejection, pre-issue preparation failures, changing seeds, wrong-event/wrong-word negative controls, ASan and Metal validation.
- NON-SCOPE: production FIFO/resource integration, Metal3/D3D11/MSC changes, mapping NSError guarantee, lost-event recovery, unsafe GPU fault, Tier/FL promotion, new scheduler or repository test runner.
- COST / ROLLBACK: one external credential/event per batch, operation storage/retains prepared before issue; source-controlled runtime cost zero. Revert focused report commit, retain external evidence. No performance claim.
- DoD: genuine GPU completion before controlled cleanup, explicit partial-host-failure vs GPU-complete distinction, negative controls reject bad oracles, identities/commands/logs retained, focused commit/no push; overall goal active. A no-event timeout remains unknown and cannot justify release.

## N13 native mapping credential prototype — Delivery, 2026-11-05

### 1. Task Summary / Gate outcome

Gate0 baseline/source contracts rechecked; Gate1 prior design narrowed to controlled native prototype; Gate2 external MRC NSObject credential and ARC GPU harness implemented; Gate3 final real GPU matrix PASS for the stated contract; Gate4 identity/scope audit PASS. No production WineMetal export, queue/resource caller, repository test or public capability changed. This is completed prototype evidence, not completed production mapping lifetime/error support. Overall goal remains active.

### 2. Baseline Evidence

Baseline feat/d3d12 at9cd2713be2bbddfec34a2c6a28d6754e00f96d31; local origin reference0 behind/3 ahead before this commit, not refreshed. Original .gitignore SHA256764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 preserved. Rechecked macOS27.2/26B123, Xcode27.2/27B52, clang21.0.0/clang-2100.4.1.1, Metal32023.922/metalfe-32023.922.4. Actual DEVICE records M1 Max, Apple7=true, Apple8/9=false, Metal4=true, PlacementSparse=true,64-KiB page size. Native buffer sparse tier1 is a Metal property, not a D3D12 Tier declaration. Neither Wine nor MSC loaded in this executable.

E9 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-mapping-credential-prototype-20261105. Retains credential.h/credential.mm/credential_probe.mm, both final executables/objects, build.py/builds.json/logs, run_matrix.py/matrix.json, each process record/log, audit.json, environment, installed SDK headers, fresh official signal/feedback JSON and pinned Mullion queue bytes. E7 baseline_probe.mm remains unchanged; preformat/ preserves the initial passing matrix/binaries before input-copy oracle/formatting. E9 final records alone support totals below.

### 3. Findings / exact evidence

| Evidence type | Finding / source anchor |
| --- | --- |
| SOURCE_AUDIT | Existing native credential conventions own NSObject queue/condition/buffer/allocator before submit:src/winemetal/unix/winemetal_unix.c:4179–4251. Existing mapping thunks4405–4470 allocate per call; they are not changed or assumed equivalent to the prototype. Production ownership gap and COM residency-destructor findings in prior Design Card remain. |
| SDK_CONTRACT | E9/MTL4CommandQueue.h:261–276 defines prior-GPU-work signal ordering,395–431 void buffer mapping/copy; MTL4CommitFeedback.h:19–40 is committed-buffer feedback. Fresh official JSON hashes match E8. A GPU event is completion evidence, not a mapping NSError channel. |
| REFERENCE_SOURCE | Fresh pinned Mullion93de32b9145885ceb2e45be7d445e56d62499b3c queue SHA256641a21ad8fce6aa6f3f6f0cb02e0b0e3ae162a97a0eb7740d738c5f70ece6492 matches retained reference. Queue593–625 owns inputs through CPU mapping wait; no source copied, no Mullion runtime/fault claim. Microsoft void Update/Copy and overlap semantics remain those cited in prior Design Card. |
| LOCAL_PASS / native | Preparation owns copied operations, source/destination/heap native refs and a separate owner lease before any mapping call:E9/credential.mm:30–77. Caller then overwrites its operation storage with a different valid mapping, yet actual GPU output follows the private original copy:credential_probe.mm:243–282. This proves copied operation data for this fixture. |
| LOCAL_PASS / native | Prepared/Issuing cannot retire; SealedPending times out under a closed gate without becoming complete; unrelated cancellation at MAX does not change it:credential.mm:79–168; probe:278–362. Private credential event is never exported to harness or CPU-signaled. Separate GPU ordering/read-done events are used for classic readback. |
| LOCAL_PASS / host injection | Partial mode prepares two different operations, issues exactly the first, records FailedAfterIssue, seals once, rejects second seal/new issue, drains real GPU work and reads only the prefix result:probe:254–302,419–431. Partial host outcome stays FailedAfterIssue after GPUComplete; no synthetic Metal error or false batch acceptance. |
| LOCAL_PASS / lease boundary | External owner variable is dropped before issue; weak owner stays alive through Prepared/Issuing/Pending. After exact mapping and classic completion, retireInputs releases once, weak owner is nil and destruction count1:probe:263–282,335–343,410–431. Native fixture still owns heaps/resources/residency; this is owner-lease release, not proof of final COM destruction, physical page reclamation or production residency removal. |
| LOCAL_PASS / pre-issue | Four deterministic preparation checkpoints reject before mapping; planned-but-rejected and empty accepted cases become NotIssued, with0 possible calls and one lease release:probe:433–486. Test injection is host preparation failure, not natural allocator OOM or GPU failure. |

### 4. Design Decisions / what was actually implemented

Prototype is a separate MRC translation unit to follow existing WineMetal retain/release conventions; ARC harness owns real GPU fixture and uses weak lifetime observation. One private event/queue/copy of native operations/owner lease per credential. No PE pointer, callback, global registry or scheduler. State and host outcome are separate: Prepared→Issuing→SealedPending→GPUComplete; zero issue→NotIssued. possible_calls increments before native invocation. Native operation storage and event are prepared before first issue. seal issues its event once, with no explicit wrapper array/event/owner allocation or copy after issue; driver/Foundation/lock internal allocations are not instrumented or ruled out. Native objects are retained and released using their actual +1 ownership; lease/input release occurs outside NSLock.

Partial mode's omitted second operation would remap to different heap data. Exact readbacks therefore distinguish issuing the valid prefix from accidentally running both operations. Every checkpoint still uses a preceding real ResourceAlias→ResourceState barrier; its callback is separately labelled preceding-barrier-only. A separate GPU event orders classic readers after credential seal; it cannot CPU-complete the credential. Source map, copy to destination, remap one page, unmap checkpoint and remap two pages retain E7's exact seeded word/guard oracle. Unmap is not read until valid remap. Data pattern changes by seed/heap/page/word.

No missing-event recovery is implemented: observe timeout leaves SealedPending, premature retire is rejected. Harness timeout uses isolated _Exit; credential premature deallocation has an isolated _Exit guard. These guards are not proposed production behavior and are not exercised as GPU-fault tests. Controlled runs always open gates/drain actual work before cleanup. Existing production signal/cancellation, native thunks, FIFO and mapping tables remain unchanged.

### 5. Changed Files / reproducibility

Only repository edit is this report's execution card/delivery. All three prototype source files, native build/run scripts and artifacts are external E9; no repository runner or src/d3d12 file added. Reproduce with python3 E9/build.py then python3 E9/run_matrix.py, expanding E9 to its absolute path and using a fresh evidence destination for new runs. Builds use xcrun clang++, C++20/blocks/O2/debug, deployment27.2; credential -fno-objc-arc, harness -fobjc-arc, Foundation/Metal. ASan adds -fsanitize=address -fno-omit-frame-pointer. Exact argv stored in builds.json. Matrix binds final source/header/binary SHA256 and actual loaded executable path/LC_UUID checked against dwarfdump. No runtime library copy/deployment or PE/Unix pairing is involved.

### 6. Validation Results

| Check | Result / exact scope |
| --- | --- |
| Final native release + ASan compile/link | PASS;6 compile/link commands exit0. Final sources formatted before these builds. |
| Validated normal-a/b, partial-a/b | LOCAL_PASS;4 positive release processes,20 checkpoints,28 readbacks,917504 compared DWORDs. Two processes use host-failed partial prefixes at every checkpoint. |
| ASan normal + partial | LOCAL_PASS;2 positive processes,10 checkpoints,14 readbacks,458752 DWORDs. detect_leaks=0:halt_on_error=1; no leak-test claim. |
| Wrong expected word / wrong observed event | Expected exit1 each; exactly one mismatch or pending-oracle rejection per process. Both still complete all5 checkpoints/7 readbacks and drain GPU work before exit. No fake capabilities/driver faults. |
| Final matrix total |8 processes =6 positive +2 expected negative;40 mapping checkpoints,56 full readbacks,1835008 DWORD comparisons including negative-process checks;32 preparation-failure checks and16 NotIssued cases.15 checkpoints use partial-host-failure mode across3 processes. Every completed credential has1 possible call/1 seal/1 owner-lease destruction; planned count2 in partial mode. |
| API / GPU validation and identities | PASS; all8 launches set MTL_DEBUG_LAYER=1, MTL_SHADER_VALIDATION=1, MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 before device creation. Timestamped API/GPU enabled notices present; all other lines JSON evidence, no validation/ASan diagnostic. MTLDebugDevice/pipeline classes, actual images/path/UUID and noMSC retained. |
| Source copy / state / cleanup | PASS final matrix for mutated caller input, pre-issue failure, timeout-pending, no early retirement, duplicate seal/issue rejection and one-time lease retirement under controlled valid work. Not a multithread stress or actual last-native-reference test. |
| Complete DXMT build / PE/Wine/32-bit / D3D11/Metal3/MSC | NOT_RUN; production source/test/ABI unchanged. Prior build/runtime evidence is not rerun or recounted. |
| Missing event / natural mapping error / driver hang / Windows/other GPU | NOT_RUN; safe benign ordering does not establish fault teardown or mapping error attribution. No invalid GPU input induced. |

Initial source generation duplicated the partial member; compiler rejected it before GPU execution, fixed before first run. First validated-a GPU run passed but collector rejected timestamp-prefixed validation-enabled notices; original log retained, exact suffix recognition fixed. An initial full8-process matrix then passed. That initial matrix is supplemental in preformat/, excluded from final totals. Final matrix reruns formatted source plus the new private-input-copy oracle; no GPU failure hidden and no reuse of older binaries as final evidence.

### 7. Capability / cost impact

No FL/SM/Tier, Metal3 shadow backing, D3D11, Airconv/MSC or Fence policy change; actual M1 Max is available and no Apple7 hardware block is asserted. Production runtime cost zero in this round. Proposed mechanism costs one event/credential and copied input/native retains, NSLock per method and existing-worker polling/wait; no wrapper post-issue data allocation, no new worker, scheduler or whole-heap scan. Allocation counts/latency/GPU overlap unmeasured; deliberate5x40ms windows make process times unsuitable as benchmarks. No optimization or no-regression claim.

### 8. Risks / remaining blockers

BACKEND_NOT_IMPLEMENTED: actual WineMetal transport/paired ABI, resource-private-COM payload and bounded Submission handoff are still absent. The native lease demonstrates protocol feasibility, not production owner integration. Existing per-call Unix allocations must not survive an eventual prepare-all/issue/seal integration without a tested prefix-failure close path.

SPEC_UNVERIFIED: void mapping asynchronous error attribution and no-event safe teardown. GPUComplete means the private event was observed after prior work; FailedAfterIssue remains host failure and mapping_error_coverage remains UNVERIFIED. Timeout/removal/cancellation does not authorize release or success. No native/PE self-retain leak/quarantine is proposed. TARGET_DEVICE_NOT_AVAILABLE applies to Windows/other GPUs only; M1 Max target works. No new MSC_SEMANTIC_GAP is demonstrated. Broader texture/packed-tail/self-overlap, cross-queue and last-COM-reference cases require future integration regression.

### 9. Git State / Rollback

Focused docs(d3d12) commit/no push; original .gitignore preserved and excluded from staging. E9/delivery-manifest.json records final SHA/status/tracking, file identities, scope audit and external artifacts. Revert focused report commit to roll back repository changes; external probe does not modify runtime/Wine prefix. Overall goal remains active.

### 10. Next Recommended Slice

Transport this controlled mapping credential through the **existing WineMetal boundary** with additive paired ABI and a controlled test caller, preserving production queue/resource callers disconnected. Validate PE↔Unix state/host-outcome separation, prepare-before-issue, partial-prefix close and real GPU completion/retains using the same oracle, paired runtime identities and applicable32/64-bit layout checks. This tests the actual boundary instead of repeating native-only evidence. Production Submission/resource integration remains a separate gate with COM residency ownership and unresolved no-event teardown explicitly addressed; do not broaden this next slice to device teardown or capability promotion.


## N13 mapping credential WineMetal transport — Execution card, 2026-11-05

- GOAL / BASELINE: at6152e245 implement controlled buffer credential through existing PE↔Unix boundary; one N13 slice. Original .gitignore preserved; production resource/queue callers disconnected.
- EVIDENCE: prior E9 native GPU protocol plus current WineMetal prepare/submit/wait owner and dispatch tables; native event completion distinct from cancellation/barrier error. Microsoft Update/Copy logical semantics remain resource-owner responsibility.
- SCOPE / MODULES: additive fixed-layout buffer operation/result and five ordinals203–207, existing thunk/header/WMT/native files, one mode in existing reserved-buffer probe. Native private inputs/event prepared before issue; host and lifecycle result separate. No texture or public capability expansion.
- DESIGN: copied prevalidated native input/retains; issue-one supports controlled valid prefix; seal once without wrapper operation allocation; wait observes private GPU-only event; retirement only NotIssued/GPUComplete. Normal final release drains any possibly issued work before native refs release; missing event may block destructor, explicitly not resolved production teardown.
- PROOF: x64 full build per script options,32-bit layout/PE build where available, isolated matched WineMetal DLL/Unix image, real seeded mapping/copy/remap/unmap GPU readbacks, partial-host-failure/timeout/cancel/mutated-input/invalid-transport controls, existing reserved/barrier/D3D11 nearby checks.
- NON-SCOPE / RISKS: production Submission/COM snapshot integration, no-event recovery, mapping NSError attribution, scheduler, second tile table, new src/d3d12 files, MSC or D3D11 semantic changes, Tier/FL promotion. Credential assumes caller supplies valid resource mapping/barrier synchronization; never equates event with mapping-success result.
- COST / ROLLBACK / DoD: one event plus prepared operation/native refs and a lock per credential; no new production hot-path caller or repeated heap scan. Fixed ABI paired deployment, preissue validation/failure cleanup, exact negative oracles and identities; focused commit/no push. Revert focused files; overall goal active.

## N13 mapping credential WineMetal transport — Delivery, 2026-11-05

### 1. Task Summary / Gate outcome

Gate0 current baseline and boundary/source contracts rechecked; Gate1 execution card fixed to one controlled buffer slice; Gate2 additive WineMetal transport, native owner and existing reserved-buffer probe implemented; Gate3 final x64/x86 builds and real paired-runtime GPU matrix PASS within the stated scope; Gate4 scope, dispatch-prefix, identities and negative controls checked. Production D3D12 queue/resource callers remain disconnected from the new credential. This completes the boundary-validation slice, not production sparse lifetime/error support. Overall goal remains active.

### 2. Baseline Evidence

Baseline feat/d3d12 at6152e245704e0f5d5be78c564afaa72219f495fe, local origin reference0 behind/4 ahead before this commit; remote not refreshed. Original .gitignore SHA256764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 preserved and excluded. Rechecked macOS27.2/26B123, Xcode27.2/27B52, Apple clang21.0.0, Metal32023.922. Actual launches report Apple7=1, Apple8=0, Apple9=0, Metal4=1, PlacementSparse=1 on the M1 Max. The probe creates and exercises genuine64-KiB sparse buffers/placement heaps; it does not forge capabilities or promote a D3D12 Tier.

E10 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-mapping-credential-bridge-20261105. Final executable evidence is exclusively final-gpu/; earlier root and initial-gpu/ runs are supplemental and excluded from final totals. E10 retains baseline/before source snapshots, environment.txt, build-options.json, SDK headers, fresh official Apple signal/feedback JSON, pinned Mullion queue bytes and sources.json hashes, build logs, ABI source/objects/commands, final run commands/env/logs/artifacts, scope and final delivery audits. Existing isolated build dxmt-metal272-identity-audit-20261104 uses release + D3D12/tests/NVAPI/NVNGX, patched LLVM installation llvm-current-patches-20261103/install and the repository LLVM-MinGW compiler. The x86 release build has D3D12/tests disabled as the script's x86 configuration; the entire new probe is additionally compile-checked for i686. No meson install to /usr/local or user Wine deployment occurred; copies go only to stage-sparse-mapping-credential-20261105 and prefix-sparse-mapping-credential-20261105.

### 3. Findings / source and reference evidence

| Evidence type | Conclusion / exact anchor |
| --- | --- |
| SDK_CONTRACT | E10/MTL4CommandQueue.h:261–276 orders a queue event after prior GPU work;395–431 exposes void update/copy mapping. MTL4CommitFeedback.h:19–40 concerns committed-buffer feedback. Fresh official signal/feedback JSON is hashed in sources.json. The new private event proves observed completion ordering, not a mapping NSError channel or acceptance of an entire host batch. |
| REFERENCE_SOURCE | Pinned Mullion93de32b9145885ceb2e45be7d445e56d62499b3c, E10/mullion-queue:593–625 retains the captured update, settles predecessors, commits residency, signals/waits its mapping event and orders subsequent readers. SHA256641a21ad8fce6aa6f3f6f0cb02e0b0e3ae162a97a0eb7740d738c5f70ece6492 matches the retained reference. Source reference only; no code copied and no Mullion runtime claim. Microsoft void Update/Copy resource contracts and logical mapping ownership remain those audited in the preceding Design Card. |
| SOURCE_AUDIT / ABI | winemetal.h:2508–2559 separates lifecycle state, host outcome and fixed64-byte native input/32-byte result. winemetal_thunks.h:59–84 appends ordinals203–207 with32/16/48-byte requests. The first203 entries of both normal/WOW64 native dispatch arrays are byte-token equivalent to baseline; existing ordinals are preserved. |
| LOCAL_PASS / transport | winemetal_thunks.c:1631–1672 transports fixed-width handles and input pointer, checks Unix-call status and copies results only on successful transport. Five actual loaded PE export prefixes, module paths and the paired Unix image's actual path/UUID are verified in every final core launch, including prepare-failure cases. Transport success never means mapping success. |
| LOCAL_PASS / private inputs | unix/winemetal_unix.c:4418–4466 validates shapes/overflow, allocates the event/native array and retains resources before any mapping issue. Caller mutates its input after prepare; GPU data follows the private original. Invalid kind/mode/zero length/overflow/heap/handle shape and huge count fail before issue. This validates the trusted internal ABI's structure, not arbitrary hostile native-object handles or all device/resource bounds. |
| LOCAL_PASS / completion | unix/winemetal_unix.c:4468–4549 increments possibly-issued count before each void call, seals exactly once, observes the private GPU-only event, and rejects retirement until NotIssued/GPUComplete. tests/dx12/dx12_reserved_buffer.cpp:307–439 checks full two-call acceptance, incomplete-prefix acceptance rejection, host-failed partial prefix, timeouts, cancellation separation, duplicate close/reuse rejection and exact mapping data. HostFailedAfterIssue remains unchanged after GPUComplete. |
| LOCAL_PASS / final release | unix/winemetal_unix.c:4550–4577 seals/drains issued work on final credential release. Probe:444–481 releases the last credential from another host thread under a closed GPU gate, observes that release cannot return, then opens the gate and verifies destruction only after actual completion. Both explicitly sealed and destructor-sealed paths pass. The fixture still owns native resources/residency: this does not prove final heap/COM destruction or page reclamation. |

### 4. Design Decisions

The credential follows existing MRC NSObject ownership. It owns one native queue, private shared event, lock and copied operation/native refs; no callbacks into PE, new scheduler, global registry, second tile table or per-call operation allocation after prepare. Each issue holds the credential lock only while enqueueing the native operation; event waits and native input releases occur outside it. Host state is independent of native completion. Prepared/Issuing cannot retire; a planned rejected or empty batch becomes NotIssued; partial issue must close as HostFailedAfterIssue. Native completion cannot overwrite that failure. Caller supplies valid device/resource/page bounds, residency and legal mapping/alias ordering. Unmap's internal input shape requires zero heap and offset.

Normal final release drains possibly issued work instead of freeing live native refs. This is intentionally not production fault recovery: if a private event never arrives, release may block indefinitely. No timeout/cancellation/preceding barrier callback is converted to completion; no self-retain cycle, quarantine or process-wide leak is introduced. Production queue/resource callers remain disconnected pending the existing terminal-boundary gate. The controlled test always opens valid GPU gates and drains work; its fatal _Exit is confined to the isolated test executable, not the bridge.

The existing standalone reserved-buffer probe gains native-WineMetal-only modes selected before its unchanged D3D12 fixture argument path. Every data step has a real preceding barrier, separate completion/read-order event and seeded per-heap/page/word pattern. A sparse unmap checkpoint is not read until valid remapping. A wrong expected word and unrelated CPU-cancelled event independently reject broken oracles. Empty, planned-rejected, abandoned-before-issue and injected event/array-preparation failures exercise zero-call cleanup. The injected failures are deterministic host controls, not real OOM or GPU failures.

### 5. Changed Files / reproduction

| File | Change |
| --- | --- |
| src/winemetal/winemetal.h | Add buffer operation/result/state contract and five C exports. |
| src/winemetal/winemetal_thunks.h | Append fixed-layout request ABI/ordinals; existing ordinals unchanged. |
| src/winemetal/winemetal_thunks.c | PE request/result transport with failure returns. |
| src/winemetal/Metal.hpp:922–948 | Existing WMT ownership wrapper for prepare/issue/seal/wait/retire. |
| src/winemetal/unix/winemetal_unix.c | Native prepared credential, state/retirement protocol, normal+WOW64 dispatch entries and opt-in test controls. |
| tests/dx12/dx12_reserved_buffer.cpp:38–491,1598–1605 | Existing test gains mapping-normal/partial/wrong-event/wrong-word/prepare-failure modes; no new repository runner/target. |
| docs/sparse-handoff-retirement-audit.md | Execution card, evidence, gate/remaining blocker and delivery record. |

Use E10/final-gpu/run_bridge.py to reproduce core cases against a freshly paired isolated stage; E10/final-gpu/regressions.py records adjacent cases. Exact command/env, seed, timeout, file SHA256, actual loaded code bytes and Unix UUID are in each process record/log. The external evidence runner is not a repository test-runner architecture. x64/x86 builds use meson compile -C with the correct LLVM-MinGW directory prepended; all default targets complete. E10/abi-builds.json contains both standalone fixed-ABI builds and the full i686 probe compilation. No new source under src/d3d12 or vendored headers is introduced.

### 6. Validation Results

| Check | Result / scope |
| --- | --- |
| Final x64 complete release build | PASS; all default targets, tests and native Unix library. Existing deprecation/override warnings remain; not a warning-free or clean-from-empty build claim. |
| Final x86 complete release build | PASS; D3D10/11/WineMetal configuration, D3D12 disabled. Full new probe i686 compile and fixed ABI i686/x64 compile also PASS. x86 runtime NOT_RUN. |
| Final normal-a/b, partial-a/b | LOCAL_PASS;4 seeded real-GPU positive data processes. Normal step0 admits two mapping calls; partial prepares two different operations but issues exactly one and preserves host failure. |
| Wrong-event / wrong-word controls | Expected exit1 each, exactly one rejected observation/mismatch, all real GPU work drained. Neither is a GPU/driver fault. |
| Event / native-array prepare failures |2 positive rejection processes; nil before any mapping call. No fake event completion. |
| Core totals |8 processes:6 expected exit0 and2 expected exit1. Six data processes perform42 mapping batches (30 data +12 final-release cases),46 native mapping calls,42 full buffer readbacks and1376256 compared DWORDs.60 native credentials created/destroyed with matched pointers;12 NotIssued,6 abandoned Prepared,30 explicitly retired GPUComplete and12 destructor-drained GPUComplete. |
| Existing reserved buffer + wrong oracle | PASS / expected exit1; remap, NULL, UAV, CopyTiles, descriptor-before-map and cross-queue fixture retains exact311296-word positive oracle. |
| Existing feedback / submitted barrier failure | PASS; preceding feedback still distinct; injected submission failure keeps removal/fence/signal rejection checks. Mapping credential never activated in these production paths. |
| Texture handoff retirement + negative | PASS / expected exit1; exact queued/retired readback and shader oracles preserved. |
| Packed-tail full mips + CopyTileMappings | PASS; mapping/remap/unmap, alias inheritance and copied NULL mapping retain existing readback oracles. |
| D3D11 nearby smoke | PASS;256-word CopyResource/Map readback through actual staged D3D11 and DXGI export prefixes and paths, paired Unix UUID checked. Original probe plus an identity-instrumented repeat, both successful; broader D3D11 rendering NOT_RUN. Wine reports DXGI.DLL uppercase, so identity path comparison uses Windows case-insensitive semantics. |
| Final delivery total / validation |17 processes =8 core +9 nearby (two D3D11 probes);13 expected exit0 +4 expected exit1. All actual API/GPU validation enabled notices present; no unexpected Metal diagnostic or GPU fault. Deliberate barrier failure/negative-oracle diagnostics are retained. final-gpu/audit.json verifies logs/exit codes and hashes. |
| Metal3-only / Windows / other GPUs / fault recovery | NOT_RUN. M1 Max Metal4 PlacementSparse is available, not BLOCKED_BY_HARDWARE. No forced Metal3-only runtime or Windows native mapping path tested. Natural mapping error, absent-event/device loss, teardown under full FIFO, multithread stress and ASan Wine runtime NOT_RUN. Prior native ASan evidence is historical, not counted. |

Retained non-GPU corrections: an initial build missed the WOW64 table-size assertion and was fixed; a build with the wrong shell PATH selected GCC and rejected -fblocks, fixed by prepending the repository LLVM-MinGW path. An external dispatch-audit parser initially counted only underscored handlers, fixed to include every table entry. D3D11 identity collection initially compared DXGI.DLL path case-sensitively, corrected using the same successful run. No failed GPU result is hidden by those collector/build corrections. Earlier root/initial-gpu runs use earlier source identities and are excluded from the final totals.

### 7. Capability / cost impact

No Feature Level, Shader Model, Tiled Resources Tier, Metal3 shadow backing, MSC flags/runtime requirements, Airconv, D3D11 behavior or production Fence policy changed. The native credential has no production caller and therefore adds no active production mapping allocation/lock path. If activated later, it costs one event/owner/lock plus an operation array and native retains per batch and a paired Unix call per issue/observation; driver/Foundation internal allocations are not instrumented. No throughput, overlap, zero-overhead or no-performance-regression claim. Existing shader/residency/COM semantics are not inferred from these native input retains.

### 8. Remaining Blockers / Regression Risks

- BACKEND_NOT_IMPLEMENTED: production resource-private-COM snapshot ownership, resource prepare/issue split, bounded Submission payload and final residency retirement. Buffer transport is now implemented/tested; texture/packed/self-overlap credential transport and actual production use are outside this slice.
- SPEC_UNVERIFIED: void mapping asynchronous NSError attribution and a safe alternate terminal boundary when the private event does not arrive. Current drain-on-release may stall forever; therefore it is not an approved production device-loss/teardown policy. Completion observation and host batch acceptance remain separate.
- MSC_SEMANTIC_GAP: no new gap established by this buffer/bridge slice; prior texture/shader/sparse semantic gates remain. Existing MSC fixtures pass but do not establish all sparse shader guarantees.
- TARGET_DEVICE_NOT_AVAILABLE: Windows native/other GPU comparison only. The actual M1 Max target was exercised; Apple7 is not used as a placement-sparse hardware blocker.
- ABI: additive ordinals require matched WineMetal PE/Unix deployment; no cross-version negotiation was added. Invalid raw NSObject handles and illegal device/resource ranges are outside this trusted internal interface's checks. Retaining native objects alone does not keep COM destructor-driven residency membership alive.

### 9. Git State / Rollback

Focused feat(winemetal) commit under the repository's recent subject style; no push. Original .gitignore excluded; no staged/untracked work remains after commit. E10/delivery-manifest.json records the resulting commit, tracking state and exact source/build/stage/log hashes. The first203 dispatch entries on both ABIs are preserved. Revert this focused commit to remove the five exports/wrappers/native owner/test mode/report; paired deployment must revert together. No public capability needs rollback and no user Wine installation was changed.

### 10. Next Recommended Slice

Perform one **resource-private-COM payload preparation and controlled lifetime slice**, in the existing buffer/resource and Submission ownership boundaries, with the new bridge still disconnected from automatic production issue. Materialize the old/new/source heap/resource snapshot before publication, retain the COM/residency owner rather than only native handles, drop public refs in a controlled test, and prove destruction/residency retirement only after the exact credential boundary. Keep production no-event integration gated; do not activate the new blocking final-release policy in ordinary queue teardown or add a scheduler/second mapping table. Scope the Design Card to prepared ownership and testable healthy completion, explicitly carry the unresolved alternate terminal-boundary requirement forward.

## N13 private COM mapping payload — Execution card, 2026-11-05

- GOAL / BASELINE: at3d92fa3e prepare transient old/new/source resource+heap private COM ownership in the existing resource boundary; controlled caller only, no automatic queue activation.
- PATH / EVIDENCE: buffer tile mutex owns logical mappings; resource/heap destructors unregister global residency, so native retain is insufficient. Existing private Com and mapping credential remain the two ownership layers.
- SCOPE: one payload in existing device.hpp, buffer affected-tile owner capture, existing reserved-buffer test and minimal include paths; no second logical table or scheduler.
- DESIGN: capture affected old/source refs under the existing tile mutex before issue, append new heap refs, attach a prepared native credential only afterward. Native credential is destroyed before COM refs, so early payload destruction drains issued work before COM destruction. Explicit retire requires its exact NotIssued/GPUComplete result.
- PROOF: real D3D12 resources/heaps, dropped public refs, native mapping/copy and seeded readback under gated completion; COM private-data destruction sentinels and exact existing residency-unregister trace after completion. Negative controls inspect native-only ownership without issuing unsafe work.
- NON-SCOPE: resource mapping publication split, production Submission activation, texture payload, natural mapping NSError/lost-event recovery, capability promotion and D3D11/MSC changes. Existing production mapping helpers unchanged.
- COST: O(affected tiles) private retains/storage only for explicit controlled capture; no production caller, global scan or new lock. Duplicates allowed; bounded FIFO admission belongs to later integration.
- RISK / ROLLBACK: final credential release may block on absent GPU event; no production use until terminal protocol resolved. Revert focused files together; no COM ABI layout change, only appended internal query.
- DoD: exact healthy completion precedes COM/destructor/residency removal; prepared failure/invalid ranges cleanly release; final build+GPU/negative+nearby evidence, focused commit/no push. Overall goal active.

## N13 private COM mapping payload — Delivery, 2026-11-05

### 1. Task Summary / Gate outcome

Completed the prepared resource-private-COM ownership slice at3d92fa3eee950877d260dbac9f6f1ad332b6e23d. Gate0 source/baseline/specification evidence checked; Gate1 execution card fixed above; Gate2 transient payload and existing buffer capture seam implemented; Gate3 full x64 build, i686 compile, real GPU/negative/nearby tests PASS within stated scope; Gate4 actual identities, destruction order, unchanged production helpers and diff audit PASS. The new capture seam has only controlled test callers. This is a reusable prerequisite for Submission lifetime ownership, not completed automatic queue integration. Overall goal remains active.

### 2. Baseline Evidence

Branch feat/d3d12; baseline3d92fa3e, local origin reference0 behind/5 ahead before commit, not refreshed. Only original .gitignore dirty initially; SHA256764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 preserved/excluded. Rechecked macOS27.2/26B123, Xcode27.2/27B52, Apple Metal32023.922, LLVM-MinGW clang23.1.1, Wine11.0. Actual test device reports Apple7=true, Apple8/9=false, Metal4=true, PlacementSparse=true. No hardware block asserted for the M1 Max. MSC SDK API/runtime identity unchanged; no runtime product-version query invented.

E11 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-private-payload-20261105. Delivery evidence is delivery-gpu/ exclusively. Root first-normal and final-gpu/ contain successful earlier executable identities, excluded from delivery totals after adding delayed import. E11 retains before/ source snapshots, current environment.json, sources.json with fresh Microsoft Update/Copy/Apple signal contracts and pinned Mullion queue hash, installed Metal SDK headers, build logs, i686 compile command/object, PE import inspection, scope audit and each actual run's commands/environment/log/artifact hashes. Existing release build dxmt-metal272-identity-audit-20261104 follows the script's x64 D3D12/NVAPI/NVNGX options, tests enabled and external patched LLVM15 installation. Isolated stage-sparse-private-payload-20261105 and prefix-sparse-private-payload-20261105 only; no install to /usr/local or shared Wine/prefix mutation.

### 3. Findings / source, specification and reference evidence

| Evidence type | Finding / anchor |
| --- | --- |
| SOURCE_AUDIT | Buffer destructor unregisters resource/VA residency:src/d3d12/d3d12_buffer.cpp:239–242; heap destructor unregisters backing/native heap:src/d3d12/d3d12_heap.cpp:60–65. Device removal of allocations commits immediately:device.cpp:2115–2123,2137–2153. Native retain alone cannot stop these C++ destructor side effects. |
| SOURCE_AUDIT | Existing Com<...,false> invokes AddRefPrivate/ReleasePrivate:src/util/com/com_pointer.hpp:48–50. Public Release reaching0 does not destroy an owner with private refs:com_object.hpp:49–69. This is the reused lifetime mechanism; no new COM refcount implementation. |
| SPEC_REQUIRED | Fresh Microsoft UpdateTileMappings and CopyTileMappings contracts are retained as E11/ms-update and ms-copy. Mapping operations alter resource-to-heap bindings and must preserve queue ordering; copy overlap requires source snapshot behavior. This slice does not change those entry points or substitute an internal ownership snapshot for D3D12 mapping state. Native Windows lifetime behavior is NOT_RUN. |
| SDK_CONTRACT | E11/MTLResidencySet.h:39–45,90–112 defines allocations added/removed then applied by commit. E11/MTL4CommandQueue.h:261–276 defines prior-work GPU event ordering. COM destructor unregister/commit is therefore an independent action that must be deferred past the resource's required use boundary; completion is not inferred from native object survival. |
| REFERENCE_SOURCE | Fresh pinned Mullion93de32b9145885ceb2e45be7d445e56d62499b3c queue:593–625 captures an owning update closure, settles, commits residency, issues and CPU-waits its mapping event;1338–1353,1392–1404 captures resource/heap owners. E11/mullion-queue SHA256641a21ad8fce6aa6f3f6f0cb02e0b0e3ae162a97a0eb7740d738c5f70ece6492. Source observation only, no transplant or runtime/fault guarantee. |
| LOCAL_PASS / prepared ownership | Existing resource internal interface appends CaptureTileMappingOwners; D3D12SparseMappingPayload holds private resource/heap refs with native completion last:device.hpp:351–384. Buffer capture validates all tile indices, pre-reserves storage and captures affected mappings under existing tile mutex:buffer.cpp:564–585. Invalid range leaves reference contents empty; capture after attaching a credential rejects. No logical mapping duplicated in the payload. |
| LOCAL_PASS / exact boundary | Controlled probe:tests/dx12/dx12_reserved_buffer.cpp:71–435 creates two real D3D12 reserved buffers and three heaps, snapshots source/overwritten destination heaps plus new heap. It clears fixture logical mappings without native issue, then releases all five public objects to public count0. Normal paths observe0 COM sentinels/destructor unregisters until exact completion/readback. Afterwards each of five owners destroys and its allocation is unregistered exactly once. |
| LOCAL_PASS / negative | payload-native-only omits all private refs, retains the native credential only, observes all five COM sentinels and matching residency unregisters before native issue, then exits1 as expected. It issues no mapping on those nonresident resources, so the negative control avoids an intentional GPU fault. This establishes why native retains cannot replace COM/residency ownership. |

### 4. Design Decisions / exact implementation boundary

D3D12SparseMappingPayload is transient retained input, not a second tile map or scheduler. It stores private resource refs, affected old/source heap refs, caller-added new heap refs, then the native credential. C++ reverse member destruction releases/drains that credential before any private COM refs can drop. Explicit Retire polls its exact credential, accepts only NotIssued/GPUComplete, retires native inputs first, resets the credential, then releases resource/heap vectors. Premature/double retirement rejects. No new worker, queue slot, callback into PE or device self-retain registry exists. The type is noncopyable/nonassignable to avoid replacing live ownership before completion. The caller must serialize the payload; it is not independently thread safe and its fields are internal trusted preparation inputs.

CaptureTileMappingOwners validates every supplied tile and vector size bounds, reserves before appending refs, holds only the resource's existing tile mutex while copying affected heap refs, and converts bad_alloc to E_OUTOFMEMORY. Duplicate heap refs are intentionally allowed, O(affected tiles), avoiding dedup maps/global scans. It does not capture all heaps or mutate resource mappings. It rejects capture after a credential is attached. Separate source/destination captures require external queue serialization and legal cross-queue synchronization; they do not form an atomic two-resource mapping transaction by themselves. Texture default returns E_NOTIMPL; no texture support claimed. New heap refs are supplied before native preparation by the caller; production construction/publication of this payload is not activated.

The controlled test uses existing internal logical mapping helpers with an empty native queue solely to establish/capture/clear ownership fixtures before manual native map/copy. Those calls are not ordinary application D3D12 queue execution and do not prove production queue ordering. Real native operations map source to heap A, destination to new heap C, copy source page0 to destination page0; readback verifies A/A and A/C page content with seed/heap/page/word distinctions and guards. Fixture residency includes only upload/readback/placed initialization allocations; the actual D3D12 heaps and sparse buffers depend on their existing global residency membership. Last public references are gone before issue. Sentinels attached via SetPrivateDataInterface observe actual C++ owner destruction, while opt-in logs are emitted after existing remove+commit calls. They are not physical deallocation/page-reclamation or independent kernel membership query evidence.

The payload-release mode starts an acknowledged worker, destroys the only payload under a closed GPU gate, verifies release stays blocked with0 destructions, opens the gate and observes native credential destruction followed by COM residency unregisters. This exercises reverse member order with genuine GPU work, but not missing-event recovery. Explicit retirement occurs after both mapping and classic readback completion; payload ownership only covers the mapping boundary, so later consumers must keep their own Submission refs. No readback is issued after retirement in the early-release case. Zero-issue rejection retires as NotIssued. Injected event/array preparation failures produce no native credential or mapping calls and release all private refs normally.

The existing test now includes internal headers and links WineMetal with a PE delayed import, so its original default D3D12 modes do not acquire a normal startup WineMetal import. E11/probe-imports.txt verifies this. Controlled native modes are WineMetal-specific. Default modes passing on Wine do not establish Windows native execution; Windows is NOT_RUN.

### 5. Changed Files

| Existing file | Purpose |
| --- | --- |
| src/d3d12/d3d12_device.hpp:351–384 | Append internal ownership-capture query; define transient private-COM/native payload and exact retirement. Existing public COM ABI and earlier internal virtual slots unchanged. |
| src/d3d12/d3d12_buffer.cpp:564–585 | Capture affected buffer mappings' resource/heap owners under existing tile mutex; no production update/copy behavior changed. |
| src/d3d12/d3d12_device.cpp:2115–2153 | Opt-in existing residency-unregister trace, after remove+commit, for exact owner-allocation matching. Static flag lookup avoids repeated environment parsing. |
| tests/dx12/dx12_reserved_buffer.cpp | Six controlled payload modes, actual objects/private-data sentinels, gated real GPU readback/retirement, native-only and wrong-word negatives. Existing mapping/default modes preserved. |
| tests/dx12/meson.build:1417–1425 | Existing target gains required internal include paths and WineMetal delayed link; no new executable/runner. |
| docs/sparse-handoff-retirement-audit.md | Execution card, evidence, risk/next integration boundary and delivery report. |

No new src/d3d12 file, WineMetal export/ordinal, reference code copy or vendored dependency change. Existing UpdateTileMappings/CopyTileMappingsFrom implementations after the inserted query are byte-identical to baseline; command_queue.cpp unchanged. E11/scope-audit.json records these checks. Reproduce delivery matrix with python3 E11/delivery-gpu/matrix.py, then regressions.py against a fresh paired isolated stage; expand E11 to its absolute path. All temporary scripts/logs/objects are external. No repository Python runner introduced.

### 6. Validation Results

| Check | Result / exact scope |
| --- | --- |
| Final complete x64 release build | PASS; meson compile -C existing isolated build, all default targets. Correct repository LLVM-MinGW path prepended, Metal27.2 and existing patched LLVM15; not a fresh-from-empty build or warning-free claim. |
| Full new probe i686 compile | PASS including internal payload/query; x86 runtime/full x86 rebuild NOT_RUN this slice because x86 configuration excludes D3D12 and no x86/WineMetal production source changed. |
| normal-a / normal-b | LOCAL_PASS;2 seeded positive processes,4 full-buffer readbacks,131072 DWORDs with guards. Public refs0, private owners alive through mapping and readback, then all5 exact unregister/destruction pairs once per process. |
| release-a / release-b | LOCAL_PASS;2 processes, acknowledged worker begins payload destruction while gate closed,0 early destructions, then3 actual native calls drain and all5 COM owners retire after private GPU event. No readback on these early-release paths. |
| wrong-word | Expected exit1;1 exact changed expectation,2 readbacks/65536 DWORDs, actual data intact and cleanup drained. |
| native-only | Expected exit1; all5 COM owners unregister/destroy despite native credential surviving;0 mapping calls, no unsafe GPU issue. |
| rejected / prepare-event / prepare-operations |3 positive zero-issue cleanup processes. Rejected=NotIssued with0 possible calls; injected preparation failures create no credential. All COM owners released exactly once. Deterministic host failures, not natural OOM. |
| Core delivery totals |9 processes:7 expected exit0 +2 expected exit1. Five issued processes,15 native mapping calls,6 full-buffer readbacks,196608 DWORD checks,45 COM sentinel destructions and45 matched existing unregister/commit trace points.7 native credentials:5 issued GPUComplete +1 NotIssued +1 native-only Prepared, all created/destroyed once. Prepare failures create0. |
| Existing reserved buffer + wrong oracle |PASS / expected exit1; positive311296-word remap/NULL/UAV/CopyTiles/cross-queue fixture retained. |
| Existing feedback/barrier fault |PASS; feedback fixture311296-word output and injected barrier failure removal/fence/signal-rejection checks. |
| Existing mapping credential normal/partial |PASS; unchanged native owner/ABI via final probe and matching PE/Unix runtime. Together14 readbacks/458752 DWORDs; partial host failure remains distinct from GPUComplete. |
| Texture retirement / packed full mips + copy mappings |PASS; existing handoff retirement and packed alias/unmap/remap/readback contracts. |
| D3D11 CopyResource/Map |PASS;256 DWORDs, actual D3D11/DXGI loaded export bytes and paths checked, same Unix UUID. Broader D3D11 rendering NOT_RUN. |
| Final total / identities / validation |18 processes:9 core+9 nearby,15 expected exit0+3 expected exit1. API/GPU validation enabled notices in every actual launch; no unexpected Metal diagnostic/fault. Loaded DLL paths/export prefixes, Unix image path/UUID and staged SHA256 checked. delivery-gpu/audit.json records hashes/exit validation. Core totals exclude nearby and earlier supplemental matrices. |
| Missing-event / device loss / Windows / forced Metal3 / other GPU / ASan |NOT_RUN. No GPU fault induced and no safe recovery claim. Target M1 Max available; Windows/other GPUs unavailable for this run only. |

First build failed because the newly added trace used the environment utility without its header; added util_env.hpp before GPU testing. First exact paired-runtime normal test passed. A9-process formatted matrix then passed, but delivery review caught a normal WineMetal import introduced by the test's new internal wrapper calls; corrected to delayed import, inspected PE tables, relinked and reran the entire9-process delivery matrix plus9 nearby processes. Earlier successful identities are retained separately and excluded. No failed GPU result was discarded or replaced with synthetic capability data.

### 7. Capability / cost impact

No Feature Level/Shader Model/Tier, Metal3 shadow backend, D3D11 behavior, MSC runtime/flags or production Fence/queue policy change. No automatic capture caller, so new snapshot storage/private retain costs do not enter production mapping work yet. Explicit capture costs one resource ref and up to one heap ref per affected tile, two pre-reserves and the existing tile mutex; no whole heap scan/new mutex. Production unregister gains a cached false-by-default trace branch; enabled diagnostics serialize through the existing logger while already holding residency_lock_. Not a performance optimization. No A/B timing or performance no-regression claim; gated40ms tests are not benchmarks. Full future admission/retirement cost remains to measure before activation.

### 8. Remaining Blockers / Regression Risks

BACKEND_NOT_IMPLEMENTED: resource prepare/issue/publish plan construction, attaching the prepared payload to bounded Submission before publication, texture/packed/self-copy owner coverage and ordinary production retirement. Current capture includes affected old/source heaps; complete transient new/intermediate heaps must come from a real prepared plan, not inferred after mapping issue. The fixture's direct internal map/clear setup is intentionally not production D3D12 semantics.

SPEC_UNVERIFIED: safe terminal boundary when the private mapping event never signals, plus void mapping asynchronous NSError attribution. Payload reverse destruction inherits the existing native drain behavior, which may wait forever on a lost event. No automatic production use until the terminal ownership/teardown gate is resolved. Timeout/cancellation/preceding barrier feedback do not authorize COM release or FIFO success.

MSC_SEMANTIC_GAP: none newly established by this ownership slice; unchanged MSC reserved-resource fixtures passing does not promote all sparse shader semantics. TARGET_DEVICE_NOT_AVAILABLE: Windows/other GPU reference only; M1 Max PlacementSparse is exercised. Raw internal casts/delayed WineMetal probe paths require a matched DXMT build; not a third-party COM extension ABI. Internal query is appended, not exported as a new public interface. Payload users must not independently reset its completion or refs before the protocol permits it; only controlled test calls exist now.

### 9. Git State / Rollback

Focused feat(d3d12) commit/no push; original .gitignore preserved/excluded. E11/delivery-manifest.json records final SHA, working tree/tracking, source/build/stage/log hashes and audit results. After commit no staged/untracked files; only original .gitignore remains dirty. Revert the focused six-file change to remove the payload/query/probe/diagnostics/build registration/report; WineMetal ABI does not change this round. Isolated stage/prefix stays external and user Wine untouched.

### 10. Next Recommended Slice

One **buffer mapping prepare/issue/publish plan** inside the existing buffer resource owner: reuse the current validation/tile traversal to construct immutable native operations plus this payload's old/source/new/intermediate refs before any handoff publication; preserve self-overlap snapshot semantics and zero/partial failure outcome. Avoid a parallel map or duplicated parser. Exercise it through the controlled caller first, retaining the production no-event/Submission activation gate. This advances actual input preparation rather than adding another disconnected lifetime model; texture/general production queue integration remains a separate decision.


## Buffer prepare / issue / publish — execution card, 2026-11-05

GOAL: Build one real buffer mapping plan from the existing Update/Copy parser before controlled native handoff.
BASELINE: 28f5397db35fbe9b7b53b7f2cc9956ef97e4e0e4; original .gitignore excluded.
CURRENT PATH: queue -> MTLD3D12Buffer UpdateTileMappings / CopyTileMappingsFrom -> WineMetal -> tile_mappings_.
CONTRACT: Microsoft Update/CopyTileMappings; source snapshot for overlapping self-copy; no publication after failed issue.
EVIDENCE: E11 retained Microsoft pages / pinned Mullion queue; current independent parsers and manual payload fixture.
SCOPE: Reuse validation and tile traversal in existing buffer owner; prepare owned transient operations, old/source/new refs.
DESIGN: Stack plan for legacy callers; opaque internal plan for controlled credential calls; one authoritative tile_mappings_.
BOUNDARY: No production credential/Submission activation, new scheduler, texture path, capability promotion or D3D11 change.
VALIDATE: Paired x64 build, real seeded GPU readbacks, input mutation, self-overlap, partial failure and nearby probes.
RISKS: Serialized mapping owner required between prepare and issue; lost-event teardown remains SPEC_UNVERIFIED.
ROLLBACK: Revert this focused slice; prior bridge/payload remains usable.
DONE: Existing callers use common parser; controlled plan proves ownership and publication; tests/build/audit; commit, no push.


## Buffer prepare / issue / publish — delivery, 2026-11-05

### Task Summary / Baseline Evidence

One N15/N13 buffer preparation slice on feat/d3d12, baseline28f5397db35fbe9b7b53b7f2cc9956ef97e4e0e4. E12 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-buffer-plan-20261105. macOS27.2/26B123, Xcode27.2/27B52, Metal32023.922, Wine11.0, Apple M1 Max. Actual probe reports Apple7=1/Apple8=0/Apple9=0/Metal4=1/PlacementSparse=1; no capability result mocked. Original user .gitignore excluded. Existing queue and texture implementations unmodified.

### Findings / Evidence

| Type | Evidence and conclusion |
|---|---|
| SPEC_REQUIRED | Microsoft Update/CopyTileMappings contracts, freshly retrieved E12/ms-update and ms-copy with sources.json hashes. Validated inputs are copied before returning preparation. Copy overlap uses saved source mappings. Logical publication follows complete host acceptance; it does not prove GPU completion. Windows native NOT_RUN. |
| REFERENCE_SOURCE | Fresh Mullion93de32b9145885ceb2e45be7d445e56d62499b3c queue:593–625 commits residency, issues mapping, waits its mapped event;1338–1353,1392–1404 retain resource/heap closure inputs. SHA256641a21ad8fce6aa6f3f6f0cb02e0b0e3ae162a97a0eb7740d738c5f70ece6492. Source only; no code transplant or Mullion runtime claim. |
| SOURCE_AUDIT | d3d12_buffer.cpp:133–298 owns one transient MappingPlan; BuildUpdatePlan:750 and BuildCopyPlan:833 retain existing parsers. Legacy wrappers:880–902 and controlled preparation:905–940 share these parsers and Publish. tile_mappings_ is still the sole logical map. |
| LOCAL_PASS | Existing reserved-buffer probe now constructs real plans instead of manually deriving their operations. Mutation of original coordinate/size/range/offset/count arrays after preparation cannot affect issue or publication. Host partial failure preserves prior logical destination even while real native prefix changes are read back. |

### Design Decisions / Changed Files

- src/d3d12/d3d12_buffer.cpp: MappingPlan remains inside the existing buffer owner. A stack plan serves synchronous legacy callers; the optional legacy heap is borrowed only for synchronous issue and valid new heaps remain owned by pending entries; only explicit controlled callers allocate an opaque plan and capture private ownership. No new locks, scheduler, logical map, queue wait or native export. Old/source/new/intermediate heap refs survive native drain. Self-copy emits saved mappings to preserve overlap. Legacy native batching/failure hooks remain intact.
- src/d3d12/d3d12_device.hpp: append two internal preparation methods after the existing virtual prefix; opaque D3D12SparseBufferMappingPlan exposes preparation/issue/host finish/wait/retirement. Public D3D12 COM ABI unchanged; matched internal DXMT build required. Finish(accepted) publishes once only after all native calls; rejected and failed-after-issue never publish. Retire permits zero-issue terminal or actual private GPU event only.
- tests/dx12/dx12_reserved_buffer.cpp: reuse payload's actual device/residency/lifetime/seeded readback fixture, grow controlled plan case to3pages. Modes plan-normal/partial/self/release/rejected/prepare-failure/wrong-word/cost. Default public D3D12 probe still uses delayed WineMetal imports.
- docs/sparse-handoff-retirement-audit.md: execution card, source contracts, delivery evidence and remaining gates. No new repository files/runners.

A duplicate parser or separate logical-state tracker was rejected. Automatic production credential activation remains rejected until the no-event terminal ownership contract is solved. Controlled caller must serialize source/destination mapping changes from prepare through issue/publication; capturing COM owners does not freeze arbitrary concurrent GPU mapping changes. No cross-queue concurrent mapping support is claimed.

### Validation Results

| Check | Result |
|---|---|
| Full x64 default build | PASS, release configuration following .github/build_dxmt.sh, repository LLVM-MinGW + patched LLVM installation, tests/D3D12/NVAPI/NVNGX enabled. Incremental full default target, not a clean/warning-free rebuild. No shared install. |
| Scoped formatting / git diff --check | PASS; modified hunks only. |
| i686 full probe compile | PASS, including internal plan/payload header; compile-only, no32bit runtime. |
| Plan matrix | PASS11 processes: changing seeds for normal/partial/self, pending final release, wrong-word expected exit1, rejection, native event/operations preparation failures. Seven readback processes compare14 whole3page buffers:688128DWORDs plus guards. Native map/NULL/REUSE/SKIP, self overlap, cross-resource copy, empty plan, mutated input and exact-once finish/retirement checked. |
| Ownership | PASS55 COM sentinel destructions matched to exact resource/heap unregister identities; zero public refs while issued plans remain pending; no premature destruction. This observes real COM destructor/unregister boundaries, not a kernel membership query. |
| Nearby behavior | PASS9 processes: reserved buffer shader/remap/NULL/closed-list/cross-queue311296DWORD oracle, expected wrong-oracle exit1, feedback/barrier fault, native mapping normal/partial14readbacks458752DWORDs, texture retirement, packed full mips/copy, D3D11 real256DWORD copy and loaded D3D11/DXGI export/path identity. |
| Failure regression | PASS11 processes: Update prepare/submitted, NULL unmap, Copy snapshot/prepare/submitted, copied NULL, self overlap/NULL/mixed/multiheap submitted failure. Actual mapping is drained by pre-existing test-only hook before terminal removal. Pending fence becomesMAX and later Signal rejected. |
| Validation/runtime pairing | Metal API/GPU validation enabled notices in all31 final correctness processes; no unexpected Metal fault. PE loaded paths/export prefixes, paired WineMetal Unix UUID/path and staged/build hashes retained in delivery-gpu. MSC dylib SHA256cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4; compiled SDK API4.0.3, runtime product version unknown. |
| Windows/Mullion runtime, forced Metal3, other GPU, ASan, event-loss/device fault | NOT_RUN. Target M1 Max available and exercised; no Apple7 hardware block. |

Initial GPU normal path passed. Temporary Python audit initially encoded a word-boundary regex as backspace; logs passed but audit failed, fixed external script and reran. Failure collector first used nonexistent phase issued, which exercised healthy mapping and correctly failed its removal oracle; corrected to the existing submitted hook and reran. These are preserved harness failures, not hidden GPU passes. Final review removed an unnecessary private retain of the optional unvalidated legacy heap argument before the final rebuild/matrix. Earlier identities/timing are supplemental and kept separate.

### Cost / Capability Impact

Production legacy path still uses stack ownership with no native credential allocation, additional lock or GPU submit. Controlled preparation adds one plan allocation, affected-tile vectors and private references; native preparation copies one operation per affected non-SKIP tile. Existing full resource_tiles reserve behavior is unchanged. Copy pending entries now also store destination tile indices; no all-resource/descriptor scan.

CPU-only isolated A/B (ABBA/BAAB8processes, same executable,3tile Update/Copy,20000calls/sample, discard sample0,16warm samples/build/type, validation off) measured baseline Update median216.6475ns vs candidate219.085ns (+1.1%); Copy3812.765ns vs3812.245ns. Ranges Update212.815–440.065/214.75–228.33ns; Copy3769.46–4866.59/3775.715–3916.31ns. INCONCLUSIVE; no optimization/no-regression claim. This first comparison predates the final removal of the optional legacy heap retain; final source timing is recorded separately in cost-final-summary.json. Final-source comparison (same8process order): Update baseline/candidate median 220.113/213.897ns, P95 226.545/227.005ns, ranges 213.075–226.545/209.765–227.005ns; Copy baseline/candidate median 3819.495/3825.532ns, P95 3879.875/3888.370ns, ranges 3769.425–3879.875/3782.820–3888.370ns; distributions overlap, INCONCLUSIVE. CPU-only test is not GPU throughput or mapping completion timing. No Feature Level, Shader Model, Tier, MSC flags, Metal3 fallback, D3D11 source or production Fence policy change.

### Remaining Blockers / Regression Risks

BACKEND_NOT_IMPLEMENTED: bounded Submission attachment/queue admission and texture/packed mapping plans. SPEC_UNVERIFIED: independent terminal proof when mapping private event never signals, plus asynchronous NSError attribution of void mapping calls. Native final release may wait indefinitely; timeout/removal/cancellation/preceding barrier feedback still do not authorize retirement. MSC_SEMANTIC_GAP: none newly established; sparse shader coverage remains limited to existing fixtures. TARGET_DEVICE_NOT_AVAILABLE: Windows/other GPU comparisons only, not this Apple7 device.

Logical host acceptance does not mean GPU completion. Partial native failure deliberately leaves CPU logical state unchanged, which requires terminal submission policy rather than rollback/continued normal issue. Existing parser limitations are not widened into a higher Tiled Tier. Controlled cross-resource preparation assumes serialized mapping owner; no generation/CAS/conflict map introduced.

### Git State / Rollback / Next Recommended Slice

Commit this focused four-file slice with repository feat(d3d12) style; no push, no history rewrite, original .gitignore unchanged. E12/delivery-manifest.json records final SHA/status/hashes. Revert the focused commit to restore independent legacy methods; existing WineMetal completion ABI/payload remains intact.

Next single slice: **audit and specify the bounded Submission attachment and no-event terminal ownership together against the existing queue owner**, with explicit device-loss/teardown evidence. Do not activate automatic production plan issue just because happy-path private events passed. Texture plans and capability promotion remain separate.


## Submission attachment / terminal boundary — execution card, 2026-11-05

GOAL: Resolve the next ownership boundary from current Submission/FIFO/removal code and real native post-mapping completion evidence.
BASELINE:04687a53ecffc6795b965ac0b86da7d0546280ec; original .gitignore preserved.
CURRENT PATH: BeginSparseMapping commits classic handoff + preceding Metal4 barrier; legacy resource issues; End signals cancellable sparse_event.
EVIDENCE: Current source + installed SDK contracts; prior mapping-event prototype alone cannot establish device-loss retirement.
SCOPE: Read-only production-source audit and external native GPU probe for an ordered post-mapping ResourceState barrier/commit feedback.
NON-SCOPE: No production activation, new scheduler, private event mutation, forced GPU fault, capability or backend change.
DESIGN: Reuse existing bounded Submission owner in the proposed integration; separate host acceptance, GPU completion, CPU report and cancellation.
PROOF: Gate mapping, observe earlier callback, omit independent mapping signal, require later real callback/readback, partial issue and wrong-oracle controls.
RISK: Successful later callback may prove ordered completion; error callback attribution/quiescence remains a separate contract to verify.
ROLLBACK: Documentation-only commit; external probe does not modify runtime ABI/source or user Wine.
DONE: Source/SDK/Mullion evidence, actual readback results, ownership/lock table, failure matrix and actionable next Design Card; commit/no push.


## Submission attachment / terminal boundary — delivery and Design Card, 2026-11-05

### 1. Task Summary / Baseline Evidence

This round completes the E12-recommended read-only production-source audit and external native GPU experiment. It specifies one bounded admission owner and a candidate independent completion boundary; **it does not enable production attachment or establish fault-safe teardown**. Baseline is feat/d3d12 at 04687a53ecffc6795b965ac0b86da7d0546280ec. Local stored origin/feat/d3d12 is behind0/ahead7 before this documentation commit; not fetched. Original .gitignore SHA256764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 is preserved/excluded.

E13 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-submission-terminal-20261105. Device is Apple M1 Max; macOS27.2/26B123, Xcode27.2/27B52, Metal32023.922. Real queries in every final process report Apple7=1, Apple8=0, Apple9=0, Metal4=1, PlacementSparse=1, sparse page65536 bytes. These are actual native results, not injected D3D12 capabilities. The probe uses classic native MSL, a Metal4 mapping queue and committed residency; Wine/MSC are not loaded. Prior E12 Wine/MSC results are historical, not rerun evidence for this round.

### 2. Current Source Findings

All following source anchors refer to baseline04687a53; no production source changes this round.

| Evidence type | Current source / contract | Finding |
|---|---|---|
| SOURCE_AUDIT | src/d3d12/d3d12_command_queue.cpp:86–114,459–548 | Submission owns main command buffer and preceding sparse_barrier only. Completion moves a ring entry into a local owner but retains its capacity until retirement. No mapping plan/completion payload is attached. Main completion and preceding barrier feedback do not cover later standalone mapping calls. |
| SOURCE_AUDIT | Same file:551–585,626–666 | commit_mutex_ serializes admission; WaitForSubmissionSpaceLocked checks bounded capacity. BeginSparseMapping submits/publishes the handoff record before the resource mapping call. Simply adding a late plan pointer would race completion and would not repair ownership. |
| SOURCE_AUDIT | Same file:670–682,786–846 | EndSparseMapping schedules the shared mapping signal; Update/Copy still invoke legacy resource methods after Begin. The shared timeline orders subsequent classic work, but can also be CPU-signaled by removal. It is unsuitable as an independent retirement credential. |
| SOURCE_AUDIT | Same file:125–144,615–623,688–692; src/d3d12/d3d12_device.cpp:1790–1822 | Removal sets sparse_event to UINT64_MAX and wakes host conditions; device records/remits the failure. Stop sets stopping and joins the completion thread. None of these operations establishes mapping GPU quiescence or interrupts an infinite native credential drain. |
| SOURCE_AUDIT | src/d3d12/d3d12_fence.cpp:131–143 | Removal wakes public fence waits with MAX. This reports failure, not successful mapping completion or permission to free mapped heaps. |
| SOURCE_AUDIT | src/winemetal/unix/winemetal_unix.c:4140–4343 | Existing SparseBarrierCompletion retains its actual commit callback state; it describes the preceding All→ResourceState barrier. Its synthetic-error/delayed-report controls do not widen actual GPU coverage. |
| SOURCE_AUDIT | Same file:4380–4580, especially4486–4550 | Mapping credential prepares/retains native operations, tracks possibly-issued before each void invocation, seals once and only permits retirement after no issue or the private event. dealloc drains indefinitely if the private event never signals. This is a real availability limitation. |
| SOURCE_AUDIT | src/d3d12/d3d12_buffer.cpp:133–298,750–940; src/d3d12/d3d12_device.hpp:362–371,409 onward | E12 plans capture old/source/new/intermediate owners and reuse the existing tile parser. Host acceptance publishes the logical map, not GPU completion. They are controlled-caller groundwork; neither texture plans nor automatic queue integration exists. |
| SPEC_REQUIRED | Installed MTL4CommandQueue.h:261–275; E13/apple-signal | A GPU signal is ordered after all prior queue GPU work. A CPU write to a different/cancellable event has no such meaning. Private-event observation remains valid when actually signaled by this queue. |
| SPEC_REQUIRED / INFERENCE | Installed MTL4CommandEncoder.h:49–72; E13/apple-afterqueue-resolved | A consumer barrier delays its beforeStages until prior afterQueueStages work completes. Therefore a real Blit marker after ResourceState→Blit + ResourceAlias is a candidate observable completion boundary for earlier mapping work. This inference is corroborated only on the tested successful path. |
| SPEC_REQUIRED / UNVERIFIED | Installed MTL4CommitFeedback.h:19–40; E13/apple-feedback and apple-commit-resolved | Callback describes completed committed workload; NSError describes an issue running committed command buffers. No retrieved contract explicitly establishes fault quiescence or error attribution for standalone void mapping calls preceding that command buffer. A callback with error must not be promoted to safe heap release without more evidence. |
| SPEC_REQUIRED | Installed MTL4CommandQueue.h:26–45; E13/ms-fence | Physical DeviceRemoved Metal4 error is deprecated as impossible on Apple Silicon in SDK27; timeout, access-revoked, OOM and internal errors remain relevant. Microsoft public GetCompletedValue=MAX on device removal is API failure notification. Neither observation proves native work drained. |

CodeGraph was used before targeted source reads; outputs are retained beside E13 as codegraph-sparse-submission-terminal-20261105.txt and codegraph-sparse-removal-20261105.txt. Official Apple JSON and Microsoft HTML plus installed SDK snapshots have URL/hashes in E13/sources.json. Guessed documentation endpoints returning404 and an initial Mullion SSL failure are retained; Apple parent references resolved the actual endpoints and the Mullion retry succeeded. No result depends on those failed downloads.

### 3. Mullion Comparison

REFERENCE_SOURCE only: pinned Mullion93de32b9145885ceb2e45be7d445e56d62499b3c, freshly retrieved src/d3d12/d3d12_command_queue.cpp SHA256641a21ad8fce6aa6f3f6f0cb02e0b0e3ae162a97a0eb7740d738c5f70ece6492 (E13/mullion-queue). This is not a latest-upstream claim or MULLION_OBSERVED runtime result.

- Lines593–625: Map closure owns captured inputs; settle previous work, check removal, commit residency, issue mappings, GPU-signal a dedicated mapped event, CPU-wait that event, then make subsequent work wait it. This is a stronger lifetime boundary than a preceding callback alone, but still depends on its mapped event arriving.
- Lines1338–1353: Update closure captures resource, heap, operations and settle owner. Lines1392–1404 capture both Copy resources/operations. These owners and ordering are useful semantic references.
- Lines1380–1392: self-overlap and unmap/settle handling have independent semantics; do not replace DXMT's validated E12 snapshot plan with an unrelated transplant.
- The referenced path does not supply a verified missing-event/error-quiescence solution for DXMT's bounded FIFO. No Mullion Gate/Hold scheduler, resource backend or code is copied. Existing source attribution/license files remain unchanged.

### 4. Native Validation Design and Results

E13/terminal_boundary.mm is an external ARC Objective-C++ prototype, not repository code. It prepares two sparse buffers and two2page placement heaps, populates seeded data through resident producer buffers, then performs6 mapping steps: map source A; copy to destination; partially issue one of two intended remap tiles B; fully remap destination B; unmap destination; copy A after unmap. Each process compares10 whole2page buffer readbacks,327680DWORDs plus canaries. During unmap it only reads the still-mapped source; no assertion about reading a native unmapped destination is made.

Before each step, the corrected probe waits for the actual preceding barrier callback, enqueues a closed gate, issues actual mapping, deliberately omits the independent mapping event signal, and commits a ResourceState→Blit/ResourceAlias barrier followed by a256byte marker. A separate cancellation event is CPU-signaled to MAX. While gated, preceding callback count is1, post callback0, marker unchanged and omitted event0. After opening the gate, the real post callback succeeds once, marker becomes0x3c, then classic compute reads the mapped data. The omitted event remains0. Marker allocation is committed resident and retained through actual completion; no allocator reset or removal precedes completion. The callback owns native/shared state, not a PE queue pointer.

| Test | Result / meaning |
|---|---|
| Final native probe compile | PASS, xcrun clang++ ARC/O2 C++20 against installed Metal/Foundation; E13/build-corrected.log. This is not a full DXMT build. |
| Three changing-seed ordinary processes | LOCAL_PASS18 actual mapping steps,30 readbacks,983040DWORDs. Successful post callback remains usable while independent mapping event stays0. |
| Two delayed-report processes | LOCAL_PASS12 steps,20 readbacks,655360DWORDs. On all12 steps native callback count1 precedes deliberately delayed CPU report0; then report1. Transport delay does not create earlier completion evidence. |
| Wrong preceding-boundary oracle | LOCAL_PASS as negative control: expected exit1,6 actual steps,10 readbacks. Substituting the already-received preceding callback is rejected while mapping/post work remains pending. No native result is fabricated. |
| One-word wrong oracle | LOCAL_PASS as negative control: expected exit1,6 steps,10 readbacks, exactly one data mismatch, guards clean. |
| Combined final matrix | PASS7 processes:5 exit0 +2 expected exit1;42 mapping steps,70 readbacks,2293760DWORDs. API and GPU validation enabled notices in each; no unexpected Metal validation failure. E13/matrix.json/log and per-process logs retain arguments/actual identities/hashes. |
| Host partial issue | LOCAL_PASS in every process: native first tile changes and unissued second tile retains A. This is a deliberate host prefix, not a synthetic NSError, rollback, or native atomicity claim. |
| Runtime identity | Actual executable path/UUID63B9FC2A-AC00-3954-8528-D94D7CB25C49 and Metal/driver image identities logged. Binary SHA256c371a8ca2c5ab22d0572549f9e7ac11bcc99e80284b4b1b9549627a792f39ba7. msc_loaded=false in every process; no Wine DLL pairing claim. |
| Full DXMT/x86 build; Windows/Mullion runtime; D3D11/MSC/Metal3 regressions; ASan; real fault/missing callback | NOT_RUN this documentation-only round. No production binary changed. Prior E12 tests do not substitute for future queue integration regression. |

Initial prototype FAIL is retained, not counted as a final pass: terminal_boundary-first.mm and first-native.log. It committed the preceding command buffer and immediately queued the later gate/mapping/post before waiting for preceding feedback. The post callback arrived after about4seconds and changed the marker before the intended gate observation; the pending oracle failed, and consuming that notification caused the later wait to fail (exit3). Root cause of this scheduling-sensitive observation remains UNVERIFIED; no driver bug or mapping contract violation is asserted. The corrected probe settles preceding feedback before enqueueing the gate and adds host-phase timestamps. This narrows the final experiment to that setup; it is not evidence that unsynchronized production submission has already been validated. Production must retain explicit event/stage ordering, not copy this host serialization as an optimization.

### 5. Design Card — Bounded Submission Attachment

**Goal.** Attach one fully-owned buffer mapping plan to the existing bounded queue Submission; maintain owners from before the first native dependency through a proven terminal boundary. Separate host acceptance, actual completion, CPU report and public cancellation. This card is a future integration design, not permission to activate it in this round.

**Scope / Non-Scope.** One buffer Update/Copy mapping envelope owned by the existing queue and E12 buffer plan. No texture/packed plan, alternate scheduler, parallel state map, global quarantine, capability promotion, MSC flag change or D3D11/backend replacement. No destructive GPU fault injection.

**Architecture / Proposed sequence.**

1. While commit_mutex_ serializes admission, validate and prepare the buffer plan, native operation storage, private owners/event and both pre/post native completion objects before any queue operation. Prepare the actual post-work/feedback options too; allocating a fallback after partial issue is unsafe. Empty/rejected plans remain not-issued and need no native wait.
2. Wait for existing ring capacity before the first native call. Since all producers share commit_mutex_ and the completion thread can only decrease count, count<K reserves one local in-admission record: committed records plus that record remain <=K. Keep it owned on this existing admission stack until publication; no second FIFO record or registry. This invariant requires auditing every admission caller before implementation.
3. Split current submit-and-publish helper only as necessary: submit classic/pre handoff while retaining the complete local record, issue the prepared mapping prefix, seal its host outcome exactly once, and submit the already-prepared terminal sequence. Only then move the complete record into the ring. Native callbacks may arrive before publication because they only mutate retained native state. No completion thread can retire an unsealed plan.
4. After any possibly-issued call, admission must publish its cleanup record even if removal races the normal success check. Do not reuse CommitSubmissionLocked's current early rejection to drop such a record. Hold the original capacity reservation and preserve FIFO/serial order. Failures before any issue may release locally; a prepared-but-not-committed terminal object must be distinguishable from submitted work.
5. Keep existing shared event ordering for subsequent classic consumers. The independent mapping event is never CPU-signaled. A successful ordered post callback is a controlled secondary candidate if that event is omitted; it must belong to this envelope and cover this prefix. An error callback, missing callback, timeout or device notification is not safe completion by itself. Do not allow a consumer waiting on an omitted event to precede the post callback needed to recover it.
6. Completion thread drains main/pre and this mapping credential outside queue locks. Host success is published only if all required statuses permit it; terminal failure is visible before later FIFO success. Partial issue never publishes the logical tile map. If private completion is absent and no proven terminal boundary exists, retain the record/owners and mark public device failure; bounded retained capacity does not imply bounded shutdown time.
7. Release native inputs and then E12 old/source/new/transient COM owners outside tile/commit/submission/residency locks. The callback does not release PE owners or call queue/device APIs. Preserve exact-once seal/retire, including zero issue and immediate callback.

A two-record pre/terminal scheme is rejected: reserving a second slot after issue can deadlock when the first record occupies the last capacity. Attaching a plan after BeginSparseMapping has published is rejected because the completion thread can already own/retire that record. Releasing on cancelled shared event, pre-feedback, elapsed timeout or device-removed reason is rejected because none proves native mapping access ended. Unbounded quarantine, synthetic success and disabling validation are rejected.

**Ownership / Lock table.**

| Owner / lock | Allowed responsibility | Prohibited operation |
|---|---|---|
| commit_mutex_ admission owner | Serialize input preparation, reserved local record, native issue/seal and FIFO publication | Waiting for GPU feedback or destroying a possibly-issued final owner while holding it |
| Buffer tile mutex | Existing parser/snapshot and logical publish; same serialized mapping owner spans prepare→issue | GPU waits, callback acquisition, a duplicate tile-state map |
| submission_state_->mutex | Count/head/tail/publication, move record to completion local, notify conditions | Native wait, COM final release, device callback recursion |
| Native credential lock / callback | Actual feedback/error and exact-once state/report transitions | PE queue pointer, MarkDeviceRemoved or COM/resource-unregister calls |
| Completion local owner | Observe sealed actual terminal result, fail/publicly retire FIFO in order, release owners unlocked | Treating cancellation as success, skipping an unresolved prefix |
| Residency owner | Keep old/source/new/intermediate native allocations resident through terminal proof | Inferring residency from a live COM pointer alone, unregistering before native completion |

Stop/removal must close admission and preserve/drain already-issued records. Current StopCompletionThread only wakes conditions; adding an indefinite mapping wait without resolving the terminal contract would worsen shutdown availability. No bounded-teardown claim is made.

**Failure matrix.**

| Boundary | Public outcome / logical map | Retirement evidence |
|---|---|---|
| Validation/allocation failure before native work | Reject; map unchanged | Not-issued state; local release |
| Handoff submitted, zero mappings issued | Failure if handoff fails; map unchanged | Actual handoff completion plus no-issued mapping state; preserve committed pre objects |
| All calls host-accepted | Publish logical map once, pending GPU | Actual private event; secondary successful post boundary remains controlled until bridge verified |
| Partial/ambiguous void issue | Fail device; no logical publication or rollback claim | Retain all prefix/native/COM owners until actual proven boundary |
| CPU report delayed after actual success | FIFO stays pending until report policy settles | Native result retained independently of transport; delay does not reverse completion |
| Public cancellation/removal | Fail/wake public waits | No native-release permission; private proof unchanged |
| Post callback NSError / callback absent / event absent | Fail public work; no later successful FIFO retirement | SPEC_UNVERIFIED for safe error quiescence; keep owners, shutdown may remain blocked |
| Queue destruction with pending mappings | Stop admission, retain/drain issued work | Same proof as normal retirement; no timeout-based early free |

**Test Plan / Definition of Done.** Before automatic queue activation, a controlled WineMetal credential must reproduce this external post-boundary result with the existing retained owner, real delayed/omitted-event controls, partial prefix and immediate callbacks. Then integrate one bounded record and verify full-capacity admission, no second-slot deadlock, removal races, FIFO signal ordering, public wait failure, pending final COM release and residency-unregister identities. Build the paired x64 DXMT/WineMetal using .github/build_dxmt.sh configuration; validate actual PE/Unix identities in an isolated stage/prefix. Run E12 buffer plan, sparse barrier/fault and reserved buffer/texture regressions, D3D11 smoke and MSC fixture with Metal API/GPU validation. Windows/other GPU results remain separately NOT_RUN until available. Genuine error quiescence needs explicit Apple contract or safe actual evidence; injecting an NSError into a report does not meet it.

**Risks / rollback.** Extra post command buffer/allocator/marker/residency and callback costs are unmeasured; do not call this a performance improvement. Native callback ordering/errors outside committed buffers remain uncertain, including the retained initial probe anomaly. Cross-queue generation hazards and textures remain outside scope. This round rolls back by reverting its single documentation commit; external evidence is retained. Future implementation must remain a separate focused commit and keep legacy behavior available until gates pass.

### 6. Remaining Blockers / Capability Impact

| Class | Current status |
|---|---|
| BACKEND_NOT_IMPLEMENTED | Post-mapping credential in WineMetal, reservation/publication changes and buffer plan attachment to Submission; texture/packed plans; corresponding integration regressions. Successful external prototype does not implement these. |
| SPEC_UNVERIFIED | Error feedback attribution/quiescence for standalone void mappings, missing-event/missing-callback teardown, initial probe gate anomaly; no safe general fault release proven. |
| MSC_SEMANTIC_GAP | None newly established by this native ownership test. Broader sparse sampling/NULL and DXIL semantics are not covered by the native MSL oracle. |
| TARGET_DEVICE_NOT_AVAILABLE | Windows reference and other GPUs only. M1 Max Metal4 PlacementSparse is available and passed real64KiB buffer operations; no Apple7 hardware block. |

No public Tiled Resources Tier, Feature Level or Shader Model changes. Metal3 shadow backing, existing Reserved Resource tests, D3D11 and MSC runtime compatibility remain unchanged because production sources are untouched. Gate0/1 audit/design and external validation complete; automatic integration Gate2 is **not passed** by this evidence.

### 7. Changed Files / Git State / Next Recommended Slice

Only docs/sparse-handoff-retirement-audit.md changes this round: execution card, source/SDK/Mullion evidence, native outcomes, bounded ownership design and unresolved fault gate. No repository source/test/build files, dependencies or runtime ABI changes. Commit using docs(d3d12) style, no push; original .gitignore remains dirty/excluded. E13/delivery-manifest.json records committed SHA/status/source and evidence hashes.

Next single slice: **extend the existing WineMetal mapping credential with a controlled post-ResourceState completion path**, prepared before issue and validated through its existing controlled caller with actual omitted-event, delayed-report and partial-prefix readbacks. Reuse the current native owner/state machine; keep ordinary production callers and error-quiescence retirement disabled. This tests the bridge boundary before changing queue admission. Do not automatically combine buffer Submission activation or texture mapping plans into it.


## Controlled WineMetal post-mapping credential — execution card, 2026-11-05

GOAL: Reproduce E13's ordered successful post-mapping completion in the existing WineMetal credential.
BASELINE: 0dd1d762846ea6a62e28784fd35a5223353b0759; original .gitignore excluded.
PATH: Existing buffer batch -> explicit prepareTerminal before issue -> seal -> private event / real post callback -> wait/retire.
EVIDENCE: E13 SDK contracts, current source and pinned Mullion; actual successful native readbacks, error quiescence unverified.
SCOPE: Additive controlled configuration; reuse credential owner and existing result ABI; native callback payload has no PE owner.
BOUNDARY: Ordinary callers unchanged; no production queue attachment, texture plans, capability or D3D11 changes.
PLAN: Prepare allocator/command/marker/residency/options before issue; real ResourceState→Blit barrier; success only, error never substitutes.
PERF: Extra native objects/submit only after explicit opt-in; no global scan or production hot-path change.
PROOF: Existing mapping probe, omitted event, delayed report, partial prefix, wrong boundary/word and final release controls.
RISK: Callback may precede or outlive caller; callback state independent, native inputs remain owned through actual completion.
ROLLBACK: Revert focused bridge/probe change; no public D3D12 ABI or existing mapping result layout change.
DONE: Matched x64 build/stage, actual validation/readbacks/identities, nearby regressions, delivery audit and commit/no push.


## Controlled WineMetal post-mapping credential — delivery, 2026-11-05

### Task Summary / Baseline Evidence

One N15/N13 bridge slice, baseline0dd1d762846ea6a62e28784fd35a5223353b0759 on feat/d3d12. E14 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-terminal-bridge-20261105. Apple M1 Max, macOS27.2/26B123, Xcode27.2/27B52, Metal32023.922, Wine11.0. Current actual probe queries report Apple7/Metal4/PlacementSparse true and Apple8/9 false. Original .gitignore preserved. Previous turn was progress: E13 audit, native evidence and commit0dd1d762 completed. This round implements the recommended controlled bridge; overall sparse/MSC roadmap remains incomplete.

### Findings / Specification / Reference

SPEC_REQUIRED: installed SDK MTL4CommandEncoder.h:49–72 orders ResourceState work before subsequent Blit work; MTL4CommitFeedback.h:19–40 describes actual completion of committed workload, with error attribution limited to committed command buffers. Apple JSON refreshed this round matches E13 hashes446d58464e935d7d5e957469f5b807901160460b7cbaceee04c3f9daa193a270 and ea330ec2a6b5639a74dfb9c50ad437ad9e6f4c7449471ad899bf60f10b3d608c; URLs/hashes in E14/sources-fresh.json. A successful ordered post callback is corroborated by current bridge GPU readbacks; error quiescence of standalone void mappings remains SPEC_UNVERIFIED. No error callback authorizes release.

REFERENCE_SOURCE: Mullion93de32b9145885ceb2e45be7d445e56d62499b3c queue593–625 retains its mapping closure through dedicated mapped-event wait;1338–1353/1392–1404 retain Update/Copy inputs. E14/mullion-queue is the retained E13 fresh retrieval (SHA256641a21ad8fce6aa6f3f6f0cb02e0e3ae162a97a0eb7740d738c5f70ece6492, sources.json). No new Mullion runtime or latest-HEAD claim, no code transplant. Microsoft public device-removal/fence behavior remains the E13 contract; it does not define native heap-release safety. Windows native NOT_RUN.

SOURCE_AUDIT: existing ordinary MappingCompletion used only its private GPU event. E13's ordered post callback existed only in an external prototype. The new explicit prepareTerminal entry brings it through the existing native batch owner and PE/Unix transport, leaving production queue admission and buffer plans unchanged.

### Design Decisions / Changed Files

- src/winemetal/unix/winemetal_unix.c:4401 onward adds native-only WMTMappingTerminalFeedback, retained by commit options/callback without capturing the batch or any PE owner. prepareTerminal:4550 creates command buffer, allocator,256byte marker, committed command-buffer-scoped residency set, feedback state and options before issue. ResourceState→Blit/ResourceAlias followed by a real marker fill gives the callback actual ordered work. Preparation failure releases local objects and submits nothing; retry remains possible.
- Same file:seal/wait/dealloc preserve the existing batch state machine. Seal commits prepared post work once, normally also signals the independent event; explicit test flags omit that signal or delay reporting actual feedback by200ms. wait snapshots retained evidence under batch lock, then waits outside it. Either actual private event or successful reported post callback permits GPUComplete; event_value remains the actual event value, including0. Error report alone remains pending; timed condition slices avoid busy-spin and still observe a later independent GPU event. Early final release seals/drains via the same path. Callback may outlive batch when private event finishes first, but only owns independent native state; no self-cycle or PE callback.
- src/winemetal/winemetal.h:2553 adds the explicit controlled API and documents flags, serialization, retirement semantics and unchanged32byte result ABI. Existing64byte operation and16/48byte request layouts remain unchanged.
- src/winemetal/winemetal_thunks.h/.c append ordinal208 and one export, reusing the existing fixed-width16byte input transport. Both native/wow64 tables are appended and length-asserted; no prior ordinal shifts or incompatible result replacement.
- tests/dx12/dx12_reserved_buffer.cpp:719 extends the existing mapping probe. It tests omitted event, partial issue, delayed report, event-present comparison, preparation failure, rejected/abandoned/zero plans, wrong event/word and final release. Terminal readback is CPU-submitted only after credential completion with no auxiliary done-event signal/wait, avoiding an accidental second completion proof. The test uses native WineMetal buffers/blits; it is not an MSC sparse shader test.
- docs/sparse-handoff-retirement-audit.md records this design, evidence, validation and unresolved gate. No new repository files/runners, D3D12 source files or Metal backend replacement.

Successful callback and host acceptance remain distinct. Partial prefix failure drains actual changed native mappings but does not imply CPU map rollback. Zero/rejected/abandoned preparation does not submit terminal GPU work. Ordinary callers do not call prepareTerminal, allocate its objects, log terminal reports or gain a post submit.

### Validation Results

| Check | Result |
|---|---|
| Full x64 default target | PASS using .github/build_dxmt.sh release configuration, repository LLVM-MinGW, external patched LLVM15 installation, tests/D3D12/NVAPI/NVNGX enabled. Incremental full build, not clean/warning-free. Existing managed-storage deprecations/nonvirtual sentinel warnings remain. No shared install. |
| i686 compile | PASS final probe and PE thunk objects with repository LLVM-MinGW; compile-only, no32bit runtime. Existing static ABI checks apply. |
| Scoped clang-format / git diff --check | PASS modified code hunks only. |
| Omitted-event terminal matrix | LOCAL_PASS7 processes: changing-seed normal2,partial2,delay1 and2 expected-negative exits.35 mapping/readback steps +14 final-release cases;49 buffer readbacks,1605632DWORDs, guards clean except exactly one deliberate wrong word. All successful terminal retirements show actual event0. Each process has7 actual native callbacks/reports and10 batch-owner create/destroy identities. |
| Preparation failures | LOCAL_PASS6 processes: buffer,allocator,marker,residency,options,encoder preparation failures; zero possibly-issued calls and no terminal callback. Invalid flags, duplicate/late prepare, zero, rejection and abandonment also checked in the matrix. |
| Private-event present comparison | LOCAL_PASS2 processes, normal/delayed-report:14 readbacks,458752DWORDs; actual event1 and post callback/report7each. Pending final release drains, while callback state may finish independently of the caller. These are correctness cases, not timing benchmarks. |
| Existing mapping / buffer plans | LOCAL_PASS ordinary mapping normal/partial; plan-normal/partial/self/release. Ordinary event-only path shows no terminal work; plan oracle98304DWORDs each for3readback modes,5COM sentinels destroyed exactly once per plan process. |
| Nearby regression | LOCAL_PASS public reserved buffer311296DWORD oracle, wrong-oracle expected exit1, feedback normal, submitted-barrier fault/removal, reserved texture handoff retirement, packed full-mips/copy and actual D3D11 buffer copy256DWORDs. |
| Final total / identities | PASS28 processes,3 expected exit1 controls. Nine terminal processes alone give63 actual callbacks,63 readbacks,2064384DWORDs. Metal API/GPU validation notices in all28; no unexpected Metal fault. Actual PE export bytes/paths, Unix UUID/path and build/stage pairing audited. E14/artifacts.json and delivery-audit.json retain hashes. Compiled MSC SDK API4.0.3; dylib product version unknown; dylib SHA256cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4. |
| Forced Metal3, Windows/Mullion runtime, other GPU, ASan, real GPU fault/missing callback, automatic queue integration | NOT_RUN / not implemented. No hardware block assigned to this M1 Max. Native blit readbacks do not prove broader MSC sparse semantics. |

Retained non-GPU failures: initial build selected PATH GCC and rejected -fblocks; corrected by prepending repository LLVM-MinGW, full rebuild passed. Initial delay matrix found BOOL value2 rather than normalized1 (actual delayed GPU work succeeded); normalized before final rebuild. Review fixed infinite error-report busy-spin and retained local callback snapshot. Temporary regression audit expected PAYLOAD_RESULT instead of actual PLAN_RESULT; corrected external collector. One Wine child appended loader diagnostics to the first case log after tested process exit; original recorded prefix hash was independently recovered/verified, final hash and appended bytes retained in late-log-audit.json. These are not hidden GPU failures. Final suite uses build-delivery-final.log, matrix-final.log and regressions-final.log; preliminary runs remain supplemental.

### Capability Impact / Cost / Regression Risks

No Tiled Resources Tier, Feature Level, Shader Model, MSC compatibility flag, D3D11 behavior or Metal3 shadow change. src/d3d12 command_queue/buffer/device.hpp are byte-identical to baseline. Extra allocation/command buffer/allocator/condition/marker/residency set/commit occurs only for explicit controlled prepareTerminal, never ordinary resource mappings or draw/dispatch. No new global registry, scheduler, heap scan or production mutex. This is correctness groundwork, not an optimization; GPU/CPU performance A/B NOT_RUN, no no-regression/speedup claim. Native owner size increases, even though no ordinary production caller creates it today.

SPEC_UNVERIFIED remains real error quiescence and standalone mapping NSError attribution; if neither private event nor successful ordered callback arrives, final release can wait indefinitely. No timeout, synthetic error or public device-removal notification authorizes early free. BACKEND_NOT_IMPLEMENTED remains bounded Submission admission/publication attachment and texture/packed plans. MSC_SEMANTIC_GAP: none newly demonstrated; general sparse shader behavior still unproven. TARGET_DEVICE_NOT_AVAILABLE: Windows/other GPUs, not Apple7. Previous gate scheduling anomaly remains documented in E13 and is not resolved by this controlled test.

### Git State / Rollback / Next Recommended Slice

Commit this focused six-file change as feat(winemetal), no push; original .gitignore is excluded/preserved. No staged/untracked leftovers expected; final SHA/status/hashes in E14/delivery-manifest.json. Reverting the focused commit removes the additive controlled API/probe while restoring the prior native credential; paired PE/Unix rebuild required when switching.

Next single slice: **attach a prepared buffer plan to one bounded Submission envelope through a controlled queue test entry**, using E13's existing admission owner and E14's prepared post completion, with capacity reservation, publish-after-seal, partial/removal race and FIFO retirement tests. Keep ordinary production activation disabled until ownership and error/teardown gates are met; do not combine texture plans or capability promotion.


## Controlled bounded sparse Submission — execution card, 2026-11-05

GOAL: Carry an owned buffer plan through one existing bounded FIFO record until real completion.
BASELINE:6bacfbef436a6e28e420c1f7ce26be5b0dc16e6c; .gitignore preserved.
PATH: Explicit internal test entry -> existing commit owner/capacity -> prepared plan + pre/post credentials -> sealed record -> completion.
EVIDENCE: E13 admission design; E14 real bridge readbacks; current FIFO publishes before legacy mapping and has no plan owner.
SCOPE: Existing buffer plan and queue; opt-in controlled entry only, no ordinary Update/Copy activation.
DESIGN: Prepare before native issue; reserve one local admission slot; publish even after issued removal race; drain/release outside locks.
FILES: Existing queue/device/buffer headers and implementations, existing reserved-buffer probe/report; no new source module.
PERF: One nullable plan pointer per FIFO record and completion branch; extra work only controlled entry, no heap scan/new scheduler.
PROOF: Seeded readback, partial logical nonpublication, omitted private event/delayed report, public-ref drop, capacity32/33 and removal race.
RISKS: Real fault quiescence remains unverified; actual terminal proof required, no timeout/cancellation release.
ROLLBACK: Revert focused commit; preserve ordinary legacy calls and paired bridge ABI.
DONE: Build/identities/API+GPU validation/nearby regressions/audit; commit/no push; capability unchanged.

## Controlled bounded sparse Submission — delivery audit, 2026-11-05

### Task Summary / Baseline Evidence

One N15/N13 ownership slice is complete: an explicitly enabled, matched-build internal probe can transfer a prepared buffer mapping plan into one existing bounded queue Submission. Ordinary public UpdateTileMappings/CopyTileMappings still use their established legacy path. Baseline6bacfbef436a6e28e420c1f7ce26be5b0dc16e6c on feat/d3d12; no remote refresh or latest-upstream claim. E15 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-submission-owner-20261105. Apple M1 Max, macOS27.2/26B123, Xcode27.2/27B52, Wine11.0; actual probes report Apple7/Metal4/PlacementSparse true and Apple8/9 false. Original .gitignore hash764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 is preserved. Overall sparse/MSC work remains active.

### Findings / Specification / Mullion Evidence

- SOURCE_AUDIT: src/d3d12/d3d12_command_queue.cpp:650–705 previously published the preceding handoff before calling the legacy resource mapping operation; no mapping-plan owner belonged to that record. Attaching an owner afterwards would race completion. All current ring producers serialize through commit_mutex_ and wait for count<32; completion alone reduces count. A local record reserved under that owner therefore needs no second queue slot.
- SPEC_REQUIRED: installed MTL4CommandEncoder.h:49–72 orders prior ResourceState work before the terminal Blit stage; MTL4CommitFeedback.h:19–40 describes actual committed-work completion. Fresh Apple JSON and Microsoft GetCompletedValue HTML match the retained E13/E14 hashes; E15/sources.json records URLs, installed SDK snapshots and hashes. Microsoft's UINT64_MAX fence value on removal is public failure notification, not proof that native sparse accesses stopped. Genuine error quiescence and standalone mapping NSError attribution remain SPEC_UNVERIFIED.
- REFERENCE_SOURCE: retained Mullion93de32b9145885ceb2e45be7d445e56d62499b3c, src/d3d12/d3d12_command_queue.cpp:593–625 owns the mapping closure, commits residency, submits mappings, waits on its dedicated mapped event, and orders later consumers. Its Update/Copy captures at1338–1353/1392–1404 retain inputs. Source SHA256641a21ad8fce6aa6f3f6f0cb02e0b0e3ae162a97a0eb7740d738c5f70ece6492. No transplant, Mullion runtime observation or latest-HEAD claim.
- LOCAL_PASS: omitted private mapping events complete through actual successful post callbacks; gated native work holds FIFO ownership through partial-prefix failure, simulated public removal and final queue release. Native per-prefix results remain distinct from logical acceptance. These controlled native blit readbacks do not prove all D3D12 consumers or MSC sparse semantics.

### Design Decisions / Changed Files

- src/d3d12/d3d12_command_queue.cpp:90 adds one unique plan owner to Submission. Completion at517–529 waits for a sealed actual native result, retires native inputs then COM owners outside queue locks, and only then permits later FIFO signal records to retire.
- src/d3d12/d3d12_command_queue.cpp:578 factors the existing ring publication without changing ordinary commit order. The controlled entry at719–795 waits for capacity, prepares all fallible native objects before issue, holds one admission record, submits pre/mapping/post work, seals once, and publishes the complete record. Already-issued cleanup publication survives removal. A failed prefix closes device admission before releasing commit_mutex_, so a later Signal cannot publish success through that interval. The optional host-race wait checks its result; timeout rejects host acceptance but never grants native release permission.
- src/d3d12/d3d12_device.hpp:197 appends an internal controlled virtual, gated by DXMT_TEST_SPARSE_SUBMISSION=1; public COM methods/exports and WineMetal ABI are unchanged. Caller retains the queue and serializes mapping preparation through submission; this is not a safe general concurrent public API. The plan interface at424 adds terminal preparation. src/d3d12/d3d12_buffer.cpp:210 adapts it to E14's existing native credential without changing the logical tile-map authority or legacy mapping path.
- tests/dx12/dx12_reserved_buffer.cpp:267 extends its existing retained fixture with plan-queue modes: seeded map/copy, partial prefix, zero-issue rejection, empty accepted plan, missing private event, delayed report, private-event comparison, public-ref release,32/33 admission and full-capacity removal, preparation failures, and wrong-word control. Existing main dispatch at2613 routes these modes. Three real plans use one FIFO record each; no alternate scheduler or registry is added.
- docs/sparse-handoff-retirement-audit.md records the card, evidence, costs and remaining gates. No new repository files/runners, D3D12 modules, shader changes or vendored changes.

A two-slot pre/post design is rejected because it can deadlock at full capacity. Cancellation/shared timeline/pre-feedback/timeouts are rejected as native retirement proofs. Callback code retains no PE queue pointer. The only plan user is this controlled entry; no production opt-in or automatic public mapping activation is introduced.

### Validation Results

| Check | Result / evidence |
|---|---|
| Full x64 default build | PASS using .github/build_dxmt.sh release configuration with repository LLVM-MinGW, external patched LLVM15, D3D12/tests/NVAPI/NVNGX enabled. E15/build-formatted.log. Incremental full build, not a clean/warning-free build; pre-existing override/sentinel warnings remain. No shared install. |
| i686 source/probe compile | PASS queue, buffer and existing probe with repository LLVM-MinGW; E15/sources-i686.json, probe-i686.json. Compile-only, no32bit runtime or full32bit link claim. |
| Controlled final queue matrix | LOCAL_PASS20 processes:13 normal/partial/removal/release/capacity/empty/delay/rejected/event/negative cases, one full-capacity removal, six terminal-preparation failures. Two changing seeds each for normal, partial and removal. Exactly one expected wrong-word exit1. Every emitted record is sealed before publication; maximum count32,33rd call blocks with its plan retained; removal wakes it without issue. E15/matrix-final.log. |
| Additional lifecycle/error paths | LOCAL_PASS6 processes: pending public-ref release with resource/heap unregister identities, two native mapping-preparation failures and three pre-barrier preparation failures. All five sentinel identities are destroyed exactly once per process. Zero-issue failures publish no sparse plan record/callback and leave the caller's plan retained. One heap's existing shadow-backing allocation adds a sixth unregister log; identity matching distinguishes it from the five required COM owners. E15/extra-final.log. |
| Queue-only totals | LOCAL_PASS26 processes,43 FIFO plan completions,41 actual successful native callbacks,130 sentinel destructions. Twelve whole source/destination readback oracles compare1179648DWORDs plus guards; wrong-word control finds exactly one expected mismatch. Two release cases intentionally omit readback after public COM destruction. No unmapped reads are asserted. |
| Nearby behavior | LOCAL_PASS15 processes: existing public reserved-buffer DXIL/MSC fixture311296DWORD oracle, expected wrong-oracle exit1, feedback and submitted-barrier removal, ordinary native mapping normal/partial, texture handoff retirement, packed full-mips/copy, D3D11 actual256DWORD copy, four buffer-plan modes and two private-event terminal comparisons. E15/regressions-final.log. Shader blobs contain DXIL (no SHDR/SHEX); actual MSC library log reports compiled SDK API4.0.3 and unknown runtime product version. |
| Final functional audit | PASS41 selected processes,2 expected exit1 controls; API/GPU validation enabled in all. No unexpected assertion/validation/address fault. Actual loaded PE paths/export bytes and Unix path/UUID8963C1DC-2CEA-3397-AEC6-0AC846C82683 verified, matching build/stage WineMetal pair. Log/artifact hashes and exact labels retained in delivery-audit.json, selected-labels.json, artifacts.json. Supplemental first/pre-format runs excluded from totals. |
| Formatting / diff | PASS scoped clang-format on modified code hunks, git diff --check. Original .gitignore unchanged. Device capabilities, texture/heap implementation, WineMetal source/ABI verified byte-identical to baseline. |
| Other platforms / fault proof | NOT_RUN Windows/Mullion runtime, forced Metal3, other GPU,32bit runtime,ASan,genuine GPU fault/missing callback and GPU trace. Current M1 Max is not BLOCKED_BY_HARDWARE. |

Retained non-GPU tooling issue: the first external residency collector expected exactly five unregister logs, but the existing heap shadow backing generates an additional allocation unregister. The GPU/lifetime test itself passed; collector now matches each explicit resource/heap identity exactly once after the pending boundary. Initial and final collector logs are retained. No GPU failure is hidden by that correction.

### Cost / Capability Impact / Regression Risks

Compiler-measured x64 layout (E15/record-sizes.json): Submission264→272bytes; queue9120→9376bytes, +256bytes for32nullable plan pointers. The external baseline layout probe supplies a stub for the newly appended pure virtual test method when compiling baseline queue source against current headers; no fields are added by that stub. Ordinary completion adds one nullable branch; no new ordinary allocation, lock, heap scan, scheduler or terminal command buffer. Controlled plans retain the pre-existing E14 post-work allocation/commit cost. No speedup is claimed.

Coarse A/B reuses unchanged fifo-perf:128 ordinary public mappings plus16384DWORD readback per process. Twelve ABBA processes, six per build, identical final test/DXGI/D3D11/WineMetal/MSC bytes, only D3D12 baseline/candidate differs; API/GPU validation disabled for these timing samples. Baseline median300.667969us/mapping, nearest-rankP95308.153125, range287.378125–308.153125; candidate median291.859766, P95298.083594, range270.357813–298.083594. This small overlapping coarse sample is INCONCLUSIVE, not proof of improvement or no regression. E15/performance-runs.json and performance-summary.json retain identities/commands/environment; controlled post-path performance and GPU timeline NOT_RUN.

No Feature Level, Tiled Resources Tier, Shader Model, MSC compatibility flag or D3D11/Metal3 behavior changes. BACKEND_NOT_IMPLEMENTED: automatic public buffer mapping activation, texture/packed owned plans, general cross-queue/concurrent preparation coverage. SPEC_UNVERIFIED: genuine native error quiescence and void-call error attribution; final release may still wait indefinitely without a proven boundary. MSC_SEMANTIC_GAP: none newly demonstrated; owned-plan→actual D3D12 shader-consumer ordering has not yet been exercised. TARGET_DEVICE_NOT_AVAILABLE applies to Windows/other GPU comparisons only. The E13 scheduling-sensitive prototype anomaly remains recorded and unresolved; successful controlled tests do not erase it.

### Git State / Rollback / Next Recommended Slice

Commit the five intended files as feat(d3d12), no push. Original .gitignore stays unstaged/excluded; no new repository files. Final committed SHA, Git state and source/evidence hashes are recorded externally in E15/delivery-manifest.json. Reverting the focused commit removes the controlled adapter/owner/tests, preserving E14's bridge and ordinary legacy behavior; use matched-build internal tests after rebuild.

Next single slice: **verify controlled owned buffer plans followed immediately by actual D3D12 ExecuteCommandLists/MSC shader consumers**, without a CPU mapping-completion wait, including same-queue ordering and one explicit cross-queue fence handoff. Reuse existing reserved-buffer DXIL fixture/runner and current admission owner; keep public mapping activation disabled and do not combine texture plans or capability promotion. Gate0/1 for that slice remains necessary; this round stops at delivery.

Gate conclusions: Gate0 evidence refreshed; Gate1 controlled card satisfied; Gate2 scoped implementation complete; Gate3 stated build/functional checks PASS with explicit NOT_RUN limits; Gate4 diff/identity/ownership/capability audit PASS. These gates approve delivery of this controlled slice, not automatic public activation or genuine-error retirement.

## Controlled sparse plans to MSC consumers — execution card, 2026-11-05

GOAL: Verify real D3D12 shader consumers immediately after the controlled owned plan, with no CPU mapping-completion wait.
BASELINE: d8be64478f368aacb9cbd0bcfafa7734b8d8e6b1; original .gitignore preserved; previous round delivered progress.
PATH: Existing reserved-buffer DXIL fixture -> owned buffer plan -> existing bounded queue -> ExecuteCommandLists -> MSC dispatch/readback.
INVARIANTS: Mapping and preceding consumers ordered; public fences respect FIFO; closed recording/descriptors/VA survive remap; no capability promotion.
EVIDENCE: E15 proved native blit retirement/ownership; actual D3D12/MSC consumption of that controlled path remains unverified.
SCOPE: Extend existing Run/phase fixture for same queue, explicit direct-to-compute fence, owned copy plans, NULL, UAV writes and replay.
NON-SCOPE: Production mapping activation, texture plans, genuine GPU-fault retirement, new shader/backend/runner in repository.
DESIGN: Record before mapping, issue/close owned mapping, submit shader immediately, assert pending behind a real gate, then check whole-page output.
PERF: Test-only work; production source must stay byte-identical. No optimization or no-regression claim.
PROOF: Changing seeds, wrong-word negative, omitted private event/delayed report, same/cross-queue consumers, closed-list remap replay and adjacent E15 probes.
RISKS/ROLLBACK: Any scheduling or shader failure is retained and diagnosed, not masked by CPU completion waits; revert focused test/report commit.
DONE: Actual identities/paired WineMetal, x64 build and directed GPU validation, scoped audit, repository-style commit/no push; overall goal stays active.

## Controlled sparse plans to MSC consumers — delivery audit, 2026-11-05

### Task Summary / Baseline Evidence

One N15/N13 validation slice is complete: controlled owned buffer mapping plans now have regression coverage followed immediately by real D3D12/MSC compute consumers on the same DIRECT queue or an explicitly synchronized COMPUTE queue. No CPU mapping-completion wait is inserted. CPU waits for the final consumer/readback remain necessary. This is test coverage, not production activation or completion of the wider sparse/MSC goal.

Baseline d8be64478f368aacb9cbd0bcfafa7734b8d8e6b1 on feat/d3d12; locally cached origin comparison was ahead10/behind0 before this commit, without remote refresh. E16 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-msc-consumer-20261105. Actual device: Apple M1 Max, macOS27.2/26B123, Xcode27.2/27B52, Wine11.0. Runtime probes report Apple7/Metal4/PlacementSparse true and Apple8/9 false. Compiled MSC SDK API4.0.3; runtime product version is not dynamically queried. Original .gitignore hash764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 remains unchanged.

### Findings / Specification / Mullion Evidence

- SPEC_REQUIRED: Microsoft ID3D12CommandQueue::Wait queues a GPU-side wait and returns immediately; it is not a CPU completion wait. UpdateTileMappings and CopyTileMappings supply queue mapping operations whose producer/consumer ordering must remain valid. E16/sources.json retains freshly fetched official Wait/Update/Copy pages, URLs and SHA256 values. These contracts do not establish genuine Metal fault quiescence.
- SPEC_REQUIRED: installed MTL4CommandEncoder.h:49–72 describes ordering prior queue stages before later command stages; MTL4CommitFeedback.h:19–40 describes actual committed-work completion. Installed SDK snapshots/hashes and refreshed Apple documentation are retained with E16. The private mapping event may be omitted only by the existing controlled test hook; actual successful native terminal feedback remains required.
- REFERENCE_SOURCE: pinned Mullion93de32b9145885ceb2e45be7d445e56d62499b3c, src/d3d12/d3d12_command_queue.cpp:593–625 owns mapping inputs, commits residency, submits mappings, observes its mapped event and orders consumers. Update/Copy captures at1338–1353/1392–1404 retain their inputs. Retained source SHA256641a21ad8fce6aa6f3f6f0cb02e0b0e3ae162a97a0eb7740d738c5f70ece6492. This is pinned reference-source evidence, not a current Mullion HEAD or runtime result; its separate queue architecture is not transplanted.
- LOCAL_PASS: E15's existing bounded owned-plan path supports seeded MSC SRV reads/UAV writes, A/B remapping, copied mappings, NULL behavior and closed-list replay in the tested compute cases. Both gated pending observations and immediate ungated submissions pass. Missing private mapping events do not lose the public consumer ordering in these cases. This does not prove all shader stages, general concurrent preparation or safe retirement after genuine GPU faults.

### Design Decisions / Changed Files

- tests/dx12/dx12_reserved_buffer.cpp:2341 extends the existing Run fixture with optional owned modes. Actual device checks at2383 require real Metal4/PlacementSparse and the matched internal test entry; there is no fabricated capability result. Ordinary fixture arguments retain the legacy public mapping path.
- tests/dx12/dx12_reserved_buffer.cpp:2517 prepares existing owned update/copy plans, transfers them into the existing bounded Submission, signals the public mapping fence and optionally queues a direct-to-compute Wait. Initial backing population remains the established public producer path. No second scheduler, resource registry, new shader or repository runner is introduced.
- tests/dx12/dx12_reserved_buffer.cpp:2562 replays the exact closed list from A against B after remapping, using its original allocator, descriptors and virtual address. Fresh lists are otherwise recorded and closed before mapping submission. Full-page expected patterns and first/last UAV writes expose stale mappings and aliases; later phases also verify the untouched backing.
- tests/dx12/dx12_reserved_buffer.cpp:2620 releases the real native gate on assertion unwinding so test failure cannot strand native work during queue destruction. At2657 gated cases require both mapping and consumer fences to remain pending before opening that gate. Ungated modes omit this extra gate and submit immediately. Opening a gate is never counted as completion evidence.
- tests/dx12/dx12_reserved_buffer.cpp:2757 emits final phase/word evidence; main at2814 accepts seven positive modes and one deliberate wrong-word mode. Both changing seeds exercise each positive mode. docs/sparse-handoff-retirement-audit.md records the execution card, source evidence, results and remaining gates.

Rejected shortcuts: a CPU wait for mapping completion would hide the ordering under test; a later draw overwriting an earlier result would hide stale reads; private-event presence alone would omit the E15 fallback path. No change to production src/include, HLSL, Meson targets, public mapping entry points or MSC flags is needed for this slice.

### Validation Results

| Check | Result / evidence |
|---|---|
| Full x64 default build | PASS using .github/build_dxmt.sh configuration, repository LLVM-MinGW and external patched LLVM15, release with D3D12/tests/NVAPI/NVNGX enabled. E16/build-delivery.log. This is a full-target incremental build, not a clean or warning-free build; the pre-existing SparsePayloadSentinel non-virtual-destructor warning remains. |
| i686 probe compile | PASS with repository LLVM-MinGW against the final source; probe-i686-final.json/log. Compile-only: no full32bit build/link or runtime claim. |
| Owned-plan MSC matrix | LOCAL_PASS14 positive processes, seven modes times two changing seeds. 154 phases compare4358144DWORDs, including14 closed-list remap replays and14 same-mapping replays. Each process checks311296DWORDs across11 phases. Modes: owned-same, owned-cross, owned-copy-cross, owned-delay-cross, owned-event-same, owned-ungated-same, owned-ungated-cross. E16/matrix-delivery.log and consumer-summary.json. |
| Native terminal/order evidence | LOCAL_PASS154 actual successful native callbacks and154 sealed FIFO plan completions. 132 completions omit the private mapping event;22 include it. 110 phases establish pending fences behind the real native gate;44 run without that gate. Delayed-report cases use the existing delayed callback hook. All shader submissions occur before a CPU mapping-completion wait; CPU final-readback waits are retained. |
| Wrong-word negative control | LOCAL_PASS one expected exit1: the first completed shader readback disagrees in exactly the deliberately altered first-word oracle. No success result is accepted. This is an oracle control, not simulated GPU failure or native error-quiescence evidence. |
| Nearby regressions | LOCAL_PASS five processes: existing public reserved-buffer MSC fixture311296DWORDs; bounded queue capacity; full-capacity removal; packed full-mips/mapping-copy fixture; actual D3D11 copy256DWORDs with loaded export checks. E16/regressions-delivery.log. |
| Final identity / validation audit | PASS20 selected processes,19 expected successes plus one deliberate exit1. API and GPU validation enabled in all20, without unexpected validation/assertion/address faults. Actual loaded PE paths/export bytes and WineMetal Unix path/UUID8963C1DC-2CEA-3397-AEC6-0AC846C82683 match the isolated build/stage pair. DXIL blobs contain DXIL and no SHDR/SHEX. E16/delivery-audit.json, artifacts.json, runs.json retain exact commands, environments, identities and hashes. |
| Diff / formatting / preservation | PASS scoped clang-format, git diff --check, original .gitignore hash and no src/include/HLSL/Meson changes versus baseline. Final tests were rerun after adding failure-path gate cleanup. Earlier pre-format/pre-cleanup runs are retained separately and excluded from final totals. |
| Other platforms / broader proofs | NOT_RUN Windows native, Mullion runtime, other GPU, forced Metal3,32bit runtime, genuine GPU fault/missing callback, GPU trace and performance A/B. Current M1 Max is not BLOCKED_BY_HARDWARE. |

Reproduction: build the existing dx12_reserved_buffer target and unchanged reserved_buffer.probe_read.cs.cso / reserved_buffer.probe_write.cs.cso fixtures. In a matched isolated Wine stage/prefix with API/GPU validation and a separate cache, run dx12_reserved_buffer.exe reserved_buffer.probe_read.cs.cso reserved_buffer.probe_write.cs.cso 23781 1 owned-cross; repeat with seed771239 and each listed mode. E16/matrix.py, regressions.py and per-process JSON preserve the exact local invocation/environment, including DLL overrides, Unix path and MSC library selection. Those orchestration scripts are external evidence, not new repository infrastructure.

Retained tooling correction: the first collector expected a DXMTMSCCompileDXIL export log that this fixture does not emit. The GPU case itself passed. The collector now checks the existing D3D12CreateDevice / DXMTMSCGetCapabilities export identity logs, actual Unix identity, matched artifacts and real DXIL readback. matrix-first-collector.log retains the initial collector failure; all selected final runs use the corrected checks and final executable. No GPU failure is converted to PASS.

### Capability Impact / Regression Risks

No Feature Level, Tiled Resources Tier, Shader Model, D3D11 behavior, Metal3 backend or MSC runtime compatibility change. All production source bytes remain baseline-identical; this test-only slice supplies evidence rather than a runtime optimization. Performance NOT_RUN and no speedup/no-regression claim.

- BACKEND_NOT_IMPLEMENTED: public owned-plan activation, texture/packed owned plans and general concurrent mapping preparation/source-generation coverage. Existing public shadow backing and reserved-resource behavior remain the production authority.
- SPEC_UNVERIFIED: native quiescence after genuine GPU errors, standalone void-mapping error attribution and the previously retained scheduling-sensitive prototype anomaly. Successful controlled consumer runs do not close those gaps; final release may still wait indefinitely without actual terminal proof.
- MSC_SEMANTIC_GAP: none newly demonstrated in these compute SRV/UAV/NULL cases; graphics stages, all descriptor access forms and other shader families are not established by this matrix. NULL assertions use existing logical handling and do not claim native unmapped-read guarantees.
- TARGET_DEVICE_NOT_AVAILABLE: Windows native/other GPU comparisons only. Apple7 hardware capability is not used as a placement-sparse blocker. Test command/resources remain alive through readback; this round does not establish an additional public early-release lifetime contract.

### Git State / Rollback / Next Recommended Slice

Commit only tests/dx12/dx12_reserved_buffer.cpp and this report with a focused test(d3d12) subject; do not push. Original .gitignore remains unstaged and excluded, with no new repository files. E16/delivery-manifest.json records the final committed SHA, status, source/evidence hashes and no-push state after commit. Revert this focused test/report commit to remove the added modes without altering E15 or production mapping behavior.

Next single recommended slice: **audit and design the source-mapping generation/ownership contract across buffer-plan preparation and queue admission**, especially CopyTileMappings source changes between those steps. Identify the existing authoritative owner and locking boundary before proposing the smallest concurrency regression/implementation. Do not activate public mappings, add a parallel state map or combine texture work into that slice. This round stops after delivery.

Gate conclusions: Gate0 evidence refreshed; Gate1 execution card satisfied; Gate2 scoped regression implementation complete; Gate3 final builds and directed GPU checks PASS with the explicit NOT_RUN limits; Gate4 identity/diff/capability delivery audit PASS. Overall sparse/MSC development remains active and no capability promotion is authorized by these results.

## Buffer-plan preparation versus admission — N15 audit, 2026-11-05

### Task Summary / Baseline Evidence

This round completes the recommended read-only source audit and Design Card for source mapping/ownership between plan preparation and bounded queue admission. Only this report and its resource-audit checklist link change in the repository. Production code and repository tests remain untouched. A temporary external probe exercises current, unmodified DLLs and demonstrates a logical/native divergence when a controlled copy is prepared ahead of an intervening source remap. This closes an evidence gap; it does not implement the fix or complete the overall sparse/MSC objective.

Baseline11d0184704480eef538a2e757f4d14916451c333, branch feat/d3d12. Starting worktree contains only the original unstaged .gitignore; hash764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 retained. Cached origin comparison ahead11/behind0; remote not refreshed. Apple M1 Max, macOS27.2/26B123, Xcode27.2/27B52, Wine11.0. E17 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-generation-audit-20261105. E17/audit.json, sources.json, probe-build.json and per-process JSON retain exact source/artifact identities, commands and outcomes. CodeGraph exploration preceded targeted source reads. No memory-registry match was used.

### Findings / Source Evidence

| Evidence type | Current source | Finding and boundary |
|---|---|---|
| SOURCE_AUDIT | src/d3d12/d3d12_buffer.cpp:147–174,874–879,928–940 | BuildCopyPlan snapshots source heap/offset into updates under the source tile mutex. Capture runs afterwards, taking separate destination/source owner snapshots. The source snapshot and captured current owners are not one atomic transaction. Snapshot heap refs in updates preserve the original heap, but do not establish that native source mappings still equal that snapshot. |
| SOURCE_AUDIT | Same file:178–204,219–225 | Cross-resource PrepareNative emits a native copy from the live source buffer, without using the saved heap/offset for that copy. Accepted Finish publishes the earlier updates snapshot. A source remap between preparation and admission therefore lets native copy and logical publication name different backing. Self-copy instead emits explicit map/unmap operations from its saved snapshot; blindly changing it to sequential native self-copies would lose validated overlap semantics. |
| SOURCE_AUDIT | Same file:725–748; src/d3d12/d3d12_device.hpp:389–408 | CaptureTileMappingOwners privately retains current resource/heap objects, and payload retirement releases native inputs before COM owners. This is object lifetime evidence, not an immutable version of a live buffer's mapping. A destination remapped after Capture may also introduce an overwritten heap absent from that plan's captured old-target refs. A use-after-free is not established here; this is an unclosed ownership interval requiring an admission-time capture. |
| SOURCE_AUDIT | src/d3d12/d3d12_command_queue.cpp:728–755,763–790 | The controlled entry locks commit_mutex_ and waits for capacity only after the caller has prepared/captured the plan. It allocates native objects and issues/seals/publishes under that existing owner, but never refreshes source contents or old destination owners. A capacity wait can enlarge the stale interval. |
| SOURCE_AUDIT | Same file:889–949; src/d3d12/d3d12_device.hpp:195–200 | Ordinary Update/Copy builds and issues the legacy plan inside that queue's commit mutex. This round does not demonstrate the same gap in a serial public call sequence. The internal test entry explicitly requires caller serialization. Its prepare-ahead usage is not a promised concurrent public API; prior E15/E16 results assumed their prepared mappings stayed semantically equal. |
| SOURCE_AUDIT | src/winemetal/unix/winemetal_unix.c:4498–4551,4611–4628 | Native batch preparation retains source/destination buffers and explicit update heaps. A copy operation retains the buffer object, not a fixed copy of its tile mapping. At issueNext it invokes copyBufferMappingsFromBuffer on that live object. More reference counting alone cannot reconcile stale logical contents. |

The defect is reproducible with one host thread; no CPU data race is required. Intervening source changes ordered before the native copy are enough. Per-resource tile mutexes prevent simultaneous container access, but do not span preparation through admission. A per-queue commit mutex also cannot serialize different queues. Unsynchronized cross-queue mapping/use remains outside the proposed fix's guarantee; queue fences order GPU work, not arbitrary later CPU publication of a second logical state.

### Specification / Mullion Comparison

SPEC_REQUIRED: freshly fetched Microsoft CopyTileMappings defines copying source mappings and overlapping self-copy as if through a temporary location; UpdateTileMappings applies regions in order with later updates overwriting earlier overlapping ones. These public contracts do not define DXMT's internal PrepareBufferTileCopy timestamp or grant it an independently observable stale snapshot. The selected design must make logical and native effects agree with the accepted queue order. E17/sources.json retains the official Copy/Update/queue-design URLs and hashes; no native Windows result is claimed.

SPEC_REQUIRED: installed MTL4CommandQueue.h:410–430 describes copying live placement-sparse buffer mappings, matching page sizes and the application's alias-use responsibility. The SDK snapshot is retained in E17/apple-installed-queue.source. The actual GPU observations below support the live-source distinction without claiming a hardware fault. Existing actual-terminal and residency requirements remain unchanged.

REFERENCE_SOURCE: retained Mullion93de32b9145885ceb2e45be7d445e56d62499b3c, src/d3d12/d3d12_command_queue.cpp:593–625 places an owned closure inside its Map/StartCommitting flow and orders later consumers. Lines1392–1404 retain source/destination and call native copy plus device_->CopySparseBufferMappings together in that closure. Thus its buffer bookkeeping is not copied out by the DXMT-style externally prepared plan shown here. However its separate Hold/Settle/queue design and texture bookkeeping are not proof of general concurrent correctness, and cannot be transplanted wholesale. Source SHA256641a21ad8fce6aa6f3f6f0cb02e0b0e3ae162a97a0eb7740d738c5f70ece6492; retained pinned source only, not a fresh Mullion HEAD/runtime observation. The guessed sibling checkout does not exist, so no local Mullion worktree status is asserted.

### Reproduction / Validation Results

The external generation_probe.cpp derives from the existing CheckSparseMappingPayload fixture. It preserves actual queue admission/native completion and readback. Its explicit delta selects heap2 for the pending source remap, keeps the already-prepared copy snapshot at heap0, logs both logical identities and compares the native output against both seeded patterns. No production binary is patched; no capability result is fabricated. Source transformation and exact compile/link commands are retained in E17/prepare_probe.py and probe-build.json.

1. Establish source logical mapping to heap0; initialize distinct full-page patterns in heap0 and heap2. Prepare source update→heap2, destination update→heap2 and a two-page copy while the source still logically names heap0.
2. Submit the source remap, destination remap and prepared copy in that order through the existing controlled bounded owner, all behind one real GPU gate. Logical accepted source names heap2; destination publishes the saved heap0 snapshot. Assert this before releasing the gate.
3. Release the gate, drain through actual successful native completion, then blit-read both three-page buffers plus32 guard words. The copied destination pages contain heap2's pattern, disagreeing with all32768DWORDs of the saved heap0 expectation. Native-result oracle and guards remain exact.
4. Repeat with a second seed. Run the unchanged plan-queue-normal fixture at both seeds as controls. The external probe returns0 when it has reproduced the defect; that exit status is not a consistency PASS.

| Check | Status / evidence |
|---|---|
| Logical/native equality under prepare-ahead source change | LOCAL_FAIL twice: GENERATION_DIVERGENCE logical_heap=0 native_heap=2 words=32768 stale_mismatches=32768 native_bad=0 guard=0. These are deliberately exercised unsupported controlled-entry sequences, not two public-API conformance failures. |
| Unchanged controls | LOCAL_PASS twice, plan-queue-normal; each compares98304DWORDs and32 guards. |
| Combined native experiment | Four processes,393216DWORDs checked,12 actual successful native callbacks,12 FIFO completions and20 explicit sentinel destructions. Real Apple7/Metal4/PlacementSparse checks pass. API/GPU Validation enabled without unexpected faults. E17/run-probe.log, generation-0/1.log and control-0/1.log. |
| Runtime identity | PASS actual loaded D3D12/WineMetal export bytes and paths; build/stage hashes; Unix UUID8963C1DC-2CEA-3397-AEC6-0AC846C82683. New isolated stage/prefix and per-process shader caches; original/shared Wine environment untouched. |
| Compile / source preservation | PASS external x64 probe compile/link against baseline import libraries; src/include/tests byte-identical to HEAD. No full rebuild, i686 or new MSC dispatch: NOT_RUN because this round changes only the report and uses the existing actual native mapping fixture. Previous E16 MSC results remain bounded historical evidence. |
| Broader tests | NOT_RUN Windows native/Mullion runtime, forced Metal3, other GPUs, real GPU fault, cross-queue mapping race, destination overwrite lifetime reproduction, NULL/ABA source-change variations and performance A/B. No simulated result is substituted. |

Tooling notes: initial skill aliases were absent and the catalog's actual skill paths were used; one shell glob had no match and one transport JavaScript string failed to parse before execution. Corrected targeted reads succeeded. No native GPU experiment was restarted because of observation timeout, and all four completed processes are retained.

### Design Card — Controlled Admission-Time Mapping Resolution

**Goal.** Make one controlled buffer plan's logical publication and native effects refer to the same source snapshot in the existing accepted same-queue order; capture overwritten destination owners at that same boundary.

**Baseline / evidence.** HEAD11d01847 and E17's two reproduced divergences; E15 bounded ownership/terminal evidence and E16 MSC consumer coverage. This is a proposed next implementation slice, not code implemented in this round.

**Scope.** Existing buffer MappingPlan and its PrepareNative admission call, existing bounded queue owner and existing reserved-buffer regression fixture. Resolve cross-resource copy inputs and old-target lifetime at controlled admission. Preserve self-overlap snapshot semantics, NULL mappings, SKIP, duplicates and all allocation-failure paths.

**Non-Scope.** Public activation, texture plans, Metal3 replacement, new scheduler/version registry, all-queue transaction semantics, real-error retirement and capability promotion.

**Architecture / module boundary.** tile_mappings_ remains the sole logical authority. MappingPlan owns transient immutable inputs and native retirement refs. commit_mutex_ owns same-queue admission order. Resource locks protect a bounded source/target snapshot transaction; they must not span capacity waits, GPU waits, callbacks, possible native drain or final COM destruction. Different-resource locks require a single deadlock-safe acquisition scheme; self-copy takes one lock. No lock is added to ordinary draw/dispatch.

**Proposed changes.** After the current capacity wait, but before native preparation/any queue operation, resolve copy contents from current tile_mappings_ under the existing resource owner. Build refreshed pending updates and current old-target/source ownership into temporary storage as one source/target transaction. Then prepare the native credential from those accepted inputs. On allocation/preparation failure, publish no logical state and issue no native calls; retain a valid caller-owned plan with no leaked/double-retired refs. Preserve initial resource refs so objects remain alive until admission. Never drop captured prior heaps while holding a resource lock. Preserve publish-on-host-acceptance and sealed cleanup after partial issue.

The first fix should retain native cross-buffer copy and rebase its logical snapshot under the same serialized admission owner. Converting every cross-buffer copy into explicit heap updates merely makes an early snapshot win and can misrepresent queue order; it is not selected. Self-copy must refresh its snapshot at the accepted boundary but continue explicit heap map/unmap for overlap. Do not introduce a generation counter/CAS map solely to reject a sequence the queue can resolve correctly; an unchanged counter check followed by unlocked issue would still leave an interval. Any conflict scheme for independently ordered queues requires a separate design.

**Test plan.** Move the E17 source-remap case into the existing probe and require logical heap2/native heap2. Add source→NULL and NULL→mapped; add source A→B→A to catch mistaken identity-only assumptions; exercise a self-overlap copy after an earlier accepted remap. Use differing destination old mappings to verify current overwritten-owner retention until actual terminal proof. Include a full-capacity wait with source change completed on the host before admission, changing seeds, wrong-word negative and preparation-allocation failure. Re-run E15 partial/removal/release/capacity and E16 same/cross/copy/ungated MSC cases with matched runtime identities. An explicit cross-queue GPU Wait case must be reported separately from concurrent CPU mapping publication; do not claim general race safety from it.

**Risks / cost.** Re-resolving O(affected tiles) and retaining current old-target refs adds bounded work only to this controlled path. Reuse/reserve existing vectors where ownership/failure guarantees permit; no whole-device scan. Same-queue serialization prevents interleaved accepted host mapping calls, but doesn't by itself prevent another queue changing native source mappings later. The controlled entry's caller-serialization requirement remains until that larger contract is proven. Genuine native fault quiescence remains SPEC_UNVERIFIED.

**Rollback.** One focused implementation/test commit, preserving the public legacy path and paired native ABI. Revert it without touching E17's retained failure evidence or the user's .gitignore.

**Definition of done.** Final same-queue cases prove logical/native equality and actual owner lifetime, including failure/NULL/self-overlap/capacity boundaries; required builds and nearby regressions pass; no new resource-state authority; public Tier/FL unchanged; exact costs/NOT_RUN limits reported; repository-style commit without push. If locking or independent-queue ordering requires broader architecture, stop expansion and update this card before activation.

### Capability Impact / Remaining Blockers / Git State

BACKEND_NOT_IMPLEMENTED: controlled admission-time source/old-target refresh, general concurrent mapping ownership and texture owned plans; public activation stays off. SPEC_UNVERIFIED: real native error quiescence, void mapping error attribution, cross-queue CPU-publication/native-order correspondence and the retained earlier scheduling anomaly. MSC_SEMANTIC_GAP: none newly demonstrated; this audit's readback is native blit, and shader-family coverage is not expanded. TARGET_DEVICE_NOT_AVAILABLE: Windows native and other GPU comparisons only. M1 Max placement-sparse capability is confirmed by the real probe, not a hardware blocker. No FL/Tier/Shader Model/MSC flag changes.

Changed files: this report records the evidence/Design Card; docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md adds its partial-status index link. Commit as docs(d3d12), do not push. Original .gitignore stays unstaged; no repository untracked files. E17/delivery-manifest.json records final SHA, diff scope, source/evidence hashes and final status. Documentation rollback is a focused revert.

Next single recommended slice: implement and regress **controlled admission-time buffer mapping resolution** according to this Design Card, retaining the same existing queue/resource owners. Gate0/1 for this audit/design round are complete; Gate2 production implementation is deliberately NOT_RUN, Gate3 reproduces a bounded consistency failure and passing controls, Gate4 source/evidence/capability audit passes for delivery. This round stops at the design boundary; the overall development goal remains active.

## Controlled admission-time buffer mapping resolution — delivery, 2026-11-05

### Task Summary / Baseline Evidence

This N15 slice implements the preceding Design Card for the existing controlled buffer-plan path. Mapping contents and current overwritten-target owners are resolved at PrepareNative, after the existing queue capacity wait, under the existing resource locks. The E17 prepare-ahead source-remap case now publishes the same backing the native copy uses. Public UpdateTileMappings/CopyTileMappings activation is not changed. Overall sparse/MSC development remains active.

Baseline de137193f871b6857cac1086ebbfcaac9406bf80 on feat/d3d12; cached origin ahead12/behind0 before this commit, no remote refresh. Original .gitignore hash764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759 preserved. E18 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-admission-resolution-20261105. Apple M1 Max, macOS27.2/26B123, Xcode27.2/27B52, Wine11.0; actual Apple7/Metal4/PlacementSparse true, Apple8/9 false. MSC compiled SDK API4.0.3; runtime product version remains unknown. E18/sources.json refreshes Microsoft Copy/Update/queue-design documentation and verifies installed Apple/pinned Mullion source hashes. CodeGraph was used before targeted code reads.

### Findings / Design Decisions / Changed Files

- SPEC_REQUIRED / SOURCE_AUDIT: the E17 distinction remains: source mapping contents copied by the native queue must match logical publication for the accepted serialized order. CopyTileMappings overlap semantics require a snapshot; object lifetime alone does not freeze a live buffer's mappings. Official source URLs/hashes are retained in E18, with installed MTL4CommandQueue.h:410–430. Windows native differential remains NOT_RUN.
- REFERENCE_SOURCE: pinned Mullion93de32b9145885ceb2e45be7d445e56d62499b3c queue:593–625,1392–1404 owns mapping inputs and performs buffer-copy bookkeeping in its mapping closure. Its scheduler/closure architecture is not copied. E18 retains the same source SHA256641a21ad8fce6aa6f3f6f0cb02e0b0e3ae162a97a0eb7740d738c5f70ece6492; no latest-HEAD or runtime claim.
- src/d3d12/d3d12_buffer.cpp:178 adds ResolveAdmission to the existing MappingPlan. It reserves temporary storage before taking locks, locks different source/destination mutexes together with std::lock, and takes one mutex for self-copy. It snapshots current source heap/offset and current overwritten destination heaps in one bounded transaction. Only after unlocking does it swap refreshed pending updates and owners into the plan. tile_mappings_ remains the sole logical authority.
- Same file:227 calls this resolution from PrepareNative. Existing queue:728–734 already calls it after WaitForSubmissionSpaceLocked under commit_mutex_; no queue, header, ABI or bridge changes are needed. Cross-resource copies retain native copy operations; self-overlap retains explicit snapshot map/unmap operations. Update plans also capture their current overwritten target owners.
- Prior captured heap references are retained through terminal retirement, including native-preparation retries, so replacing a stale snapshot cannot destroy the last heap reference while commit_mutex_ is held. Both source-resolution failure hooks run before native credential creation, one before temporary construction and one after the snapshot but before swaps. Failure leaves logical state untouched, caller plan retained and device healthy; no queue calls are issued by that attempted copy. Temporary refs die after resource locks unlock. Final native/COM retirement remains the existing FIFO responsibility.
- tests/dx12/dx12_reserved_buffer.cpp:274 extends the existing plan-queue fixture with generation, generation-aba, generation-self, generation-capacity, generation-failure, generation-failure-late, generation-null, generation-mapped and generation-lifetime. Two changing seeds each. Capacity at473 holds the copy itself behind32 records after the source remap. Lifetime at426 completes an intervening destination remap, releases its heap's public ref, then verifies the newly captured heap survives the next gated copy and dies only after actual terminal proof.
- NULL mode at619 checks logical unmapping and reads only the unaffected mapped tail with guards; it never uses native unmapped reads as an oracle. Existing MSC NULL shader coverage is rerun separately. Existing partial/rejected/duplicate/SKIP/self/teardown mechanics remain in their original fixture. docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md indexes this bounded implementation; this report records evidence and limits.

Rejected alternatives: adding a generation/CAS map duplicates authority without closing the check-to-issue interval; mapping all cross-buffer copies from the early saved heap would preserve stale call order; native sequential self-copy would break overlap snapshots. A global mapping lock or new scheduler is not introduced to claim independent-queue safety.

### Validation Results

| Check | Result / evidence |
|---|---|
| Full x64 default build | PASS release configuration following .github/build_dxmt.sh, external patched LLVM15, repository LLVM-MinGW, D3D12/tests/NVAPI/NVNGX enabled. E18/build-final.log includes390 build steps; build-reviewed.log compiles the final ownership-review source. Incremental full-target builds, not a clean/warning-free build. Existing override/sentinel/Airconv warnings remain. Isolated deployment only. |
| i686 affected sources | PASS compile-only buffer implementation and probe using repository LLVM-MinGW, final source; E18/i686.json and named logs. Full32bit link/runtime NOT_RUN. |
| New admission matrix | LOCAL_PASS18 processes, nine modes times two seeds,1605632DWORDs plus guards. Mapped copies/self/ABA/failure/capacity/lifetime each read98304DWORDs; NULL reads16384 mapped tail DWORDs and checks all relevant logical NULL tiles. E18/generation-delivery.log and generation-summary.json. |
| Regression sensitivity | PASS new test executable against the retained pre-fix DLL exits3 on logical prefix publication, before accepting a success result. E18/red-baseline.json/run log. This is the intended old-build failure; E17 separately retains real native divergence readbacks. The new gated assertion failure is not itself a GPU retirement proof. |
| MSC consumers | LOCAL_PASS14 positive processes,154 phases,4358144DWORDs, same/cross queue, owned copy, omitted private event, delayed callback, explicit event and ungated paths. One expected wrong-oracle exit1. Closed-list remap replay and stable VA/descriptors remain covered. E18/msc-delivery.log; identical existing DXIL fixtures, no shader changes. |
| Nearby regressions | LOCAL_PASS five processes: public reserved-buffer MSC311296DWORD fixture, capacity, removal, packed mapping/copy/full-mips and actual D3D11 copy256DWORDs with loaded-code identity. E18/nearby-delivery.log. |
| Lifecycle / parser / failure edges | LOCAL_PASS14 processes: partial issue, public-ref release, capacity/removal, zero-issue rejection, empty/SKIP, delay/event, expected wrong-word exit1, low-level normal duplicate/range/SKIP, partial and self cases, two native-preparation failures. E18/lifecycle-delivery.log. |
| Final selected audit | PASS52 processes, two expected exit1 oracle controls;50 normal/controlled success exits. The separate baseline sensitivity process is excluded from52.242 actual successful terminal callbacks and244 FIFO completions across selected logs; sealed zero-issue outcomes explain differing counts. Actual PE export bytes/path and Unix UUID8963C1DC-2CEA-3397-AEC6-0AC846C82683 match the paired final build/stage. API/GPU validation enabled throughout, without unexpected faults. Exact labels/hashes in E18/delivery-audit.json. |
| Diff / compatibility | PASS scoped clang-format and git diff --check. Only buffer implementation, existing probe and two existing reports change. No queue/header/WineMetal/MSC/feature code, HLSL, Meson or vendored edits. Original .gitignore excluded. |
| Other proofs | NOT_RUN Windows native/Mullion runtime, forced Metal3, other GPUs, full32bit runtime, true concurrent cross-queue mapping, genuine GPU fault/quiescence, GPU trace and performance A/B. No capability increase or no-regression/speedup claim. |

Reproduce a new mode in a matched isolated stage: dx12_reserved_buffer.exe plan-queue-generation 0x77321abc; repeat with0x11223344 and suffixes -aba/-self/-capacity/-failure/-failure-late/-null/-mapped/-lifetime. Existing MSC invocation remains dx12_reserved_buffer.exe reserved_buffer.probe_read.cs.cso reserved_buffer.probe_write.cs.cso 13721 1 owned-cross. E18 external orchestration/per-process JSON retains the exact environment, DLL overrides, stage/prefix, isolated caches and validation flags. No runner is added to the repository.

Retained development failure: generation-lifetime-initial-failure.log reaches terminal ownership/destruction checks but fails its readback because the fixture initialized only logical third-page mapping, leaving that native page absent. The corrected fixture explicitly submits the initial destination native mapping before the intervening two-page remap; final two-seed runs compare all pages and guards. This is a real failed initial test, not counted as PASS or hidden by weakening its oracle. A later review preserves old captured heaps through retirement; all selected final suites rerun after that production change. Preliminary passes/hashes are retained under pre-late-injection and pre-retain-review and excluded from final totals.

### Cost / Capability Impact / Regression Risks

The ordinary legacy Update/Copy path does not call ResolveAdmission; its implementation, public capabilities and draw/dispatch hot paths are unchanged. Controlled preparation adds O(affected tiles) copying, one temporary resolved vector for copies, one temporary heap vector and paired resource locking. Capacity waits and native/GPU work remain outside resource locks. No whole-device scan, generation field, map, additional native command buffer, Fence wait or queue record is added. Previous captured refs are kept as well as current admission refs; repeated failed native preparation can accumulate redundant private refs until plan retirement/destruction. This bounded-by-attempts cost is explicit, not an optimization; unbounded adversarial retries and performance acceptance are not established.

No Tiled Resources Tier, Feature Level, Shader Model, MSC flag or runtime compatibility promotion. BACKEND_NOT_IMPLEMENTED: production owned-plan activation and texture plans; generalized cross-queue mapping ownership remains open. SPEC_UNVERIFIED: independently queued native source remaps versus CPU logical publication, real native error quiescence/void-call attribution and the retained earlier scheduling anomaly. MSC_SEMANTIC_GAP: none newly shown by the rerun compute matrix, which does not prove every shader stage. TARGET_DEVICE_NOT_AVAILABLE: Windows/other GPU comparisons only, never Apple7 placement-sparse hardware. The controlled entry still requires serialized resource mapping preparation/use; do not widen its concurrency contract from these tests.

### Git State / Rollback / Next Recommended Slice

Commit the buffer fix, existing probe and two report updates with fix(d3d12); no push. Preserve original unstaged .gitignore and no repository untracked files. E18/delivery-manifest.json records final SHA/status and source/evidence hashes. Revert the focused commit to restore prior controlled behavior; keep E17's failure evidence and use matched-build fixtures.

Next single recommended slice: **audit and design cross-queue source mapping ownership and submission order**, with one producer mapping fence handoff and a later source remap around a queued copy. Determine what the existing authoritative resource/queue owners can guarantee without a second mapping state map; preserve public activation until that contract and real-error retirement are satisfied. This round stops after delivery. Gate0/1 evidence/design refreshed; Gate2 scoped implementation complete; Gate3 stated checks pass with explicit limits; Gate4 owner/identity/diff/capability audit passes. Overall goal remains active.

## Cross-queue mapping publication and native copy — N15 audit, 2026-11-05

### Task Summary / Baseline Evidence

This round is a read-only production-source audit and Design Card, with an external diagnostic derived from the existing reserved-buffer probe. It proves that the previous same-owner admission fix does not establish cross-queue mapping semantics. Only this report and its checklist link change in the repository. Production source, headers, tests, shaders and build configuration remain HEAD-identical. Overall sparse/MSC development remains active.

Baseline0119f90ae87471365d266acf1f78ef8229f71d52, feat/d3d12; cached origin ahead13/behind0, target remote not refreshed. Starting status only original unstaged .gitignore, SHA256764aac09922db4654ca6de63fd20cf014739d9292623eb979f448f38e609b759. Apple M1 Max, macOS27.2/26B123, Xcode27.2/27B52, Wine11.0. Actual Apple7/Metal4/PlacementSparse true, Apple8/9 false. No Apple7 hardware blocker. E19 = /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-crossqueue-audit-20261105. baseline.json, source-audit.json, sources.json, probe-build.json and audit.json retain identities, commands, normative sources and classified results. CodeGraph preceded targeted code reads. No relevant memory-registry result was used.

### Findings / Source Evidence

| Evidence type | Current file / lines | Finding and scope |
|---|---|---|
| SOURCE_AUDIT | src/d3d12/d3d12_command_queue.cpp:78–114,823–836 | Each D3D12 queue owns a different classic queue, sparse queue, event/value and commit mutex. A per-queue lock serializes its host calls, not cross-queue GPU execution. There is no device-wide mapping ordering owner. |
| SOURCE_AUDIT | Same file:1859–1898; src/d3d12/d3d12_fence.cpp:216–252 | Wait commits a latched shared-event wait, then returns. A later host mapping can immediately publish. Fence registration protects wait lifetime/value semantics, but does not postpone CPU mapping resolution behind the wait. |
| SOURCE_AUDIT | src/d3d12/d3d12_buffer.cpp:178–223,227–259,275–283; queue:728–790 | ResolveAdmission refreshes the resource-owned tile table under paired locks. Native copy is issued immediately and accepted Finish publishes immediately, even behind an unresolved GPU Wait. An admitted future remap can contaminate an earlier copy; a later-admitted producer can leave its saved logical result stale. This is ordering, not a C++ container data race. |
| SOURCE_AUDIT | queue:469–549,1813–1855 | Existing bounded FIFO/completion waits for actual command-buffer/native terminal completion before signaling public fences. This owner can observe completed predecessor waits; current admission does not defer mapping resolution/issue until then. |
| SOURCE_AUDIT | queue:889–949; buffer:296–350,950–956 | Public mappings also issue/publish under only their queue's mutex. This is a related source lead, but the experiment uses the opt-in controlled entry, not a newly proven public-API conformance failure. The ordinary legacy path is not established fixed. |
| SOURCE_AUDIT | src/winemetal/unix/winemetal_unix.c:4498–4551,4611–4628 | Native operations retain source/destination objects, not immutable mapping versions, then issue live copyBufferMappingsFromBuffer. Heap retention/CPU locks cannot determine its source sampling point behind another queue's unresolved event. |

### Specification / Mullion Comparison

SPEC_REQUIRED: freshly retrieved Microsoft ID3D12CommandQueue::Wait queues a GPU wait and returns immediately. Multi-engine synchronization defines application-visible queue timelines ordered through fences. CopyTileMappings copies source mappings, with temporary-snapshot semantics for self overlap. Those contracts do not permit replacing execution order with the CPU's latest global map. Tests use TILE_MAPPING_FLAG_NONE; current flags documentation labels NO_HAZARD unsupported. E19/sources.json retains official URLs/hashes, with normalized ms-*.txt. Windows native NOT_RUN; the combined interpretation is not presented as a native Windows observation.

APPLE_SDK_SOURCE: installed MTL4CommandQueue.h:261–275 describes GPU event ordering;410–430 describes buffer mapping copy and page-size/alias obligations. These comments do not explicitly specify source-map sampling relative to another queue's host submissions. The observations establish local bridge/runtime behavior, not an Apple specification violation or universal driver rule. Exact native cause remains SPEC_UNVERIFIED.

REFERENCE_SOURCE: read-only git ls-remote confirms Mullion HEAD/main93de32b9145885ceb2e45be7d445e56d62499b3c this round. Its queue source freshly fetched at that SHA has SHA256641a21ad8fce6aa6f3f6f0cb02e0b0e3ae162a97a0eb7740d738c5f70ece6492. Lines469–510 Hold/CommitHeld retain work behind gates;593–625 Map invokes Settle before mappings;1080–1092 Settle ends preceding work, waits for the gate and actual completion;1392–1404 couple native copy and bookkeeping in that held closure. Wait2373–2422 and Signal2349–2370 establish gates; Commit1450–1456 explicitly reads sparse mappings past a wait. This identifies a relevant scheduling boundary missing from DXMT, not proof Mullion passes this experiment. Mullion runtime NOT_RUN; its whole held-work/fence scheduler is not transplanted.

### Experiment / Validation Results

E19/build_probe.py derives crossqueue_probe.cpp from the unchanged HEAD probe, adds a diagnostic branch/main dispatch and links current import libraries. No DLL is patched. Two Direct queues use three-page source/target resources; copy first two pages. A/B are different heaps initialized with seed- and page-dependent full contents. One real shared-event gate holds native execution until host submission completes. All work finishes before classic blit readback; target third page and32 guard words remain checked. No concurrent host calls or intentionally unordered GPU mapping accesses are required.

Let F be producer-ready and G copy-complete, each monotonic1 and with one signaling queue:

| Mode / CPU submission order | Required GPU order / actual result | Classification |
|---|---|---|
| ordered, two seeds: Q0 map(source,A), Signal(F); Q1 map(target,B), Wait(F), Copy, Signal(G); Q0 Wait(G), map(source,B) | A → Copy(A) → B. Logical/native targetA, final sourceB.98304DWORDs and guards match. | LOCAL_PASS control |
| completed, two seeds: Q0 mapA thenB, Signal(producerDone); CPU drains producerDone before Q1 Copy | Copy(B), logical/native targetB and final sourceB. All words/guards match. A completed-predecessor control, not an asynchronous fix. | LOCAL_PASS control |
| future, two seeds: Q0 mapA, Signal(F), Wait(G), mapB; then Q1 Wait(F), Copy, Signal(G) | Fence order requires A → Copy(A) → B. Both logical/native target are B:32768 copied DWORDs fail the queue-order oracle; other data/guards match. Future host admission affects the copy despite GPU Wait(G). | LOCAL_FAIL native queue-order oracle, exit2. Logical/native agreement alone hides it. |
| forward, two seeds: Q0 mapA, no Signal(F); Q1 Wait(F), Copy, Signal(G); Q0 mapB, Signal(F), Signal(producerDone) | B → Signal(F) → Copy(B). Native targetB obeys GPU order; saved logical targetA disagrees in all32768 copied DWORDs. | LOCAL_FAIL logical/native equality; executable exits0 because native oracle passes, audit still classifies FAIL. |
| wrong, one seed: ordered with one deliberately flipped expected word | Exactly one mismatch, guards intact, drained queues and five unique COM sentinel destructions. | Expected negative-control exit1, not implementation failure. |

Final diagnostic batch: nine processes,884736DWORDs,36 actual successful native callbacks and36 FIFO completions. Four passing controls, two native queue-order failures, two logical-equality failures, one negative control. Additional unchanged HEAD plan-queue-generation passes98304DWORDs and3 callbacks/FIFO records. Combined ten selected processes:983040DWORDs,39 actual successful callbacks/FIFO completions. Both semantic failure classes remain open; exit0 alone never proves correctness.

| Check | Result / limits |
|---|---|
| External probe build | PASS x64 compile/link using existing release/LLVM-MinGW configuration; exact commands in probe-build.json. No full production rebuild because source/build inputs are unchanged. |
| Runtime identity | PASS actual PE export bytes/path, build/stage byte pairing, Unix UUID8963C1DC-2CEA-3397-AEC6-0AC846C82683, final probe hash and source preservation. Separate cloned stage/prefix/per-case caches; shared Wine/prefix untouched. |
| Validation / lifetime | API/GPU Validation enabled without unexpected faults. Final diagnostics drain both queues and destroy five unique sentinels each. All39 native callbacks report actual success; this does not prove mapping correctness or real-fault quiescence. |
| Same-owner correction | LOCAL_PASS unchanged HEAD regression. Its bounded claim is preserved; cross-queue semantics remain unimplemented. |
| Preservation / diff | PASS src/include/tests HEAD-identical, .gitignore hash preserved, only two existing reports intended for commit. |
| Broader proofs | NOT_RUN Windows native/Mullion runtime, other GPUs, forced Metal3, full32bit runtime, MSC dispatch in this new sequence, genuine GPU fault/quiescence, timeline capture and performance A/B. Earlier MSC results remain historical bounded evidence. |

Retained investigation failures: compile-initial-failure.log records Event instead of SharedEvent in an external cleanup guard; initial-launch-failure.log records missing diagnostic main dispatch. Neither reached GPU work. first-native-failure-* and future-diag retain the unexpected queue-order failure before diagnostic expansion; failed assertions are not counted as passing controls. matrix.log retains a collector expecting forward to exit2, although its native result correctly passed and its logical failure remained visible. Corrected summary.json/audit.json separate both oracles; diagnostic logs and the initial collector output are retained. No process was restarted solely for observation timeout.

### Design Card — Deferred Controlled Mapping Issue Behind Existing FIFO Predecessors

- **Goal / Baseline / Evidence.** Close both demonstrated cross-queue gaps on HEAD0119f90a using E19's forward/future oracles. Preserve those modes as regressions in the existing probe, then make controlled submissions satisfy queue order AND logical/native equality.
- **Scope / Non-Scope.** Existing Submission, controlled sparse admission, CompletionThread and buffer-owned plan only. No new src/d3d12 files, global scheduler, mapping history table, ABI or shader backend. Public owned-plan activation, textures, mixed legacy/controlled mutations on the same resources and capability promotion remain outside this gate.
- **Architecture / Boundary.** tile_mappings_ remains sole resolved logical authority; plan is transient owned intent; existing bounded Submission ring is sole queue ordering owner. Deferred records own unissued plans, not a second tile map or unbounded deque. Native/COM terminal retirement remains in that FIFO.
- **Proposed Changes.** Reserve capacity and existing odd/even sparse handoff values at controlled admission. Commit the classic pre-handoff command buffer in normal queue order and retain the plan in its FIFO record; following classic work waits on the reserved mapping-complete value. Defer ResolveAdmission, native sparse wait/issue/seal and mapping-complete signal until CompletionThread reaches that record after prior command buffers/latched waits complete. Do not insert a late classic command buffer behind a later blocked one, or signal mapping-complete before issue. All controlled mutations must use this boundary: retaining eager future updates preserves the defect.
- **Errors / Ownership.** Separate queue acceptance from native success. Deferred PrepareNative OOM cannot be returned synchronously to the original caller. Validate structure before transfer; retain accepted intent through deferred OOM/removal and actual prefix terminal proof. Attribute asynchronous failure at the existing queue/device owner; use established removal/cancellation rules to release dependencies without publishing success. Exact error/prefix cleanup must be verified before declaring the implementation complete. Cancellation never proves an issued plan can die.
- **Locks / Liveness.** No GPU waits under commit_mutex_, submission mutex, resource locks or fence mutex. Completion must not reacquire an admission lock held by a capacity-blocked producer. Prove Q0 Wait(G)/Q1 Wait(F) progresses with the two existing completion threads and no self-dependent even-event wait. Resource locks cover snapshot/publication only. Stop and revise the card if this requires another scheduler or duplicated authority.
- **Tests / Definition of Done.** Two seeds each ordered/completed/forward/future must satisfy both oracles, full pages/guards, actual loaded identities and terminal destruction. Preserve capacity/removal/zero-issue/partial/retry/early-release and existing MSC same/cross consumers. Inject failure before native issue and after a real prefix; retain ownership and disallow later success. Extend NULL/ABA/self only where mechanism changes them. CPU-drained controls/global waits do not satisfy the asynchronous sequence. Document missing Windows/real-fault evidence.
- **Costs / Risks / Alternatives.** CPU-observed predecessor completion delays mapping issue and may reduce overlap; measure issue/admission latency, pending capacity, CPU time and end-to-end throughput before production activation. No present performance claim. Global mutexes cannot resolve future admission order and can deadlock dependencies; generation/CAS maps or stale explicit-heap copies cannot fix forward order; blocking public Wait violates its asynchronous contract. Mullion's scheduler is rejected as a replacement.
- **Rollback.** Keep controlled opt-in; revert its focused implementation commit without changing public dispatch/capabilities. Preserve E19 red evidence. No implementation occurs in this audit round.

### Capability Impact / Remaining Blockers / Git State

BACKEND_NOT_IMPLEMENTED: CPU mapping resolution/issue behind cross-queue predecessors; public owned-plan activation, texture plans and mixed-path ownership. SPEC_UNVERIFIED: Apple's mapping-sampling point, Windows equivalence, true error quiescence/void-call attribution and earlier retained scheduling anomaly. MSC_SEMANTIC_GAP: none newly demonstrated; native blit here is not a new MSC shader proof. TARGET_DEVICE_NOT_AVAILABLE: Windows/other-GPU comparisons only. Apple7 placement sparse/64KiB compatibility are not blockers. Feature Level/Tiled Tier/SM and Metal3/D3D11/MSC compatibility policies unchanged.

Changed files: this report captures failures/evidence/Design Card; docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md adds the bounded-status link. Commit docs(d3d12), no push. Original .gitignore stays unstaged, no new repository files. E19/delivery-manifest.json records final SHA/status, source/evidence hashes and no-push state. Documentation rollback is a focused revert.

Next single recommended slice: **implement and regress deferred controlled mapping issue through the existing bounded FIFO**, closing forward/future without public capability promotion. Gate0/1 complete with actual negative evidence; Gate2 production implementation NOT_RUN under audit/design scope; Gate3 diagnostic execution/preservation complete with four explicit semantic failures; Gate4 report/identity/scope audit complete. Overall objective stays active.

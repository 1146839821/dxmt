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

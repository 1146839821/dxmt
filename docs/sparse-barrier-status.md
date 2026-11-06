# N15 — checked ResourceState barrier submission

2026-11-04. Baseline feat/d3d12 `e0b485b059ea3451a711a713cb2ab3d485662c19`, only the user's existing .gitignore edit. One synchronous barrier-result slice, not complete sparse transaction handling.

## Gate0 / evidence

BeginSparseMapping submits main signal and mapping wait, then calls a void barrier bridge. Unix reports STATUS_UNSUCCESSFUL for missing command buffer/allocator/encoder, but release PE discards it and debug PE asserts. UpdateTileMappings/CopyTileMappings subsequently call resource methods, so CPU mapping publication is not prevented.

The queue already closes event pairs on resource validation failure and has terminal submission-object failure policy via MarkDeviceRemoved(DXGI_ERROR_DEVICE_REMOVED). Its removal listeners wake sparse events and fence waits. We reuse these owners; no scheduler/state map is added.

`SPEC_REQUIRED`: [Apple sparse mapping operation](https://developer.apple.com/documentation/metal/mtl4updatesparsetexturemappingoperation?language=objc) requires ResourceState synchronization; SDK MTL4CommandQueue.h documents this. [Microsoft fence GetCompletedValue](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue) returns UINT64_MAX on removal. The generic removed reason is DXMT's existing loss-of-ordered-work policy, not a claim that a nil factory specifically proves OOM.

Mullion pinned `93de32b9145885ceb2e45be7d445e56d62499b3c` uses a different Map/Settle/event schedule (command_queue.cpp:587–655); it does not supply a drop-in implementation of this WineMetal barrier-result contract. No implementation is copied. Windows native/Mullion runtime and other target devices are NOT_RUN. M1 Max/macOS27.2/Xcode27.2 are available for real tests. Remote checks and environment evidence are retained externally (origin check may fail transient TLS; no fetch/rewrite).

## Gate1 / Design Card

- **Goal:** synchronously failed ResourceState barrier preparation must return through WineMetal and prevent resource mapping calls/publication.
- **Scope:** append checked PE entry/wrapper, reuse existing Unix call id/packet, handle false in BeginSparseMapping, opt-in regression in reserved-buffer fixture.
- **Non-Scope:** update/copy result ABI, partial mapping rollback, asynchronous Metal4 feedback, current command-buffer/allocator release contract, Tier/FL/SM, scheduler/resource backend replacement.
- **Boundary:** Unix owns native barrier preparation/submission status; PE translates status to uint32 boolean without the debug assert macro; queue owns handoff and terminal error; resource owner remains unchanged.
- **Changes:** add SparseMappingQueue_tryBarrierBeforeResourceState; preserve original void export and its behavior. Zero Unix status means success; new export returns1 on success,0 otherwise. WMT uses bool. If false, enqueue EndSparseMapping before MarkDeviceRemoved, then return false before either resource Update/Copy method. Future drawing must not silently continue using old mappings.
- **Test plan:** Unix-only opt-in exact phase `DXMT_TEST_SPARSE_BARRIER_FAILURE=command_buffer|allocator|encoder|submitted`, with `DXMT_TEST_SPARSE_BARRIER_FAIL_AT=1|2`. Atomic test call counter is touched only for a valid selected phase. Skip the chosen nullable factory/encoder to simulate preparation failure; submitted reports failure only after a real valid barrier commit, for a safe old-behavior control. Do not alter real device capabilities. Test public update and copy, first handoff or after a completed prior mapping. Require removed reason, pending fence event wake, UINT64_MAX and Signal refusal; bounded process watchdog covers queue teardown. Normal MSC full-page, replay and queue-admission tests still run with paired PE/Unix runtime and API/GPU validation. Instrumented control ignores false only for submitted-phase tests, so every resource mapping still has a real barrier.
- **Risks:** any synchronous barrier failure becomes terminal because void D3D12 mapping calls cannot report a dropped command. Device-removal cancellation may CPU-signal sparse events as in the existing policy; EndSparseMapping is queued first. No inference of native object retention safety or async submission success. New caller requires matched PE deployment; old ABI entries/Unix tables stay intact.
- **Rollback:** revert this focused bridge/queue/test/report commit; preserve isolated runtime and evidence. Do not alter old packet layouts.
- **DoD:** negative preparation cases observable without stranded waits, old ignore-status control rejected safely, normal GPU regression and full build pass, actual runtime identities checked, no capability promotion, commit without push.

The user's “继续下一项” authorizes this slice. Gate0/1 completed before implementation. Gate2–4 results follow after execution.

## Gate2–4 / delivery results

Evidence directory (**E**): `/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-barrier-status-20261104`. It contains commands, source variants, logs, artifact hashes, audit.json and the post-commit delivery-manifest.json. All stages/prefixes/caches are separate from the user's Wine deployment.

Changed files and responsibilities:

- `src/winemetal/winemetal.h`: additive uint32 checked export; original void signature preserved.
- `src/winemetal/winemetal_thunks.c`: checked entry calls WINE_UNIX_CALL directly and translates zero Unix status to 1. It does not pass expected failure through the debug assert macro.
- `src/winemetal/Metal.hpp`: bool wrapper for the new entry; old wrapper remains.
- `src/winemetal/unix/winemetal_unix.c`: opt-in failure phases at existing failure paths, plus submitted-phase control. Normal barrier encoding/commit/release remains unchanged. No new Unix call id, packet or table entry.
- `src/d3d12/d3d12_command_queue.cpp`: check result, queue EndSparseMapping, report terminal removal, return false. Both public mapping callers already return before invoking resource methods when BeginSparseMapping fails. Thus preventing CPU table publication here is a **source-level control-flow conclusion**, not a GPU read after device removal. Previously successful mappings are not rolled back.
- `tests/dx12/dx12_reserved_buffer.cpp`: share runtime identity logging (including checked export), add process-isolated barrier-update/barrier-copy probes, pending fence and later submission assertions. On unexpected healthy-device control failure it first signals/waits to drain valid mapping work before releasing resources.
- This report: independent design card, contract and scoped validation.

| Validation | Result | Evidence |
|---|---|---|
| Release fault matrix | LOCAL_PASS:4 phases ×2 public APIs ×2 handoff positions =16 processes | E/dxmt-{phase}-{update,copy}-{1,2}.log; results.json |
| Native preparation failures | command_buffer/allocator/encoder nil cases return failure through Unix→PE→WMT→D3D12; device reason, pending event wake/UINT64_MAX and later Signal rejection pass | 12 release cases; no mapping resource method entered after the failed Begin |
| Later handoff | First mapping completes while healthy; failure on call2 is observable and teardown completes | Eight release cases |
| Normal GPU data | LOCAL_PASS:3 processes ×16 rounds plus 2 queue-admission rounds, default and ignored-hook rounds =52 rounds/replays; 16,187,392 word comparisons | E/dxmt-normal{1,2,3}.log, queue-admission/default/invalid-hook logs |
| Prior admission regression | LOCAL_PASS:36 missing queue/event creation cases and successful retry/fence completions | E/dxmt-queue-admission.log |
| Safe ignore-result controls | Expected exit1 for update/copy at calls1/2; all four controls rejected | E/control-submitted-*.log; variants.json |
| Debug-asserting PE thunk | LOCAL_PASS:four fault phases and one full GPU round/replay; 311,296 word comparisons; no assertion | E/debug-fixed-*.log; debug-fixed-results.json |
| Wrong CPU oracle | Expected exit1; full-page mismatch detected | E/dxmt-wrong.log |
| D3D11 adjacent fixture | LOCAL_PASS:legacy SHEX/SHDR creation and DXIL/no-executable/hybrid rejection; no draw/readback claim | E/d3d11-boundary-with-compiler.log |
| Metal validation | API/GPU validation enabled before device creation; no graphics-validation errors observed in delivery runs | Complete logs, audit.json |
| Runtime identity | Actual PE paths and memory export prefixes; Unix image UUID 05F7D264-FADA-37FC-9868-4D56E62F9A8C and staged SHA; matching build/PE/Unix pairing | E/dxmt-artifacts.json, audit.json |
| Build | PASS:full x64 release default target, plus external debug-thunk and control variant builds | E/full-build.log, delivery-build-final.log, variant build records |
| Static/preservation | PASS:diff check, scoped formatting, unchanged Unix packets and both dispatch tables, user's .gitignore hash unchanged | E/audit.json, delivery-manifest.json |
| Windows native, Mullion runtime, x86/WOW64, real no-placement device, older SDK/OS, full debug build | NOT_RUN | No synthetic capability coverage |

The debug variant compiles only winemetal_thunks.c with -UNDEBUG; other objects and the Unix library remain release. Its binary contains the assertion message while release does not. This is not represented as a full debug DXMT build. Across release and that debug variant, **20 fault cases** pass and **53 positive buffer rounds/replays compare 16,498,688 words**. Four native submitted-phase tests plus one debug submitted-phase test report a deliberately false status after a real barrier commit; they do not represent actual native allocation failures.

Controls change only the queue's check/removal branch to ignore the checked result, retaining the same bridge and test code. They use submitted-phase injection exclusively, so the underlying barrier really commits and subsequent mappings remain synchronized. The test drains the healthy queue before reporting the missing-removal failure. These are instrumented controls, not historical unmodified binaries; no raw nil-barrier fault is run against the ignore-result control.

Runtime environment rechecked: Apple M1 Max/macOS27.2 `26B123`, Xcode27.2 `27B52`, Wine11.0, actual Metal4/PlacementSparse/Apple7 queries true, page64=65536. MSC compiled SDK API4.0.3; runtime product version unknown; actual loaded dylib SHA remains `cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`. No converter policy/cache ABI changes.

Reproduction: E/run.py stages matching candidate files and runs the release matrix/regressions. E/run_variants.py runs the externally built controls/variants; build commands are retained in the corresponding JSON records; E/run_debug_fixed.py uses the corrected same-basename debug artifact and existing isolated debug stage. Select fresh output directories/labels before repeating to preserve logs. CLI fault modes require rounds=1; the Unix fault counter is process-local. Build follows .github/build_dxmt.sh feature configuration plus tests, with the existing external patched LLVM build and docs/DEVELOPMENT.md private deployment rules. No installation into the user's Wine/CrossOver environment.

Retained setup failures: the first debug DLL was linked as debug-winemetal.dll, so Wine could not initialize its Unix library despite an on-disk rename. It never exercised the tested function. Relinking under the internal basename winemetal.dll and stamping it as builtin corrected the isolated variant. The first D3D11 launch lacked D3DCOMPILER_47.dll; a compiler already present in the user's existing external test stage was copied only into this new stage, hashed, and the fixture then passed. Both original logs are retained and excluded from pass counts. An audit assertion initially confused the native fault marker substring with the positive result marker; the predicate was narrowed, without rerunning or changing GPU results.

Safety/limits: EndSparseMapping is **enqueued** before terminal removal; removal listeners may CPU-cancel event waits as before. This is not proof of successful asynchronous execution of the failed barrier or of the final event signal completing on the GPU. Queue teardown and pending-fence wake complete in the bounded tests. API validation does not establish all Metal4 instrumentation coverage or the unchanged temporary allocator retention contract. Fault hook getenv checks add per-mapping CPU work; performance is NOT_MEASURED.

Public Feature Level, Shader Model and Tiled Tier remain unchanged. Existing Metal3 path, D3D11 backend boundary and MSC runtime policy remain intact. Only the focused files above are committed in repository style; no push; final SHA/state in E/delivery-manifest.json, with only the user's .gitignore remaining unstaged. Gate0/1 preceded implementation; Gate2/3 and delivery audit completed for this synchronous barrier-result slice.

Remaining `BACKEND_NOT_IMPLEMENTED`: mapping update/copy calls still discard Unix failures and can advance CPU tables after those failures. Partial native batches, asynchronous error propagation and allocator ownership proof remain separate. **Next single slice:** checked buffer UpdateTileMappings submission and CPU-table publication order, without combining self-copy/texture partial transaction handling or capability promotion.

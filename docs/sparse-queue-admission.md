# N15 — sparse queue admission failure

2026-11-04. One slice of the sparse mapping failure/result work; the complete result/transaction work remains open.

## Gate 0 / evidence

Baseline `feat/d3d12` at `a5f986e8ecb6523265429f48774623a2b0086c15`, only the user's `.gitignore` change. Current remote refs are checked in the external delivery evidence. M1 Max / macOS27.2 / Xcode27.2; the preceding buffer validation established real 64 KiB placement-sparse execution, without changing Tiled Tier.

`SOURCE_OBSERVED`: command_queue.cpp:638–646 clears mapping queue/event on either creation failure but continues returning success. Buffer/texture native allocation independently uses actual supportsPlacementSparse. BeginSparseMapping:578–580 then treats absent mapping queue as success, allowing CPU-only mapping publication. Native sparse resource plus a successful but mapping-incapable queue is an inconsistent mode, not a safe shadow fallback.

`SPEC_REQUIRED`: [Microsoft CreateCommandQueue](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommandqueue) defines creation failure through HRESULT, including E_OUTOFMEMORY. [Apple newMTL4CommandQueue](https://developer.apple.com/documentation/metal/mtldevice/makemtl4commandqueue%28%29?language=objc) can return nil. SDK MTLDevice.h:1404 confirms nullable return. The precise Metal nil cause is not queried by the current bridge; E_OUTOFMEMORY is this backend's allocation-failure convention, not proof of physical memory exhaustion.

`MULLION_OBSERVED`: pinned `93de32b9145885ceb2e45be7d445e56d62499b3c`, command_queue.cpp:1182–1208 fails queue initialization when required events are missing, rather than publishing a partially initialized execution path. Its queue scheduler is not transplanted. Windows native allocation failure and Mullion runtime are NOT_RUN.

## Gate 1 / Design Card

- **Goal:** on real placement-sparse-capable devices, never publish a D3D12 queue missing its mapping queue/event pair.
- **Scope:** Initialize failure admission and focused fault/recovery regression in the existing reserved-buffer fixture. Applies to both CreateCommandQueue and CreateCommandQueue1 through their shared factory.
- **Non-Scope:** WineMetal status ABI, already-started/partial mapping transactions, device removal policy, async Metal4 feedback, allocator ownership, public Tier/FL, other resource backends.
- **Owner/boundary:** queue Initialize owns queue/event prerequisites. Existing WMT References and queue destructor release partial objects and stop/join its completion thread. The device retains existing allocation and residency policies.
- **Proposed changes:** return E_OUTOFMEMORY if either required object is missing. Register mapping residency only after both exist. Preserve the no-placement path and normal handoff. Test-only exact-match `DXMT_TEST_SPARSE_QUEUE_INIT_FAILURE=queue|event|both` skips the selected allocation; it does not fake a capability or alter successful objects. No injection is consulted on the real unsupported path.
- **Test plan:** on the actual M1 Max, ensure a real reserved buffer has a nonzero VA; exercise direct/compute/copy queues, each missing-object case, and both public creation APIs. Require E_OUTOFMEMORY, cleared output pointer and healthy device. Unset the hook and retry; recovered queues must signal/complete a fence. Then retain the full MSC buffer remap/NULL/CopyTiles/replay oracle and stable VA. Run independent processes, default CLI and wrong-oracle controls under API/GPU validation with matched WineMetal deployment. Run a test with the old clear-and-continue failure branch restored in an isolated temporary build to prove the regression rejects the bug.
- **Risks:** queue allocation failure now becomes visible to applications; a test hook is opt-in and process-local. Nil has no detailed error cause at this bridge. Failing initialization must not strand the completion thread or poison the device. A real no-placement device is unavailable, so that runtime branch remains NOT_RUN.
- **Rollback:** revert only this focused queue/test/report commit; leave stage/prefix/cache evidence intact. No ABI change.
- **DoD:** failure/recovery matrix and GPU regression pass with correct identities; original failure behavior is rejected; full build/diff checks pass; remaining status/transaction gaps explicitly stay open. Commit in repository style, no push.

The user's “继续下一步” authorizes this incremental implementation. Gate0/1 completed before editing production. Validation and delivery results follow below after execution.

## Gate 2–4 / delivery

Implementation is limited to `src/d3d12/d3d12_command_queue.cpp:67` (exact-match test hook) and `:647` (required sparse queue/event admission). No shared WineMetal header, ABI, shader or resource implementation changes. Failed initialization returns through the existing factory, leaving device-layer `InitReturnPtr` output NULL; the queue destructor stops/joins the completion thread and releases partially constructed WMT References. Residency association is only done after the pair exists.

`tests/dx12/dx12_reserved_buffer.cpp:71` adds the opt-in `queue-init-failure` mode to the original CLI. With a real reserved buffer already created and its VA nonzero, it tests both creation APIs for DIRECT/COMPUTE/COPY and missing queue/event/both, restores the environment, checks device health, then recreates/signals/waits each queue. After these failures it runs the complete existing remap/NULL/full-page shader+CopyTiles/replay oracle with the resource/VA retained. Existing default and wrong-oracle modes remain available. Only this report is otherwise added.

Evidence root (**E**): `/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-sparse-queue-admission-20261104`.

Environment independently rechecked: Apple M1 Max, placement=true, Metal4=true, Apple7=true, page64=65536; macOS27.2 `26B123`, Xcode27.2 `27B52`. Wine11.0; MSC compiled SDK API4.0.3, runtime product version unknown. Actual loaded converter SHA256 remains `cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`. E/device-query.log, results.json, dxmt-artifacts.json and audit.json retain the exact data/identities.

| Check | Result | Evidence |
|---|---|---|
| Fault/recovery matrix | LOCAL_PASS: 3 processes ×16 rounds ×18 cases = **864 rejected creations and 864 successful retries/fence completions** | E/dxmt-fault{1,2,3}.log; every type/missing/API tuple occurs48 times |
| Resource/queue correctness after failures | LOCAL_PASS:48 full MSC buffer rounds and48 closed-list replays | Same fault logs; 311,296 shader/copy word comparisons per round |
| Default CLI and ignored hook value | LOCAL_PASS:2 additional rounds/replays; no injected failures | E/dxmt-default.log, dxmt-invalid-hook.log |
| Wrong oracle | Expected exit1 with CPU full-page mismatch; no validation fault | E/dxmt-wrong.log |
| Instrumented old behavior | Expected exit1: regression reports `missing sparse prerequisite was not rejected` | E/control-run.log, control-build.json, control-result.json |
| API/GPU validation | Enabled before device creation; no graphics-validation errors observed in delivery runs | Complete per-process logs |
| Identity and pairing | PASS: memory export prefixes, actual PE/Unix/MSC paths, staged hashes and build pairing | E/audit.json; independent control DLL hash recorded separately |
| x64 release full default build | PASS, script-equivalent config; isolated build with patched external LLVM; no deploy to user Wine | E/full-build.log |
| Scoped format/diff and preservation | PASS; user's original `.gitignore` retained | E/delivery-manifest.json |
| Windows/Mullion runtime, x86/WOW64, D3D11, real no-placement target | NOT_RUN; no capability values faked to obtain coverage | Source scopes and limitations above |

Total positive buffer checks are50 ×311,296 = **15,564,800** uint32 comparisons. The deliberately wrong expected value is not counted as a GPU failure.

The old-behavior control replaces only the new rejection block with the former clear-and-continue branch, while preserving the test hook and candidate code elsewhere. Its queue source, object and DLL are built outside the repository with the original compile/link commands, redirecting all outputs (including dependency/import library files) to E. It is an **instrumented control**, not an unmodified historical binary. A separate stage/prefix/cache holds it; the production source and candidate build files are not reverted or overwritten. It fails before any mapping operation, so it does not submit a knowingly invalid mapping workload.

The positive stage and prefix are `stage-sparse-queue-admission-20261104` / `prefix-sparse-queue-admission-20261104` under the common external capability-gate directory. Run E/run.py to reproduce, selecting fresh output labels before rerunning to preserve evidence. Build uses `.github/build_dxmt.sh` feature configuration plus tests, docs/DEVELOPMENT.md private matched-runtime deployment, and the existing `dxmt-metal272-identity-audit-20261104` build. All test environments, commands and hashes are in E. No existing prefix/cache or user Wine installation is replaced.

Gate0/1 completed before implementation; Gate2/3 and Gate4 passed for this admission slice. The commit contains exactly the queue, existing test and this report, follows repository history, and is not pushed. Final SHA/status are in E/delivery-manifest.json. Only the user's `.gitignore` remains unstaged.

Capability impact: no Feature Level/SM/Tier promotion, no advertised hardware change. On a capable device, failed sparse prerequisites now cause visible queue creation failure. No fallback is invented. The existing unsupported-device path and successful handoff are unchanged in source; a real unsupported device is unavailable for runtime verification. Nil cause remains opaque, and this test does not prove the absence of native allocation leaks.

**Remaining:** mapping update/copy/barrier Unix failures still cross void interfaces without a result; CPU tables can still advance after those failures. Partially issued mapping batches and asynchronous Metal4 errors remain separate transaction/device-error questions. This commit must not be reported as complete sparse failure propagation.

**Next single recommended slice:** checked ResourceState-barrier submission through WineMetal and its queue error path, including releasing the already-established event handoff and preventing resource mapping publication on synchronous barrier failure. Extend within the existing bridge/queue owners; do not add a scheduler or silently change old ABI entries. Design before implementing.

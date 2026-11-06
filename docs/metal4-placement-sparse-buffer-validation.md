# N15 — M1 Max 64 KiB Placement Sparse buffer validation

Date: 2026-11-04. Scope: the approved buffer readback Design Card; no production backend or capability changes. Gate 0/1 audit and card: `/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-metal4-sparse-reclassification-20261104/M1-Max-Metal4-Sparse-Audit-and-Design.md`, sections 3–11. User approved Gate 2/3 before these GPU runs.

## Baseline and evidence

- DXMT `feat/d3d12`, baseline `d8e16fe2eb34f71582c2fd7961713b4f1a0a049c`; live origin `d1e83d41db5abb86d03bdbde4b7254c54debac6f`.
- Mullion reference `93de32b9145885ceb2e45be7d445e56d62499b3c` (remote HEAD rechecked). Not executed; no implementation copied.
- Apple M1 Max, macOS 27.2 `26B123`, Xcode 27.2 `27B52`, Metal 32023.922, Wine 11.0.
- Real native queries: Apple7/Metal4/PlacementSparse true, Apple8/9 false; explicit page64 is 65,536 bytes. Created buffers report `MTLBufferSparseTier1`.
- MSC compiled SDK API 4.0.3; runtime product version unknown. Loaded dylib SHA256 `cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.
- The user's existing `.gitignore` edit is preserved; no existing prefix/cache/runtime was replaced. A separate stage, prefix and per-case cache are used.

Evidence directory (abbreviated **E** below):

`/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-metal4-sparse-validation-20261104`

E contains the native source/binary, invocation scripts, logs, results, artifact hashes and delivery audit. Native validation is deliberately an external probe, as specified in the approved card. Tests and reports in this commit can be reviewed independently of the production backend.

## Contract and decisions

`SPEC_REQUIRED`: D3D12 tile granularity is 64 KiB; Tier2 NULL reads return zero and writes are discarded. CopyTiles linear buffer offsets must be 64 KiB aligned. Apple sparse buffer tier1 defines unbacked reads/writes; cross-queue mapping ordering uses events and ResourceState synchronization. Official source snapshots and installed SDK anchors are retained in the Gate 0 report; the default 16 KiB sparse page is not substituted for an explicit 64 KiB page.

`MULLION_OBSERVED`: its mapping code uses a spare tile/Settle sequence after unmap and a different CPU/GPU submission schedule (reference command_queue.cpp:587–655,1291–1405). We test the observable unmap contract without copying that workaround or scheduler. A passing bounded buffer case does not invalidate all reasons for Mullion's workaround.

Native probe uses ordinary Metal compute/blit execution and a separate Metal4 mapping queue. It retains heaps, sparse buffers, staging buffers, allocators and command buffers until GPU completion. Every mapping is ordered main signal → mapping wait → ResourceState/ResourceAlias barrier → mapping → mapping signal → main wait. The asynchronous mode only waits on the CPU after the batch, with a 30-second completion watchdog; the serial diagnostic mode waits per phase. All command-buffer completion statuses are checked for the ordinary queue. Mapping APIs are void; the probe does not separately collect Metal4 commit feedback, so this is not a complete asynchronous error reporting test.

Four virtual 64 KiB pages and four physical pages carry seed/page/word-dependent contents, including a mapped zero at each page midpoint. Nine read phases cover A/B, remap to B, back to A, all NULL, NULL writes, remap after NULL writes, both guard pages, mixed NULL/mapped writes, and final restoration. Each phase has separate shader and blit output slices. Both paths compare **every uint32** against the CPU model. Stable GPUVA and actual tier are logged per round. Negative control changes one CPU expected bit; both shader and copy comparisons detect that same wrong bit.

DXMT retains its existing two-page reserved-buffer fixture and descriptors created before mapping. `Pattern` at tests/dx12/dx12_reserved_buffer.cpp:71 provides nonuniform physical-page data and mapped zero. The read shader now checks all 16,384 words, adding one to distinguish execution from an unwritten zero output; the CPU model applies the same arithmetic. CopyTiles independently reads the complete mapped page at an aligned offset. Mapped UAV writes touch the first/last words; all other words must remain intact. The first closed read list is replayed **after its initial output has been checked**, with its allocator alive and a separate completion fence, and both full-page results are checked again. Existing delayed direct→compute handoff and heap public-refcount checks remain.

No sparse shader status is inferred from a value of zero. This test checks values, not residency-status ABI.

## Changed files

- `tests/dx12/dx12_reserved_buffer.cpp`: seeded full-page oracle, aligned copy readback, closed-list replay, runtime PE identity evidence, bounded seed/round arguments and wrong-oracle control. Windows native can omit WineMetal; DXMT runner requires it.
- `tests/dx12/reserved_buffer.hlsl`: 64-thread `probe_read`, dispatched across one complete 64 KiB page. `main` and mapped-write entry points remain compatible with their existing behavior. Probe shader and executable must be rebuilt together.
- This report: scoped evidence, limits and reproduction.

No `src/` implementation or WineMetal ABI changes. No new runner, scheduler, global mapping table, production workaround or default MSC flag.

## Validation results

| Check | Result | Evidence |
|---|---|---|
| Native asynchronous | LOCAL_PASS: 3 processes × 16 rounds × 9 phases; 56,623,104 shader/copy word comparisons | E/native-final-batch{1,2,3}.log, native-final-results.json |
| Native serial diagnostic | LOCAL_PASS: 16 rounds, 144 phases, 18,874,368 word comparisons | E/native-final-serial.log |
| Native wrong oracle | Expected exit 1; one wrong expected bit produces two comparison errors | E/native-final-wrong.log |
| DXMT MSC seeded | LOCAL_PASS: 3 processes × 16 rounds, 528 phases plus 48 replays; 14,942,208 word comparisons | E/dxmt-final-seed{1,2,3}.log, dxmt-final-results.json |
| DXMT original CLI | LOCAL_PASS: default seed0, 11 phases plus replay; 311,296 comparisons | E/dxmt-final-default.log |
| DXMT wrong oracle | Expected exit1 with a full-page mismatch; no production failure injected | E/dxmt-final-wrong.log |
| Validation | API and GPU validation enabled in all delivery processes; no validation diagnostics observed | Headers and complete logs in E |
| Loaded artifacts | Build/stage PE bytes paired (documented Wine builtin-marker adjustment only); memory export prefixes, actual PE paths, Unix path and MSC path checked | E/dxmt-artifacts.json and audit.json |
| Release x64 default-target build | PASS, script-equivalent existing isolated configuration, patched external LLVM | E/delivery-build.log |
| Scope/static checks | PASS: diff check and bounded source changes | E/delivery-manifest.json |
| Windows native, Mullion runtime, x86/WOW64, D3D11 | NOT_RUN; no shared production code changed | No result extrapolated |
| Texture/packed/3D/status/LOD/other formats/placed alias/failure injection/performance | NOT_RUN or existing separate gaps | Not covered by this buffer result |

Build follows `.github/build_dxmt.sh` feature configuration and docs/DEVELOPMENT.md private Wine deployment rules, using the existing isolated release build and external patched LLVM. Full script deployment to the user's Wine environment is intentionally not performed.

Reproduction (absolute paths to the retained artifact scripts):

```sh
# Native script sets API/GPU validation before device creation.
python3 /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-metal4-sparse-validation-20261104/run_native.py
# DXMT script stages paired files and uses its own Wine prefix/cache.
# Preserve prior evidence by choosing new labels/output files before rerunning.
PYTHONDONTWRITEBYTECODE=1 python3 /Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-metal4-sparse-validation-20261104/run_dxmt.py
```

The raw application CLI remains `dx12_reserved_buffer.exe <probe_read.cso> <probe_write.cso>`; optional `seed rounds [wrong-oracle]` enables repetitions and a deterministic expected failure. Maximum rounds is 64. Fixture targets are `dx12_reserved_buffer_probe_read_fixture` and `dx12_reserved_buffer_probe_write_fixture`, SM6.0 DXIL; the native probe uses MSL and does not load MSC.

## Retained failures and limits

Initial expanded DXMT test used a 256-byte CopyTiles readback offset. DXMT rejected it at recording/Close; this was a test-authoring error, not a sparse hardware failure. It was corrected to offset 65,536 before delivery. Original log E/dxmt-first.log is retained. The corrected preliminary run passed.

An intermediate native negative log interleaved buffered stdout and stderr, splitting a SUBMIT marker and failing the log counter after the intended data mismatch. Explicit stdout flush fixed the collector evidence; the final source/binary and all five final runs agree. Intermediate logs are not counted as delivery passes.

`LOCAL_PASS` now replaces the unverified buffer map/remap/NULL data contract **only for these paths/configurations**. There is no evidence here for full D3D12 Tiled Tier2/3. Production still has the statically identified sparse queue initialization/error-propagation gap; the native probe's conservative retention does not prove the production temporary allocator release contract. DXMT's per-phase CPU completion remains a coverage limit for asynchronous mapping stress, despite the native asynchronous batch passing. Metal validation being enabled does not prove every Metal4 API has equivalent instrumentation coverage.

## Gate 4, capability and next slice

Gate2/3: approved scoped validation completed; Gate4: source/results/identity checks completed. No public Feature Level, Shader Model, Tiled Tier or D3D11 behavior change. This is a validation result and regression fixture improvement, not capability promotion or a performance claim.

The focused test/report commit follows repository history; no push. Delivery SHA and final Git state are recorded in E/delivery-manifest.json after commit. Only the user's original `.gitignore` remains unstaged.

Next recommended single slice: a Design Card for sparse mapping failure/result handling through the existing WineMetal→queue boundary, so queue/event/allocation failures cannot publish CPU-only mappings as success. Do not automatically implement it, or bundle texture/packed/status work into this verification slice. The broader MSC/HLSL/performance objective remains incomplete.

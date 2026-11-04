# MSC Compute-to-Render dependency contract

The N13 dependency audit is complete. Keep the current Vertex/PreRaster fence
wait. Current residency records cannot prove that moving it to Fragment is safe.
The producer-to-vertex regression now covers direct, transitive and split-list
dependencies; its results are recorded below. This report does not implement a
scheduling optimization or establish a performance gain.

## Design card — 2026-11-04

1. **Goal:** Define the proof required to let ordinary MSC vertex work overlap earlier compute work.
2. **Baseline:** `feat/d3d12`, `cf2af83760ce26cede7534031d9ae52218e6854b`; macOS/Xcode 27.2, Metal 32023.922, Apple M1 Max.
3. **Current path:** Recorded commands → admission/state validation → live descriptor resolution → allocator marks → queue translation → completion references.
4. **Invariants:** Preserve D3D12 execution/cache/layout obligations, all RAW/WAR/WAW dependencies, aliases, and recording/submission lifetimes.
5. **Evidence:** Source/specification tables below; earlier paired traces establish zero observed overlap, not that an alternative schedule is safe.
6. **Scope:** N13 design, with N12 barrier contracts as inputs; source, test and reference audit only.
7. **Non-scope:** Production fence changes, new scheduler/state map, Metal 4 migration, backend replacement or capability promotion.
8. **Options:** An immediate-resource intersection is incomplete; Mullion's grouped fences require unrelated architecture. Retain the existing wait and build dependency tests first.
9. **Boundary / proposed follow-up:** Command list owns immutable command/barrier facts; existing queue owns the per-submission decision after live resolution. Unknown facts select the current wait.
10. **Files / cost:** This report and the MSC work log only. A future decision must avoid extra heap scans, per-draw locks and encoder splits; measure CPU submit cost before adoption.
11. **Proof / test plan:** Exact changing-seed readbacks, deliberate wrong-oracle controls, direct and transitive producers, then fallback cases in the matrix below. Reuse existing probes and external evidence storage.
12. **Risks / rollback / done:** Incomplete reflection or alias identity blocks relaxation. Reverting these documents changes no runtime. Done means evidence, boundaries and the next test slice are reviewable; the broader MSC task remains open.

## Baseline and evidence provenance

The worktree initially contained only the user's `.gitignore` edit. HEAD was
49 commits ahead / 0 behind the locally stored `origin/feat/d3d12`; that remote
tracking reference was not refreshed. `sw_vers`, `xcodebuild -version` and
`xcrun metal --version` were checked again: 27.2 (26B123), 27.2 (27B52),
32023.922. The previous [clean build](msc-runtime-cache-identity-design.md#macos-272-revalidation--2026-11-04)
completed 388 steps, including seven AIR compilations and a metallib link.
The earlier missing-Metal-toolchain limitation is resolved. This design-only
round does not repeat the full build or deploy into the user's Wine environment.

Evidence directory:
`/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/result-msc-fence-dependency-20261104/`.
`sources.json` records downloaded primary-source URLs, installed SDK paths and
SHA-256 hashes. Microsoft document line references below refer to those retained
copies. SDK references are copied from Xcode's macOS SDK Metal framework headers.
The MSC manual is `/opt/metal-shaderconverter/docs/UserManual.md`.

Mullion's remote HEAD/main was queried with `git ls-remote` and pinned to
`93de32b9145885ceb2e45be7d445e56d62499b3c`. Its source/test files were downloaded
at that exact SHA outside DXMT. The earlier reference
`b580492a557944591bf92f972497417333410925` is not used as current evidence.
No Mullion checkout was built or executed; `MULLION_OBSERVED` below means
source-observed behavior only. Windows native results are `NOT_RUN`.

## Specification contract

| Evidence type | Source / line | Consequence |
| --- | --- | --- |
| SPEC_REQUIRED | `resource-barriers.md:35`, `:41`, `:45` | Transitions, aliases and UAV ordering are distinct contracts. Aliased resources may have different object handles; NULL UAV barriers cannot be reduced to one named resource. |
| SPEC_REQUIRED | `resource-barriers.md:161` | A resource with a pending split transition cannot be accessed until its matching end. |
| SPEC_REQUIRED | `enhanced-barriers.md:282`, `:292` | Global barriers cover indicated accesses; explicit SyncBefore/SyncAfter scopes order work independently of which resource handles a heuristic finds. |
| SPEC_REQUIRED | `enhanced-barriers.md:304`, `:308`, `:356` | Static-while-set data has binding-time obligations; DRAW includes early stages; adjacent/sequential barriers must retain their combined ordering. |
| SPEC_REQUIRED | `enhanced-barriers.md:510`, `:667`, `:711` | Aliased memory and split barriers need their own ordering proof. Access/layout bookkeeping alone is insufficient. |
| SPEC_REQUIRED (Metal API) | `MTLRenderCommandEncoder.h:798`, `:806` | Fence updates capture work at specified stages; a wait blocks specified stages and can permit other stages to overlap. A Fragment wait does not order preceding Vertex accesses. |
| SPEC_REQUIRED (Metal API) | `MTLComputeCommandEncoder.h:246`, `:254` | Compute fence operations capture/order work; drivers can defer updates or move waits to encoder boundaries. Do not introduce a wait after updating the same fence in one encoder. |
| SPEC_REQUIRED (Metal API) | `MTLResource.h:147`; `MTLRenderCommandEncoder.h:966` | Untracked resources require explicit dependency handling. `useResource` does not retain the object; residency, hazard tracking and lifetime are separate requirements. |
| SOURCE_AUDIT | MSC `UserManual.md:936` | Shared argument-buffer access can restrict overlap for tracked textures. The recommendation is measurement, not permission to remove DXMT's explicit ordering. |

These rules are a dependency-safety contract, not a claim that every existing
DXMT barrier path is conformant. A source-observed conservative wait also does
not prove complete alias cache coherence on every supported device.

## Current DXMT paths and missing proof

All locations in this table refer to baseline `cf2af837`. `SOURCE_AUDIT` means
the mechanism was read; it is not a new GPU pass result.

| Evidence type | Source / line | Finding and implication |
| --- | --- | --- |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_queue.cpp:72`, `:1382`, `:1398`, `:1410` | One queue-owned fence links ordinary Render, Blit and Compute. Render waits at Vertex or PreRaster and updates at Fragment. An intervening compute encoder can carry an older producer's dependency. |
| SOURCE_AUDIT | `src/winemetal/unix/winemetal_unix.c:1446` | WineMetal forwards the specified fence stages to Metal; no automatic stage refinement occurs here. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_queue.cpp:859`, `:881`, `:913`, `:928`, `:491` | State validation precedes live descriptor collection and allocator marks. Submission holds references until completion. A future decision belongs after resolution and before encoding, without changing these owners. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_list.cpp:2255`, `:2278`, `:4376` | Recorded masks merge declarations; submission collection omits already-covered usage/stages. `descriptor_resource_uses` is a delta, not a complete read/write set. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_list.cpp:2710`, `:2742`, `:3403` | Ordinary MSC VB binding emits direct vertex-buffer commands; IB has explicit use declarations. Reading only the use-mask map misses direct binding facts. This finding is about proof completeness, not a demonstrated VB failure. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_list.cpp:2603`, `:2664`, `:3197`, `:3365` | AIRCONV compact VB tables, MSC object/mesh tables and advanced stages differ. A render encoder aggregates stages across draws/PSO switches, so its first draw cannot certify the whole encoder. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_list.cpp:3937`, `:4341`, `:4418`, `:4537` | Root VAs resolve allocations; volatile table/bindless intents resolve live descriptors at submit, including counter views. Recording-time descriptor contents cannot certify a replay or a later submission. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_list.cpp:4620`, `:4637`, `:4756` | MSC uses broad stage declarations; the precise footprint path is Airconv-only. Root visibility and declaration masks do not establish all actual lowered shader accesses. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_list.cpp:2328`, `:2448`; `src/d3d12/d3d12_command_allocator.cpp:423` | Predication generates GPU arguments/counts. Render ExecuteIndirect also emits a vertex resolver that writes an ICB. An ordinary shader with no SRV can still have early internal dependencies or writes. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_allocator.hpp:367`; `src/d3d12/d3d12_command_list.cpp:3415` | TLAB/argument uploads and internal outputs can occupy one allocator buffer. Whole-buffer identity cannot prove their ranges independent; object liveness does not make contents immutable. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_list.cpp:525`, `:2456`, `:2513`, `:8383` | Barrier packets carry broad scopes; pending barriers are replayed into the next encoder. Enhanced lowering retains nonempty execution dependencies even with NO_ACCESS. A handle intersection must not erase those obligations. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_list.cpp:2596`, `:6235`, `:6254` | Split halves and alias barriers invalidate the pass. Aliasing has no separate per-boundary alias proof in `EncoderData`; narrowing the next fence would weaken existing ordering. |
| SOURCE_AUDIT | `src/d3d12/d3d12_buffer.cpp:146`; `src/d3d12/d3d12_texture.cpp:676`; `src/dxmt/dxmt_buffer.cpp:179`; `src/d3d12/d3d12_texture.cpp:191` | Placed objects can share heap ranges; buffer/texture allocation paths use untracked resources. Different native handles do not imply disjoint memory. Reserved mappings add another identity dimension. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_list.cpp:6264`; `src/d3d12/d3d12_command_queue.cpp:910`, `:1690` | Bundles replay into parent recording; sparse mapping and queue waits use events. A decision must cover replayed commands and preserve queue/event boundaries. |
| SOURCE_AUDIT | `src/d3d12/d3d12_command_encoder.hpp:67`, `:81`, `:141` | Encoder references own lifetime; use records contain native handle/usage/stages. There is no complete early-access/barrier/alias certificate to reuse today. |

## Required proof before any stage relaxation

Let **P** include preceding work whose ordering would be lost by removing the
early wait, including transitive predecessors. Let **E** include every early
operation in the entire receiving encoder: vertex fetch, shader accesses,
indirect argument processing, helpers and pre-raster writes. The resource part
of the proof must exclude required RAW, WAR and WAW hazards:

```text
W(P) overlaps R(E) or W(E)  -> preserve early wait
R(P) overlaps W(E)         -> preserve early wait
explicit early execution obligation, unknown range/alias/access -> preserve early wait
```

This is a conservative gate, not a requirement to serialize legally unordered
accesses. Ranges must refer to shared backing memory and valid generations,
not only COM/native object identity. Broad/global barriers remain obligations
even when the sets appear disjoint. Reducing a wait requires proof both of
memory independence and of permission to execute the early stage sooner.

For example, A writes a VB, B performs unrelated compute, and C reads that VB.
B currently waits for A, updates the shared fence, and C waits before Vertex.
Moving C's wait to Fragment based only on B's writes lets C's Vertex run before
A has finished. The same reasoning covers a prior reader and a later early
writer, attachment aliases, command-list boundaries and replay. Checking only
the immediately preceding compute encoder is insufficient.

The eventual implementation must derive a conservative summary from the
existing recording owner, never maintain a competing resource-state map.
Any boundary facts needed for a decision must survive pass invalidation and
be attached to the existing immutable encoder/list records. Queue-local
resolution uses the existing descriptor snapshot/lifetime path; a new heap
scan or a second descriptor snapshot could disagree with the resources actually
encoded. Failure to obtain optimization evidence falls back to today's wait;
failure of existing residency/lifetime validation keeps its existing error path.
Replay must derive a fresh decision, not mutate the closed recording.

A first possible *experiment* is a direct, nonindexed ordinary MSC draw with a
procedural VS, CPU-written immutable constants, no resource descriptors visible
to VS, and no helper-generated input or early writes. It additionally requires
proof for lowered stage-in/TLAB accesses, all earlier dependency scopes and all
draws in the encoder. A root-signature PIXEL bit or `slot_mask == 0` alone is
insufficient. The existing ALL/PIXEL probe has a UAV barrier and transition;
neither may be silently dropped. The current implementation has no certificate
covering these conditions, so **even this candidate retains the early wait**.

## Fallback and regression matrix

Rows below are proposed coverage. Unless explicitly marked as prior evidence,
they are `NOT_RUN` in this design round. All ambiguous cases retain the current
Vertex/PreRaster wait; the matrix is not a promise to enable every case later.

| Case | Required observable / fallback | Existing test to extend or reuse |
| --- | --- | --- |
| CS texture → PS, independent procedural VS | Exact image; retain all barriers; paired timeline. Prior readback/trace evidence only, no optimized run. | `tests/dx12/dx12_msc_execution_overlap.cpp:269` and `msc_execution_overlap.hlsl:26` |
| CS → VB, IB, VS SRV or root CBV | Changing seed must reach the draw. Preserve early wait for each input path. | Extend the overlap probe; root fixtures in `graphics_root_srv_sm6.hlsl`, `graphics_root_cbv_sm6.hlsl` |
| Older producer → unrelated compute → vertex consumer | Exact output with no CPU wait between operations; verify the unrelated compute is a distinct encoder. Test one list and two lists in one Execute call. | Extend the overlap probe; this is the next focused slice. |
| Prior reader → VS UAV/SO writer; prior writer → same early writer | Readback verifies the old value and final value, covering WAR/WAW as well as RAW. | `dx12_stream_output.cpp`; `graphics_root_uav_sm6.hlsl` |
| Root/table visibility ALL versus PIXEL; PSO switch and later dependent draw | No first-draw-only classification; entire encoder becomes conservative. | Overlap probe and `dx12_graphics_sm6.cpp` |
| Volatile descriptor edited legally after Close, then replay | Each submit sees its own live resource; old descriptor snapshots cannot authorize overlap. | Descriptor fixture/probe paths; extend with explicit GPU output before using as a gate. |
| Bindless/unbounded table; append/consume counter view | Unknown reachable resources remain conservative; include counter backing/view identity. | `dx12_compute_sm6.cpp` and descriptor/append fixtures |
| Predication or GPU-written indirect arguments/root/VBV | Verify generated count, arguments and output; retain internal Vertex/Compute dependencies. | `dx12_predication.cpp:435`; `dx12_graphics_sm6.cpp:967` |
| UAV NULL/global barrier; enhanced NO_ACCESS with nonempty sync | Preserve explicit early-stage ordering even without a named intersection. Check encoded wait decision as well as output. | `dx12_enhanced_barriers.cpp:1116`, `:1281`; add a scoped GPU case if needed. |
| Legacy/enhanced split; begin/end across lists/submissions | Matching pairs and forbidden intervening accesses retain their admission and ordering behavior. | `dx12_enhanced_barriers.cpp:1247`, `:1768`, `:2125` |
| Placed aliases, NULL alias barriers, reserved remap | Identity differs but backing overlaps; preserve early wait/event obligations. First establish a valid baseline alias oracle. | `dx12_resource_tests.cpp`; Mullion candidates below |
| Bundle, replay, allocator reset and release around submission | Parent draw aggregation and completion ownership remain valid; illegal resets/replays retain their rejection behavior. | `dx12_bundle.cpp`, `dx12_command_list_type.cpp`, `dx12_descriptor_ownership.cpp` |
| GS/TS/mesh, queries, AS/RT, clear/resolve/CopyTiles helpers | Unknown/generated accesses retain PreRaster or current ordering; no initial experiment eligibility. | Existing stage/barrier/query probes; separate validation slices only when affected. |
| Queue Wait, separate Execute calls, sparse mapping event | Cross-batch and cross-queue dependencies remain intact; no new CPU waits hide missing GPU ordering. | Existing fence/resource probes and split submission modes |

For the next test slice, run baseline correctness with API/shader validation
and a wrong-oracle control, using changing seeds and complete readback. An
intentional wrong oracle verifies the test, not the existence of a hardware
race. For a later scheduler change, also assert which wait/fallback was emitted:
a race can remain invisible under a particular GPU schedule. Trace attribution
currently assumes one Compute/Render pair per workload; the transitive test
must not be fed to that analyzer until its schema explicitly supports the extra
encoder. Keep its exact-output oracle independently usable.

Only after semantic gates pass should an opt-in timing experiment compare
alternating baseline/candidate runs with identical runtime hashes, inputs and
cache conditions. Keep validated correctness separate from timing/capture;
report median/P95/range, CPU submit cost and metadata/memory overhead. Any
incomplete timeline, extra synchronization or noisy result blocks a speedup
claim. Occupancy, bandwidth and representative-game performance remain unproven.

## Mullion reference and portability boundary

| Evidence type | Pinned source / line | Usable lesson / limitation |
| --- | --- | --- |
| MULLION_OBSERVED (source only) | `src/d3d12/d3d12_command_queue.cpp:150`, `:182`, `:220` | `PassSync`, grouped fences and `WaitAll` preserve a predecessor set. The dependency relation is useful to study; copying a Fragment wait without those invariants is unsafe. |
| MULLION_OBSERVED (source only) | Same file `:1981` | Normal waits target PreRaster; a separate set targets Fragment. This is not blanket Compute→Fragment relaxation. |
| MULLION_OBSERVED (source only) | Same file `:1060`; `d3d12_command_list.cpp:5013` | Alias ordering includes a Metal 3 command-buffer/event path and a separate Metal 4 path. These mechanisms require independent DXMT cache/lifetime validation and are not transplanted. |
| SOURCE_AUDIT | `tests/d3d12/d3d12_pass_overlap.cpp:1`; `d3d12_alias_submissions.cpp:78`, `:107`; `d3d12_sparse_remap.cpp:74` | Candidates for readback, cross-submit alias and remap scenarios. Runtime execution and DXMT portability are NOT_RUN. |

The pinned repository LICENSE is LGPL 2.1 text. No source is copied into DXMT
by this slice. Any later port must check the selected file's copyright,
dependencies and attribution requirements separately. This reference update is
limited to synchronization and tests; it is not a renewed full Mullion gap audit.

## Delivery audit

| Gate / check | Result | Scope |
| --- | --- | --- |
| Gate 0 | PASS | Current Git/toolchain state, CodeGraph-first source audit, installed Apple API contract and retained Microsoft/Mullion sources. |
| Gate 1 | PASS | Conservative contract, ownership boundaries, fallback/test matrix and next minimal test slice are defined. |
| Gate 2 | NOT_RUN by design | No production or test implementation in this slice. |
| Gate 3 | PASS for documents/evidence | Source anchors, links, source hashes and existing 16-case trace parser regression checked; no new GPU result claimed. |
| Full build / GPU / Windows / Mullion runtime | NOT_RUN | Documentation-only change; prior build/readback/paired-trace evidence retains its original scope. |
| Scheduling optimization | BLOCKED | Missing complete early-access and boundary obligations; no enabled path is certified. |
| Gate 4 | PASS within design scope | Focused documentation diff; no new runner, module, ABI, shader policy, state owner or capability. |

Changed files: this report defines the dependency contract and next test;
`docs/D3D12_MSC_HLSL_PERFORMANCE_WORK.md` links it and updates task status.
Runtime risk is unchanged. The outstanding risk is treating a partial use set,
root visibility, source audit or successful synthetic image as scheduling proof.
Public Feature Level, Shader Model and Tier remain unchanged.

Commit only these two files using `docs(d3d12): ...`; preserve the user's
`.gitignore` edit and do not push. The next recommended slice is the **CS→VB
transitive-dependency regression**, extending the existing overlap C++/HLSL
probe while preserving production scheduling. This design round stops here.

## CS-to-VB regression — 2026-11-04

**Design card:** Baseline `bb7dda451805fcfc919f7533dc6b1479e0cf1973`;
extend the existing overlap probe, HLSL, Meson fixtures and runner. A compute
shader writes per-vertex integer attributes into a DEFAULT UAV buffer; the
ordinary MSC vertex stage fetches them through IA and forwards them to an
R32_UINT target. Preserve UAV-to-VB transitions and exact changing-seed output.
Test direct consumption, a single-list transitive chain separated by an
unrelated copy, and two lists in one Execute call. The unrelated compute writes
a distinct scratch buffer whose contents are independently checked. Inspect
recorded encoder order to reject accidentally merged compute work. No CPU wait
is allowed inside a workload. Reuse isolated build/Wine identity checks and
API/shader validation, and require wrong-oracle controls to fail. Production
scheduling/ABI/capabilities stay unchanged. Rollback removes only test/report
changes. Done requires correct images, scratch output, encoder topology and
preserved original texture-mode results; passing does not prove a relaxed
schedule safe or demonstrate overlap.

### Implementation and evidence

This N13 test slice is complete. The branch remains `feat/d3d12`; before this
slice it was 50 commits ahead / 0 behind the locally stored remote-tracking
reference, which was not refreshed. The pre-existing `.gitignore` edit is
excluded. No production file, public capability or runtime Fence policy changes.

| Changed file / source anchor | Purpose / evidence type |
| --- | --- |
| `tests/dx12/dx12_msc_execution_overlap.cpp:72`, `:403`, `:476`, `:510` | SOURCE_AUDIT: add three opt-in workloads, recorded encoder topology checks, exact image and scratch oracles. The producer and consumer have no intervening CPU wait. |
| `tests/dx12/msc_execution_overlap.hlsl:16`, `:36`, `:61` | SOURCE_AUDIT: CS writes 6,144 uint attributes; ordinary MSC VS fetches `TEXCOORD0` using a 4-byte IA stride; PS forwards the flat checksum. Original texture fixture bytes remain identical. |
| `tests/dx12/meson.build:280` | SOURCE_AUDIT: three opt-in SM6.0 VB fixtures extend existing targets. |
| `tests/dx12/run_msc_execution_overlap.py:49`, `:81`, `:119`, `:147` | SOURCE_AUDIT: `--vertex-dependency` validates configuration, topology/list boundaries, scratch sequence, loaded runtime identity and validation state. It performs no VB performance comparison. |
| `tests/dx12/test_msc_execution_overlap_trace.py:161` | LOCAL_PASS (synthetic log tests): reject missing configuration, merged compute, wrong list boundaries/counts and missing/duplicate/truncated scratch records. These tests are separate from GPU evidence. |
| `docs/msc-fence-dependency-design.md`; `docs/D3D12_MSC_HLSL_PERFORMANCE_WORK.md` | Design, retained failures, results, limitations and next slice. |

The specification and pinned Mullion evidence in the preceding audit remain
the design inputs. No Mullion implementation was copied. `CR`, `CBCR` and
`C|CR` describe recorded Compute/Blit/Render encoders; `|` marks the command-list
boundary. They do not measure GPU execution intervals. The transitive workload
copies an upload sentinel into a separate scratch resource between the two
compute encoders. The second compute rewrites the scratch, whose entire 24,576
bytes are independently checked after submission. The split-list case submits
both lists through one `ExecuteCommandLists(2, ...)` call. Every successful
image checks all 65,536 bytes of the 128×128 R32_UINT target against a changing
seed. Negative controls flip one expected bit at sample 2, pixel (0,0).

Environment: Apple M1 Max, macOS 27.2 `26B123`, Xcode 27.2 `27B52`, Metal
`32023.922`, Wine 11.0, compiled MSC SDK API 4.0.3. The loaded MSC product
version is not queried; its path and content hash identify this run. Build
configuration follows the x64 release section of `.github/build_dxmt.sh`, with
tests enabled and the already validated patched LLVM installation. The script's
installation into the user's Wine/prefix is not executed. This slice performs
an incremental full-default-target build in the isolated Metal 27.2 build tree;
the earlier clean-build evidence retains its original scope.

All paths below are under
`/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/`:

- Build: `dxmt-metal272-identity-audit-20261104`.
- LLVM: `llvm-current-patches-20261103/install`.
- DXC build prefix: `prefix-dxc-build-cache-gpu-20261104`.
- Runtime stage/prefix: `stage-msc-vb-dependency-20261104` and `prefix-msc-vb-dependency-20261104`.
- Evidence: `result-msc-vb-dependency-20261104`; final batches are `vb-verified/` and `texture-verified/`.

Reproduction commands from the repository root (use new output directories):

```sh
base=/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate
export PATH="$PWD/toolchains/llvm-mingw-20260908-ucrt-macos-universal/bin:$PATH"
export WINEPREFIX="$base/prefix-dxc-build-cache-gpu-20261104"
meson compile -C "$base/dxmt-metal272-identity-audit-20261104" \
  dx12_msc_execution_overlap \
  dx12_msc_execution_overlap_vb_cs dx12_msc_execution_overlap_vb_vs \
  dx12_msc_execution_overlap_vb_ps dx12_msc_execution_overlap_cs \
  dx12_msc_execution_overlap_vs dx12_msc_execution_overlap_ps
meson compile -C "$base/dxmt-metal272-identity-audit-20261104"
python3 -B tests/dx12/run_msc_execution_overlap.py \
  --stage "$base/stage-msc-vb-dependency-20261104" \
  --prefix "$base/prefix-msc-vb-dependency-20261104" \
  --build "$base/dxmt-metal272-identity-audit-20261104" \
  --converter /usr/local/lib/libmetalirconverter.dylib \
  --processes 2 --iterations 1024 --vertex-dependency --samples 10 \
  --output "$base/result-msc-vb-dependency-20261104/vb-verified"
# Original texture regression: omit --vertex-dependency, use --samples 2,
# and use the separate texture-verified output directory.
python3 -B tests/dx12/test_msc_execution_overlap_trace.py -v
```

Deployment uses final postprocessed build artifacts. For D3D12/DXGI native
overrides only, replace the `Wine builtin DLL` marker at bytes 64:80 with spaces;
the runner requires every other byte to match the build. WineMetal PE and Unix
artifacts remain byte-identical to their build outputs. The runner also checks
PE export bytes in the loaded process, native/builtin load modes, and the
actual dyld paths for WineMetal Unix and MSC. `delivery-audit.json` rechecks
those identities, stage/build pairing, all output sequences and fixture hashes.

### Validation and delivery audit

| Gate / check | Result | Evidence / limits |
| --- | --- | --- |
| Gate 0 / Gate 1 | PASS | Baseline, prior source/specification contract and scoped test design above. |
| Gate 2 | PASS | Existing probe/fixtures/runner extended; no production scheduler change. |
| x64 full default-target build and six shader fixtures | LOCAL_PASS | `build-manifest.json`, `build-final.log`, `full-build-result.json`, `delivery-build.log`. |
| Three VB modes | LOCAL_PASS | 12 processes: three validated normal runs, three validated negative controls, and two repeat processes per mode. 90 successful full-image readbacks and 60 full-scratch readbacks, including warmups. All recorded topologies match. |
| Original texture modes | LOCAL_PASS | Eight processes, 28 successful full-image readbacks including warmups, and two expected negative exits. Original CS/VS/PS DXIL SHA-256 values unchanged. |
| Metal API / shader validation | LOCAL_PASS | Enabled and clean in all validation and negative-control processes; intentionally disabled in repeat/timing processes. |
| Wrong-oracle controls | LOCAL_PASS | Five total expected exit-1 results, each at sample 2 / (0,0) with exactly one differing bit. They validate the oracle, not hardware race detection. |
| Log/trace parser regression | LOCAL_PASS | `parser-tests.log`: 22 tests, including six dependency-log tests. |
| Formatting / diff | PASS | `clang-format --dry-run --Werror` for the C++ probe; `git diff --check`. |
| x86 / Windows native / Mullion runtime / new GPU trace | NOT_RUN | No execution or performance claim for these environments or traces. |
| Gate 3 / Gate 4 | PASS within test scope | Final pairing and loaded identities checked; only seven slice files staged. Existing user edit preserved; commit locally, do not push. |
| Scheduling relaxation / performance improvement | BLOCKED / UNVERIFIED | Full early-access/barrier/alias proof is still absent. Successful conservative runs do not close that gap. |

Retained intermediate failures are not counted as passes: new target names
initially required Meson regeneration; the first topology check rejected the
allocator's `Null` sentinel before submit (`vertex/validation-vb-direct.log`),
then was corrected; an early texture run rejected a pre-postprocessing stage
after the full build rewrote PE headers. Final artifacts were redeployed and
both complete batches rerun. `vertex-final/` is the earlier successful VB
batch; the delivery conclusions use only `vb-verified/` and `texture-verified/`.

Final MSC SHA-256 is
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`.
WineMetal Unix SHA-256 is
`c9e20da27632144651fd744d01ddb26445800ea4baa1fbd36a0257188f1fbd8c`;
WineMetal PE SHA-256 is
`9a2f272f352ca831db8f9ec574f5d8faaa50f7c146b5b3856af57f4a7948d6f6`.
The final executable is
`e05bfb46114321af1ed08f8c8c665f8d362d1d852b69f5ce6a2adbc3a3e94c98`.
Complete DLL/fixture hashes and commands are in the two `summary.json` files,
`delivery-runs.json` and `delivery-audit.json`. The post-commit
`delivery-manifest.json` records the resulting Git SHA and remaining worktree.

Public Feature Level, Shader Model and Tier remain unchanged. Regression risk
is limited to the opt-in tests and runner; the original texture path was
rechecked. Recorded topology is intentionally coupled to DXMT internals and
must be reviewed when encoder grouping changes. The flat checksum observes
the vertex fetches contributing to the rasterized triangles, not an exhaustive
oracle for every vertex lane. This is neither a general conformance test nor
a proof that a relaxed schedule would fail on this GPU. The transitive trace
must not be passed to the existing one-Compute/Render-pair analyzer.

**Next recommended slice:** add a conservative early-access completeness
record for ordinary MSC direct VB/IB bindings to the existing encoder-owned
facts. Preserve it across draws/list boundaries and verify unknown paths select
the existing wait. Keep production Fence stages unchanged until the broader
barrier/alias/transitive proof and regressions are complete; do not introduce a
second resource-state or residency owner. The overall MSC task remains open.

## Input assembly recording slice — design card, 2026-11-04

1. **Goal / node:** N13; preserve the ordinary MSC input-assembly facts needed by a later dependency proof.
2. **Baseline:** `2eb8f928`, `feat/d3d12`; only the user's `.gitignore` is dirty; previous three-mode GPU regression passed.
3. **Current path / evidence:** `Draw[Indexed]Instanced → PreDraw → EncodeVertexBuffers → RenderEncoderData → queue`; direct VB commands bypass residency masks, while the queue still waits at Vertex/PreRaster.
4. **Invariant:** Aggregate every draw in an encoder; preserve immutable closed recordings and existing resource/lifetime owners. This summary is not a complete shader-access, alias or barrier proof.
5. **Selection:** Add a bounded input-assembly summary to the existing render encoder; reject a second resource map or additional Metal useResource calls merely to manufacture dependency metadata.
6. **Scope:** Ordinary MSC direct draw records active input-layout slots and indexed use only when emitting its draw command; every other PreDraw caller defaults to unclassified. An empty encoder cannot count as complete.
7. **Boundary:** Commands retain concrete binding history; this summary classifies only IA coverage. Do not infer resource independence, actual fetch ranges or safe Fragment waits from it.
8. **Files:** Existing encoder header, command-list implementation, overlap probe/runner and reports; no new production module or public ABI.
9. **Cost:** Fixed summary per render encoder and constant-time classification/accumulation per draw; no heap scan, lock, per-draw allocation or encoder split.
10. **Proof:** Recording checks for sparse slots, indexed/nonindexed accumulation, indirect fallback, new encoders and replay immutability; existing changing-seed VB GPU oracles plus original texture regression.
11. **Risks / rollback:** Incomplete paths must remain unclassified; revert this isolated commit. Preserve the existing fence policy regardless of the summary.
12. **Done / capability:** Build, recording assertions, GPU output and paired runtime identity pass; no FL/SM/Tier promotion or performance improvement claimed.

### Implementation and scope

`RenderEncoderData::InputAssemblyUse` (`d3d12_command_encoder.hpp:144`) stores
the union of input-layout slots used by ordinary MSC direct draws, whether an
indexed draw was recorded, whether any direct draw was recorded, and a sticky
unclassified flag. `complete()` means only that these IA facts cover the
recorded draw paths. It says nothing about shader resources, ranges, aliasing,
barriers or transitive dependencies; it must never be used alone to relax a
fence. No queue decision consumes this summary in this slice.

`PreDraw` (`d3d12_command_list.cpp:3172`, `:3366`) defaults to unclassified.
Only direct `DrawInstanced` / `DrawIndexedInstanced` explicitly select the
ordinary-input path; geometry/tessellation/mesh, Airconv, predication, stream
output and indirect/internal callers remain unclassified. The accumulation
helper at `:3918` runs immediately before emitting an ordinary direct draw,
after early rejection and specialized draw branches. Slot/index bits accumulate
instead of following only the last binding. New encoders start empty; submission
and replay do not mutate the record. Closed command records remain the source
for concrete binding history; no second resource-state, residency or lifetime
owner is added. Existing direct-binding lifetime behavior is not changed or
certified by this summary.

The existing probe adds `vb-indexed`, `vb-input-mixed` and
`vb-input-indirect` modes (`tests/dx12/dx12_msc_execution_overlap.cpp:131`).
The mixed case draws left/middle/right stripes with slot 0 nonindexed, slot 3
indexed, then slot 0 nonindexed. Thus the middle draw must produce correct
pixels and the final record must retain mask 9 plus indexed use. It alternates
recording and replaying exactly the same closed list; seeds change on each
new recording, with resource transitions valid for replay. The indirect case
places a non-updating indirect draw between two direct stripes and requires
the summary to remain unclassified. These tests use the existing VB DXIL;
no shader, Meson target or new runner is introduced.

All modes inspect the actual encoder-owned record before submission and again
after completion, while retaining the original image/scratch oracles. IA logs
are mandatory for new runner invocations. Historical trace captures without
them remain accepted by the shared parser; any supplied IA sequence is checked
in full. Synthetic cases reject lost slot/index bits, omitted replay evidence,
missing IA records and false completeness after indirect work. The trace
analyzer continues to support only the original single-Compute/Render workload.

The specification contract earlier in this report remains unchanged:
Microsoft `resource-barriers.md:135-155` permits explicit promotable before
states after buffer decay and does not decay between lists in one Execute
call. Installed Apple `MTLRenderCommandEncoder.h:798`, `:806`, `:966` keeps
fence ordering separate from declarations and resource lifetime. Pinned
Mullion `93de32b9145885ceb2e45be7d445e56d62499b3c` remains source-only evidence:
its `PopulateVertexBufferTable` (`mullion-command-list.cpp:881`) and grouped
`NextSync` (`mullion-command-queue.cpp:182`) are different owners/mechanisms.
No source was transplanted; Windows and Mullion execution remain NOT_RUN.

### Validation evidence

Baseline: `2eb8f928e878cde8ccd5081db545ca9ad30c5710`, 51 ahead / 0 behind the
locally stored `origin/feat/d3d12` (not refreshed). Apple M1 Max, macOS 27.2
`26B123`, Xcode 27.2 `27B52`, Metal `32023.922`, Wine 11.0 and the same patched
LLVM x64 release build are used. Evidence is under
`dxmt-test/n95-capability-gate/result-msc-input-assembly-20261104/` outside the
repository; runtime stage/prefix are `stage-msc-input-assembly-20261104` and
`prefix-msc-input-assembly-20261104`. The user's Wine installation is untouched.
`build-final-result.json` records the full default-target build command and
exit status; `build-final.log` retains compiler diagnostics, including existing
warnings. Final GPU results use the `ia-final`, `vb-final`, `texture-final`
batches, not the earlier exploratory batches.

Use the preceding runner command with the new stage/prefix and output paths.
For IA cases add `--vertex-dependency --input-assembly --samples 2`; for the
existing three VB cases use `--vertex-dependency --samples 2`; for texture
use `--samples 2`. All use `--processes 2 --iterations 1024`, with additional
validated normal and negative cases generated by the existing runner. Exact
argv and outcomes are retained in `runs-final.json` and each `summary.json`.

The x64 compiler reports `sizeof(RenderEncoderData)` 808 → 816 bytes and
`sizeof(InputAssemblyUse)` 8 bytes (`record-sizes.json` and the emitted assembly).
The record has no dynamic allocation. This is storage size, not a measured
resident-memory or application throughput result. The existing 4,096-draw
`dx12_graphics_sm6 --vertex-stride ... perf-vertex` benchmark is used to compare
baseline and candidate recording/submit costs with identical executable and
DXIL, alternating four processes per build. Its first four draws are reported
separately; every pass retains its 4,096-byte exact-output oracle. The baseline
DLL hashes are matched to the previous delivery manifest, and both sides use
the same WineMetal/MSC bytes. No GPU overlap improvement is claimed.

| Evidence / check | Result | Scope |
| --- | --- | --- |
| Gate 0 / Gate 1 | PASS | Current Git/toolchain, CodeGraph-first owner audit, prior spec/reference evidence, bounded design above. |
| Gate 2 | PASS | Two existing production files add IA facts; no queue policy or public ABI change. |
| x64 full default-target build | LOCAL_PASS | `build-final-result.json` and the formatting rebuild `build-formatted-result.json`, exit 0. |
| IA indexed/mixed/indirect | LOCAL_PASS | `ia-final/`: 12 processes, 42 full-image readbacks, seven replay submissions and three expected negative exits. |
| Previous direct/transitive/split-list VB modes | LOCAL_PASS | `vb-final/`: 12 processes, 42 image and 28 scratch readbacks, three expected negative exits. |
| Original texture modes | LOCAL_PASS | `texture-final/`: eight processes, 28 image readbacks and two expected negative exits. |
| API/GPU validation and loaded identities | LOCAL_PASS | Validated normal/negative processes only; repeat and performance processes intentionally run without validation. `correctness-audit.json` independently checks sequences, exit codes and identities. |
| Parser regression | LOCAL_PASS | `parser-tests-final.log`: 23 tests. Includes old trace logs, six VB modes and missing/incorrect IA evidence. |
| Scoped formatting / diff checks | PASS | Changed production regions and C++ probe checked with clang-format; `git diff --check`. |
| CPU cost A/B | MEASURED / INCONCLUSIVE | Eight alternating processes, 24 full-image readbacks, eight recording samples per build; values below. No speedup or no-regression guarantee. |
| x86 / Windows native / Mullion runtime / new GPU trace | NOT_RUN | No widened environment, shader, scheduling or capability claim. |

The performance harness has three submissions per process: pass 0 records,
pass 1 replays without recording, pass 2 records again. Its first-four-draw
time includes PSO setup in the first recording and must not be combined with
warm per-draw cost as one distribution. Warm recording samples exclude those
four draws. The A/B values are:

| Metric | Baseline median / P95 / range | Candidate median / P95 / range |
| --- | --- | --- |
| Warm CPU record, µs/draw (8 samples each) | 0.289406 / 0.341642 / 0.260239–0.341642 | 0.304741 / 0.333065 / 0.267669–0.333065 |
| CPU submit, µs (12 submissions each) | 1775.85 / 7298.60 / 1466.10–7298.60 | 1664.30 / 6856.10 / 1482.60–6856.10 |

Observed warm median increases by 0.015335 µs/draw (about 5.3%). Process means
and sample ranges overlap, and this short microbenchmark does not isolate
instruction cost from noise. This is an explicit unresolved cost, not evidence
of an optimization or a promise of zero regression. End-to-end frame time,
occupancy, bandwidth and resident memory were not measured. Fixed 8-byte
encoder storage and no new allocation/lock/heap scan bound the implementation
cost; later scheduling adoption still requires its own correctness/performance
gate. Keep the record only as dependency-proof groundwork, not as a claimed
speedup. Full runs, identities and raw samples are in `performance-runs.json`
and `performance-summary.json`.

Retained failures: the first parser change wrongly required new IA fields in
historical trace logs; this was corrected with mandatory fields for new runs
and optional-but-fully-checked fields for old captures. The temporary benchmark
collector initially required a recording sample for replay pass 1; the actual
GPU run passed and its saved sample was reused after correcting the parser.
Storage-size probing initially selected the host's GCC wrapper and then
duplicated a header through `d3d12_device.hpp`; the successful probe uses the
same LLVM-MinGW command as the build and compares the prior struct declaration
against the current type. These collector failures are not GPU failures.

The final formatting rebuild changes PE build identifiers and three `.text`
bytes in the `IMPLEMENT_ME` log line constants for SetMarker/BeginEvent/EndEvent.
All other code/data sections match; the overlap executable changes only its
build identifier. `format-equivalence.json`, `format-disassembly.txt` and saved
preformat binaries retain that comparison. The full functional batches above
used the preformat identity; the A/B candidate and `ia-delivery/` use the final
formatted build. This distinction is recorded rather than treating mismatched
hashes as identical deployments.

The formatted delivery batch also passes all 12 processes, 42 full-image
readbacks, seven replay submissions and three expected negative controls.
`delivery-artifact-audit.json` verifies final build/stage pairing, loaded export
bytes, converter identity and validation results. Gate 3 and Gate 4 are PASS
within this IA-recording scope. Seven changed files are committed locally with
the repository's `feat(d3d12): ...` style; no push. The user's `.gitignore`
change remains excluded. `delivery-manifest.json` records the resulting SHA,
files and remaining Git state.

Public FL/SM/Tier, production Fence stages and WineMetal ABI remain unchanged.
Remaining risks: IA completeness does not establish a full early-access set;
the slot union is conservative for zero-size or unused bindings and does not
identify backing/alias ranges. Future paths that bypass the direct entry points
must preserve unclassified status. The measured recording overhead remains
inconclusive and must be accounted for before adopting a scheduling change.

**Next recommended slice:** preserve explicit early-stage execution obligations
from legacy/enhanced barriers in the existing encoder-boundary records, including
barriers with no resource access and split/alias boundaries. Validate those
facts against existing barrier probes before combining them with IA coverage;
keep current waits. The larger MSC/HLSL/performance task is still incomplete.


## Explicit barrier recording slice — design card, 2026-11-04

1. **Goal / node:** N13; retain explicit barrier obligations alongside the existing immutable encoder chain before any fence-stage experiment.
2. **Baseline:** `65fdbedb`, `feat/d3d12`; preserve the user's `.gitignore`; macOS/Xcode/Metal 27.2 and the existing isolated patched-LLVM build.
3. **Evidence:** legacy barriers emit mapped packets; enhanced SyncBefore/SyncAfter are discarded after admission; split/alias barriers invalidate the pass. See `d3d12_command_list.cpp:2512`, `:2601`, `:6238`, `:6272`, `:8402` at baseline.
4. **Invariants:** INV-03/04/07/08; retain explicit execution dependencies even with NO_ACCESS. Existing transition events own resource state, split pairing and lifetime.
5. **Selection:** a bounded summary on each existing EncoderData, including the head sentinel; reject a new barrier/event vector, resource map, scheduler or additional encoder splits.
6. **Ownership:** accumulate on the current encoder, or the last linked encoder/head when no encoder is open; it covers barriers inside that encoder and its trailing gap, not a precise packet location.
7. **Content:** legacy render-stage masks mapped per barrier before union (including COMMON), original enhanced sync masks and transition/UAV/alias/split-kind bits. Union conservatively; never infer matching pairs, resource independence or safe waits from this summary alone.
8. **Scope / files:** existing encoder header, command-list recording, command-list probe and these reports. Queue policy, native packets, pending flush and resource admission are unchanged.
9. **Cost:** fixed storage per encoder/head and constant-time OR operations per admitted barrier; no new locks, scans, dynamic allocations, native barriers or draws.
10. **Proof:** head/trailing and empty/sync-only barriers, mixed masks, split/alias across boundaries, reset/ClearState, immutable replay; existing rendered/compute barrier oracles and split submission probes with runtime identity and validation.
11. **Risks / rollback:** future consumers must include the head, trailing gaps and all prior relevant nodes; masks are not a dependency certificate. Revert this focused commit if recording or GPU regressions occur.
12. **Done / capability:** build, metadata assertions, GPU regression and cost evidence; no FL/SM/Tier or scheduling promotion. Remaining full early-access/alias/transitive proof stays open.


### Ownership and specification evidence

`EncoderData::BarrierUse` (`d3d12_command_encoder.hpp:74`) is an immutable
recording summary once Close succeeds. `RecordBarrier` (`d3d12_command_list.cpp:2458`)
accumulates on the open encoder, or the last linked encoder/head in a gap.
Consumers must examine the head sentinel, barriers inside each encoder and its
trailing gap, and relevant predecessors across lists/submissions. Attaching a
barrier to the predecessor does not make it only an outgoing dependency: a
barrier inside an encoder may constrain its later commands against much older
work. The summary is conservative and loses exact intra-encoder positions.

Legacy transition stages are mapped **before** union, so COMMON's zero state
cannot disappear when mixed with a narrow later state. These fields describe
render stages only; legacy/UAV/alias/split kind bits must still force conservative
handling of other domains. Enhanced sync bits retain the application's original
scopes, including one-sided sync and NO_ACCESS. A completely empty
NONE/NONE + NO_ACCESS/NO_ACCESS barrier without a layout change leaves no record.
SPLIT is retained as an opaque scope bit with begin/end reasons: an aggregated
mask is not a legal individual D3D12 barrier and cannot establish pairing.
Existing resource transition events remain the only state/pairing authority.

Split barriers record both halves before pass invalidation (`:2616`, `:6270`);
aliasing records its reason even for NULL/NULL (`:6312`). ClearState may end the
pass but does not erase recorded obligations. Reset starts a new zeroed head;
previous allocator-owned chains survive until allocator Reset. No queue consumer,
new pass boundary, fence wait, resource reference or WMT packet layout is added.

For an enhanced texture range, the scope packet and summary are now emitted
once after range admission (`:8677`), while layout events remain per subresource.
Previously these identical adjacent packets were already merged (or ORed into
the pending scope). No GPU work occurs between the layout entries. This keeps
summary collection constant per admitted barrier and retains the serialized
native scope; it is not a new synchronization optimization or a changed layout
validation rule. Buffer/global entry points share the same record/emission helper
at `:8442`. Existing error/admission tests remain required.

**SPEC_REQUIRED:** Microsoft Enhanced Barriers, retained source lines 287–304,
separates explicit execution scopes from resource accesses; lines 707–735 define
split synchronization. Legacy barrier stage inference must remain conservative.
The two Microsoft primary sources were fetched again on 2026-11-04; their hashes
match the prior dependency-design evidence. See external `sources.json`.
Installed Metal fence/encoder API evidence in the earlier section still applies.

**MULLION_OBSERVED (source only):** pinned `93de32b9145885ceb2e45be7d445e56d62499b3c`,
`mullion-command-list.cpp:5007–5068` and `mullion-command-queue.cpp:150–220` use
barrier ordering, grouped pass synchronization and predecessor sets. DXMT keeps
its existing recording owner; no Mullion source or LGPL-covered implementation
is transplanted. Windows native and Mullion runtime observations are NOT_RUN.

### Regression coverage

The existing `dx12_command_list_type.cpp` probe gains `--barrier-records`
(`:4788`): nine checks cover empty, one-sided, early sync-only, mixed legacy
COMMON/narrow stages, UAV, NULL alias, both split types, head/trailing boundaries,
ClearState, same-allocator new recordings, allocator Reset and invalid sync.
`--barrier-records wrong` changes only an expected kind bit and must exit 1.
These are private recording assertions, not GPU ordering or alias-data proof.

`--barrier-records-gpu` extends the existing global/buffer/texture compute oracle
with recorded-scope and post-completion immutability assertions (`:5745`).
Global/buffer cases replay the same closed list; texture cases run once because
their initial/final layouts differ. Each data-bearing submission checks all 64
uint outputs. NO_ACCESS cases intentionally perform no resource access: they
check the native command packets and completion, and label readback NOT_RUN.
`--adjacent-memory-barriers` additionally checks explicit PIXEL→VERTEX NO_ACCESS
scope in a rendered case and retains its native packet/pixel/buffer oracle.

The unchanged enhanced-barrier executable covers admission and actual split
copy/readback across one list, ordered lists, replay and separate submissions
with allocator Reset. These are synchronization regressions; the alias flag's
presence is tested, but a new placed-alias memory-reuse oracle is NOT_RUN.

### Validation and cost evidence

Environment: Apple M1 Max, macOS 27.2 `26B123`, Xcode 27.2 `27B52`, Metal
`32023.922`, Wine 11.0, SDK API 4.0.3 and the existing patched LLVM x64 release
build. All temporary tooling/evidence remains outside the repository in
`dxmt-test/n95-capability-gate/result-msc-barrier-records-20261104/`.
Stages are `stage-msc-barrier-records-20261104` and
`stage-msc-barrier-baseline-20261104`; both use the isolated
`prefix-msc-barrier-records-20261104`. The user's Wine/prefix/toolchain is intact.
The baseline stage's D3D12 bytes are checked against the `65fdbedb` delivery
manifest. Candidate stage files are checked against the current build, including
WineMetal PE/Unix pairing. Logs verify actual load paths and, for the modified
probe and MSC runner, loaded export bytes.

The compiler size probe uses headers from both baseline and candidate:
`EncoderData` 96 → 112 bytes; `RenderEncoderData` 816 → 832 bytes;
`BarrierUse` is 16 bytes. Existing encoder allocations grow by that fixed amount;
there is no new allocation, lock, descriptor scan, draw-time traversal or native
barrier. This is storage size, not measured application resident memory.

Retained non-GPU failure: the first recording fixture switched directly from
legacy COPY_DEST into enhanced buffer semantics. Close correctly rejected it;
the fixture now transitions through COMMON. The initial log is retained.
The size-only compiler probe initially resolved the host GCC wrapper, which
rejects `-fblocks`; it was rerun with the build's explicit LLVM-MinGW compiler.
Neither failure caused a production admission change.


The external performance collector initially mistook `native_buffers` for a
copy-command count; it actually counts new Metal buffer allocations and is zero
in this probe. The executable had passed all 25 readbacks. The corrected
collector reused that saved baseline log, verified its sequences/identities,
and continued without repeating successful GPU work. Both scripts/logs remain.

| Gate / check | Result | Evidence and practical limit |
| --- | --- | --- |
| Gate 0 / Gate 1 | PASS | Current `65fdbedb` baseline, CodeGraph-first owner audit, refreshed Microsoft sources, pinned Mullion source and bounded design above. |
| Gate 2 | PASS | Existing recording owner only; five changed files, no new source module, resource map or queue branch. |
| Full x64 default-target build | LOCAL_PASS | `build-verified.log` / `build-result.json`; exit 0, existing compiler warnings retained. |
| Recording assertions | LOCAL_PASS | Nine checks; altered expected-bit control exits 1. Invalid sync leaves an empty record and rejects Close. |
| Compute/render barrier GPU regressions | LOCAL_PASS | 36 compute packet/record checks (including eight repeated submissions), four render cases, 22 data readbacks; NO_ACCESS cases explicitly label readback NOT_RUN. |
| Enhanced admission / split execution | LOCAL_PASS | Existing admission suite and same-list, ordered-list, replay, cross-Execute/reset split copy/readback cases. Four split result markers; raw logs retain each mode's work. |
| API/GPU validation / deployment | LOCAL_PASS | All 12 barrier processes have API and GPU validation enabled; no validation errors. `runs.json`, `correctness-audit.json`, loaded paths/export bytes and build/stage pairing. |
| MSC IA / VB / texture regression | LOCAL_PASS | 32 processes, 112 full-image and 28 scratch readbacks, eight expected wrong-output exits. `overlap-audit.json`; actual artifact hashes match final stage/build. Validated cases and timing cases remain separate. |
| Scoped formatting / diff check | PASS | `static-checks.json`; only changed C++ regions formatted. |
| Recording/submit cost A/B | MEASURED | Eight alternating processes, 200 exact changing-payload readbacks; summary below. |
| x86 / Windows native / Mullion runtime / new GPU trace / placed-alias data oracle | NOT_RUN | No promotion, overlap or complete dependency claim. |
| Gate 3 / Gate 4 | PASS within recording scope | Source/record/native-packet/output checks pass; performance cost and remaining dependency gaps are explicit. |

Cost uses the pre-existing `dx12_command_list_type --split-cost`: 2,000
begin/end pairs and two copies per round, 25 rounds per process, four processes
per build in ABBA/ABBA order. Exclude each process's first round from timing
statistics (96 warm samples per build); include every round in correctness.
Both builds use the same test executable, WineMetal PE/Unix and MSC bytes.
DXGI differs only in `.buildid`; all other sections are identical
(`dxgi-section-comparison.json`). The two native blit encoders and zero new
Metal buffers remain the same. API/GPU validation is disabled for timing.

| Metric | Baseline median / P95 / range | Candidate median / P95 / range |
| --- | --- | --- |
| CPU record, µs/split pair | 0.096725 / 0.107750 / 0.093750–0.116000 | 0.101050 / 0.109100 / 0.097100–0.114900 |
| CPU Close, µs/round | 45.25 / 50.80 / 43.70–59.70 | 44.20 / 48.60 / 42.00–49.40 |
| CPU submit, µs/round | 101.35 / 153.20 / 91.80–178.50 | 106.35 / 159.40 / 95.80–181.40 |

The observed warm recording median increases by 0.004325 µs per split pair
(about 4.5%). Submit median also rises by 5 µs. The ranges overlap; this is
measured overhead in a barrier-heavy microbenchmark, **not a no-regression
result or performance optimization**. Application frame time, GPU overlap,
occupancy and resident memory were not measured. Retain these bounded facts
only as groundwork for dependency analysis; any scheduling proposal must
justify their cost with its own end-to-end benefit and correctness proof.

Reproduction: use the build environment from `docs/DEVELOPMENT.md` with the
same external patched LLVM, cross file and isolated build-time DXC prefix.
Run `meson compile -C BUILD`, then stage matching PE/Unix files. `runs.json`
contains exact executable arguments and isolated environment. Probe commands:
`dx12_command_list_type.exe --barrier-records`, `--barrier-records wrong`,
`--barrier-records-gpu`, `--adjacent-memory-barriers`,
`--texture-access-only-barriers`; use `dx12_enhanced_barriers.exe` and its
`--split-order-probe MODE` cases above. MSC regression uses the unchanged
`run_msc_execution_overlap.py` with `--processes 2 --samples 2 --iterations 1024`
and the IA/VB switches in the previous section. External `run_checks.py`,
`run_overlap_perf.py` and `resume_perf.py` retain orchestration and raw results;
no runner is added to the repository.

### Delivery audit and next slice

| Changed file | Responsibility |
| --- | --- |
| `src/d3d12/d3d12_command_encoder.hpp:74` | Fixed encoder/head barrier summary, with explicit kind and scope domains. |
| `src/d3d12/d3d12_command_list.cpp:2458` | Collect admitted barrier facts before they are lowered or pass boundaries lose their reason; emit identical texture-range scopes once. |
| `tests/dx12/dx12_command_list_type.cpp:4788` | Recording/negative probes, GPU packet/scope checks, closed-list replay immutability and optional loaded-code identity evidence. |
| `docs/msc-fence-dependency-design.md` | Design, specification/source anchors, regression and cost evidence, boundaries of the proof. |
| `docs/D3D12_MSC_HLSL_PERFORMANCE_WORK.md` | Updated overlap status and remaining work. |

Public FL/SM/Tier, queue Fence stages, WineMetal/public COM ABI and D3D10/11
behavior are not changed. Internal encoder layout changes, so private-record
probes must be rebuilt with the DLL. Unknown shader/indirect/internal accesses,
alias backing identity, descriptor mutation and transitive predecessor sets
remain unresolved; IA plus barrier summaries are not sufficient to relax waits.
The closed-list guard prevents this new summary helper from altering an
allocator's subsequent recording through a closed list. It does not broaden
existing invalid-command-after-Close behavior guarantees.

Commit this slice with `feat(d3d12): preserve explicit barrier execution scopes`;
leave the user's `.gitignore` unstaged and do not push. The external delivery
manifest records the final commit and file hashes. Rollback is this focused
commit; there is no cache/schema migration. Overall MSC/HLSL/performance work
remains incomplete.

**Next recommended slice:** establish ordinary MSC vertex shader resource-access
completeness using the existing reflection/binding owner, with controlled VS
root/table reads and writes plus negative unknown/volatile/indirect paths.
Preserve current waits. This fills the next missing proof component without
adding a scheduler or treating residency deltas as complete resource accesses.

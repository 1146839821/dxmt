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

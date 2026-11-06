# ROTTR D3D12 AIRCONV differential audit and repair

## Outcome

The user confirmed and supplied a screenshot of the correctly rendered main-menu room after the fixes. The previous startup black screen/exit and missing room geometry were traced to D3D12 descriptor and upload behavior. No AIRCONV compiler change or legacy-DXBC-to-MSC rerouting was required.

Menu confirmation: `/tmp/dxmt-rottr-20260918-224730/menu-correct-user.png`. Final clean-build regressions passed; the user also confirmed normal rendering in the built-in benchmark and then manually exited. A visible menu alone does not establish benchmark/gameplay stability or performance.

## Repository state

| Item | Revision/state |
|---|---|
| Baseline B and verified merge base | `d31278d4e7b760feb14ba8d3aaf754b13adedc28` |
| Upstream U | `7c8dee1c2d73415301ceb7d1fa810861cef4cd67` |
| User-supplied ROTTR support boundary | `e67727f28f8e218ac1aaa591a70d5f755a944386` |
| Investigation and review base F | `554322664f8d6fc1a1b15e851d9c574e7f13f60a` |
| Starting remote fork | `f36a5ee561e656139000ff94ccecd5be85ac44ea` |
| Branch | `feat/d3d12` |
| Commit counts after B | U: 83; F: 271 |
| New commits / pushes before final review | None |
| Original local changes | Four unpushed commits; pre-existing untracked `.porting/` files preserved |
| Final changes | D3D12 command list, descriptor heap, device, two regression tests, this report |

Baseline and upstream comparison worktrees remain `/tmp/dxmt-rottr-base-20260918` and `/tmp/dxmt-rottr-upstream-20260918`. They were not modified. All pre-existing game logs were excluded from causal conclusions. Fresh runs used the user's Wine, prefix, Steam launch, and exact synchronization script.

## Proven runtime defects

### 1. Valid descriptor copies were ignored

`CopyDescriptors` rejected null source/destination range-size arrays. These arrays are optional; each omitted entry size is one. The loop already supported that rule, but an early argument check made it unreachable. Fork commit `651c078` introduced the regression.

ROTTR actually calls this with a null source-size array, including one destination range and 16 source ranges. Before the correction, texture tables were left unpopulated and GPU page faults occurred in startup/UI rendering. After the correction, those calls copy their descriptors, the relevant UI draw accounts for its texture, and the game reaches a visible menu.

Regression: omitted destination sizes, omitted source sizes, and both omitted fail before the correction and pass after it. Logs: `/tmp/dxmt-rottr-copy-optional-before.log` and `/tmp/dxmt-rottr-copy-optional-after.log`.

### 2. Required upload size included trailing row padding

`GetCopyableFootprints` returned `slice_pitch * depth`, counting padding after the final row. Required bytes must end at the last byte of that row; row and slice pitches still describe padding between rows/slices. This regression also originates in `651c078`.

A 33-byte buffer was reported as requiring 256 bytes. In ROTTR, matching DEFAULT/UPLOAD buffers of 40,140, 846,608 and 207,024 bytes were created but never mapped or copied. Microsoft's [UpdateSubresources implementation](https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3dx12_resource_helpers.h) returns before Map when the intermediate buffer is smaller than the reported required span.

GPU capture independently showed all 103,512 indices and all 846,608 vertex bytes of one large mesh were zero; another mesh's 40,140-byte index buffer was also zero. Visible small objects used a populated dynamic index buffer. These large draws produced no depth or normal pixels, explaining missing walls, floors and furniture without a shader hypothesis.

The fix subtracts the final `row_pitch - row_size` padding. After it, the same game allocations report exact required sizes, Map and CopyBufferRegion occur, and source data is nonzero. The user confirmed that the room is correctly rendered. Evidence: `/tmp/dxmt-rottr-20260918-224730/upload-recovery-evidence.log` and the screenshot in that directory.

Regressions cover buffer widths 33, 256, 257 and 4097. Texture expected totals are independently corrected to 4612 bytes for the RGBA8 mip/array fixture, 272 bytes for 5x5 BC1, and 1796 bytes for 4x4 depth/stencil. GPU readbacks cover arrays, BC, depth/stencil and 3D copies using the corrected minimal sizes.

### 3. CPU descriptor reads raced with descriptor updates

A fresh crashpad dump from this investigation (21:14 on September 18) identifies `Texture::view` at d3d12 RVA `0x68224`, called by the SRVTexture branch of `EncodeMSCResourceUses`. Its supposed texture pointer was a GPU VA and its view key a CBV size. The descriptor memory had already become type ConstantBuffer: the reader selected one union alternative and then consumed another after a concurrent update. A later fresh Wine SEH trace independently reproduced the same PC and pattern.

Fork commit `43828bc` made ordinary AIRCONV graphics invoke this pre-existing shared CPU resource walker. Its name does not prove MSC shader or GPU-descriptor ABI leakage; the concrete bug is unsynchronized CPU descriptor access.

The fix returns a scoped descriptor read that holds the heap mutex while its type, union payload and owned resources are consumed. Creation, clear and descriptor copy use the same lock; copies lock both heaps without deadlocking, and same-heap copies lock once. Both residency enumeration and UAV clear readers use this API.

A synthetic concurrent SRV-to-CBV overwrite fails before the change through direct creation and descriptor copy, and passes afterward. The earlier Texture::view access violation did not recur in the subsequent tested path. Logs: `/tmp/dxmt-rottr-descriptor-before.log` and `/tmp/dxmt-rottr-descriptor-after.log`.

## Additional validation corrections

- Compute encoders translate RenderTargets barrier scope to Textures; Metal compute only accepts buffer/texture scope bits.
- Scissors are clipped to the actual render-pass attachment extent instead of a fixed 16384 limit.

Both were exposed by fresh Metal API validation. They are general corrections and were insufficient by themselves to resolve ROTTR.

## Backend and GS isolation

The locally captured corpus contains 877 unique legacy DXBC blobs: 153 VS, 696 PS, 13 GS and 15 CS. DXIL, HS and DS counts are zero. The initial creation census observed 2760 AIRCONV-only pipeline initialization objects, with no MSC or mixed-backend initialization. These are creation counts, not a claim that every PSO was created successfully or submitted.

The 13 GS blobs independently declare DXBC program type GS. All instrumented runs through the tested menu path recorded zero GS draws. Thus the game creates GS shader/pipeline inputs, but this tested failure does not execute GS. No claim is made about untested later gameplay.

Loading the MSC runtime for capability queries is not MSC shader conversion. MSC compiler/capability files, DXIL routing, AIRCONV_VERSION 27, and public AIRCONV/GPU descriptor layouts were left unchanged. The shared CPU descriptor lock applies consistently to both callers.

## Differential replay

Inputs were kept locally under `/tmp/dxmt-rottr-corpus-20260918`; `manifest-menu.json` records SHA-256, size, stage and container. No proprietary blob, IR, trace or image was added to the repository.

| Set | Captured tuple | Result |
|---|---|---|
| 15 CS | DXBC, root signature, common flags/version, entry point | B/U/F parse, compile, library, function and compute PSO pass; all U/F metallibs identical and F matches runtime |
| Startup/UI: 1 VS + 3 PS | Root signature, common data, input layout or full PS state including pixel formats | B/U/F parse/compile/library/function pass; U/F/runtime bytes identical |
| Scene: 6 VS hashes / 21 tuples | Actual layout variants and root signatures | All U/F/runtime metallibs identical |
| Scene: 4 PS tuples | Depth-prepass/color-pass pixel formats and flags | All U/F/runtime metallibs identical |

B differs from U/F for two CS shaders; U and F match. No U-good/F-bad compiler result was found in the replayed sets. Graphics replay does not assert complete graphics PSO or GPU execution equivalence.

Results: `/tmp/dxmt-rottr-replay/results.json`, `graphics-results/target-results.json`, `menu-vs-results/results.json`, and `menu-ps-results/results.json`.

## Capture evidence

- `/tmp/dxmt-rottr-20260918-220212/rottr-80028-menu.gputrace`: 997 draws, 20 dispatches, 113 encoders.
- The same run's `rottr-80028-menu-confirmed.gputrace`: 999 draws, 20 dispatches, 113 encoders.
- `/tmp/dxmt-rottr-20260918-221805/rottr-84477-menu.gputrace`: replayable diagnostic capture; 994 draws, 20 dispatches, 114 encoders. Exported depth, normals, vertex and index data establish the zero-buffer defect.

The first two traces are statically readable but local replay rejects render memory barriers. A temporary opt-in pass-splitting diagnostic enabled the third trace's replay and reproduced the same missing geometry. That workaround and every diagnostic added during this task have been removed from final source. The successful room run had the workaround disabled.

## Regression and runtime validation

The final validation record is `/tmp/dxmt-rottr-final-evidence/`. It contains the exact user-script output, deployed-file SHA-256 checks, test logs and structured results. Each code rebuild was followed by the provided synchronization script; relevant x64/x86 DLLs and the Unix SO were checked against deployed copies. Game launches used Steam AppID 391220 and `-d3d12` through the specified Wine/prefix.

The resource regression, descriptor regression, 16 SM5 GPU readbacks and enhanced-barrier tests are the final check set. D3D11 shared AIRCONV runtime regressions were not required by a shared compiler change: no shared AIRCONV or D3D11 implementation was edited. The earlier full build included D3D11; a new D3D11 runtime validation is not claimed.

Correct menu rendering and normal benchmark scenes are user-confirmed. Final cleaned-build details appear below. No performance improvement is claimed from a single menu screenshot.

## Audit scope and preservation

Compiler findings are based on the changed-file inventory, selected high-risk source comparisons and exact captured tuples. An exhaustive opcode census and every public-struct/function variant were not completed; this report does not label those as passed. The causal fixes are in D3D12 host API behavior and CPU descriptor access.

No tiled feedback, stream output, tessellation, DXIL, MSC, or capability feature was removed. No executable-name/shader-hash special case remains. Temporary game-specific buffer-size probes were removed. The initial local commits and `.porting/` files remain intact.

## Selected AIRCONV compiler comparisons

| file / symbol | B behavior | U behavior | F behavior | classification | ordinary DXBC? | ROTTR relevance / evidence |
|---|---|---|---|---|---|---|
| `airconv_public.h`: `AIRCONV_VERSION` | 24 | 26 | 27 | BOTH_CHANGED_DIFFERENTLY | cache only | Cold-cache diagnostic; version is not a root cause by itself. |
| `airconv_public.h`: `SM50_SHADER_PSO_PIXEL_SHADER_DATA.pixel_formats` | absent | present | present | COMMON_EQUIVALENT | yes | F call site fills formats from the render PSO; compare against U before changing. |
| `dxbc_converter.cpp`: `component_type_from_pixel_format` | signature type only | RT format type selection | same RT format selection | COMMON_EQUIVALENT | yes | Required for typed MRTs; no fork-only divergence found. |
| `dxbc_converter.cpp`: `convert_dxbc_pixel_shader` | signature type only | copies `pixel_formats[8]` | same | COMMON_EQUIVALENT | yes | F passes `ORIGINAL_FORMAT(info.colors[i].pixel_format)`; call-site tuple must be checked. |
| `dxbc_signature.cpp`: `handle_signature_ps` | signature component type | format-derived fallback | same plus bounds guard | SEMANTIC_EQUIVALENT | yes | Only changes invalid/out-of-range metadata fallback. |
| `dxbc_instructions.cpp/.hpp`: feedback operands and `CHECK_ACCESS_FULLY_MAPPED` | no feedback AST | no feedback AST | parses feedback opcodes/status destinations | FORK_ONLY | only tiled/sparse shaders | Corpus captured; opcode census still pending. |
| `nt/dxbc_converter_base.cpp`: `StoreFeedback` and feedback stores | no status store | no status store | stores D3D mapped status when optional destination exists | FORK_ONLY | only tiled/sparse shaders | Conditional on a feedback operand; keep, but do not let it alter no-feedback lowering. |
| `air_type.cpp`: `dxmt_stream_output_buffer_entry` | pointer + pointer | pointer + pointer | pointer + pointer + `long size` | FORK_ONLY | only stream output | P0 ABI risk for SO; SO submission was not comprehensively counted; no SO code was changed. |
| `dxbc_converter.cpp`: vertex SO epilogue | no bounds check | unbounded VS SO epilogue | uses new size field for bounds check | FORK_ONLY | only stream output | Host/shader layout must remain atomic; no SO code was changed. |
| `compiled_bitcode.hpp`, `metallib_patch.cpp` | no patch API | no patch API | unsupported-double AIR patch API | FORK_ONLY | MSC/DXIL only | Must remain frozen for an AIRCONV-only ROTTR repair. |
| `dxbc_converter_ts.cpp`: separate VS/HS root arguments | old binding model | separate root-signature support | falls back to HS root when VS root is absent | BOTH_CHANGED_DIFFERENTLY | tessellation only | Upstream D3D12 rejects HS/DS; do not treat as ROTTR cause without runtime counts. |
| `nt/air_builder.cpp`: null texture query | undefined null query | zero on null handle | same | COMMON_EQUIVALENT | yes | Safe parity fix, unrelated to MSC. |


## Canonical changed-file inventory

The inventory below was generated from B→U and B→F. “Needs function-level classification” explicitly denotes remaining broad audit scope, not a discovered ROTTR failure.

| File | B→U | B→F | U→F | Scope |
|---|---|---|---|---|
| `src/airconv/air_signature.hpp` | True | True | equivalent ignoring EOL whitespace | runtime: needs function-level classification |
| `src/airconv/air_type.cpp` | False | True | different | runtime: needs function-level classification |
| `src/airconv/airconv_cli.cpp` | False | True | equivalent ignoring EOL whitespace | offline CLI only |
| `src/airconv/airconv_public.h` | True | True | different | runtime: needs function-level classification |
| `src/airconv/compiled_bitcode.hpp` | False | True | different | runtime: needs function-level classification |
| `src/airconv/darwin/meson.build` | False | True | different | build only |
| `src/airconv/dxbc_converter.cpp` | True | True | different | runtime: needs function-level classification |
| `src/airconv/dxbc_converter.hpp` | True | True | different | runtime: needs function-level classification |
| `src/airconv/dxbc_converter_ts.cpp` | True | True | different | runtime: needs function-level classification |
| `src/airconv/dxbc_instructions.cpp` | False | True | different | runtime: needs function-level classification |
| `src/airconv/dxbc_instructions.hpp` | True | True | different | runtime: needs function-level classification |
| `src/airconv/dxbc_signature.cpp` | True | True | different | runtime: needs function-level classification |
| `src/airconv/meson.build` | False | True | different | build only |
| `src/airconv/metallib_patch.cpp` | False | True | different | MSC metallib patch, not SM50Compile |
| `src/airconv/nt/air_builder.cpp` | True | True | different | runtime: needs function-level classification |
| `src/airconv/nt/dxbc_converter_base.cpp` | True | True | different | runtime: needs function-level classification |
| `src/airconv/nt/dxbc_converter_base.hpp` | False | True | different | runtime: needs function-level classification |
| `src/airconv/tests/lower_unsupported_double.cpp` | False | True | different | MSC metallib patch, not SM50Compile |
| `src/airconv/transforms/lower_unsupported_double.cpp` | False | True | different | MSC metallib patch, not SM50Compile |
| `src/airconv/transforms/lower_unsupported_double.hpp` | False | True | different | MSC metallib patch, not SM50Compile |



## Final clean build verification

All temporary diagnostics and the split-render-barrier workaround were removed before the final build. x64 and x86 D3D12 DLL builds passed; the Unix SO target was current. The supplied script synchronized deployment and SHA-256 verification matched all 25 copies.

Final tests with Metal API validation all exit 0: `dx12_descriptor_ownership`, `dx12_resource_tests`, `dx12_graphics_sm5 --all` (16 GPU readbacks), and `dx12_enhanced_barriers`. The logs contain no Metal failed assertion, Invalid Metal usage, or command-buffer error-domain report. Expected negative D3D12 test warnings are not counted as failures. Exact results are `/tmp/dxmt-rottr-final-evidence/tests.json` and `hashes.json`.

The cleaned build ran from Steam in `/tmp/dxmt-rottr-20260918-230006`, PID 96238, with ordinary info logging and no shader validation/capture/dump. The user confirmed normal benchmark scene rendering and then manually exited. Full benchmark completion, exact benchmark duration and a performance score were not established.


No new renderer/GPU failure or game exception was found in the final clean-build logs. This establishes the tested menu and benchmark-scene rendering result, not an exhaustive game-completion or performance claim.


## Final review

Independent Standards and Spec/correctness reviews found no code blockers. Their documentation finding (stale running/pending status) was corrected to the user-confirmed normal benchmark scene and manual exit. The broader function/opcode audit remains explicitly incomplete. No unrelated capability/compiler changes or proprietary assets are included in the commit scope.

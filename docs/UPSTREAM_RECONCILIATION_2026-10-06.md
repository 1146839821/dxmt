# Task Analysis

## Current Branch

`feat/d3d12-1`, clean at start; HEAD `593f4ac`.

## Baseline

User-confirmed 3Shain/dxmt main, pinned
`e40c9d7ae1cf476ee9cc54b9c2cab6ed09b2e595`. Range strictly after
`fb4515681daefb789a4d0f403c4bdbca88f3b3de`: 14 commits. No merge/rebase or
moving-head integration; source objects fetched without updating branch refs.

## Local Commits Since origin/feat/d3d12

Current local D3D12 branch extends its separate pinned `bad6756a` reconciliation
history; this task is the newly requested upstream main range, not those 17.

## Current State

All 14 upstream commit objects available. Local shader, typed-origin, MinMax,
descriptor flags and submission lifetime work must remain intact.

## Existing Implementation

Many paths diverge, particularly D3D12 graphics/command list, AIR lowering and
NVNGX. Member-init repair at the excluded start commit is already present.

## Relevant Files

18 upstream files across D3D10/11/12, DXMT, AIRCONV, NVAPI/NVNGX and native cache.

## Existing Tests

12 native host regressions; D3D11 fixture inventory and cached D3D12 API/GPU
probes. Coverage must be distinguished from compile-only integration.

## D3D12 Contract

Preserve fail-closed pipeline construction, descriptor flags, submission
ownership, backend isolation, existing resource states and capability limits.

## DXBC / AIRCONV Impact

Upstream interpolation/integer operation and declaration logic fixes need
adaptation without discarding later direct API safety or sampler changes.

## DXIL / MSC Impact

Input-layout failure ordering and copy-paste repair only where applicable;
preserve compiler routing and later emulation work.

## Shared Runtime Impact

D3D11 binding/mutex/copy/default-output and shared clear fixes; NV ABI/string
defaults; no new universal frontend or thunk numbering redesign.

## Missing Pieces

Per-commit semantic adaptation ledger, original-message local commits,
proportional build/regression evidence and recoverable temporary-file archive.

## Hypothesis / Evidence / Expected Effect

Exact upstream patches identify narrow defects. Compare current source before
adapting each; textual application alone is not proof of preserved semantics.

## Risks

Old context overwriting newer branches; partial patch equivalence; shared LLVM
semantics; introducing unsupported format advertisements; deleting evidence.

## Minimal Implementation Plan

Apply_patch per commit, self-review, finish builds before staging and commit with
`git commit -C <upstream SHA>` to preserve original message/author. Independent
final Standards/Spec review; final regression; archive only explicit obsolete
temporary files after inventory, retain evidence and current runtime overlays.

## Validation Plan

Reconfigure both build modes before checks. Full normal/no-private checkpoints,
host tests and relevant existing fixture runs; record untested GPU breadth.
Compare all local commit messages against upstream and every requested semantic
slice. No push, game/prefix DLL writes or Steam/process-management actions.

## Capability Impact

No Feature Level promotion. Corrections to existing format lookup entries are
not qualification of previously disabled D3D12 features.

# Integration ledger

| Upstream | Intent | Local adaptation |
| --- | --- | --- |
| `830eb419` | Vertex binding slots | `72c399f`: 32-slot constant |
| `f9beaea3` | Mipped clear dimensions | `cbfd357`: view dimensions; retain local depth-plane handling |
| `acda4f94` | Remove false try_lock | `f208cf1`: unused false-success method removed |
| `a22700c7` | Copy-paste repairs | `53db846`: four fixes; `630742a`: floating UAV clear repair found in review; integer path already correct |
| `59eb4c1a` | NGX error values | `250c24a`: empty coverage record; existing extended enum already correct |
| `542ca170` | Pre-exposure default | `699f8fd`: shared D3D11/12 default 1.0 |
| `e4f868dc` | D3D10 output pointer | `91670f7`: clear caller output |
| `0fff7ffe` | NVAPI terminators | `1d8c9bb`: five string-copy terminators |
| `ce79ad0a` | Dilated motion vectors | `7dac5f0`: cache key, format and view fixes |
| `0b639908` | Fence context locking | `bf6255d`: recursive device mutex guards |
| `ae912698` | AIR logic fixes | `2dbe760`: reflection/intrinsics plus fail-closed unknown opcode and cache version 30 |
| `a3d90055` | Input-layout failure ordering | `9e42b45`: return before consuming output count |
| `68af85e9` | Null state outputs | `0036cbb`: nullable validation-only output forwarded |
| `e40c9d7a` | Cross-type texture copy | `f464b34`: independent dimensions and temporary-buffer path |

# Task Result

## Implementation and review

All 14 upstream commits represented in order, with one review-driven follow-up,
without merge or rebase. Automated
comparison confirms complete commit messages, author names/emails and author
dates match the pinned upstream commits. The already-covered NGX error change
has an explicitly empty record rather than replacing the richer local enum.
Every source patch was inspected before its individual local commit.

AIRCONV deliberately does not silently drop unknown instructions: the parser
returns a defined placeholder and records the unsupported opcode; initialization
rejects it before publishing the shader. Both optional-error variants are tested.
The valid control shader initializes and compiles. This reproducer failed with
SIGSEGV before the fix and passes afterward. The GS signature check, float2
interpolation-offset signature and mul_hi intrinsic name are ported; version 30
invalidates AIR caches affected by changed lowering/reflection.

## Validation

Normal and no-private configurations completed full builds at every source-change
checkpoint. NVAPI was originally disabled: initial component-independent builds
are not NVAPI acceptance. Both builds were reconfigured with enable_nvapi=true
and completed again, including NVAPI, before its commit and all later checkpoints.
Final native tests: 12/12 normal and 12/12 no-private. git diff --check passes.
Evidence retained at /Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE:
upstream-01 through upstream-14 build logs (05 has no source change),
upstream-08-nvapi-build*.log, upstream-11-native-*.log and
upstream-final-native-*.log.

These are build/native API results, not end-to-end GPU qualification. Fresh
D3D11 cross-type-copy and mip-clear GPU readback, nullable-state Wine probes,
NVAPI ABI probes, motion-vector rendering, fence concurrency and intrinsic GPU
results remain unverified. No new game performance/tessellation result is
claimed. FL/capability promotion remains blocked by the existing broader matrix.

## Recoverable cleanup

After listing the exact targets, five superseded temporary files were moved,
not deleted, from /Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE to
/Users/zhangbo/.cache/dxmt-test-archive-2026-10-07.X97Teq. See MANIFEST.md there
for names, SHA-256 and restore instructions. Current verification evidence,
runtime overlays, build directories, .codegraph, game directories and Wine
prefix were not cleaned or overwritten. Archiving does not reclaim disk space.

## Remaining scope

The requested upstream source integration is complete. GPU acceptance is
not inferred from compilation. The broader FL12_0 to FL12_1 development goal
remains open; no push performed.

## Standards

Independent review: no documented-standard hard violation. One judgement
suggestion: duplicated source/destination dimension dispatch in texture-copy
subresource decoding, also similar to texture-update decoding. A shared decoder
is a possible future refactor, not required for preserving the upstream patch.
No unrelated runtime refactor was folded into these imported commits.

## Spec

Independent review found one incomplete adaptation in a22700c7: the integer UAV
clear was correct but the float path still accessed SRVTexture. Fixed in
630742a using the same original upstream message, without history rewriting;
the retained ReadDescriptor owner was preserved. Both full builds and 12/12
native tests in each configuration passed again after this fix. No other
specific mismatch was reported across the remaining 13 patches.

Summary: Standards 0 hard violations and 1 maintainability suggestion; Spec 1
patch omission found and fixed, 0 unresolved implementation findings. GPU
qualification gaps listed above remain open, not review-proven successes.

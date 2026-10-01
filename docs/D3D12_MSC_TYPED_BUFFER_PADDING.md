# Task Analysis

## Current Branch

`feat/d3d12-1`.

## Baseline

`3e94415`, exact-range MSC typed-buffer views. Preserve unrelated dirty
`include/native/directx` submodule; commit locally without pushing.

## Local Commits Since origin/feat/d3d12

Latest local commits: `3e94415`, `9f84f84`, `5ffc864`, `6c03bed`, `3b47b7f`,
`f73e535` (read-only `git log origin/feat/d3d12..3e94415`). This diagnostic compares against `3e94415`,
not against a reset or rewritten upstream history.

## Current State

Fresh staged DXIL buffer-only execution: 42 PASS / 12 FAIL. Direct invocation
from the repository root initially lacked relative shader files and produced
54 failures; discarded as an invalid reproduction. The staged harness copies
all eight DXIL shader files and the matching runtime before execution.

## Existing Implementation

Descriptor-owned exact-range texture-buffer views support aligned offsets.
Static unsupported views fail recording; volatile final unsupported views reject
translation. Static descriptors never enter submission live reread.

## Relevant Files

`src/d3d12/d3d12_descriptor_heap.cpp`, `d3d12_command_list.cpp`,
`src/winemetal/unix/metalirconverter.c`, installed MSC companion/API headers,
`tests/dx12/typed_uav_formats.hlsl`, new native probe and atomic HLSL.

## Existing Tests

144-case GPU matrix, 126 UAV / 126 SRV contracts per backend, submission
resolver seam, and gate unit tests. Native padding semantics lack an independent
test outside Wine/DXMT; this task adds one.

## D3D12 Contract

FirstElement shifts the logical view origin without violating alias coherence.
Legal D3D offsets cannot be treated as a different origin. A capability is not
complete while arbitrary legal offsets remain unsupported.

## DXBC / AIRCONV Impact

None planned; preserve whole-buffer view and compiler-applied element offset.

## DXIL / MSC Impact

Investigate the documented padding ABI without changing executable family or
rewriting generated AIR.

## Shared Runtime Impact

None planned. The native diagnostic bypasses DXMT/Wine and uses the installed
MSC API and companion setter directly.

## Missing Pieces

Ranked falsifiable hypotheses:

1. Compiler compatibility settings enable padding: BoundsCheck must change
   coordinates and make legal padded views match the CPU oracle.
2. Runtime encoding differs from the helper contract: using the companion
   `IRDescriptorTableSetBufferView` directly must fix the mismatch.
3. Atomic operations consume padding differently: an InterlockedAdd probe must
   address the desired element even if ordinary SRV/UAV access does not.

## Risks

Metal validation may not report a valid but semantically wrong address.
Copy-to-aligned shadow storage breaks simultaneous raw/typed/overlapping aliases
unless a separate coherent lowering architecture is proven. A diagnostic's
expected-unsupported exit must never promote D3D12 capabilities.

## Minimal Implementation Plan

Native GPU probe with one explicit RS1.1 table, reflection-derived TLAB offset
and threadgroup size, legal aligned native views, companion padding encoding,
and independent full-buffer/output CPU oracle. Test SRV, UAV, atomics and
BoundsCheck with aligned controls. Only implement a production repair if one
of these paths proves the required semantics.

## Validation Plan

Reproduce production rejection through staged Wine execution. Inspect fresh
unmodified MSC AIR. Run native probe normally and with Metal API/GPU validation;
build it under normal/no-private configuration, run Meson regressions and review
both Standards and Spec. Keep expected-unsupported diagnostics out of FL gates.

## Capability Impact

None. Additional typed UAV formats remain disabled; FL12 promotion remains
blocked. Failure evidence is not completed feature support.

# Task Result

## Implementation and evidence

Added an opt-in native C/Objective-C ARC diagnostic, not production behavior.
It compiles the existing R32_UINT UAV/SRV fixtures and an atomic fixture using
the installed MSC library. It uses the companion `IRBufferView` and setter,
including element padding, desired GPU VA and logical byte length.

Five FirstElement values: 0, 1, 4, 257, 260. R32 native alignment on Apple M4
is 16 bytes. The native view origin is rounded down legally; only the diagnostic
uses this wider view, specifically to test whether MSC applies the padding.
Production continues to reject unsupported offsets instead of issuing it.

MSC 4.0.1, Apple9 target, minimum macOS 16, explicit RS1.1, compatibility
TextureMinLODClamp with and without BoundsCheck:

| Operation | Zero-padding controls | Padding=1 cases |
| --- | --- | --- |
| UAV read/store | 6 match | 4 mismatch output and buffer |
| SRV read | 6 match | 4 mismatch output; buffer unchanged |
| Typed atomic add | 6 match | 4 mismatch output and buffer |

Total: 18 matched aligned controls, 12 mismatched padded cases. The full input
allocation, including prefix/suffix sentinels, is compared to the CPU oracle.
Metal API and GPU Validation activation is confirmed; no Metal validation error
was observed. A successfully completed command buffer did not establish correct
view semantics.

Fresh MSC `-c` output, read with LLVM 15 `llvm-dis`, also shows typed read,
write, and atomic coordinates adding constant zero. BoundsCheck changes its
module flag but does not introduce descriptor-padding coordinate adjustment.
No AIR mutation or shader backend fallback was performed.

All three hypotheses failed for this tested converter/device combination.
This isolates an MSC integration blocker; it does not claim that every converter
version or GPU has the same behavior. No native compiler flag was enabled in
production because it did not solve the independent GPU test.

## Status

- Changed files: native probe C, atomic HLSL, opt-in Meson targets, this report.
- Tests run: both native targets build; both Meson suites pass (including 16
  gate unit tests). Native default mode exits 77 with UNSUPPORTED; expected-
  unsupported mode exits 0 under Metal validation in both build configurations.
  Both produce 18 matched controls and 12 padded mismatches. These native runs
  bypass both Wine runtime variants, not full no-private runtime acceptance.
  No PE/Unix library was replaced; installed normal runtime remains unchanged.
- Git status: only task files included; dirty DirectX submodule preserved.
- Task: BLOCKED on a validated padding-aware MSC lowering or equivalent coherent
  implementation; non-aligned support is **not fixed**.
- Capability: PARTIAL; no API feature bits or feature levels changed.
- DXBC/AIRCONV and shared runtime: unchanged.
- Local commit: the commit containing this report.
- Push: NOT PUSHED.
- Remaining work: obtain and validate an MSC correction; alternatively, design
  an explicit DXIL-side lowering that preserves format conversion, atomics,
  bounds and aliases before implementing it. This is not a hidden AIRCONV fallback.
- No game, performance, tessellation or end-to-end FL12 acceptance claimed.

## Self-review

### Standards

One P2 evidence-classification finding was fixed: failed aligned controls or no
padded cases now print INCONCLUSIVE, never SUPPORTED. No remaining actionable
standards/heuristic findings identified. ARC objects survive completion; native
MSC/compiler/root signature cleanup order was reviewed.
Negative control: deliberately supplying the atomic shader in the ordinary
UAV slot makes six aligned controls fail; the probe reports INCONCLUSIVE and
exits 1, rather than treating shader/oracle mismatch as supported semantics.

### Spec

No new actionable defect in the diagnostic change. The actual feature request
remains incomplete under master prompt section 16: the probe isolates the
tested MSC padding limitation but does not implement arbitrary offset support.
Expected-unsupported results are not feature gate inputs. Shader backend
separation and production capability reporting remain unchanged.

## Reproduction

Configure as usual, then build the opt-in native target:

```sh
meson setup --reconfigure build
meson compile -C build msc_typed_buffer_padding_probe
```

Use existing `typed_uav_1_0.cso` and `typed_uav_srv_1.cso`. Generate the new
atomic fixture with the same DXC used by the existing matrix. The local game
Wine invocation (no global wineserver restart) is:

```sh
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
WINESERVER=/Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wineserver \
MVK_CONFIG_LOG_LEVEL=0 \
/Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wine tools/dxc/bin/x64/dxc.exe \
  -E main -T cs_6_0 tests/dx12/msc_typed_buffer_padding.hlsl \
  -Fo build/tests/dx12/msc_typed_buffer_padding.cso

MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 \
MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 \
build/tests/dx12/msc_typed_buffer_padding_probe \
  build/tests/dx12/typed_uav_1_0.cso build/tests/dx12/typed_uav_srv_1.cso \
  build/tests/dx12/msc_typed_buffer_padding.cso
```

Exit 77 and `status=UNSUPPORTED` mean padding semantics are incomplete, not a
passing GPU capability. Opt-in `--expect-unsupported` returns zero only after
valid aligned controls and mismatches in every observed padded case; that mode
checks reproducibility of the known limitation, not correctness. Compile,
pipeline, allocation, GPU completion and aligned-control failures return 1.
Do not register that mode as a feature acceptance test.

SDK/library absence omits the native target rather than adding an SDK dependency
to normal DXMT builds. The probe currently requires Apple9 and only tests
R32_UINT, in compute, with in-bounds indices. Wider format/OOB/graphics support
must be independently tested when a candidate production solution exists.

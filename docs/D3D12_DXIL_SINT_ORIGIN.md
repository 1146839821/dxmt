# Scalar signed INT offline typed-origin lowering

## Task Analysis

Baseline `3ad5c54`, branch `feat/d3d12-1`, clean task-start worktree.
Hypothesis: scalar SINT uses the existing i32 load/store grammar and can reuse
logical-count/origin guards while preserving signed texture conversion.
Evidence: the real `typed_uav_2_0.cso` DXC dump declares `RWBuffer<int>`, component
metadata 4, i32 typed load/store, and UINT u1 output; FLOAT/UINT already have a
bounded test adapter and reflected native probe with whole-buffer oracle.
Expected effect: offline signed load/store support only, not production support.
Risk: wrong sign extension for R8/R16, accepting an unsupported signed atomic,
or treating two native builds as two production backend acceptance matrices.
Validation: parser boundaries, full-container input/output validation, native
R8/R16/R32_SINT negative/boundary values, OOB and sentinels, original shader
negative controls, fresh FLOAT/UINT regressions in both builds.
Scope: test adapter/probe only. No production descriptors/root/cache, alias
translation, capability or FL promotion. LLVM/MSC/validation skills apply;
self-review and local commit required, never push.

## Task Result

Implemented bounded `Buffer<int>` / `RWBuffer<int>` load/store support in the
test-only adapter. An explicit component enum requires metadata 4 for SINT,
5 for UINT and 9 for FLOAT; output u1 stays UINT. SINT shares i32 typed operations
and the existing logical-count/non-wrapping-origin guards. OOB load merges zero,
OOB store skips; direct static resource identity restrictions are unchanged.
An explicit atomic flag keeps signed atomics rejected without confusing an i32
load aggregate with an i32 atomic result. UNORM/vector and general provenance
remain outside the grammar. See [component contract](../tests/dx12/dxil_origin_transform.hpp#L68).

The native probe's single bounded format table holds element width, stored
bits, expected loaded bits and sentinel seeds. Signed R8/R16 use low-byte
backing representations and sign-extended 32-bit output expectations. R32
uses full-width bits. Each set contains minimum negative, maximum positive,
-1 and one other positive value. Entire backing allocations are compared,
not merely output values. Existing FLOAT and UINT data/poison policies are
preserved. See [format oracle](../tests/dx12/msc_typed_buffer_padding_probe.c#L15).

The atomic HLSL fixture defaults to UINT exactly as before; an opt-in Meson
target supplies TYPE=int for a real signed-atomic rejection container. This
does not add signed atomic compilation or dispatch to the native probe.

### Fresh validation — 2026-10-02

Both configurations were reconfigured before compilation. After the final
review fixes, each passed Meson 3/3 and parser 52/52. Synthetic parser mutations
are not valid-DXIL semantic evidence. SINT mismatched-operation, SRV-write,
atomic and partial-mask negatives check exact reasons; the partial mask targets
only the SINT input store, leaving UINT output mask 15 unchanged.

The final two tools each accepted and DXC-validated seven original containers:
SINT UAV/SRV, FLOAT UAV/SRV and UINT UAV/SRV/atomic. Each rejected five real
unsupported vector/UNORM profiles plus the new real SINT atomic fixture. All
26 expected contracts passed; input SHA-256 remained unchanged, rejected output
files absent. The signed atomic reaches the adapter after full input validation
and specifically rejects with `signed atomic outside bounded grammar`.
Accepted outputs undergo assembly and full output validation before writing.

Final native execution on Apple M4 with API and Shader validation enabled before
device creation produced these results **per build**:

| Matrix | Cases | Result |
| --- | ---: | --- |
| R8_SINT UAV/SRV | 140 | All output/backing matches; 112 padded, 28 aligned |
| R16_SINT UAV/SRV | 140 | All matches; 112 padded, 28 aligned |
| R32_SINT UAV/SRV | 140 | All matches; 56 padded, 84 aligned |
| R16/R32_FLOAT regression | 280 | All matches |
| R32_UINT UAV/SRV/atomic regression | 210 | All matches |
| Original SINT shaders, all three formats | 60 | 40/40 padded mismatches, 20/20 aligned matches |

Semantic matrices use FirstElement 0/1/4/257/260, counts 8/0/1/3/4/5/7, UAV/SRV
and MSC BoundsCheck off/on. Original-shader controls use full count and bind
the same origin/count CBV without transforming the shader. R8 and R16 each
have 16/16 padded mismatches; R32 has 8/8. The external matrix requires actual
case counts, completed GPU execution, valid aligned controls and
`PROTOTYPE_MISMATCH`, all padded cases mismatching, and no GPU/MSC/validation
errors. Exit 1 alone is insufficient. Unknown mode rejects before device
creation in each build. No observed API/Shader validation errors or warnings.

Fresh production selected cases in the unchanged normal Wine prefix remain
separate: DXIL R16_SINT buffer FirstElement=4 fails at recording with native
view unavailable and Close `0x80004005`; FirstElement=8 passes readback. DXBC
FirstElement=4 passes readback. All three expected contracts passed. No runtime
DLLs were replaced, so this is not a two-configuration production GPU matrix.

### Reproduction / receipts

For each BUILD (`build`, `build-no-private`):

```sh
meson setup --reconfigure BUILD
meson compile -C BUILD dxil_roundtrip dxil_origin_transform_test msc_typed_buffer_padding_probe \
  msc_typed_buffer_padding_fixture msc_typed_buffer_padding_sint_fixture
meson test -C BUILD --print-errorlogs
```

Through repository Wine, pass `typed_uav_2_0.cso` / `typed_uav_srv_2.cso` to
`BUILD/tests/dx12/dxil_roundtrip.exe`, each with a fresh nonexistent output,
absolute Windows DXC directory and `--lower-typed-origin`. DXC directory is
`Z:/Users/zhangbo/Documents/Vibe-Codeding/dxmt/tools/dxc/bin/x64`. The real
`msc_typed_buffer_padding_sint.cso` is an expected rejection, not GPU acceptance.
For each native format, use the transformed UAV/SRV outputs:

```sh
env MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 \
  BUILD/tests/dx12/msc_typed_buffer_padding_probe LOWERED_UAV LOWERED_SRV - --origin-cbv-r8sint-oob
```

Repeat with r16sint/r32sint. Original-shader controls use original containers
and the same options without `-oob`; expected exit 1 must accompany the complete
valid-control/mismatch signature described above. Final ignored receipts:
`build/sint-origin-final-containers.json`, `build/sint-origin-final-native.json`,
`build/sint-origin-production.json`, and each build's Meson test log.
Fresh generated containers are in receipt-identified `sint-origin-final.*`
directories. Earlier `sint-origin-*` receipts predate the final review changes
and are not the final acceptance evidence.

### Limits / next

This is a finite offline signed load/store corpus, not production integration.
Signed atomics, dual-input SINT alias coherence, large-origin overflow execution,
normalized/vector semantics, dynamic handle provenance, hidden-root collision
mapping, metadata generations, cache identity and D3D12 barrier translation
remain unvalidated or unsupported. Both native configurations use the same
installed MSC library; they are not two production backend families. No
mandatory-format/capability or FL promotion. Production unaligned views remain
fail-closed. Next bounded step: scalar UNORM offline conversion and native
readback, before wider vector and production integration work.

### Self-review

Standards: 0 documented-standard violations; 2 actionable heuristics resolved
with the explicit atomic flag and shared format table. Final builds and matrices
were rerun after those changes.

Spec: 2 validation findings closed. The SINT partial-mask mutation now targets
the input store, and the FLOAT-on-SINT parser negative supplies consistent f32
types/declaration/extraction/bitcast plus the exact mismatch reason. These remain
synthetic parser evidence, not valid-container claims. The external GPU matrix
independently verifies the full original-shader mismatch signature; reviewer
confirmed the initial receipts, and the main agent reran final binaries.

Summary: Standards 2 heuristic findings resolved; Spec 2 validation findings
closed; no remaining actionable findings. LLVM/MSC skills constrained component
and reflected ABI handling; validation/code-review skills separated evidence
layers and review axes. Local commit only; no push.

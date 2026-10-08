# Scalar FLOAT offline typed-origin lowering

## Task Analysis

Baseline `acfeb75`, branch `feat/d3d12-1`, clean task-start worktree.
Hypothesis: the same guarded logical-index/origin transformation can preserve
scalar FLOAT typed loads/stores, including R16_FLOAT conversion, without
changing the production native-view rejection policy.
Evidence: existing real FLOAT buffer fixtures reject at the adapter's UINT-only
resource grammar; existing native padding probe has a whole-buffer/readback
oracle and reflected private CBV binding reusable for FLOAT.
Expected effect: bounded offline scalar FLOAT support, not production support.
Risk: confusing float bit patterns with integer conversion, allowing mismatched
resource/operation types, or assuming native alias evidence proves D3D12 barriers.
Validation: parser positive/negative units, full-container DXC validation,
R16/R32_FLOAT native MSC readback with logical OOB and sentinel checks, original
shader negative controls, and UINT regressions in both builds.
Scope: test adapter and opt-in native diagnostic only; no capability promotion,
production ABI, descriptor, root mapping or cache changes. LLVM and MSC skills
guide typed IR/ABI handling; Metal validation checks the native execution.

## Task Result

Implemented a bounded scalar FLOAT extension to the test-only DXC textual
adapter. Resource class `Buffer<float>` / `RWBuffer<float>` must match component
metadata 9; UINT remains metadata 5, and output u1 remains UINT. Typed operations
must match the handle's component type. FLOAT load/store use the same logical
bounds and non-wrapping origin guards; rejected loads merge a zero f32 aggregate,
stores skip. Component-zero extraction and float-to-i32 bitcast preserve the
existing fixture's `asuint` output. No floating arithmetic, immediate FLOAT
store operands, FLOAT atomics, INT, UNORM or vector support is added. SRV writes,
partial masks, observable status lanes, dynamic provenance and existing root
collisions still reject. Full DXC input/output validation remains mandatory.
See [resource grammar](../tests/dx12/dxil_origin_transform.hpp#L74),
[operation checks](../tests/dx12/dxil_origin_transform.hpp#L161) and
[guard generation](../tests/dx12/dxil_origin_transform.hpp#L180).

The existing native probe now reuses its reflected root/CBV binding and complete
backing-buffer oracle for R16/R32_FLOAT. Values 0.5, -2, 1.5 and 32 have exact
half/float representations: R16 backing writes compare half bits while output
compares float32 bits; R32 compares float32 bits for both. Zero/short count drops
OOB stores and returns zero loads. All sentinel bytes outside permitted writes
must remain unchanged. An explicit option table preserves the existing UINT
modes and rejects unknown modes before device creation.

### Fresh validation — 2026-10-02

Both build configurations were reconfigured before compilation. Each passed
Meson 3/3 and parser 42/42. Parser mutations alone are not valid-DXIL evidence.

Each tool transformed and DXC-validated the original FLOAT UAV/SRV fixtures and
the UINT UAV/SRV/atomic fixtures (5 accepted), and explicitly rejected six
unsupported real type profiles (INT, float4, uint4, int4, unorm float,
unorm float4). All 22 container contracts passed across both tools; rejected
outputs were absent. The main 18-case receipt also checks input SHA-256 unchanged.
No HLSL replacement shader was substituted inside lowering.

Native execution on Apple M4, with API and Shader validation enabled before
device creation, produced the following per-build results:

| Matrix | Cases | Result |
| --- | ---: | --- |
| R16_FLOAT UAV/SRV, counts 8/0/1/3/4/5/7 | 140 | All output/backing comparisons match; 112 padded, 28 aligned |
| R32_FLOAT UAV/SRV, same counts | 140 | All comparisons match; 56 padded, 84 aligned |
| R32_UINT UAV/SRV/atomic regression | 210 | All comparisons match; 84 padded, 126 aligned |
| Original FLOAT shaders, R16/R32 full count | 40 | 24/24 padded mismatches, 16/16 aligned matches |

Each semantic matrix uses FirstElement 0/1/4/257/260 and MSC BoundsCheck off/on.
The negative controls bind the private CBV but use original untransformed
containers: actual dispatch/readback completes, with R16 16/16 and R32 8/8
padded mismatches. These are not compiler-error or GPU-error exits. The final
probe builds also reject an unknown mode (one check per build). No observed
API/Shader validation error or warning occurred in these receipts.

Fresh normal-prefix D3D12 selected cases remain distinct: DXIL R16_FLOAT buffer
FirstElement=4 fails recording (`MSC typed-buffer view unavailable`, Close
`0x80004005`), while the same DXBC case passes GPU readback. No runtime DLL was
replaced. These two selected cases are not a full production backend matrix.

### Reproduction and evidence limits

For each `BUILD` (`build`, `build-no-private`), run:

```sh
meson setup --reconfigure BUILD
meson compile -C BUILD dxil_roundtrip dxil_origin_transform_test msc_typed_buffer_padding_probe
meson test -C BUILD --print-errorlogs
```

Through repository Wine, run `BUILD/tests/dx12/dxil_roundtrip.exe` with original
`typed_uav_0_0.cso` and `typed_uav_srv_0.cso` inputs, a separate fresh output for
each, absolute Windows DXC directory and `--lower-typed-origin`. DXC directory:
`Z:/Users/zhangbo/Documents/Vibe-Codeding/dxmt/tools/dxc/bin/x64`.
Then run the native probe:

```sh
env MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 \
  BUILD/tests/dx12/msc_typed_buffer_padding_probe LOWERED_UAV LOWERED_SRV - --origin-cbv-r16float-oob
```

Repeat with `--origin-cbv-r32float-oob`; negative controls use original containers
and `--origin-cbv-r16float` / `--origin-cbv-r32float` (expected mismatch exit 1).
Ignored receipts: `build/float-origin-containers.json`,
`build/float-origin-uint-containers.json`, `build/float-origin-native.json`, and
`build/float-origin-production-{red,dxbc}.log`; per-build Meson test logs.
Fresh transformed containers are in ignored `float-origin.*` and
`float-origin-uint.*` directories, identified in the receipts.

This proves only the bounded offline load/store/bitcast corpus and finite exact
values. NaNs, infinities, denormals, rounding, arbitrary FLOAT operations,
FLOAT dual-input alias coherence, origin-addition overflow with large metadata,
production hidden-root/metadata generations, cache identity and D3D12 barrier
translation remain unvalidated. Both native configurations use the same
installed MSC library; their results do not establish two production backend
families. No mandatory-format/capability or feature-level promotion follows.
The [prior production audit](D3D12_TYPED_ORIGIN_PRODUCTION_AUDIT.md) describes the
pre-extension snapshot; its production integration blockers remain applicable.

### Self-review

Standards: no documented-standard violation. Two actionable maintainability
judgments (repeated mode predicates and duplicated FLOAT store fixture) were
resolved with the explicit option table and a shared fixture helper. Final
build/unit/native matrices above were run after those changes.

Spec: no implementation finding. Reviewer required actual readback mismatches
for original-shader controls, rather than merely nonzero exits; final receipts
meet that requirement with all aligned controls matching and every padded
control mismatching after completed GPU execution.

Summary: Standards 2 heuristic findings, resolved; Spec 0 implementation
findings; no remaining actionable findings. LLVM/MSC skills constrained the
typed IR and reflected ABI; Metal-validation and code-review skills separated
runtime evidence and review axes. Local commit only; no push. Next bounded
step: scalar signed INT offline lowering/native readback, keeping production
unaligned views fail-closed until root/descriptor/cache and alias contracts pass.

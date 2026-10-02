# Scalar UNORM offline typed-origin lowering

## Task Analysis

Baseline `b7f6eac`, branch `feat/d3d12-1`, clean task-start worktree.
Hypothesis: scalar UNORM is an f32 typed access with distinct DXIL component
metadata and can reuse the existing guarded FLOAT access transformation.
Evidence: fresh dump of `typed_uav_6_0.cso` has `RWBuffer<float>`, component 14,
f32 load/store and UINT output. Ordinary FLOAT uses component 9. The existing
native probe has reflected root/CBV binding and whole-buffer/readback oracle.
Expected effect: offline scalar normalized load/store, not production support.
Risk: accepting SNORM or wrong component types, precision error hidden by a
loose oracle, or claiming general quantization/clamping from a copy round-trip.
Validation: parser boundaries; full-container input/output validation; native
R8/R16_UNORM loads and copy stores with raw values 0/1/midpoint/max; logical OOB,
sentinels and original-shader negative controls; FLOAT/SINT/UINT regressions.
Output oracle uses independently computed nearest float32 bits for raw/(2^n-1);
backing stores compare bytes exactly. No new epsilon tolerance is introduced.
Numeric reference: [D3D11.3 conversion precision and UNORM conversion rules](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.2.3.5%20UNORM%20-%3E%20FLOAT).
Scope: opt-in adapter/probe only; no production root/descriptor/cache/barrier
integration or capability/FL promotion. LLVM/MSC/validation skills constrain
IR, reflected ABI and evidence layers; self-review then local commit, no push.

## Task Result

Completed this bounded offline milestone; production support remains unverified.

The adapter accepts scalar float-class component 14 independently of FLOAT 9,
preserves normalized metadata and f32 accesses, and reuses guarded origin/count
lowering. SNORM, vectors, signed atomics and non-UINT output remain rejected.
The opt-in SNORM fixture supplies a real DXC-valid negative input.

Fresh validation in both `build` and `build-no-private`:

- Meson tests: 3/3; parser fixtures: 64/64 in each build. Parser fixtures alone
  are not validated DXIL or GPU evidence.
- Full-container contracts: 30/30 total, nine accepted and six rejected per
  build. Inputs stayed unchanged; rejected outputs were absent. Accepted outputs
  passed DXC validation before native compilation.
- Native positive readback: 1190/1190 per build, comprising UNORM 280, SINT 420,
  FLOAT 280 and UINT 210. Each UNORM format covers UAV/SRV, seven logical counts,
  five origins and two bounds-check settings, including zero count and OOB.
  Exact output float bits and full backing bytes matched the independent oracle.
- Original UNORM shader controls: 40 cases per build; all eight aligned cases
  matched and all 32 padded cases mismatched, with GPU completion required.
  Negative-control exit status alone was not used as proof.
- Metal API/shader validation was enabled; receipts contain no detected
  validation or pipeline errors. Unknown-mode rejection passed in each build.
- Unchanged production prefix, R8_UNORM selected cases: DXIL FirstElement 4
  retained the expected recording rejection; aligned DXIL FirstElement 16 and
  DXBC FirstElement 4 passed GPU readback. No DLL deployment was performed.

Ignored receipts retain commands' outputs and per-case verdicts:
`build/unorm-origin-containers.json`, `build/unorm-origin-native.json`, and
`build/unorm-origin-production.json`. Build/configuration logs use the
`unorm-origin-` prefix. Reconfigure each build with `meson setup --reconfigure`,
compile `dxil_roundtrip`, `dxil_origin_transform_test`,
`msc_typed_buffer_padding_probe`, and `dxil_typed_buffer_snorm_fixture`, then run
`meson test --print-errorlogs`. Native UNORM modes are
`--origin-cbv-r8unorm-oob` and `--origin-cbv-r16unorm-oob`; corresponding modes
without `-oob` run the original-shader controls.

Standards and specification reviews found no blocking defect. Two optional
maintenance suggestions (component mapping consolidation and shared negative
fixture construction) were deferred to keep this extension bounded. Final
self-review checked scope, rejection boundaries and complete receipt verdicts.

Evidence is NATIVE_OBSERVED for this finite copy/load corpus only: four encoded
values per format, not arbitrary normalized stores, clamping, NaN or complete
quantization semantics. Both native build configurations use the same installed
MSC; they are not two production-backend acceptances. Production root binding,
descriptor generations, cache identity and D3D barrier integration remain
UNVERIFIED. Unaligned production views stay fail-closed; no capability or feature
level is promoted. Next bounded work is vector typed-origin grammar and readback.

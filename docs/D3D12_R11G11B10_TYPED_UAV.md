# R11G11B10 Typed UAV bring-up

Baseline: 4e22bffa, feat/d3d12-1. User priority: add R11G11B10 Typed UAV support
after masked private-variant acceptance. This checkpoint establishes the
existing backends' basic packed load/store behavior and the remaining public
API gap. It is not completion of the requested format support or full Typed
UAV qualification.

## Hypothesis / evidence / expected effect / risk / validation

The format already maps to RG11B10Float and is in the D3D12 typed-UAV allowlist.
The current public policy exposes view/store but suppresses load when the
shared additional-format contract is disabled. Ranked initial predictions:
packed numerical conversion is missing, buffer origin handling is wrong, or
basic conversion works and public reporting is the remaining gap. A selected
CPU-seeded load/store case distinguishes these without changing capability bits.

Expected effect: identify whether AIR/MSC require new packing/lowering before
changing admission. Risks: treating packed storage as four bytes per component,
accidentally changing the old matrix, ignoring alpha defaults, or promoting an
all-or-nothing format set from one optional format. Validate decoded float bits,
exact stored bytes, buffer poison guards, six view shapes, three buffer origins,
both backends/builds, runtime identities and API validation.

## Probe

`dx12_typed_uav_formats --dxil|--dxbc --case R11G11B10_FLOAT SHAPE FIRST_ELEMENT`
is opt-in. The established 18-format default matrix, expected totals, API policy
and gate registration are unchanged. Reuse the existing float4 shaders and
source rather than adding a converter or binding implementation.

An independent CPU encoder packs exact positive values 0, 0.5, 2.25 and 4 using
five-bit exponents and 6/6/5 mantissas. It checks four GPU-loaded components:
RGB against the seeded float bits, alpha against 1. Alpha occupies no storage;
all packed channels share one four-byte texel. Store destinations are compared
as raw packed bytes, and buffer prefix/suffix poison checks are retained.
Texture upload/readback footprints and view creation use the existing fixture.

The selected summary stays separate from complete-matrix markers. Public
view/load/store bits are printed separately from GPU acceptance, so a semantic
PASS cannot be mistaken for advertised typed-load support.

## Evidence

Evidence directory: /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq.

- r11-dxil-initial.jsonl and r11-dxbc-initial.jsonl: no-private builds pass
  eight cases per backend: buffer FirstElement 0/4/260 and 1D/1D-array/2D/
  2D-array/3D at origin 0.
- r11-normal-final.jsonl: normal build passes the same sixteen cases. Across
  both builds, 32 cases PASS with Metal API validation and matched DXMT PE/Unix
  identities. DXIL probes additionally verify loaded DXC/validator identities.
- DXBC uses the existing prefix d3dcompiler_47.dll copied only into ignored
  fixture output/staging. Source SHA256:
  f4e0b080aa2663270bedcf13881a9640bc3d3397ff062dd6ca579f0da6d9c3f6.
  Legacy compiler staging/hash is not the DXIL compiler provenance contract.
- r11-regression-controls.jsonl: eight existing R32G32B32A32_FLOAT numeric
  buffer/2D controls and both unchanged additional=0 API-policy checks PASS.
- r11-full-build-{normal,no-private}.log: complete builds PASS;
  r11-host-{normal,no-private}.log: host suites PASS 17/17 each, including the
  existing 92 gate tests. git diff --check passes.
- r11-cap-readout-final.jsonl records the final public query/GPU conjunction
  explicitly; public load remains disabled rather than silently promoted.

Main-agent Standards/Spec self-review checks packed byte width, field masks,
alpha handling, shared-texel writes, unchanged legacy branches/matrix totals,
and separate API/GPU claims. Compiler/integration/validation skills guided reuse
of the existing binding and shader paths. No independent review, shader
validation, native Windows oracle, WOW64 GPU run or game acceptance is claimed.
Runtime deployment is cache-only, with no game/prefix DLL modification or
Wine/Steam/game restart. Commits remain local.

## Remaining implementation gap

Basic R11G11B10 load/store does not reproduce a backend defect in this bounded
scope. Public typed-load reporting is still blocked by
SupportsD3D12TypedUAVLoad and TypedUAVLoadAdditionalFormats=FALSE. Next work is
the production per-format capability/shader-feature policy and its consistency
with the shared contract, not another repetition of these basic 32 cases.
Do not claim the user request complete while public load is still absent.

[Microsoft's typed-UAV documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d11/typed-unordered-access-view-loads)
distinguishes the fifteen all-or-nothing additional formats from individually
queried optional formats, including R11G11B10. Preserve that distinction and
the shader-feature requirement when changing policy. A single-format result
does not establish support for the full shared set.

[Apple's capability tables](https://developer.apple.com/metal/capabilities/)
list RG11B10Float texture-buffer read and write accesses separately; these are
not a generic read_write guarantee. Do not widen native Metal capability flags
solely to satisfy the D3D12 query. The existing backend evidence must be tied to
the actual resource/shader implementation rather than a fabricated native cap.

Rounding/ties, denormals, extreme values/NaNs/infinities, new nonexact stores,
clears, nonzero mips, descriptor mutation/lifetime/indirect paths, other shader
stages, other GPU families and native comparison remain unqualified. Existing
FL12_0/SM6.6/LogicOp MSAA experimental switches remain off.

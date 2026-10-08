# Task Analysis

Baseline `a15305e`, branch `feat/d3d12-1`. Next authorized milestone: expand
the offline static resource/handle model and validate aliases. Preserve dirty
DirectX submodule; self-review and commit locally, never push.

Hypothesis: resolving direct handles from resource-class/range-ID/register
metadata rather than resource-list position permits two independent typed
inputs. Per-input origin/count records preserve logical bounds for overlapping
typed views sharing one allocation, with raw access to the same backing bytes.

Evidence: preceding restricted transform has full DXC input/output validation
and GPU OOB/wrap coverage, but exactly one input and no simultaneous aliases.

Expected effect: bounded two-input acceptance and typed/raw alias evidence.
No production capability, binding ABI or performance change.

Risk: conflating metadata IDs with registers, sharing origin/count between
views, hidden-CBV ABI mismatch, alias visibility without synchronization,
and data races. Bind inputs at t0/u0 and t2/u2, output u1; map metadata identities
explicitly, reject ambiguous slots, retain independent 16-byte records.
Use separate race-free dispatch phases with Metal buffer/texture barriers.

Validation: DXC full-container input/output validation; parser acceptance and
rejection tests; native MSC/Metal API+GPU validation; independent CPU simulation
of every phase output and complete backing allocation. Re-run prior single-
input/OOB/wrap and synthetic format regression matrices in both native builds.

Scope: offline tests only. Dynamic handles, arrays, PHI/select, bindless, root
signature transformation, production metadata generations/static-vs-volatile,
D3D12 barrier translation, games and capability promotion remain out of scope.

# Task Result

Implemented the bounded offline adapter and native diagnostic. Handles resolve
by `(resource class, range ID)` and must match their static register index.
Input register 0 uses record 0, register 2 uses record 1, with independent
origin/count pairs at 16-byte strides. Reordered lists and range IDs different
from register indices are covered by parser tests. Ambiguous t0/u0 slots,
duplicate IDs/slots, dynamic indices, arrays and unsupported spaces reject.
The private CBV metadata size is now 16 bytes for one record or 32 for two;
this is a test ABI, not an installed production ABI.

Three fresh SM6.0 containers were compiled from `dxil_static_alias.hlsl`.
Phases 0/2 passed DXC full-container validation before and after typed lowering.
Phase 1 passed unchanged-IR round-trip validation and uses native raw access;
it is not an AIRCONV fallback. MSC compilation is shared with the prior probe
through `msc_probe_compile.h`, with caller-owned root and retained PSO output.

Verified on 2026-10-01, Apple M4, MSC 4.0.1, Metal API and GPU validation enabled.
Both `build` and `build-no-private` were reconfigured before compilation:

- 60 alias sequences per build: 5 origins, 6 independent count pairs, MSC
  bounds checking off/on. All 180 phase outputs and 60 complete backing buffers
  matched independent CPU expectations, including zero-count views.
- Swapped-count negative control per build: exactly 40 unequal-count sequences
  mismatched and 20 equal-count controls matched. Every sequence's expected
  verdict is checked; incomplete execution or unexpected verdict fails.
- Newly regenerated single-input containers passed DXC validation and all
  210 OOB cases per build. Prior transformed wrap fixtures passed all 210 cases.
- Prior synthetic raw/typed R32, R8 and R16 probes passed 100 cases per build.
  Original unlowered fixtures retained their 18 aligned matches and exactly
  12 padded mismatches, independently per build.
- Meson tests: 3/3 passed per build, including the bounded parser tests.
  Parser: 27 cases, zero failures (not itself DXIL/GPU validation).
  Logs show validation enabled and no validation errors/warnings.

Local evidence: ignored `build/dxil-static-alias.JaH2zi/`, including `input_*`,
`lowered_*`, regenerated `compat_*` and per-build positive/negative/regression
logs. Fixture/source and opt-in Meson targets are implemented locally;
commit pending. Binaries remain untracked build artifacts.

Reproduce: build `dxil_roundtrip`, `dxil_origin_transform_test`,
`msc_typed_buffer_padding_probe`, `msc_static_alias_probe` and optional
`dxil_static_alias_0/1/2` fixture targets. With the existing Wine prefix, invoke
`dxil_roundtrip.exe input output ABSOLUTE_DXC_DIRECTORY --lower-typed-origin` for
phases 0/2, omitting the flag for phase 1. Use fresh output paths (CREATE_NEW).
Then run `msc_static_alias_probe lowered_0.cso lowered_1.cso lowered_2.cso`,
with `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1
MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1`; repeat with
`--expect-swapped-counts`. Paths passed to Wine must use its Windows mapping;
the DXC directory must be an absolute Windows path, for example
`Z:/Users/zhangbo/Documents/Vibe-Codeding/dxmt/tools/dxc/bin/x64`.

Self-review: Standards and Spec reviewed separately against `a15305e`.
Standards: one P3 reproduction-path finding, corrected to an absolute mapped
DXC-directory requirement. Spec: one P2 documentation finding about
prematurely claiming a commit; corrected to pending before committing.
No production code, capability report, installed
DLL, game or performance change. Native Metal barriers provide alias evidence
only, not D3D12 barrier translation acceptance. Production metadata generations,
static/volatile recording/submission behavior, root-signature integration and
general handle provenance remain subsequent gates.

## Branch

`feat/d3d12-1`

## Baseline

Task baseline `a15305e`; integration baseline `origin/feat/d3d12`.

## Local Commit

See the final handoff for the hash of the local commit containing this report.
No push is authorized. The pre-existing dirty DirectX submodule is excluded.

## Changed Files

`dxil_origin_transform.hpp`, its parser tests, `dxil_roundtrip.cpp`,
`msc_typed_buffer_padding_probe.c`, `meson.build`, and new
`dxil_static_alias.hlsl`, `msc_probe_compile.h`, `msc_static_alias_probe.c`,
plus this report (test paths all under `tests/dx12`).

## Implementation

Bounded class/range/register handle mapping with two independent typed inputs;
details and private-record ABI above.

## DXBC / AIRCONV Impact

None; no fallback path or AIRCONV changes.

## DXIL / MSC Impact

Offline transform/probes only; expanded accepted static resource subset.

## Shared Runtime Impact

None; compiler helper is shared by native diagnostics, not production runtime.

## Tests Added

Parser mapping/rejection coverage and native three-phase alias/negative probes.

## Tests Run

Both-build Meson, DXC validation, native validation and regression matrices
listed above; opt-in fixture targets also built and matched fresh inputs.

## Runtime Results

Native offline alias readback passes. No game or production D3D12 run.

## Known Limitations

Bounded direct static UINT inputs only; excluded production/general-handle
gates are listed above. Mixed SRV/UAV acceptance is parser coverage, not
mixed-class alias GPU coverage.

## Capability Status

PARTIAL: bounded offline evidence passes; production capability unchanged.

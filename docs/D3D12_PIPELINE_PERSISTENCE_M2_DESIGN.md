# M2 Design Card: D3D12 Pipeline Persistence Correctness Hardening

Status: Gate 2 approved; implementation and validation complete
Date: 2026-08-30
Branch: feat/d3d12
Baseline: baa8f257516b6be1cd5003fa618cce7ded3f63f9

This card is the single follow-up milestone after the M1 audit. Gate 2 approval
authorizes the scoped source and test changes described below.

## Goal

Make the existing D3D12 pipeline persistence interface accept only valid,
owned, canonical state: valid descriptor and stream inputs keep producing
functional PSOs, while malformed inputs reject deterministically and equivalent
inputs produce the same persistence key without depending on caller pointers or
inactive descriptor bytes.

## Evidence

The M1 implementation has no Phase 0 regression: all 27 D3D12 manual cases
passed in the isolated Wine/MSC environment, and the existing Meson test passed
(`1/1`). The audit nevertheless found contract gaps inside the M1 dependency
closure:

- `D3D12PipelineStreamData` stores raw D3D12 descriptor pointers
  (`src/d3d12/d3d12_pipeline_persistence.hpp:30-34`), and
  `ReadStreamPayload` copies those pointer values from caller memory
  (`src/d3d12/d3d12_pipeline_persistence.cpp:414-424`).
- Stream parsing does not fully validate normalized descriptor values before
  the existing graphics creator indexes or consumes them
  (`src/d3d12/d3d12_pipeline_persistence.cpp:874-1019`,
  `src/d3d12/d3d12_pipeline_graphics.cpp:592,794-807`).
- `PutBlendDesc` hashes all eight render-target slots
  (`src/d3d12/d3d12_pipeline_persistence.cpp:245-251`), while the graphics
  descriptor path only uses active render targets. Equivalent descriptors can
  therefore receive different keys.
- The persistence implementation unconditionally uses `ID3D12PipelineLibrary1`
  (`src/d3d12/d3d12_pipeline_persistence.cpp:511-531`). The configured MinGW
  headers provide the stream and library declarations, but the bundled WIDL
  header at `include/native/directx/d3d12.h` does not.
- The persistence runner exercises HRESULT/readback paths, but live library
  loads are not dispatched/drawn, and malformed stream, token, descriptor, and
  caller-lifetime coverage is incomplete.
- `MTL_DEBUG_LAYER=1` reports a missing viewport in the persistence graphics
  fixture before the graphics readback path, so validation-clean test evidence
  is not yet available.

## Scope

### Owned normalized stream state

Replace pointer-bearing stream output with an internal owned normalized
representation. It must own or retain every value needed after parsing,
including shader bytecode, cached-token bytes, input-layout semantic strings,
input-layout records, and root-signature ownership. A temporary D3D12
descriptor may be materialized only while calling the existing compute or
graphics creator.

### Deterministic validation

Make stream parsing and cache-data construction bounds-safe and explicit:

- Reject null/non-null and size combinations that cannot be consumed safely.
- Reject invalid, duplicate, conflicting, or incomplete subobjects.
- Reject unsupported values instead of passing them to unchecked mapping tables.
- Validate render-target counts and write masks, sample data, input-layout
  ranges/strings, shader bytecode ranges, and other supported descriptor fields
  before key construction or PSO creation.
- Preserve explicit `E_NOTIMPL` behavior for recognized unsupported subobjects.

### Canonical persistence keys

Make descriptor and stream creation share one normalized key representation.
The key must serialize fields explicitly, exclude pointer addresses and padding,
exclude input `CachedPSO` bytes, and include only active render-target state.
The existing DXMT cache-token version and compatibility identity remain the
authoritative cache contract.

### Header and ABI contract

Make the required D3D12 declaration provenance explicit in the cross build and
source. The implementation may use `ID3D12PipelineLibrary1` and stream APIs
only when the configured headers provide the declarations. No hand-written COM
vtable or duplicate ABI definition is allowed. Missing required declarations
must result in a clear configuration/compile-time outcome rather than a silent
ABI guess.

### Regression runner

Extend the existing persistence runner without creating a second shader or PSO
creation path. The runner will:

- Execute live compute and graphics library loads through dispatch/draw and
  readback oracles.
- Cover malformed streams, invalid descriptor values, malformed/foreign/stale
  cache tokens, truncated libraries, duplicate names, mismatched descriptors,
  and wrong-device pipelines.
- Verify equivalent descriptor/stream forms produce compatible keys, including
  inactive render-target slots.
- Verify deterministic serialization and exact `GetSerializedSize` behavior.
- Set the required viewport in the graphics fixture so API validation can run
  without the current viewport error.

## Non-Scope

- No changes to D3D11, NGX, swapchain, resource residency, MetalFX, or shader
  conversion cache behavior.
- No new Metal shader-conversion backend or pipeline-cache performance promise.
- No real-engine, Windows-reference, visual-window, or deployment work.
- No manual ABI/vtable duplication to support incomplete headers.
- No registration of PE runners in `meson test` unless the configured test
  wrapper can execute them without overstating coverage.

## Module And Seam

The external seam remains:

```text
ID3D12Device2::CreatePipelineState
ID3D12Device1::CreatePipelineLibrary
ID3D12PipelineState::GetCachedBlob
ID3D12PipelineLibrary[1] methods
```

The existing `src/d3d12/d3d12_pipeline_persistence.*` module remains the
internal seam. Its callers should continue to see only operations for parsing
and normalizing streams, building cache data, creating cache tokens, and
managing named records. Pointer ownership, canonical serialization, bounds
checking, and ABI feature detection remain inside the module/build contract.
The existing compute and graphics creators remain the adapters to MSC and
Metal.

## Planned Changes

- Update `src/d3d12/d3d12_pipeline_persistence.hpp` and
  `src/d3d12/d3d12_pipeline_persistence.cpp` with owned normalized stream
  storage, safe descriptor materialization, and complete supported-value
  validation.
- Refactor key construction so descriptor and stream paths use the same
  canonical active-state rules.
- Add explicit header feature checks and preserve one compiler-provided D3D12
  ABI definition in the build.
- Keep `src/d3d12/d3d12_device.cpp`,
  `src/d3d12/d3d12_pipeline_compute.cpp`, and
  `src/d3d12/d3d12_pipeline_graphics.cpp` as thin integration points unless
  the ownership change requires a narrowly-scoped call-site update.
- Extend `tests/dx12/dx12_pipeline_persistence.cpp` and its existing Meson
  target only; do not change unrelated test fixtures.

## Test Plan

### Static and build checks

- Build the configured x64 and x86 cross targets.
- Verify the selected compiler headers contain the required stream and library
  declarations, with no duplicate ABI definitions.
- Run `git diff --check` and inspect that only M2 source, test, build metadata,
  and design-card files changed.

### Isolated runtime checks

- Run the persistence runner under the supplied Wine binary with the isolated
  prefix and isolated shader-cache path.
- Run with `MTL_DEBUG_LAYER=1` and confirm no viewport/API validation error.
- Run with `MTL_SHADER_VALIDATION=1` and confirm no shader validation error.
- Re-run all 27 Phase 0 D3D12 cases and the existing Meson suite.

### Required persistence cases

- Valid descriptor and stream creation for compute and graphics.
- `GetCachedBlob` round-trip for both pipeline kinds.
- Live and rehydrated named-library load followed by dispatch/draw readback.
- Deterministic serialization and exact-size/undersized-buffer behavior.
- Invalid/duplicate/conflicting stream subobjects and invalid descriptor values.
- Missing, foreign, stale, malformed, and truncated persistence data.
- Duplicate/missing names, descriptor-key mismatch, and wrong-device storage.
- `ID3D12PipelineLibrary1::LoadPipeline` when the build headers expose it.

## Definition Of Done

- Valid M1 descriptor, stream, cache-token, and library behaviors remain
  functional.
- Stream creation never relies on caller-owned pointer values after parsing and
  rejects invalid supported descriptor values before unchecked use.
- Equivalent descriptor and stream state produces one canonical key; inactive
  render-target state cannot cause a false mismatch.
- The build has one explicit, compiler-provided D3D12 ABI source and does not
  silently compile against an incomplete declaration set.
- Persistence tests exercise live and rehydrated pipelines with dispatch/draw
  readbacks and pass under Metal API and shader validation.
- All 27 Phase 0 D3D12 cases and the existing Meson test pass.
- No unrelated subsystem or capability status changes.

## Risks And Decisions

- The bundled WIDL header is incomplete for this interface surface. The chosen
  decision is to make header availability explicit, not to copy vtables.
- D3D12 descriptors contain caller pointers and padding. The chosen decision is
  to canonicalize pointed-to content into owned internal state before hashing
  or invoking a creator.
- Stricter validation can expose malformed application input that previously
  reached unchecked code. This is intentional and follows the M1 contract.
- The graphics API-validation failure is currently fixture-related; M2 must
  prove the persistence path after the fixture sets a valid viewport.

## References

- `docs/D3D12_PIPELINE_PERSISTENCE_M1_DESIGN.md`
- `src/d3d12/d3d12_pipeline_persistence.hpp:30-61`
- `src/d3d12/d3d12_pipeline_persistence.cpp:245-251,414-424,874-1054`
- `src/d3d12/d3d12_pipeline_graphics.cpp:571-612,794-807`
- `src/d3d12/d3d12_device.cpp:1040-1042,1443-1446`
- `tests/dx12/dx12_pipeline_persistence.cpp:222-248,493-590`
- `tests/dx12/meson.build:29-31`
- `include/native/directx/d3d12.h`
- `docs/DEVELOPMENT.md:62-80,108-118`
- `DXMT_D3D12_Master_Prompt.md`

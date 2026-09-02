# D3D12 Resource Audit Checklist

Status: In progress
Branch: `feat/d3d12`
Scope: Phase B resource, heap, tiled-resource, and `CopyTiles` contract audit.

This ledger is the source of truth for this audit. Before starting a new slice,
check the completed list and its slice record. Every completed slice must add a
record under `docs/` and link it below. Do not reimplement a completed item
without first documenting a changed contract or a regression.

## Status Rules

- `[x]` implemented and covered by the recorded validation.
- `[~]` partially implemented or known to have a bounded limitation.
- `[ ]` not started or not yet validated.
- A slice record must state changed files, invariants, tests, and follow-ups.

## Completed Slices

- [x] Capability boundary and feature-level contract. `D3D12CreateDevice`
  keeps the supported maximum at FL11_1 on Apple 7 hardware and does not claim
  tiled-resource Tier 2 support. Established before this audit.
- [x] Reserved buffer bookkeeping, tiling, mapping, and buffer `CopyTiles`.
  Existing commits: `fad8f7d`, `9503ae0`.
- [x] Reserved texture bookkeeping, standard-mip tiling, partial tiling, and
  texture mapping. Existing commits: `9469816`, `17d86aa`.
- [x] Command-queue ordering and complete-submission serialization for tiled
  resource operations. Existing commits: `d756051`, `e487a63`, `8af503c`.
- [x] Deferred tile resolution for recorded `CopyTiles` operations. Existing
  commit: `617e94f`.
- [x] Explicit default `CopyTiles` direction handling. Existing commit:
  `b38f069`.
- [x] Resource and heap type validation for placed resources, including
  buffer-only, non-RT/DS-only, and RT/DS-only heaps. Existing validation is in
  `src/d3d12/d3d12_device.cpp` and is covered by the resource runner.
- [x] Reserved RT/DS texture mapping compatibility. See
  `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_HEAP_MAPPING.md`.

## Next Slices

- [ ] Audit committed-resource heap flags against resource dimension and RT/DS
  usage. Keep the validation in one shared resource/heap seam and add negative
  tests before changing allocation code.
- [ ] Audit remaining resource helper validation paths for null, range, state,
  alignment, and node-mask behavior. Record each contract separately.
- [ ] Complete the x64 validation matrix for private and no-private builds,
  including API/shader validation where the fixture supports it.
- [~] Real Metal sparse residency remains unavailable through the current
  Metal 3 wrapper. Shadow backing is the supported bounded emulation; do not
  raise the advertised D3D12 tiled-resource tier as part of this audit.

## Permanent Constraints

- Target Metal 3 only for this work.
- Do not add a new dedicated `src/d3d12` source file unless a later slice
  explicitly justifies a new seam.
- Do not change the advertised capability tier to make a test pass.
- 32-bit validation is outside the current user-approved scope.
- The repository's reference-document files may be untracked local material;
  do not stage them with implementation commits unless explicitly requested.

## Validation Notes

- Build targets used for the current slice are `build`, `build-no-private`,
  and `build-X86`; current runtime conclusions are x64 and no-private only.
- The test executable directories contain stale local DLL copies. Run the
  executable from an isolated directory without those copies, or otherwise
  verify the loaded DLL path before trusting a failure.
- `git diff --check` is required for each slice. Whole-file clang-format checks
  currently report pre-existing violations in these large test/source files;
  do not reformat unrelated lines while closing a slice.

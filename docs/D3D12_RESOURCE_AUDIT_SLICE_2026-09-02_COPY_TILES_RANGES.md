# Resource Audit Slice: CopyTiles Recording Ranges

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

`CopyTiles` previously deferred tile-region and linear-buffer range validation
until queue translation. An invalid region or buffer footprint could therefore
be recorded successfully and only abort when the command list was submitted.

## Implemented Contract

- The command-list entry point resolves the logical tile region before
  allocating a `CopyTiles` encoder.
- It rejects empty or invalid regions, tile-count multiplication overflow, and
  buffer ranges that do not contain one 64 KiB footprint per tile.
- Invalid calls mark the command list as failed, so `Close` returns `E_FAIL` and
  no copy encoder is produced.
- Mapping lookup remains deferred until queue translation, preserving remap
  behavior for valid recorded commands.
- Capability reporting and the bounded shadow-backing model remain unchanged.

## Changed Files

- `src/d3d12/d3d12_command_list.cpp`
  - Added recording-time region and linear-buffer footprint checks.
- `tests/dx12/dx12_resource_tests.cpp`
  - Changed invalid boxed and non-zero-Z cases to require recording failure.
  - Added an out-of-bounds linear-buffer range case.

## Validation

- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
  passed.
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
  passed.
- x64 private and no-private resource runners passed after deploying matching
  `d3d12.dll` outputs to each test directory.
- `git diff --check` passed.

## Do Not Reimplement

- Do not resolve tile mappings while recording; mapping changes must remain
  visible to the deferred translation path.
- Do not advertise Tiled Resources Tier 2 or introduce Metal 4 APIs.
- Do not remove the queue-side checks, which remain defensive for submitted
  command data.

## Follow-Up

Continue auditing the remaining resource helper contracts. Packed mip metadata
and logical mapping are covered by
`D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_PACKED_MIPS.md`; `CopyTiles` still rejects
packed mip tiles per the D3D12 contract.

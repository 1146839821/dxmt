# Resource Audit Slice: Copyable Footprints

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

`GetCopyableFootprints` could calculate layouts for resource descriptors that
the resource creation path would reject. Its depth/stencil footprint path also
multiplied row-pitch alignment by the number of planes instead of aligning each
plane independently.

## Implemented Contract

- Buffer footprints use the same descriptor-shape validation as allocation
  queries, including non-zero width and row-major layout.
- Texture footprints validate dimensions, mip counts, layouts, and resource
  flag combinations before querying the format.
- Invalid descriptors return `UINT64_MAX` through `pTotalBytes` and sentinel
  values through requested per-subresource outputs.
- A valid empty subresource query leaves per-subresource output arrays
  untouched and returns zero total bytes.
- Every plane uses the D3D12 256-byte row-pitch alignment independently;
  subresource placement remains aligned to 512 bytes.
- `GetCopyableFootprints1` continues to delegate to the same base contract.

## Changed Files

- `src/d3d12/d3d12_device.cpp`
  - Added descriptor validation to `GetCopyableFootprints`.
  - Corrected per-plane row-pitch alignment.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added invalid descriptor and exact depth/stencil footprint coverage.

## Validation

- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe tests/dx12/dx12_interface_support.exe`
  passed.
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe tests/dx12/dx12_interface_support.exe`
  passed.
- Private and no-private x64 `dx12_resource_tests.exe` runners passed using
  the project Wine build and prefix.
- Private and no-private x64 `dx12_interface_support.exe` runners passed using
  the project Wine build and prefix.
- `git diff --check` passed.

## Do Not Reimplement

- Do not multiply row-pitch alignment by the number of planes; each footprint
  has its own 256-byte row-pitch requirement.
- Do not treat a valid zero-subresource query as an invalid descriptor query.
- Do not advertise Tiled Resources Tier 2 or introduce Metal 4 APIs.

## Follow-Up

Continue auditing the remaining resource helper and `CopyTiles` contract paths.
Small-resource eligibility remains a bounded Metal-dependent approximation as
recorded in `D3D12_RESOURCE_AUDIT_SLICE_2026-09-02_RESOURCE_ALIGNMENT.md`.

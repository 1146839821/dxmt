# D3D12 Resource Audit Slice: Resource Ranges

Date: 2026-09-02

## Scope

This slice audits texture subresource and host-data pointer ranges for
`WriteToSubresource` and `ReadFromSubresource`.

## Invariants

- A texture subresource must be within its mip and array/depth range before any
  Metal region operation is issued.
- 3D textures use one array slice; 1D and 2D textures use
  `DepthOrArraySize` array slices.
- A non-empty host transfer must provide a non-null source or destination data
  pointer.
- Invalid ranges and null data pointers return `E_INVALIDARG` without issuing a
  Metal operation.

## Changes

- Added subresource range checks to texture read/write operations.
- Added null source/destination checks to the same operations.
- Added x64 runner coverage for an out-of-range subresource and null data on
  both transfer directions.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Continue with row-pitch, slice-pitch, and resource alignment contracts.
